/*
 * GameSync PS5 client - PS2 memory card (8 MB) parser/builder.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A C port of the server's server/app/services/ps2mc.py (filesystem) and
 * ps2_cards.py (ECC spare bytes). Both sides must lay a card out the same
 * way: the server hashes the P2FD of a folder, so the bytes this file puts
 * in a P2FD have to match what the server would build from the same card.
 *
 * Format references (as for the server code): the ps2savetools.com "PS2
 * Memory Card File System" notes and ps2dev/mymc by Ross Ridge.
 *
 * PS5SX2 (like PCSX2) keeps every game on one shared card (Mcd001.ps2). To
 * change one game's folder the whole card is parsed, the folder swapped, and
 * a fresh card built from the result: no in-place FAT surgery, so a bug can't
 * leave a half-linked chain behind. File contents, names, modes and
 * timestamps carry over; the previous image is kept as <card>.bak.
 */
#include "ps2mc.h"

#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PAGE_SIZE 512u
#define SPARE_SIZE 16u
#define PAGES 16384u
#define CLUSTER_SIZE 1024u
#define CLUSTERS_PER_CARD 8192u
#define ALLOC_OFFSET 41u
#define ALLOC_END 8135u
#define IFC_CLUSTER 8u
#define FAT_CLUSTER_START 9u
#define FAT_CLUSTER_COUNT 32u
#define FAT_PER_CLUSTER 256u
#define DIRENTS_PER_CLUSTER 2u

#define FAT_ALLOCATED 0x80000000u
#define FAT_MASK 0x7FFFFFFFu
#define FAT_CHAIN_END 0xFFFFFFFFu
#define FAT_FREE 0x7FFFFFFFu

#define DF_DIRECTORY 0x0020u
#define DF_EXISTS 0x8000u
#define MODE_DIR 0x8427u
#define MODE_DOTDOT 0xA426u
#define MODE_FILE 0x8417u

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void set_error(char *error, size_t size, const char *text)
{
    if (error && size)
        snprintf(error, size, "%s", text);
}

/* ------------------------------------------------------------ reading */

typedef struct
{
    const uint8_t *data;
    uint32_t alloc_offset;
    uint32_t rootdir;
    uint32_t ifc[32];
} Reader;

static const uint8_t *cluster_ptr(const Reader *r, uint32_t cluster)
{
    if (cluster >= CLUSTERS_PER_CARD)
        return NULL;
    return r->data + (size_t)cluster * CLUSTER_SIZE;
}

static bool fat_value(const Reader *r, uint32_t index, uint32_t *value)
{
    uint32_t indirect_index = index / FAT_PER_CLUSTER;
    if (indirect_index / FAT_PER_CLUSTER >= 32)
        return false;
    const uint8_t *ifc = cluster_ptr(r, r->ifc[indirect_index / FAT_PER_CLUSTER]);
    if (!ifc)
        return false;
    const uint8_t *fat = cluster_ptr(r, rd32(ifc + (indirect_index % FAT_PER_CLUSTER) * 4));
    if (!fat)
        return false;
    *value = rd32(fat + (index % FAT_PER_CLUSTER) * 4);
    return true;
}

/* Up to max clusters of the chain starting at first; returns the count. */
static uint32_t chain(const Reader *r, uint32_t first, uint32_t max, uint32_t *out)
{
    uint32_t n = 0;
    uint32_t cluster = first;
    while (cluster != FAT_MASK && n < max)
    {
        out[n++] = cluster;
        uint32_t entry;
        if (!fat_value(r, cluster, &entry) || !(entry & FAT_ALLOCATED))
            break;
        uint32_t next = entry & FAT_MASK;
        if (next == FAT_MASK)
            break;
        cluster = next;
    }
    return n;
}

typedef struct
{
    uint16_t mode;
    uint32_t length;
    uint32_t cluster;
    uint8_t created[8];
    uint8_t modified[8];
    char name[33];
} Dirent;

