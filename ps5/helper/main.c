/*
 * GameSync PS5 helper - large downloads for the app, as a payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The PS5 throttles an app's writes to ~2 MB/s once a ~1.3 GB burst is
 * spent; a payload process writes at full speed (~215 MB/s sustained in a
 * test on the same /data). The GameSync app sends this payload to the ELF
 * loader (port 9021) and hands it each large download over
 * 127.0.0.1:HELPER_PORT; the protocol is described in
 * ps5/src/helper_client.c. The download itself is the app's own code
 * (ps5/src/fetch.c, http.c), built in here.
 *
 * Built with the PS5 Payload SDK (John Törnblom, GPL-3.0) by ps5/build.sh.
 */
#include "fetch.h"
#include "helper_proto.h"

#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define LOG_PATH "/data/gamesync/helper.log"
#define IDLE_SECONDS (60 * 60)

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile time_t last_activity;
static volatile int jobs;

/* fetch.c and util.c log through this, as the app's config.c does */
void app_log(const char *fmt, ...)
{
    pthread_mutex_lock(&log_lock);
    const char *path = getenv("GAMESYNC_HELPER_LOG"); /* the PC test's */
    FILE *f = fopen(path && *path ? path : LOG_PATH, "a");
    if (f)
    {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        fprintf(f, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    pthread_mutex_unlock(&log_lock);
}

static bool send_line(int fd, const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(line))
        return false;
    return send(fd, line, (size_t)n, 0) == n;
}

typedef struct
{
    int fd;
    struct timespec last;
    char part[1024];
    volatile int cancel; /* a newer job for the same file wants it */
} Job;

/* Jobs in progress: two must never write one file. A new job for a file
 * still being written (a pause and an instant resume) cancels the old one
 * and waits for it to let go. */
#define MAX_JOBS 16
static pthread_mutex_t jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static Job *active[MAX_JOBS];

static bool part_busy(const char *part, bool cancel_it)
{
    bool busy = false;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < MAX_JOBS; ++i)
        if (active[i] && strcmp(active[i]->part, part) == 0)
        {
            busy = true;
            if (cancel_it)
                active[i]->cancel = 1;
        }
    pthread_mutex_unlock(&jobs_lock);
    return busy;
}

static bool job_register(Job *job)
{
    part_busy(job->part, true);
    for (int i = 0; i < 600 && part_busy(job->part, false); ++i) /* up to 30 s */
    {
        struct timespec ts = {0, 50 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    bool ok = false;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < MAX_JOBS && !ok; ++i)
        if (!active[i])
        {
            active[i] = job;
            ok = true;
        }
    pthread_mutex_unlock(&jobs_lock);
    return ok;
}

static void job_unregister(Job *job)
{
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < MAX_JOBS; ++i)
        if (active[i] == job)
            active[i] = NULL;
    pthread_mutex_unlock(&jobs_lock);
}

static int job_progress(uint64_t done, uint64_t total, void *user)
{
    Job *job = user;
    if (job->cancel)
        return 1;
    /* the app hung up (a pause): stop at once, not at the next report */
    char c;
    if (recv(job->fd, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
        return 1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double since = (double)(now.tv_sec - job->last.tv_sec) +
                   (double)(now.tv_nsec - job->last.tv_nsec) / 1e9;
    if (since < 0.25)
        return 0;
    job->last = now;
    last_activity = time(NULL);
    /* the app hung up (a pause): stop */
    return send_line(job->fd, "P %llu %llu\n", (unsigned long long)done,
                     (unsigned long long)total)
               ? 0
               : 1;
}

/* splits line at tabs into at most max fields */
static int split(char *line, char **fields, int max)
{
    int n = 0;
    for (char *p = line; n < max;)
    {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab)
            break;
        *tab = 0;
        p = tab + 1;
    }
    return n;
}

static void *serve(void *arg)
{
    int fd = (int)(intptr_t)arg;
    __sync_fetch_and_add(&jobs, 1);
    char line[2048];
    size_t have = 0;
    while (have < sizeof(line) - 1)
    {
        ssize_t n = recv(fd, line + have, sizeof(line) - 1 - have, 0);
        if (n <= 0)
            break;
        have += (size_t)n;
        line[have] = 0;
        if (strchr(line, '\n'))
            break;
    }
    line[have] = 0;
    char *nl = strchr(line, '\n');
    if (nl)
        *nl = 0;
    char *f[13];
    int n = nl ? split(line, f, 13) : 0;
    if (n == 13 && strcmp(f[0], "GET") == 0 && atoi(f[1]) != HELPER_PROTO)
    {
        send_line(fd, "E protocol %d, the app speaks %s\n", HELPER_PROTO, f[1]);
        close(fd);
        app_log("app speaks protocol %s, this helper %d: quitting so it can start its own",
                f[1], HELPER_PROTO);
        exit(0);
    }
    if (n != 13 || strcmp(f[0], "GET") != 0)
    {
        send_line(fd, "E bad request\n");
        close(fd);
        __sync_fetch_and_sub(&jobs, 1);
        return NULL;
    }
    HttpServer server;
    memset(&server, 0, sizeof(server));
    util_copy(server.host, sizeof(server.host), f[2]);
    server.port = atoi(f[3]);
    util_copy(server.base_path, sizeof(server.base_path), f[4]);
    util_copy(server.api_key, sizeof(server.api_key), f[5]);
    util_copy(server.console_id, sizeof(server.console_id), f[6]);
    const char *path = f[7], *part = f[8], *label = f[12];
    uint64_t from = strtoull(f[9], NULL, 10), total = strtoull(f[10], NULL, 10);
    int timeout = atoi(f[11]);
    Job job;
    memset(&job, 0, sizeof(job));
    job.fd = fd;
    util_copy(job.part, sizeof(job.part), part);
    if (!job_register(&job))
    {
        send_line(fd, "E busy\n");
        close(fd);
        __sync_fetch_and_sub(&jobs, 1);
        return NULL;
    }
    int status = 0;
    app_log("job: %s -> %s from %llu", label, part, (unsigned long long)from);
    int rc = fetch_to_file(&server, path, part, from, total, timeout, job_progress, &job, &status,
                           label);
    job_unregister(&job);
    app_log("job: %s done, rc %d, HTTP %d", label, rc, status);
    send_line(fd, "R %d %d\n", rc, status);
    close(fd);
    last_activity = time(NULL);
    __sync_fetch_and_sub(&jobs, 1);
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN); /* a hung-up app shows as a failed send */
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0)
        return 1;
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HELPER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(ls, 8) != 0)
    {
        close(ls); /* one is running already */
        return 0;
    }
    app_log("GameSync helper (protocol %d) listening on 127.0.0.1:%d", HELPER_PROTO, HELPER_PORT);
    last_activity = time(NULL);
    for (;;)
    {
        struct pollfd p = {ls, POLLIN, 0};
        int r = poll(&p, 1, 30 * 1000);
        if (r == 0)
        {
            if (jobs == 0 && time(NULL) - last_activity > IDLE_SECONDS)
            {
                app_log("idle for an hour: quitting");
                break;
            }
            continue;
        }
        if (r < 0)
            continue;
        int fd = accept(ls, NULL, NULL);
        if (fd < 0)
            continue;
        last_activity = time(NULL);
        pthread_t t;
        if (pthread_create(&t, NULL, serve, (void *)(intptr_t)fd) == 0)
            pthread_detach(t);
        else
            close(fd);
    }
    close(ls);
    return 0;
}
