/*
 * GameSync PS5 client - save folders and the 3DSS bundle format.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Layout as in server/app/services/bundle.py (and the Vita/Wii U clients'
 * bundle.c): "3DSS", u32 version, title id (v1/v2: u64 BE; v3: 16 bytes;
 * v4: 32; v5: 64), u32 timestamp, u32 file count, u32 payload size, then
 * the zlib payload (v1: plain): per file u16 path length, path, u32 size,
 * sha256; then the file data in the same order.
 */
#include "bundle.h"

#include "sha256.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define MAX_PAYLOAD (512u * 1024 * 1024)

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* ------------------------------------------------------------ trees */

typedef struct
{
    TreeFile *files;
    int count, cap;
} Builder;

static bool push(Builder *b, const char *path, uint8_t *data, uint32_t size)
{
    if (b->count == b->cap)
    {
        int cap = b->cap ? b->cap * 2 : 32;
        TreeFile *grown = realloc(b->files, (size_t)cap * sizeof(TreeFile));
        if (!grown)
            return false;
        b->files = grown;
        b->cap = cap;
    }
    TreeFile *f = &b->files[b->count++];
    util_copy(f->path, sizeof(f->path), path);
    f->data = data;
    f->size = size;
    return true;
}

static bool walk(const char *root, const char *rel, const char *skip_top, Builder *b)
{
    char dir[PATH_LEN];
    if (rel[0])
        util_path_join(dir, sizeof(dir), root, rel);
    else
        util_copy(dir, sizeof(dir), root);
    DIR *d = opendir(dir);
    if (!d)
        return false;
    bool ok = true;
    struct dirent *de;
    while (ok && (de = readdir(d)) != NULL)
    {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!rel[0] && skip_top && strcmp(de->d_name, skip_top) == 0)
            continue;
        char child_rel[256], child[PATH_LEN];
        if (rel[0])
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name);
        else
            snprintf(child_rel, sizeof(child_rel), "%s", de->d_name);
        util_path_join(child, sizeof(child), root, child_rel);
        bool is_dir = de->d_type == DT_DIR || (de->d_type == DT_UNKNOWN && util_dir_exists(child));
        if (is_dir)
        {
            ok = walk(root, child_rel, skip_top, b);
            continue;
        }
        size_t size;
        uint8_t *data = util_read_file(child, &size);
        if (!data)
        {
            ok = false;
            break;
        }
        ok = push(b, child_rel, data, (uint32_t)size);
        if (!ok)
            free(data);
    }
    closedir(d);
    return ok;
}

static int by_path(const void *a, const void *b)
{
    return strcmp(((const TreeFile *)a)->path, ((const TreeFile *)b)->path);
}

bool tree_read(const char *dir, const char *skip_top, SaveTree *out)
{
    memset(out, 0, sizeof(*out));
    Builder b = {0};
    if (!walk(dir, "", skip_top, &b))
    {
        SaveTree t = {b.files, b.count};
        tree_free(&t);
        return false;
    }
    if (b.count > 1)
        qsort(b.files, (size_t)b.count, sizeof(TreeFile), by_path);
    out->files = b.files;
    out->count = b.count;
    return true;
}

void tree_free(SaveTree *tree)
{
    for (int i = 0; i < tree->count; ++i)
        free(tree->files[i].data);
    free(tree->files);
    tree->files = NULL;
    tree->count = 0;
}

void tree_hash(const SaveTree *tree, char out[65], uint64_t *total_size)
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    uint64_t total = 0;
    for (int i = 0; i < tree->count; ++i)
    {
        sha256_update(&ctx, tree->files[i].data, tree->files[i].size);
        total += tree->files[i].size;
    }
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i)
    {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
    if (total_size)
        *total_size = total;
}

bool remove_tree(const char *path)
{
    /* never anything as short as "/" or "/data" */
    int slashes = 0;
    for (const char *p = path; *p; ++p)
        slashes += *p == '/';
    if (slashes < 2)
        return false;
    DIR *d = opendir(path);
    if (!d)
        return remove(path) == 0 || !util_file_exists(path);
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        char child[PATH_LEN];
        util_path_join(child, sizeof(child), path, de->d_name);
        bool is_dir = de->d_type == DT_DIR || (de->d_type == DT_UNKNOWN && util_dir_exists(child));
        if (is_dir)
            remove_tree(child);
        else
            remove(child);
    }
    closedir(d);
    return rmdir(path) == 0;
}

bool tree_write_replace(const char *dir, const SaveTree *tree)
{
    char fresh[PATH_LEN + 16], bak[PATH_LEN + 16];
    snprintf(fresh, sizeof(fresh), "%s.gs_new", dir);
    snprintf(bak, sizeof(bak), "%s.bak", dir);
    remove_tree(fresh);
    if (!util_mkdirs(fresh))
        return false;
    for (int i = 0; i < tree->count; ++i)
    {
        const TreeFile *f = &tree->files[i];
        if (strstr(f->path, "..") || f->path[0] == '/')
        {
            remove_tree(fresh);
            return false; /* never write outside the save folder */
        }
        char path[PATH_LEN + 300];
        snprintf(path, sizeof(path), "%s/%s", fresh, f->path);
        if (!util_write_file_atomic(path, f->data, f->size))
        {
            remove_tree(fresh);
            return false;
        }
    }
    bool had_old = util_dir_exists(dir);
    if (had_old)
    {
        remove_tree(bak);
        if (rename(dir, bak) != 0)
        {
            remove_tree(fresh);
            return false;
        }
    }
    else
        util_mkdirs_parent(dir);
    if (rename(fresh, dir) != 0)
    {
        if (had_old)
            rename(bak, dir); /* put the old save back */
        remove_tree(fresh);
        return false;
    }
    chmod(dir, 0777);
    return true;
}

