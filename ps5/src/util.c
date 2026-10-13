/*
 * GameSync PS5 client - small file and string helpers.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Existence checks go through fopen/opendir rather than access()/stat():
 * on a PS5 title that is still sandboxed, access() and lstat() are refused
 * even for files that open fine.
 */
#include "util.h"

#include "sha256.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

uint8_t *util_read_file(const char *path, size_t *size)
{
    if (size)
        *size = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)len + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len)
    {
        free(buf);
        return NULL;
    }
    buf[len] = 0;
    if (size)
        *size = (size_t)len;
    return buf;
}

bool util_write_file_atomic(const char *path, const void *data, size_t size)
{
    char tmp[PATH_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!util_mkdirs_parent(path))
        return false;
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (ok)
    {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok)
        remove(tmp);
    else
        chmod(path, 0666);
    return ok;
}

bool util_copy_file(const char *from, const char *to)
{
    size_t size;
    uint8_t *data = util_read_file(from, &size);
    if (!data)
        return false;
    bool ok = util_write_file_atomic(to, data, size);
    free(data);
    return ok;
}

bool util_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

bool util_dir_exists(const char *path)
{
    DIR *d = opendir(path);
    if (!d)
        return false;
    closedir(d);
    return true;
}

int64_t util_file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    int64_t size = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        size = (int64_t)ftell(f);
    fclose(f);
    return size;
}

bool util_mkdirs(const char *path)
{
    char buf[PATH_LEN];
    util_copy(buf, sizeof(buf), path);
    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/')
        buf[--len] = 0;
    for (char *p = buf + 1; *p; ++p)
    {
        if (*p != '/')
            continue;
        *p = 0;
        if (mkdir(buf, 0777) == 0)
            chmod(buf, 0777);
        *p = '/';
    }
    if (mkdir(buf, 0777) == 0)
    {
        chmod(buf, 0777);
        return true;
    }
    return errno == EEXIST || util_dir_exists(buf);
}

bool util_mkdirs_parent(const char *path)
{
    char dir[PATH_LEN];
    util_copy(dir, sizeof(dir), path);
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir)
        return true;
    *slash = 0;
    return util_mkdirs(dir);
}

static void hex_digest(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i)
    {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
}

bool util_sha256_file(const char *path, char out[65])
{
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    SHA256_CTX ctx;
    sha256_init(&ctx);
    uint8_t chunk[16 * 1024];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        sha256_update(&ctx, chunk, n);
    bool ok = !ferror(f);
    fclose(f);
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    if (ok)
        hex_digest(digest, out);
    return ok;
}

void util_sha256_hex(const void *data, size_t size, char out[65])
{
    uint8_t digest[32];
    sha256((const uint8_t *)data, size, digest);
    hex_digest(digest, out);
}

void util_path_join(char *out, size_t size, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/')
        snprintf(out, size, "%s%s", dir, name);
    else
        snprintf(out, size, "%s/%s", dir, name);
}

void util_strip_ext(char *name)
{
    char *dot = strrchr(name, '.');
    if (dot && dot != name)
        *dot = 0;
}

const char *util_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

const char *util_ext(const char *path)
{
    const char *base = util_basename(path);
    const char *dot = strrchr(base, '.');
    return dot && dot != base ? dot : "";
}

bool util_ieq(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

bool util_has_suffix_i(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && util_ieq(s + ls - lx, suffix);
}

void util_copy(char *out, size_t size, const char *s)
{
    if (!size)
        return;
    snprintf(out, size, "%s", s ? s : "");
}

void util_compact_serial(char *out, size_t size, const char *s)
{
    size_t o = 0;
    for (; *s && o + 1 < size; ++s)
    {
        unsigned char c = (unsigned char)toupper((unsigned char)*s);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            out[o++] = (char)c;
    }
    if (size)
        out[o] = 0;
}

void util_safe_filename(char *out, size_t size, const char *s)
{
    size_t o = 0;
    for (; *s && o + 1 < size; ++s)
    {
        unsigned char c = (unsigned char)*s;
        out[o++] = (c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                    c == '"' || c == '<' || c == '>' || c == '|')
                       ? '_'
                       : (char)c;
    }
    if (size)
        out[o] = 0;
}

void util_format_size(char *out, size_t size, uint64_t bytes)
{
    if (bytes >= 1024ull * 1024 * 1024)
        snprintf(out, size, "%.2f GB", (double)bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ull * 1024)
        snprintf(out, size, "%.1f MB", (double)bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        snprintf(out, size, "%.0f KB", (double)bytes / 1024.0);
    else
        snprintf(out, size, "%u B", (unsigned)bytes);
}

void util_url_encode(char *out, size_t size, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s; ++s)
    {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            if (o + 1 >= size)
                break;
            out[o++] = (char)c;
        }
        else
        {
            if (o + 3 >= size)
                break;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    if (size)
        out[o] = 0;
}
