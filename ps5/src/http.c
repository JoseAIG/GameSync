/*
 * GameSync PS5 client - minimal HTTP/1.0 client over BSD sockets.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Same shape as the PS2/NDS clients' http.c: one connection per request,
 * "Connection: close", no chunked encoding, no TLS. The GameSync server is a
 * LAN service and answers HTTP/1.0 with a Content-Length or by closing.
 */
#include "http.h"

#include "version.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

bool http_server_set_url(HttpServer *server, const char *url)
{
    server->host[0] = 0;
    server->port = 80;
    server->base_path[0] = 0;
    const char *p = url;
    while (*p == ' ')
        p++;
    if (strncmp(p, "http://", 7) == 0)
        p += 7;
    else if (strstr(p, "://"))
        return false; /* https and friends: not supported */
    const char *host = p;
    while (*p && *p != ':' && *p != '/')
        p++;
    size_t len = (size_t)(p - host);
    if (len == 0 || len >= sizeof(server->host))
        return false;
    memcpy(server->host, host, len);
    server->host[len] = 0;
    if (*p == ':')
    {
        server->port = atoi(p + 1);
        while (*p && *p != '/')
            p++;
    }
    if (server->port <= 0 || server->port > 65535)
        return false;
#if defined(__PROSPERO__)
    struct in_addr probe;
    if (inet_pton(AF_INET, server->host, &probe) != 1)
        return false; /* no DNS on the PS5 build, see connect_to */
#endif
    snprintf(server->base_path, sizeof(server->base_path), "%s", *p == '/' ? p : "");
    len = strlen(server->base_path);
    while (len > 0 && (server->base_path[len - 1] == '/' || server->base_path[len - 1] == ' '))
        server->base_path[--len] = 0;
    return true;
}

static int connect_to(const HttpServer *server, int timeout_sec)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
#if defined(__PROSPERO__) || defined(__FreeBSD__)
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)server->port);
    if (inet_pton(AF_INET, server->host, &addr.sin_addr) != 1)
    {
#if defined(__PROSPERO__)
        /* getaddrinfo lives only in the browser's libc on the PS5 (calling
         * into it from a game process is unsafe): IP addresses only */
        return -1;
#else
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(server->host, NULL, &hints, &res) != 0 || !res)
            return -1;
        addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
#endif
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct timeval tv = {timeout_sec > 0 ? timeout_sec : 60, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct timeval stv = {60, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
    int rcvbuf = 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
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

static uint64_t header_u64(const HttpResult *result, const char *name)
{
    char value[32];
    return http_header(result, name, value, sizeof(value)) ? strtoull(value, NULL, 10) : 0;
}

int http_request(const HttpServer *server, const HttpRequest *req, HttpResult *result)
{
    memset(result, 0, sizeof(*result));
    if (!server->host[0])
        return HTTP_ERR_URL;
    int fd = connect_to(server, req->timeout_sec);
    if (fd < 0)
        return HTTP_ERR_CONNECT;

    char head[2048];
    int n = snprintf(head, sizeof(head),
                     "%s %s%s HTTP/1.0\r\n"
                     "Host: %s:%d\r\n"
                     "X-API-Key: %s\r\n"
                     "X-Console-ID: %s\r\n"
                     "User-Agent: ps5sync/" APP_VERSION "\r\n"
                     "Connection: close\r\n",
                     req->method ? req->method : "GET", server->base_path, req->path, server->host,
                     server->port, server->api_key, server->console_id);
    if (req->range_end > 0 && n > 0 && (size_t)n < sizeof(head))
        n += snprintf(head + n, sizeof(head) - (size_t)n, "Range: bytes=%llu-%llu\r\n",
                      (unsigned long long)req->range_start, (unsigned long long)req->range_end);
    else if (req->range_start > 0 && n > 0 && (size_t)n < sizeof(head))
        n += snprintf(head + n, sizeof(head) - (size_t)n, "Range: bytes=%llu-\r\n",
                      (unsigned long long)req->range_start);
    if ((req->body_len > 0 || (req->method && strcmp(req->method, "POST") == 0)) && n > 0 &&
        (size_t)n < sizeof(head))
        n += snprintf(head + n, sizeof(head) - (size_t)n,
                      "Content-Type: %s\r\nContent-Length: %llu\r\n",
                      req->content_type ? req->content_type : "application/octet-stream",
                      (unsigned long long)req->body_len);
    if (n > 0 && (size_t)n < sizeof(head))
        n += snprintf(head + n, sizeof(head) - (size_t)n, "\r\n");
    if (n <= 0 || (size_t)n >= sizeof(head))
    {
        close(fd);
        return HTTP_ERR_URL;
    }
    if (!send_all(fd, head, (size_t)n) || (req->body_len && !send_all(fd, req->body, req->body_len)))
    {
        close(fd);
        return HTTP_ERR_SEND;
    }

    /* per request, not static: the download worker and the UI thread both talk HTTP */
    enum
    {
        BUF_SIZE = 256 * 1024
    };
    char *buf = malloc(BUF_SIZE);
    if (!buf)
    {
        close(fd);
        return HTTP_ERR_RECV;
    }
    size_t have = 0;
    char *eoh = NULL;
    while (!eoh)
    {
        if (have >= sizeof(result->headers) - 1)
        {
            free(buf);
            close(fd);
            return HTTP_ERR_RECV;
        }
        ssize_t r = recv(fd, buf + have, sizeof(result->headers) - 1 - have, 0);
        if (r <= 0)
        {
            free(buf);
            close(fd);
            return HTTP_ERR_RECV;
        }
        have += (size_t)r;
        buf[have] = 0;
        eoh = strstr(buf, "\r\n\r\n");
    }
    size_t head_len = (size_t)(eoh - buf) + 4;
    memcpy(result->headers, buf, head_len);
    result->headers[head_len] = 0;
    const char *sp = strchr(result->headers, ' ');
    result->status = sp ? atoi(sp + 1) : 0;
    result->content_length = header_u64(result, "Content-Length");

    uint64_t done = 0;
    int rc = HTTP_OK;
    if (have > head_len)
    {
        size_t extra = have - head_len;
        if (req->sink && req->sink(buf + head_len, extra, req->sink_user) != 0)
            rc = HTTP_ERR_SINK;
        done += extra;
    }
    while (rc == HTTP_OK)
    {
        if (result->content_length && done >= result->content_length)
            break;
        ssize_t r = recv(fd, buf, BUF_SIZE, 0);
        if (r == 0)
            break;
        if (r < 0)
        {
            rc = HTTP_ERR_RECV;
            break;
        }
        if (req->sink && req->sink(buf, (size_t)r, req->sink_user) != 0)
        {
            rc = HTTP_ERR_SINK;
            break;
        }
        done += (uint64_t)r;
        if (req->progress && req->progress(done, result->content_length, req->progress_user) != 0)
            rc = HTTP_CANCELLED;
    }
    if (rc == HTTP_OK && result->content_length && done < result->content_length)
        rc = HTTP_ERR_RECV; /* connection dropped mid-body */
    free(buf);
    close(fd);
    return rc;
}

bool http_header(const HttpResult *result, const char *name, char *out, size_t size)
{
    size_t nlen = strlen(name);
    const char *line = strstr(result->headers, "\r\n");
    while (line && line[2])
    {
        line += 2;
        bool match = true;
        for (size_t i = 0; i < nlen; ++i)
            if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i]))
            {
                match = false;
                break;
            }
        if (match && line[nlen] == ':')
        {
            const char *v = line + nlen + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            const char *end = strstr(v, "\r\n");
            size_t len = end ? (size_t)(end - v) : strlen(v);
            if (len >= size)
                len = size - 1;
            memcpy(out, v, len);
            out[len] = 0;
            return true;
        }
        line = strstr(line, "\r\n");
    }
    if (size)
        out[0] = 0;
    return false;
}