/* ------------------------------------------------------------ bundles */

uint8_t *bundle_build(const char *title_id, const SaveTree *tree, uint32_t timestamp, size_t *size)
{
    size_t payload_len = 0;
    for (int i = 0; i < tree->count; ++i)
        payload_len += 2 + strlen(tree->files[i].path) + 4 + 32 + tree->files[i].size;
    if (payload_len > MAX_PAYLOAD || strlen(title_id) > 31)
        return NULL;
    uint8_t *payload = malloc(payload_len ? payload_len : 1);
    if (!payload)
        return NULL;
    uint8_t *p = payload;
    for (int i = 0; i < tree->count; ++i)
    {
        const TreeFile *f = &tree->files[i];
        size_t len = strlen(f->path);
        p[0] = (uint8_t)len;
        p[1] = (uint8_t)(len >> 8);
        p += 2;
        memcpy(p, f->path, len);
        p += len;
        wr32(p, f->size);
        p += 4;
        sha256(f->data, f->size, p);
        p += 32;
    }
    for (int i = 0; i < tree->count; ++i)
    {
        memcpy(p, tree->files[i].data, tree->files[i].size);
        p += tree->files[i].size;
    }
    uLongf zlen = compressBound((uLong)payload_len);
    uint8_t *out = malloc(52 + zlen);
    if (!out)
    {
        free(payload);
        return NULL;
    }
    if (compress2(out + 52, &zlen, payload, (uLong)payload_len, 6) != Z_OK)
    {
        free(payload);
        free(out);
        return NULL;
    }
    free(payload);
    memcpy(out, "3DSS", 4);
    wr32(out + 4, 4);
    memset(out + 8, 0, 32);
    memcpy(out + 8, title_id, strlen(title_id));
    wr32(out + 40, timestamp);
    wr32(out + 44, (uint32_t)tree->count);
    wr32(out + 48, (uint32_t)payload_len);
    *size = 52 + zlen;
    return out;
}

bool bundle_parse(const uint8_t *data, size_t size, SaveTree *out)
{
    memset(out, 0, sizeof(*out));
    if (size < 24 || memcmp(data, "3DSS", 4) != 0)
        return false;
    uint32_t version = rd32(data + 4);
    size_t id_len;
    switch (version)
    {
    case 1:
    case 2: id_len = 8; break;
    case 3: id_len = 16; break;
    case 4: id_len = 32; break;
    case 5: id_len = 64; break;
    default: return false;
    }
    size_t hdr = 8 + id_len + 12;
    if (size < hdr)
        return false;
    uint32_t count = rd32(data + 8 + id_len + 4);
    uint32_t payload_len = rd32(data + 8 + id_len + 8);
    if (count > 100000 || payload_len > MAX_PAYLOAD)
        return false;
    uint8_t *payload;
    if (version == 1)
    {
        if (size - hdr < payload_len)
            return false;
        payload = malloc(payload_len ? payload_len : 1);
        if (!payload)
            return false;
        memcpy(payload, data + hdr, payload_len);
    }
    else
    {
        payload = malloc(payload_len ? payload_len : 1);
        if (!payload)
            return false;
        uLongf got = payload_len;
        if (uncompress(payload, &got, data + hdr, (uLong)(size - hdr)) != Z_OK || got != payload_len)
        {
            free(payload);
            return false;
        }
    }
    Builder b = {0};
    size_t pos = 0;
    typedef struct
    {
        char path[256];
        uint32_t size;
        uint8_t hash[32];
    } Entry;
    Entry *entries = calloc(count ? count : 1, sizeof(Entry));
    bool ok = entries != NULL;
    for (uint32_t i = 0; ok && i < count; ++i)
    {
        if (pos + 2 > payload_len)
        {
            ok = false;
            break;
        }
        uint16_t len = (uint16_t)(payload[pos] | payload[pos + 1] << 8);
        pos += 2;
        if (len >= sizeof(entries[i].path) || pos + len + 36 > payload_len)
        {
            ok = false;
            break;
        }
        memcpy(entries[i].path, payload + pos, len);
        entries[i].path[len] = 0;
        pos += len;
        entries[i].size = rd32(payload + pos);
        pos += 4;
        memcpy(entries[i].hash, payload + pos, 32);
        pos += 32;
    }
    for (uint32_t i = 0; ok && i < count; ++i)
    {
        if (entries[i].size > payload_len - pos)
        {
            ok = false;
            break;
        }
        uint8_t digest[32];
        sha256(payload + pos, entries[i].size, digest);
        uint8_t *copy = malloc(entries[i].size ? entries[i].size : 1);
        ok = copy && memcmp(digest, entries[i].hash, 32) == 0;
        if (ok)
        {
            memcpy(copy, payload + pos, entries[i].size);
            ok = push(&b, entries[i].path, copy, entries[i].size);
        }
        if (!ok)
            free(copy);
        pos += entries[i].size;
    }
    free(entries);
    free(payload);
    out->files = b.files;
    out->count = b.count;
    if (!ok)
        tree_free(out);
    return ok;
}
