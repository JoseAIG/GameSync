/*
 * GameSync PS5 client - downloading a server path into a file.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_FETCH_H
#define GAMESYNC_FETCH_H

#include "http.h"

#include <stdbool.h>
#include <stdint.h>

/* total, the bytes to have when done: a size (Range segments of 128 MB, the
 * real size taken from Content-Range), 0 for one plain request, or
 * FETCH_SIZE_UNKNOWN for segments once the first reply says the size. */
#define FETCH_SIZE_UNKNOWN UINT64_MAX

/* GET path into the file part, appending from have (0 = a new file).
 * Returns an HTTP_* code; *status is the last HTTP status (206 or 200 when
 * it went through). label names it in the speed log. */
int fetch_to_file(const HttpServer *server, const char *path, const char *part, uint64_t have,
                  uint64_t total, int timeout, HttpProgressFn progress, void *user, int *status,
                  const char *label);

/* The same, through the helper payload when it can be had (the app only). */
int fetch_file(const HttpServer *server, const char *path, const char *part, uint64_t have,
               uint64_t total, int timeout, HttpProgressFn progress, void *user, int *status,
               const char *label);

#endif
