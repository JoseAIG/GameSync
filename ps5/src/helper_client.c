/*
 * GameSync PS5 client - large downloads through the helper payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The PS5 throttles an app's writes to ~2 MB/s once a ~1.3 GB burst is
 * spent (a disk test showed it; pausing let it refill), while a payload
 * process writes at full speed. So the app asks its helper (ps5/helper,
 * built into the eboot and started through the ELF
 * loader on port 9021, as the Lapy helper is) to do a download, on
 * 127.0.0.1:HELPER_PORT, one connection per job:
 *
 *   app    -> "GET\t<proto>\t<host>\t<port>\t<base path>\t<api key>\t<console id>
 *              \t<path>\t<file>\t<have>\t<total>\t<timeout>\t<label>\n"
 *   helper -> "P <done> <total>\n" ... then "R <rc> <status>\n"
 *             (or "E <why>\n" when it can't take the job)
 *
 * Closing the connection cancels the job. With no helper (it can't be
 * started, or this is the PC build) the app does the download itself.
 *
 * The helper's bytes are built into the eboot (build.sh generates
 * helper_elf.c): reading them back from /app0/assets failed on a console.
 */
#include "fetch.h"
#include "helper_proto.h"

#include "config.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define ELF_LOADER_PORT 9021
/* helper_fetch() results that aren't a download's outcome */
#define HELPER_DECLINED (-1000) /* the app does the job itself */
#define HELPER_RESTART (-1001)  /* another protocol's helper, which has quit */

/* the helper couldn't be had this session: don't keep trying */
static bool helper_unavailable;

#if defined(__PROSPERO__)
extern const unsigned char gamesync_helper_elf[];
extern const unsigned long gamesync_helper_elf_size;
#endif

static int connect_local(int port, int timeout_sec)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct timeval tv = {timeout_sec, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static bool send_all(int fd, const void *data, size_t len)
{
    const char *p = data;
    while (len)
    {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0)
            return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

/* Sends the helper to the ELF loader; true when it was handed over. */
static bool launch_helper(void)
{
#if defined(__PROSPERO__)
    int fd = connect_local(ELF_LOADER_PORT, 10);
    if (fd < 0)
    {
        app_log("helper: no ELF loader on port %d (errno %d)", ELF_LOADER_PORT, errno);
        return false;
    }
    bool ok = send_all(fd, gamesync_helper_elf, gamesync_helper_elf_size);
    int err = errno;
    close(fd);
    if (ok)
        app_log("helper: sent to the ELF loader (%lu bytes)", gamesync_helper_elf_size);
    else
        app_log("helper: the ELF loader didn't take it (errno %d)", err);
    return ok;
#else
    return false; /* the PC build has no helper */
#endif
}

/* A connection to the helper, starting it when it isn't running. */
static int helper_connect(void)
{
    int fd = connect_local(HELPER_PORT, 5);
    if (fd >= 0 || helper_unavailable)
        return fd;
    if (!launch_helper())
    {
        helper_unavailable = true;
        return -1;
    }
    for (int i = 0; i < 40 && fd < 0; ++i)
    {
        struct timespec ts = {0, 150 * 1000 * 1000};
        nanosleep(&ts, NULL);
        fd = connect_local(HELPER_PORT, 5);
    }
    if (fd < 0)
    {
        app_log("helper: no answer after starting it; downloads stay in the app");
        helper_unavailable = true;
    }
    return fd;
}

/* One line from the helper into line (without the newline). */
static bool read_line(int fd, char *buf, size_t *have, char *line, size_t size)
{
    for (;;)
    {
        char *nl = memchr(buf, '\n', *have);
        if (nl)
        {
            size_t len = (size_t)(nl - buf);
            if (len >= size)
                len = size - 1;
            memcpy(line, buf, len);
            line[len] = 0;
            size_t rest = *have - (size_t)(nl - buf) - 1;
            memmove(buf, nl + 1, rest);
            *have = rest;
            return true;
        }
        if (*have >= 1023)
            return false;
        ssize_t n = recv(fd, buf + *have, 1023 - *have, 0);
        if (n <= 0)
            return false;
        *have += (size_t)n;
    }
}

/* The download's HTTP_* outcome, or HELPER_DECLINED / HELPER_RESTART. */
static int helper_fetch(const HttpServer *server, const char *path, const char *part, uint64_t have,
                        uint64_t total, int timeout, HttpProgressFn progress, void *user,
                        int *status, const char *label)
{
    int fd = helper_connect();
    if (fd < 0)
        return HELPER_DECLINED;
    /* a conversion on the server can keep the first byte waiting */
    struct timeval tv = {timeout + 120, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char req[2048];
    int n = snprintf(req, sizeof(req), "GET\t%d\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%llu\t%llu\t%d\t%s\n",
                     HELPER_PROTO, server->host, server->port, server->base_path, server->api_key,
                     server->console_id, path, part, (unsigned long long)have,
                     (unsigned long long)total, timeout, label ? label : "");
    if (n <= 0 || (size_t)n >= sizeof(req) || !send_all(fd, req, (size_t)n))
    {
        close(fd);
        return HELPER_DECLINED;
    }
    char buf[1024], line[256];
    size_t buffered = 0;
    /* once the job is sent, the helper may have written: no falling back
     * to the app at the old offset - a dropped line is a failed download,
     * which resumes from the file's real size */
    int rc = HTTP_ERR_RECV;
    while (read_line(fd, buf, &buffered, line, sizeof(line)))
    {
        if (line[0] == 'P')
        {
            unsigned long long done = 0, all = 0;
            sscanf(line + 1, "%llu %llu", &done, &all);
            if (progress && progress(done, all, user) != 0)
            {
                rc = HTTP_CANCELLED; /* hanging up stops the helper */
                break;
            }
        }
        else if (line[0] == 'R')
        {
            sscanf(line + 1, "%d %d", &rc, status);
            break;
        }
        else if (line[0] == 'E')
        {
            app_log("helper: %s", line + 1);
            close(fd);
            /* a helper from another build exits after saying so: start ours */
            return strstr(line, "protocol") ? HELPER_RESTART : HELPER_DECLINED;
        }
    }
    close(fd);
    return rc;
}

int fetch_file(const HttpServer *server, const char *path, const char *part, uint64_t have,
               uint64_t total, int timeout, HttpProgressFn progress, void *user, int *status,
               const char *label)
{
    *status = 0;
    int rc = helper_fetch(server, path, part, have, total, timeout, progress, user, status, label);
    if (rc == HELPER_RESTART)
    {
        struct timespec ts = {0, 500 * 1000 * 1000};
        nanosleep(&ts, NULL); /* let the old one release the port */
        rc = helper_fetch(server, path, part, have, total, timeout, progress, user, status, label);
    }
    if (rc != HELPER_DECLINED && rc != HELPER_RESTART)
        return rc;
    return fetch_to_file(server, path, part, have, total, timeout, progress, user, status, label);
}