static void unpack_dirent(const uint8_t *p, Dirent *d)
{
    d->mode = rd16(p);
    d->length = rd32(p + 4);
    memcpy(d->created, p + 8, 8);
    d->cluster = rd32(p + 16);
    memcpy(d->modified, p + 24, 8);
    memcpy(d->name, p + 64, 32);
    d->name[32] = 0;
}

/* Reads count dirents of the directory at first_cluster; malloc'd array. */
static Dirent *read_dir(const Reader *r, uint32_t first, uint32_t count, uint32_t *got)
{
    *got = 0;
    if (count == 0 || count > CLUSTERS_PER_CARD * DIRENTS_PER_CLUSTER)
        return NULL;
    uint32_t need = (count + DIRENTS_PER_CLUSTER - 1) / DIRENTS_PER_CLUSTER;
    uint32_t *clusters = malloc(need * sizeof(uint32_t));
    Dirent *out = calloc(count, sizeof(Dirent));
    if (!clusters || !out)
    {
        free(clusters);
        free(out);
        return NULL;
    }
    uint32_t n = chain(r, first, need, clusters);
    for (uint32_t c = 0; c < n && *got < count; ++c)
    {
        const uint8_t *cp = cluster_ptr(r, r->alloc_offset + clusters[c]);
        if (!cp)
            break;
        for (uint32_t i = 0; i < DIRENTS_PER_CLUSTER && *got < count; ++i)
            unpack_dirent(cp + i * 512, &out[(*got)++]);
    }
    free(clusters);
    return out;
}

