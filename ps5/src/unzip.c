/*
 * GameSync PS5 client - extracting the server's ZIP_STORED archives.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The server packs converted discs (CHD -> CUE/BIN, ?extract=cue) as
 * ZIP_STORED with allowZip64 (server/app/routes/roms.py), so walking the
 * local file headers and copying bytes is enough - the same approach as the
 * PSP and PS3 clients' zip_extract.c, plus the ZIP64 extra field for members
 * of 4 GB and more.
 */
#include "unzip.h"

#include "util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LFH_SIG 0x04034b50u
#define CDH_SIG 0x02014b50u
#define EOCD_SIG 0x06054b50u

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static bool safe_name(const char *name)
{
    if (!name[0] || name[0] == '/' || name[0] == '\\' || strchr(name, ':'))
        return false;
    for (const char *seg = name; *seg;)
    {
        const char *end = seg;
        while (*end && *end != '/' && *end != '\\')
            end++;
        if (end - seg == 2 && seg[0] == '.' && seg[1] == '.')
            return false;
        if (!*end)
            break;
        seg = end + 1;
    }
    return true;
}

static void fail(char *error, size_t size, const char *text)
{
    if (error && size)
        snprintf(error, size, "%s", text);
}

bool unzip_stored(const char *zip_path, const char *out_dir, char *error, size_t size)
{
    FILE *zf = fopen(zip_path, "rb");
    if (!zf)
    {
        fail(error, size, "can't open the archive");
        return false;
    }
    util_mkdirs(out_dir);
    enum
    {
        BUF = 1024 * 1024
    };
    uint8_t *buf = malloc(BUF);
    if (!buf)
    {
        fclose(zf);
        fail(error, size, "out of memory");
        return false;
    }
    bool ok = true;
    for (;;)
    {
        uint8_t h[30];
        size_t got = fread(h, 1, sizeof(h), zf);
        if (got == 0)
            break;
        if (got < 4)
        {
            ok = false;
            fail(error, size, "truncated archive");
            break;
        }
        uint32_t sig = le32(h);
        if (sig == CDH_SIG || sig == EOCD_SIG)
            break; /* past the members */
        if (sig != LFH_SIG || got < sizeof(h))
        {
            ok = false;
            fail(error, size, "not a zip archive");
            break;
        }
        uint16_t flags = le16(h + 6), method = le16(h + 8);
        uint64_t csize = le32(h + 18), usize = le32(h + 22);
        uint16_t name_len = le16(h + 26), extra_len = le16(h + 28);
        char name[512];
        uint8_t extra[1024];
        if (name_len >= sizeof(name) || extra_len > sizeof(extra) ||
            fread(name, 1, name_len, zf) != name_len || fread(extra, 1, extra_len, zf) != extra_len)
        {
            ok = false;
            fail(error, size, "bad member header");
            break;
        }
        name[name_len] = 0;
        /* ZIP64: the real sizes are in extra field 0x0001 */
        for (uint16_t p = 0; p + 4 <= extra_len;)
        {
            uint16_t id = le16(extra + p), len = le16(extra + p + 2);
            if (id == 0x0001)
            {
                uint16_t q = p + 4;
                if (usize == 0xFFFFFFFFu && q + 8 <= p + 4 + len)
                {
                    usize = le64(extra + q);
                    q += 8;
                }
                if (csize == 0xFFFFFFFFu && q + 8 <= p + 4 + len)
                    csize = le64(extra + q);
            }
            p += 4 + len;
        }
        if (method != 0 || (flags & 1) || (flags & 8) || csize != usize)
        {
            ok = false;
            fail(error, size, "compressed or streamed members aren't supported");
            break;
        }
        if (!safe_name(name))
        {
            ok = false;
            fail(error, size, "unsafe path in the archive");
            break;
        }
        char path[PATH_LEN + 512];
        snprintf(path, sizeof(path), "%s/%s", out_dir, name);
        if (name[name_len - 1] == '/')
        {
            util_mkdirs(path);
            continue;
        }
        util_mkdirs_parent(path);
        FILE *out = fopen(path, "wb");
        if (!out)
        {
            ok = false;
            fail(error, size, "can't create a file from the archive");
            break;
        }
        uint64_t left = usize;
        while (ok && left > 0)
        {
            size_t want = left < BUF ? (size_t)left : BUF;
            size_t r = fread(buf, 1, want, zf);
            if (r == 0 || fwrite(buf, 1, r, out) != r)
            {
                ok = false;
                fail(error, size, r == 0 ? "archive ends early" : "write failed (disk full?)");
            }
            left -= r;
        }
        if (fclose(out) != 0)
            ok = false;
        chmod(path, 0666);
        if (!ok)
            break;
    }
    free(buf);
    fclose(zf);
    return ok;
}