void http_buffer_free(HttpBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->len = buffer->cap = 0;
}

int http_buffer_sink(const void *data, size_t len, void *user)
{
    HttpBuffer *b = user;
    size_t limit = b->limit ? b->limit : 64u * 1024 * 1024;
    if (b->len + len > limit)
        return -1;
    if (b->len + len + 1 > b->cap)
    {
        size_t cap = b->cap ? b->cap : 16 * 1024;
        while (cap < b->len + len + 1)
            cap *= 2;
        uint8_t *d = realloc(b->data, cap);
        if (!d)
            return -1;
        b->data = d;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
    return 0;
}

static int buffered(const HttpServer *server, HttpRequest *req, HttpBuffer *out, HttpResult *result)
{
    HttpBuffer local = {0};
    HttpBuffer *b = out ? out : &local;
    b->len = 0;
    if (http_buffer_sink("", 0, b) != 0) /* always a valid NUL-terminated buffer */
        return HTTP_ERR_SINK;
    req->sink = http_buffer_sink;
    req->sink_user = b;
    HttpResult tmp;
    HttpResult *r = result ? result : &tmp;
    int rc = http_request(server, req, r);
    http_buffer_free(&local);
    return rc == HTTP_OK ? r->status : rc;
}

int http_get(const HttpServer *server, const char *path, HttpBuffer *out, HttpResult *result)
{
    HttpRequest req = {0};
    req.method = "GET";
    req.path = path;
    return buffered(server, &req, out, result);
}

int http_post(const HttpServer *server, const char *path, const void *body, size_t body_len,
              const char *content_type, HttpBuffer *out, HttpResult *result)
{
    HttpRequest req = {0};
    req.method = "POST";
    req.path = path;
    req.body = body;
    req.body_len = body_len;
    req.content_type = content_type;
    return buffered(server, &req, out, result);
}

const char *http_error_text(int code)
{
    switch (code)
    {
    case HTTP_CANCELLED: return "cancelled";
    case HTTP_ERR_URL: return "bad server URL";
    case HTTP_ERR_CONNECT: return "can't reach the server";
    case HTTP_ERR_SEND: return "sending failed";
    case HTTP_ERR_RECV: return "connection dropped";
    case HTTP_ERR_SINK: return "couldn't store the response";
    default: break;
    }
    static char text[32];
    snprintf(text, sizeof(text), "HTTP %d", code);
    return text;
}