static uint8_t *read_file(const Reader *r, uint32_t first, uint32_t length)
{
    uint8_t *out = malloc(length ? length : 1);
    if (!out || length == 0 || first == FAT_MASK)
        return out;
    uint32_t need = (length + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
    uint32_t *clusters = malloc(need * sizeof(uint32_t));
    if (!clusters)
    {
        free(out);
        return NULL;
    }
    uint32_t n = chain(r, first, need, clusters);
    memset(out, 0, length);
    for (uint32_t c = 0; c < n; ++c)
    {
        const uint8_t *cp = cluster_ptr(r, r->alloc_offset + clusters[c]);
        if (!cp)
            break;
        uint32_t off = c * CLUSTER_SIZE;
        uint32_t take = length - off < CLUSTER_SIZE ? length - off : CLUSTER_SIZE;
        memcpy(out + off, cp, take);
    }
    free(clusters);
    return out;
}

static bool is_dot(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

bool ps2mc_parse(const uint8_t *mc2, Ps2Card *out, char *error, size_t error_size)
{
    memset(out, 0, sizeof(*out));
    if (memcmp(mc2, "Sony PS2 Memory Card Format", 27) != 0)
    {
        set_error(error, error_size, "not a PS2 memory card (bad superblock)");
        return false;
    }
    Reader r;
    r.data = mc2;
    r.alloc_offset = rd32(mc2 + 52);
    r.rootdir = rd32(mc2 + 60);
    for (int i = 0; i < 32; ++i)
        r.ifc[i] = rd32(mc2 + 80 + i * 4);
    if (r.alloc_offset >= CLUSTERS_PER_CARD)
    {
        set_error(error, error_size, "corrupt superblock");
        return false;
    }
    const uint8_t *root = cluster_ptr(&r, r.alloc_offset + r.rootdir);
    if (!root)
    {
        set_error(error, error_size, "corrupt root directory");
        return false;
    }
    Dirent dot;
    unpack_dirent(root, &dot);
    if (dot.length == 0 || dot.length > CLUSTERS_PER_CARD)
    {
        set_error(error, error_size, "corrupt root directory entry count");
        return false;
    }
    uint32_t root_count;
    Dirent *entries = read_dir(&r, r.rootdir, dot.length, &root_count);
    if (!entries)
    {
        set_error(error, error_size, "can't read the root directory");
        return false;
    }
    out->dirs = calloc(root_count ? root_count : 1, sizeof(Ps2Dir));
    if (!out->dirs)
    {
        free(entries);
        set_error(error, error_size, "out of memory");
        return false;
    }
    for (uint32_t i = 0; i < root_count; ++i)
    {
        const Dirent *e = &entries[i];
        if (is_dot(e->name) || !(e->mode & DF_EXISTS) || !(e->mode & DF_DIRECTORY))
            continue;
        Ps2Dir *dir = &out->dirs[out->count++];
        memcpy(dir->name, e->name, sizeof(dir->name));
        dir->mode = e->mode;
        memcpy(dir->created, e->created, 8);
        memcpy(dir->modified, e->modified, 8);
        uint32_t fcount;
        Dirent *files = read_dir(&r, e->cluster, e->length, &fcount);
        if (!files)
            continue;
        dir->files = calloc(fcount ? fcount : 1, sizeof(Ps2File));
        for (uint32_t k = 0; dir->files && k < fcount; ++k)
        {
            const Dirent *f = &files[k];
            if (is_dot(f->name) || !(f->mode & DF_EXISTS) || (f->mode & DF_DIRECTORY))
                continue;
            Ps2File *pf = &dir->files[dir->file_count];
            pf->data = read_file(&r, f->cluster, f->length);
            if (!pf->data)
                continue;
            memcpy(pf->name, f->name, sizeof(pf->name));
            pf->size = f->length;
            pf->mode = f->mode;
            memcpy(pf->created, f->created, 8);
            memcpy(pf->modified, f->modified, 8);
            dir->file_count++;
        }
        free(files);
    }
    free(entries);
    return true;
}

void ps2mc_free_dir(Ps2Dir *dir)
{
    for (int i = 0; i < dir->file_count; ++i)
        free(dir->files[i].data);
    free(dir->files);
    dir->files = NULL;
    dir->file_count = 0;
}

void ps2mc_free(Ps2Card *card)
{
    for (int i = 0; i < card->count; ++i)
        ps2mc_free_dir(&card->dirs[i]);
    free(card->dirs);
    card->dirs = NULL;
    card->count = 0;
}

/* ------------------------------------------------------------ building */

static void pack_timestamp(uint32_t now, uint8_t out[8])
{
    memset(out, 0, 8);
    time_t t = (time_t)(now ? now + 9 * 3600 : 0); /* the PS2 keeps JST */
    struct tm *tm = gmtime(&t);
    if (!tm)
        return;
    out[1] = (uint8_t)tm->tm_sec;
    out[2] = (uint8_t)tm->tm_min;
    out[3] = (uint8_t)tm->tm_hour;
    out[4] = (uint8_t)tm->tm_mday;
    out[5] = (uint8_t)(tm->tm_mon + 1);
    wr16(out + 6, (uint16_t)(tm->tm_year + 1900));
}

static bool ts_set(const uint8_t ts[8])
{
    for (int i = 0; i < 8; ++i)
        if (ts[i])
            return true;
    return false;
}

static void pack_dirent(uint8_t *p, const char *name, uint16_t mode, uint32_t length,
                        uint32_t cluster, uint32_t dir_entry, const uint8_t created[8],
                        const uint8_t modified[8])
{
    memset(p, 0, 512);
    wr16(p, mode);
    wr32(p + 4, length);
    memcpy(p + 8, created, 8);
    wr32(p + 16, cluster);
    wr32(p + 20, dir_entry);
    memcpy(p + 24, modified, 8);
    size_t len = strlen(name);
    memcpy(p + 64, name, len > 32 ? 32 : len);
}

typedef struct
{
    uint8_t *data;
} Writer;

static void set_fat(Writer *w, uint32_t index, uint32_t value)
{
    /* the fresh layout: one indirect cluster (8) listing FAT clusters 9..40 */
    uint32_t fat_cluster = FAT_CLUSTER_START + index / FAT_PER_CLUSTER;
    wr32(w->data + (size_t)fat_cluster * CLUSTER_SIZE + (index % FAT_PER_CLUSTER) * 4, value);
}

static void set_chain(Writer *w, uint32_t start, uint32_t n)
{
    for (uint32_t k = 0; k < n; ++k)
        set_fat(w, start + k, k == n - 1 ? FAT_CHAIN_END : (FAT_ALLOCATED | (start + k + 1)));
}

static uint8_t *dirent_slot(Writer *w, uint32_t start_cluster, uint32_t i)
{
    uint32_t phys = ALLOC_OFFSET + start_cluster + i / DIRENTS_PER_CLUSTER;
    return w->data + (size_t)phys * CLUSTER_SIZE + (i % DIRENTS_PER_CLUSTER) * 512;
}

static void format_empty(uint8_t *data)
{
    memset(data, 0, PS2MC_MC2_SIZE);
    memcpy(data, "Sony PS2 Memory Card Format ", 28);
    memcpy(data + 28, "1.2.0.0", 7);
    wr16(data + 40, PAGE_SIZE);
    wr16(data + 42, 2);
    wr16(data + 44, 16);
    wr16(data + 46, 0xFF00);
    wr32(data + 48, CLUSTERS_PER_CARD);
    wr32(data + 52, ALLOC_OFFSET);
    wr32(data + 56, ALLOC_END);
    wr32(data + 60, 0);
    wr32(data + 64, 1023);
    wr32(data + 68, 1022);
    wr32(data + 80, IFC_CLUSTER);
    for (int i = 0; i < 32; ++i)
        wr32(data + 208 + i * 4, 0xFFFFFFFFu);
    data[336] = 2;
    data[337] = 0x52;
    uint8_t *ifc = data + (size_t)IFC_CLUSTER * CLUSTER_SIZE;
    for (uint32_t i = 0; i < FAT_PER_CLUSTER; ++i)
        wr32(ifc + i * 4, i < FAT_CLUSTER_COUNT ? FAT_CLUSTER_START + i : 0xFFFFFFFFu);
    for (uint32_t c = FAT_CLUSTER_START; c < FAT_CLUSTER_START + FAT_CLUSTER_COUNT; ++c)
        for (uint32_t i = 0; i < FAT_PER_CLUSTER; ++i)
            wr32(data + (size_t)c * CLUSTER_SIZE + i * 4, FAT_FREE);
}

uint8_t *ps2mc_build(const Ps2Card *card, uint32_t now, char *error, size_t error_size)
{
    /* Count clusters first: a card that doesn't fit is refused untouched. */
    uint32_t root_entries = 2 + (uint32_t)card->count;
    uint32_t need = (root_entries + 1) / 2;
    for (int g = 0; g < card->count; ++g)
    {
        const Ps2Dir *d = &card->dirs[g];
        need += (2 + (uint32_t)d->file_count + 1) / 2;
        for (int f = 0; f < d->file_count; ++f)
            need += (d->files[f].size + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
    }
    if (need > ALLOC_END - ALLOC_OFFSET)
    {
        set_error(error, error_size, "the saves don't fit on an 8 MB card");
        return NULL;
    }
    uint8_t *data = malloc(PS2MC_MC2_SIZE);
    if (!data)
    {
        set_error(error, error_size, "out of memory");
        return NULL;
    }
    format_empty(data);
    Writer w = {data};
    uint8_t ts_now[8];
    pack_timestamp(now, ts_now);

    uint32_t root_clusters = (root_entries + 1) / 2;
    uint32_t next = root_clusters;
    set_chain(&w, 0, root_clusters);
    pack_dirent(dirent_slot(&w, 0, 0), ".", MODE_DIR, root_entries, 0, 0, ts_now, ts_now);
    pack_dirent(dirent_slot(&w, 0, 1), "..", MODE_DOTDOT, root_entries, 0, 0, ts_now, ts_now);

    for (int g = 0; g < card->count; ++g)
    {
        const Ps2Dir *d = &card->dirs[g];
        uint32_t entries = 2 + (uint32_t)d->file_count;
        uint32_t clusters = (entries + 1) / 2;
        uint32_t start = next;
        next += clusters;
        set_chain(&w, start, clusters);
        uint32_t entry_index = 2 + (uint32_t)g;
        const uint8_t *dc = ts_set(d->created) ? d->created : ts_now;
        const uint8_t *dm = ts_set(d->modified) ? d->modified : ts_now;
        uint16_t dmode = (d->mode & DF_EXISTS) && (d->mode & DF_DIRECTORY) ? d->mode : MODE_DIR;
        pack_dirent(dirent_slot(&w, 0, entry_index), d->name, dmode, entries, start, 0, dc, dm);
        pack_dirent(dirent_slot(&w, start, 0), ".", MODE_DIR, entries,
                    entry_index / DIRENTS_PER_CLUSTER, entry_index, dc, dm);
        pack_dirent(dirent_slot(&w, start, 1), "..", MODE_DOTDOT, root_entries, 0, 0, dc, dm);
        for (int f = 0; f < d->file_count; ++f)
        {
            const Ps2File *pf = &d->files[f];
            uint32_t n = (pf->size + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
            uint32_t fstart = FAT_MASK;
            if (n)
            {
                fstart = next;
                next += n;
                set_chain(&w, fstart, n);
                for (uint32_t k = 0; k < n; ++k)
                {
                    uint32_t off = k * CLUSTER_SIZE;
                    uint32_t take = pf->size - off < CLUSTER_SIZE ? pf->size - off : CLUSTER_SIZE;
                    memcpy(data + (size_t)(ALLOC_OFFSET + fstart + k) * CLUSTER_SIZE,
                           pf->data + off, take);
                }
            }
            uint16_t fmode = (pf->mode & DF_EXISTS) && !(pf->mode & DF_DIRECTORY) ? pf->mode
                                                                                  : MODE_FILE;
            pack_dirent(dirent_slot(&w, start, 2 + (uint32_t)f), pf->name, fmode, pf->size, fstart,
                        0, ts_set(pf->created) ? pf->created : ts_now,
                        ts_set(pf->modified) ? pf->modified : ts_now);
        }
    }
    return data;
}

/* ------------------------------------------------------------ ECC */

static uint8_t parity_table[256];
static uint8_t column_masks[256];
static bool ecc_ready;

static void ecc_init(void)
{
    static const uint8_t cp_masks[7] = {0x55, 0x33, 0x0F, 0x00, 0xAA, 0xCC, 0xF0};
    for (int b = 0; b < 256; ++b)
    {
        int v = b;
        v ^= v >> 1;
        v ^= v >> 2;
        v ^= v >> 4;
        parity_table[b] = (uint8_t)(v & 1);
    }
    for (int b = 0; b < 256; ++b)
    {
        uint8_t mask = 0;
        for (int i = 0; i < 7; ++i)
            mask |= (uint8_t)(parity_table[b & cp_masks[i]] << i);
        column_masks[b] = mask;
    }
    ecc_ready = true;
}

static void ecc_chunk(const uint8_t *chunk, uint8_t out[3])
{
    uint8_t column = 0x77, line0 = 0x7F, line1 = 0x7F;
    for (int i = 0; i < 128; ++i)
    {
        column ^= column_masks[chunk[i]];
        if (parity_table[chunk[i]])
        {
            line0 ^= (uint8_t)~i;
            line1 ^= (uint8_t)i;
        }
    }
    out[0] = column;
    out[1] = line0 & 0x7F;
    out[2] = line1 & 0x7F;
}

void ps2mc_add_ecc(const uint8_t *mc2, uint8_t *ps2)
{
    if (!ecc_ready)
        ecc_init();
    for (uint32_t p = 0; p < PAGES; ++p)
    {
        const uint8_t *page = mc2 + (size_t)p * PAGE_SIZE;
        uint8_t *out = ps2 + (size_t)p * (PAGE_SIZE + SPARE_SIZE);
        memcpy(out, page, PAGE_SIZE);
        uint8_t *spare = out + PAGE_SIZE;
        memset(spare, 0, SPARE_SIZE);
        for (int c = 0; c < 4; ++c)
            ecc_chunk(page + c * 128, spare + c * 3);
    }
}

void ps2mc_strip_ecc(const uint8_t *ps2, uint8_t *mc2)
{
    for (uint32_t p = 0; p < PAGES; ++p)
        memcpy(mc2 + (size_t)p * PAGE_SIZE, ps2 + (size_t)p * (PAGE_SIZE + SPARE_SIZE), PAGE_SIZE);
}

/* ------------------------------------------------------------ P2FD */

uint8_t *ps2mc_build_p2fd(const Ps2Dir *dir, size_t *size)
{
    size_t total = 4 + 4 + 4 + strlen(dir->name) + 4;
    for (int i = 0; i < dir->file_count; ++i)
        total += 4 + strlen(dir->files[i].name) + 4 + dir->files[i].size;
    uint8_t *out = malloc(total);
    if (!out)
        return NULL;
    uint8_t *p = out;
    memcpy(p, "P2FD", 4);
    p += 4;
    wr32(p, 1);
    p += 4;
    size_t len = strlen(dir->name);
    wr32(p, (uint32_t)len);
    p += 4;
    memcpy(p, dir->name, len);
    p += len;
    wr32(p, (uint32_t)dir->file_count);
    p += 4;
    for (int i = 0; i < dir->file_count; ++i)
    {
        const Ps2File *f = &dir->files[i];
        len = strlen(f->name);
        wr32(p, (uint32_t)len);
        p += 4;
        memcpy(p, f->name, len);
        p += len;
        wr32(p, f->size);
        p += 4;
        memcpy(p, f->data, f->size);
        p += f->size;
    }
    *size = total;
    return out;
}

bool ps2mc_parse_p2fd(const uint8_t *data, size_t size, Ps2Dir *out)
{
    memset(out, 0, sizeof(*out));
    if (size < 16 || memcmp(data, "P2FD", 4) != 0 || rd32(data + 4) != 1)
        return false;
    size_t pos = 8;
    uint32_t dlen = rd32(data + pos);
    pos += 4;
    if (dlen > 32 || pos + dlen + 4 > size)
        return false;
    memcpy(out->name, data + pos, dlen);
    out->name[dlen] = 0;
    pos += dlen;
    uint32_t count = rd32(data + pos);
    pos += 4;
    if (count > 4096)
        return false;
    out->files = calloc(count ? count : 1, sizeof(Ps2File));
    if (!out->files)
        return false;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (pos + 4 > size)
            goto fail;
        uint32_t nlen = rd32(data + pos);
        pos += 4;
        if (nlen > 32 || pos + nlen + 4 > size)
            goto fail;
        Ps2File *f = &out->files[out->file_count];
        memcpy(f->name, data + pos, nlen);
        f->name[nlen] = 0;
        pos += nlen;
        uint32_t flen = rd32(data + pos);
        pos += 4;
        if (flen > size - pos)
            goto fail;
        f->data = malloc(flen ? flen : 1);
        if (!f->data)
            goto fail;
        memcpy(f->data, data + pos, flen);
        f->size = flen;
        pos += flen;
        out->file_count++;
    }
    return true;
fail:
    ps2mc_free_dir(out);
    return false;
}

/* ------------------------------------------------------------ helpers */

bool ps2mc_serial_from_dirname(const char *name, char *out, size_t size)
{
    static const char *const prefixes[] = {"SLUS", "SCUS", "SLES", "SCES", "SLPS",
                                           "SLPM", "SLKA", "SCAJ", "SLAJ", "SCKA",
                                           "SLPN", "TCPX", "PBPX", "PCPX"};
    size_t len = strlen(name);
    for (size_t i = 0; i + 4 <= len; ++i)
    {
        for (size_t k = 0; k < sizeof(prefixes) / sizeof(prefixes[0]); ++k)
        {
            bool match = true;
            for (int c = 0; c < 4; ++c)
                if (toupper((unsigned char)name[i + c]) != prefixes[k][c])
                {
                    match = false;
                    break;
                }
            if (!match)
                continue;
            const char *p = name + i + 4;
            if (*p == '-' || *p == '_' || *p == ' ')
                p++;
            if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) ||
                !isdigit((unsigned char)p[2]))
                continue;
            char digits[6] = {p[0], p[1], p[2], 0, 0, 0};
            p += 3;
            if (*p == '.')
                p++;
            if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]))
                continue;
            digits[3] = p[0];
            digits[4] = p[1];
            snprintf(out, size, "%s%s", prefixes[k], digits);
            return true;
        }
    }
    return false;
}

