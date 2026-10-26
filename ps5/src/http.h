/*
 * GameSync PS5 client - minimal HTTP/1.0 client over BSD sockets.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_HTTP_H
#define GAMESYNC_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    char host[128];
    int port;
    char base_path[128]; /* path prefix from the URL, "" for none */
    char api_key[160];
    char console_id[64];
} HttpServer;

/* "http://192.168.1.10:8000" -> host/port. Plain HTTP only; the PS5 build
 * takes an IP address, not a host name. */
bool http_server_set_url(HttpServer *server, const char *url);

/* Body sink: return 0 to continue, non-zero to abort. */
typedef int (*HttpSinkFn)(const void *data, size_t len, void *user);
/* Progress: return non-zero to cancel. total is 0 when unknown. */
typedef int (*HttpProgressFn)(uint64_t done, uint64_t total, void *user);

typedef struct
{
    const char *method;       /* "GET" when NULL */
    const char *path;         /* "/api/v1/..." (query included) */
    const void *body;
    size_t body_len;
    const char *content_type; /* defaults to application/octet-stream */
    uint64_t range_start;     /* sends Range: bytes=N- when > 0 */
    uint64_t range_end;       /* with it, bytes=N-E (inclusive) when > 0 */
    int timeout_sec;          /* receive timeout, 0 = 60 s */
    HttpSinkFn sink;
    void *sink_user;
    HttpProgressFn progress;
    void *progress_user;
} HttpRequest;

typedef struct
{
    int status;               /* 0 when the server was never reached */
    uint64_t content_length;  /* 0 when absent */
    char headers[2048];       /* raw response header block */
} HttpResult;

enum
{
    HTTP_OK = 0,
    HTTP_CANCELLED = 1,
    HTTP_ERR_URL = -1,
    HTTP_ERR_CONNECT = -2,
    HTTP_ERR_SEND = -3,
    HTTP_ERR_RECV = -4,
    HTTP_ERR_SINK = -5,
};

/* Sends one request and streams the body (any status) to req->sink. */
int http_request(const HttpServer *server, const HttpRequest *req, HttpResult *result);

/* Copies a response header's value (case-insensitive name). */
bool http_header(const HttpResult *result, const char *name, char *out, size_t size);

/* Whole body in memory, NUL-terminated. */
typedef struct
{
    uint8_t *data;
    size_t len;
    size_t cap;
    size_t limit; /* 0 = 64 MB */
} HttpBuffer;

void http_buffer_free(HttpBuffer *buffer);
int http_buffer_sink(const void *data, size_t len, void *user);

/* GET/POST into a buffer. Returns the HTTP status, or a negative HTTP_ERR_*. */
int http_get(const HttpServer *server, const char *path, HttpBuffer *out, HttpResult *result);
int http_post(const HttpServer *server, const char *path, const void *body, size_t body_len,
              const char *content_type, HttpBuffer *out, HttpResult *result);

const char *http_error_text(int code);

#endif
