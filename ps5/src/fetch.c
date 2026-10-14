/*
 * GameSync PS5 client - downloading a server path into a file.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Shared by the app and its helper payload (ps5/helper): large files come in
 * Range segments, each on a fresh connection, written through a buffered
 * sink. In the app, fetch_file() hands the job to the helper first - the PS5
 * throttles an app's writes to ~2 MB/s after a ~1.3 GB burst, a payload's
 * not at all - and does it itself when there is no helper.
 */
#include "fetch.h"

#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct
{
    FILE *f;
    double disk;      /* seconds spent writing, for the speed log */
    uint64_t pending; /* bytes written since the last flush to disk */
} FileSink;

/* Written data is pushed to the disk every 128 MB: left to pile up in the
 * system's cache, gigabytes of it made every write slower and slower (a
 * download or an Xbox conversion started fast and nearly stopped, and
 * pausing - which closes the file - made it fast again). */
#define FLUSH_EVERY ((uint64_t)128 << 20)

static void flush_to_disk(FILE *f)
{
    fflush(f);
    fsync(fileno(f));
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int file_sink(const void *data, size_t len, void *user)
{
    FileSink *s = user;
    double t = now_sec();
    bool ok = fwrite(data, 1, len, s->f) == len;
    s->pending += len;
    if (ok && s->pending >= FLUSH_EVERY)
    {
        flush_to_disk(s->f);
        s->pending = 0;
    }
    s->disk += now_sec() - t;
    return ok ? 0 : -1;
}

/* Large downloads come in segments, each on a fresh connection: over Wi-Fi a
 * long-lived connection keeps slowing down (pausing and resuming made it fast
 * again), and a new one starts at full speed. */
#define SEGMENT ((uint64_t)128 << 20)

typedef struct
{
    HttpProgressFn progress;
    void *user;
    uint64_t base;  /* bytes of this attempt before the segment */
    uint64_t total; /* bytes this attempt fetches */
} SegProgress;

static int seg_progress(uint64_t done, uint64_t total, void *user)
{
    (void)total;
    SegProgress *s = user;
    return s->progress ? s->progress(s->base + done, s->total, s->user) : 0;
}

/* GET path into sink->f, bytes [have, total): segment by segment when the
 * size is known, else one request from have. *res is the last response; a
 * server that ignores Range answers 200 with the whole file, which ends it. */
static int ranged_get(const HttpServer *server, const char *path, FileSink *sink, uint64_t have,
                      uint64_t total, int timeout, HttpProgressFn progress, void *user,
                      HttpResult *res, const char *label)
{
    HttpRequest req = {0};
    req.method = "GET";
    req.path = path;
    req.timeout_sec = timeout;
    req.sink = file_sink;
    req.sink_user = sink;
    bool discover = total == UINT64_MAX; /* size unknown: learnt from the first reply */
    if (discover)
        total = have + SEGMENT;
    if (!total || total <= have)
    {
        req.range_start = have;
        req.progress = progress;
        req.progress_user = user;
        return http_request(server, &req, res);
    }
    SegProgress sp = {progress, user, 0, total - have};
    req.progress = seg_progress;
    req.progress_user = &sp;
    uint64_t pos = have, logged = have;
    double t0 = now_sec(), disk0 = sink->disk;
    int rc = HTTP_OK;
    while (pos < total)
    {
        uint64_t end = (pos + SEGMENT < total ? pos + SEGMENT : total) - 1;
        req.range_start = pos;
        req.range_end = end;
        sp.base = pos - have;
        rc = http_request(server, &req, res);
        if (rc != HTTP_OK || res->status != 206)
            break; /* an error, or a 200 that carried the whole file */
        /* the file's real size, from "Content-Range: bytes S-E/TOTAL" */
        char range[96];
        const char *slash = http_header(res, "Content-Range", range, sizeof(range))
                                ? strrchr(range, '/')
                                : NULL;
        if (slash && slash[1] >= '0' && slash[1] <= '9')
        {
            uint64_t real = strtoull(slash + 1, NULL, 10);
            if (real > have)
            {
                total = real;
                sp.total = total - have;
            }
        }
        else if (discover)
            break; /* no size to go on: what came is what there is */
        pos = end + 1;
        if (pos - logged >= ((uint64_t)1 << 30) || pos == total)
        {
            double t = now_sec(), secs = t - t0 > 0.001 ? t - t0 : 0.001;
            app_log("download %s: %.2f GB of %.2f, last %.0f MB at %.1f MB/s (disk %.0f%%)", label,
                    pos / 1e9, total / 1e9, (pos - logged) / 1e6, (pos - logged) / 1e6 / secs,
                    100.0 * (sink->disk - disk0) / secs);
            logged = pos;
            t0 = t;
            disk0 = sink->disk;
        }
    }
    return rc;
}


int fetch_to_file(const HttpServer *server, const char *path, const char *part, uint64_t have,
                  uint64_t total, int timeout, HttpProgressFn progress, void *user, int *status,
                  const char *label)
{
    *status = 0;
    FileSink sink = {fopen(part, have > 0 ? "ab" : "wb"), 0, 0};
    if (!sink.f)
        return HTTP_ERR_SINK;
    char *buffer = malloc(1024 * 1024);
    if (buffer)
        setvbuf(sink.f, buffer, _IOFBF, 1024 * 1024);
    HttpResult res;
    memset(&res, 0, sizeof(res));
    int rc = ranged_get(server, path, &sink, have, total, timeout, progress, user, &res, label);
    bool closed = fclose(sink.f) == 0;
    free(buffer);
    chmod(part, 0666); /* the emulator is another sandboxed title */
    *status = res.status;
    if (rc == HTTP_OK && !closed)
        rc = HTTP_ERR_SINK;
    return rc;
}