int ps2mc_find_dir(const Ps2Card *card, const char *name)
{
    for (int i = 0; i < card->count; ++i)
        if (strcmp(card->dirs[i].name, name) == 0)
            return i;
    return -1;
}

bool ps2mc_put_dir(Ps2Card *card, Ps2Dir *dir)
{
    int i = ps2mc_find_dir(card, dir->name);
    if (i >= 0)
    {
        /* keep the card's directory attributes, take the new contents */
        Ps2Dir *old = &card->dirs[i];
        ps2mc_free_dir(old);
        old->files = dir->files;
        old->file_count = dir->file_count;
        memset(old->modified, 0, 8); /* changed now */
    }
    else
    {
        Ps2Dir *grown = realloc(card->dirs, (size_t)(card->count + 1) * sizeof(Ps2Dir));
        if (!grown)
            return false;
        card->dirs = grown;
        card->dirs[card->count++] = *dir;
    }
    dir->files = NULL;
    dir->file_count = 0;
    return true;
}

bool ps2mc_load_file(const char *path, Ps2CardFile *out, char *error, size_t error_size)
{
    memset(out, 0, sizeof(*out));
    size_t size;
    uint8_t *raw = util_read_file(path, &size);
    if (!raw)
    {
        set_error(error, error_size, "can't read the card");
        return false;
    }
    uint8_t *mc2 = raw;
    if (size == PS2MC_PS2_SIZE)
    {
        mc2 = malloc(PS2MC_MC2_SIZE);
        if (!mc2)
        {
            free(raw);
            set_error(error, error_size, "out of memory");
            return false;
        }
        ps2mc_strip_ecc(raw, mc2);
        free(raw);
        raw = NULL;
        out->ecc = true;
    }
    else if (size != PS2MC_MC2_SIZE)
    {
        free(raw);
        set_error(error, error_size, "not an 8 MB PS2 card");
        return false;
    }
    bool ok = ps2mc_parse(mc2, &out->card, error, error_size);
    free(mc2);
    return ok;
}

