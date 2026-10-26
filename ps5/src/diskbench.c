/*
 * GameSync PS5 client - a disk write-speed test, on request.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Big files (an Xbox ISO, a long download) start fast and then crawl. This
 * measures how writes behave here, from inside the app's own sandbox, when
 * <data>/bench.request exists at start-up (create it over FTP): each way of
 * writing a test file is timed per 256 MB into gamesync.log, for up to 2 GB
 * or 2 minutes, and the file is deleted.
 */
#include "diskbench.h"

#include "config.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BENCH_TOTAL ((uint64_t)2 << 30)
#define BENCH_STEP ((uint64_t)256 << 20)
#define BENCH_CHUNK (8u << 20)
#define BENCH_SECONDS 120.0

#ifndef O_DIRECT
#define O_DIRECT 0x00010000 /* FreeBSD's value */
#endif

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

bool diskbench_requested(void)
{
    return util_file_exists(data_path("bench.request"));
}

typedef enum
{
    B_WRITE,    /* write() 8 MB chunks */
    B_STDIO,    /* fwrite 2 KB pieces through a 1 MB buffer, as the CCI conversion did */
    B_REOPEN,   /* write() 8 MB, the file closed and reopened every 256 MB (a pause) */
    B_FSYNC,    /* write() 8 MB, fsync every 256 MB */
    B_PREALLOC, /* ftruncate to the full size, then pwrite 8 MB */
    B_DIRECT,   /* O_DIRECT write() 8 MB */
} BenchMode;

static const char *const NAMES[] = {"write 8MB", "stdio 2KB", "reopen/256MB", "fsync/256MB",
                                    "prealloc+pwrite", "O_DIRECT"};

static void bench(BenchMode mode, const char *path, uint8_t *chunk,
                  void (*tick)(const char *, void *), void *user)
{
    remove(path);
    const char *name = NAMES[mode];
    tick(name, user);
    int flags = O_WRONLY | O_CREAT | O_TRUNC | (mode == B_DIRECT ? O_DIRECT : 0);
    int fd = -1;
    FILE *f = NULL;
    if (mode == B_STDIO)
    {
        f = fopen(path, "wb");
        if (f)
            setvbuf(f, NULL, _IOFBF, 1 << 20);
    }
    else
        fd = open(path, flags, 0666);
    if (mode == B_STDIO ? !f : fd < 0)
    {
        app_log("disk test %s: can't open (%d)", name, errno);
        return;
    }
    if (mode == B_PREALLOC && ftruncate(fd, (off_t)BENCH_TOTAL) != 0)
        app_log("disk test %s: ftruncate failed (%d)", name, errno);
    double t0 = now_sec(), ts = t0;
    uint64_t done = 0;
    while (done < BENCH_TOTAL && now_sec() - t0 < BENCH_SECONDS)
    {
        bool ok;
        if (mode == B_STDIO)
        {
            ok = true;
            for (uint32_t i = 0; i < BENCH_CHUNK && ok; i += 2048)
                ok = fwrite(chunk + i, 1, 2048, f) == 2048;
        }
        else if (mode == B_PREALLOC)
            ok = pwrite(fd, chunk, BENCH_CHUNK, (off_t)done) == (ssize_t)BENCH_CHUNK;
        else
            ok = write(fd, chunk, BENCH_CHUNK) == (ssize_t)BENCH_CHUNK;
        if (!ok)
        {
            app_log("disk test %s: write failed at %llu MB (%d)", name,
                    (unsigned long long)(done >> 20), errno);
            break;
        }
        done += BENCH_CHUNK;
        if (done % BENCH_STEP == 0)
        {
            if (mode == B_FSYNC)
                fsync(fd);
            if (mode == B_REOPEN)
            {
                close(fd);
                fd = open(path, O_WRONLY | O_APPEND);
                if (fd < 0)
                {
                    app_log("disk test %s: reopen failed (%d)", name, errno);
                    break;
                }
            }
            double t = now_sec();
            app_log("disk test %s: %4llu MB at %6.1f MB/s", name, (unsigned long long)(done >> 20),
                    (double)(BENCH_STEP >> 20) / (t - ts));
            ts = t;
        }
    }
    if (f)
        fclose(f);
    if (fd >= 0)
        close(fd);
    app_log("disk test %s: %llu MB in %.1f s = %.1f MB/s", name, (unsigned long long)(done >> 20),
            now_sec() - t0, (double)(done >> 20) / (now_sec() - t0));
    remove(path);
}

void diskbench_run(void (*tick)(const char *mode, void *user), void *user)
{
    remove(data_path("bench.request"));
    uint8_t *chunk = NULL;
    /* aligned, for O_DIRECT */
    if (posix_memalign((void **)&chunk, 4096, BENCH_CHUNK) != 0 || !chunk)
    {
        app_log("disk test: out of memory");
        return;
    }
    for (uint32_t i = 0; i < BENCH_CHUNK; ++i)
        chunk[i] = (uint8_t)((i * 2654435761u) >> 13);
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s", data_path("bench.tmp"));
    app_log("disk test: start (test file %s)", path);
    for (int m = B_WRITE; m <= B_DIRECT; ++m)
        bench((BenchMode)m, path, chunk, tick, user);
    app_log("disk test: done");
    free(chunk);
}