bool ps2mc_save_file(const char *path, const Ps2CardFile *file, uint32_t now, char *error,
                     size_t error_size)
{
    uint8_t *mc2 = ps2mc_build(&file->card, now, error, error_size);
    if (!mc2)
        return false;
    /* Sanity: what we built must read back with the same folders. */
    Ps2Card check;
    if (!ps2mc_parse(mc2, &check, error, error_size) || check.count != file->card.count)
    {
        if (check.dirs)
            ps2mc_free(&check);
        free(mc2);
        set_error(error, error_size, "rebuilt card failed verification");
        return false;
    }
    ps2mc_free(&check);
    uint8_t *image = mc2;
    size_t image_size = PS2MC_MC2_SIZE;
    if (file->ecc)
    {
        image = malloc(PS2MC_PS2_SIZE);
        if (!image)
        {
            free(mc2);
            set_error(error, error_size, "out of memory");
            return false;
        }
        ps2mc_add_ecc(mc2, image);
        image_size = PS2MC_PS2_SIZE;
    }
    char bak[PATH_LEN + 8];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    if (util_file_exists(path))
        util_copy_file(path, bak);
    bool ok = util_write_file_atomic(path, image, image_size);
    if (image != mc2)
        free(image);
    free(mc2);
    if (!ok)
        set_error(error, error_size, "can't write the card");
    return ok;
}

bool ps2mc_create_file(const char *path, bool ecc)
{
    Ps2CardFile file;
    memset(&file, 0, sizeof(file));
    file.ecc = ecc;
    char error[64];
    return ps2mc_save_file(path, &file, (uint32_t)time(NULL), error, sizeof(error));
}
