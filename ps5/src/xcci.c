/*
 * GameSync PS5 client - Xbox CCI images to XISO.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CCI is the compressed Xbox disc image Cerbios and Repackinator use; XPSemu
 * reads only XISO, and a CCI decodes to exactly that XISO. Layout, based on
 * analysis of Team Resurgent's XboxToolkit (CCIContainerReader.cs and
 * CCISectorDecoder.cs, https://github.com/Team-Resurgent/XboxToolkit,
 * GPL-3.0) - no code copied:
 *
 *   0x00 u32 magic "CCIM"   0x04 u32 header size (32)
 *   0x08 u64 uncompressed size   0x10 u64 index offset
 *   0x18 u32 block size (2048)   0x1C u8 version (1)   0x1D u8 index alignment (2)
 *
 * The index holds sectors + 1 u32 entries: (entry & 0x7FFFFFFF) << alignment
 * is a sector's offset, bit 31 says it is LZ4-compressed; the next entry ends
 * it. A compressed sector (or one whose size isn't 2048) starts with a
 * padding count byte, then the LZ4 block, then that many padding bytes.
 * Large images are split into slices, each with its own header and index.
 *
 * The LZ4 block decoder follows the LZ4 block format description
 * (https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md).
 */
#include "xcci.h"

#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CCI_SECTOR 2048u
/* The compressed sectors are stored in order: read them in large windows,
 * never one sector at a time (a seek and a small read per sector is ~1.7M
 * system calls for a DVD, which the PS5's filesystem makes very slow). */
#define CCI_WINDOW (8u << 20)
/* The ISO is pushed to the disk every 128 MB: left in the system's cache, a
 * few GB of it made every write slower until the conversion nearly stopped. */
#define CCI_FLUSH ((uint64_t)128 << 20)

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int lz4_block_decode(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap)
{
    size_t i = 0, o = 0;
    while (i < src_len)
    {
        uint8_t token = src[i++];
        size_t lit = token >> 4;
        if (lit == 15)
        {
            uint8_t b;
            do
            {
                if (i >= src_len)
                    return -1;
                b = src[i++];
                lit += b;
            } while (b == 255);
        }
        if (lit > src_len - i || lit > dst_cap - o)
            return -1;
        memcpy(dst + o, src + i, lit);
        i += lit;
        o += lit;
        if (i == src_len)
            break; /* the last sequence has literals only */
        if (src_len - i < 2)
            return -1;
        size_t offset = (size_t)src[i] | ((size_t)src[i + 1] << 8);
        i += 2;
        if (offset == 0 || offset > o)
            return -1;
        size_t match = token & 15;
        if (match == 15)
        {
            uint8_t b;
            do
            {
                if (i >= src_len)
                    return -1;
                b = src[i++];
                match += b;
            } while (b == 255);
        }
        match += 4;
        if (match > dst_cap - o)
            return -1;
        if (offset >= match)
        {
            memcpy(dst + o, dst + o - offset, match);
            o += match;
        }
        else /* the match overlaps what it copies: byte by byte */
            for (size_t k = 0; k < match; ++k, ++o)
                dst[o] = dst[o - offset];
    }
    return (int)o;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

bool xcci_to_iso(const char *const *slices, int count, const char *out_path,
                 XcciProgressFn progress, void *user, char *error, size_t error_size)
{
    FILE *out = fopen(out_path, "wb");
    if (!out)
    {
        snprintf(error, error_size, "can't write %s", util_basename(out_path));
        return false;
    }
    char *obuf = malloc(1024 * 1024);
    if (obuf)
        setvbuf(out, obuf, _IOFBF, 1024 * 1024);
    uint8_t *win = malloc(CCI_WINDOW);
    uint8_t sector[CCI_SECTOR];
    uint32_t *index = NULL;
    uint64_t total = 0, done = 0;
    double t0 = now_sec(), disk = 0;
    bool ok = win != NULL;
    /* the whole image's size first, for the progress */
    for (int s = 0; s < count && ok; ++s)
    {
        FILE *f = fopen(slices[s], "rb");
        uint8_t h[32];
        ok = f && fread(h, 1, 32, f) == 32 && memcmp(h, "CCIM", 4) == 0;
        if (ok)
            total += rd64(h + 8);
        if (f)
            fclose(f);
    }
    if (!ok)
        snprintf(error, error_size, "not a CCI image");
    for (int s = 0; s < count && ok; ++s)
    {
        FILE *f = fopen(slices[s], "rb");
        uint8_t h[32];
        if (!f || fread(h, 1, 32, f) != 32)
        {
            snprintf(error, error_size, "can't read %s", util_basename(slices[s]));
            ok = false;
            if (f)
                fclose(f);
            break;
        }
        uint64_t usize = rd64(h + 8), ioff = rd64(h + 16);
        uint32_t block = rd32(h + 24);
        uint8_t align = h[29];
        if (rd32(h + 4) != 32 || block != CCI_SECTOR || h[28] != 1 || usize % CCI_SECTOR)
        {
            snprintf(error, error_size, "unsupported CCI header in %s", util_basename(slices[s]));
            ok = false;
            fclose(f);
            break;
        }
        uint64_t sectors = usize / CCI_SECTOR;
        free(index);
        index = malloc((size_t)(sectors + 1) * 4);
        if (!index || fseeko(f, (off_t)ioff, SEEK_SET) != 0 ||
            fread(index, 4, (size_t)sectors + 1, f) != (size_t)sectors + 1)
        {
            snprintf(error, error_size, "can't read the CCI index");
            ok = false;
            fclose(f);
            break;
        }
        /* the index is little-endian u32s, which is the PS5's own order */
        uint64_t win_start = 0, win_len = 0;
        for (uint64_t i = 0; i < sectors && ok; ++i)
        {
            uint64_t pos = (uint64_t)(index[i] & 0x7FFFFFFFu) << align;
            uint64_t end = (uint64_t)(index[i + 1] & 0x7FFFFFFFu) << align;
            bool lz4 = (index[i] & 0x80000000u) != 0;
            uint64_t len = end - pos;
            bool bad = end < pos || len > CCI_SECTOR + 64;
            if (!bad && (pos < win_start || end > win_start + win_len))
            {
                win_start = pos;
                win_len = 0;
                if (fseeko(f, (off_t)pos, SEEK_SET) == 0)
                    win_len = fread(win, 1, CCI_WINDOW, f);
            }
            if (bad || end > win_start + win_len)
            {
                snprintf(error, error_size, "damaged CCI at sector %llu",
                         (unsigned long long)(done / CCI_SECTOR));
                ok = false;
                break;
            }
            const uint8_t *raw = win + (pos - win_start);
            const uint8_t *data = raw;
            if (len != CCI_SECTOR || lz4)
            {
                size_t pad = raw[0];
                if (len < pad + 1 ||
                    lz4_block_decode(raw + 1, (size_t)len - pad - 1, sector, CCI_SECTOR) !=
                        (int)CCI_SECTOR)
                {
                    snprintf(error, error_size, "damaged CCI at sector %llu",
                             (unsigned long long)(done / CCI_SECTOR));
                    ok = false;
                    break;
                }
                data = sector;
            }
            double tw = now_sec();
            bool wrote = fwrite(data, 1, CCI_SECTOR, out) == CCI_SECTOR;
            if (wrote && ((done + CCI_SECTOR) % CCI_FLUSH) == 0)
            {
                fflush(out);
                fsync(fileno(out));
            }
            disk += now_sec() - tw;
            if (((done + CCI_SECTOR) & ((1u << 30) - 1)) == 0)
            {
                double t = now_sec(), secs = t - t0 > 0.001 ? t - t0 : 0.001;
                app_log("CCI to ISO: %.1f GB of %.1f at %.1f MB/s (disk %.0f%%)",
                        (done + CCI_SECTOR) / 1e9, total / 1e9, 1073.741824 / secs,
                        100.0 * disk / secs);
                t0 = t;
                disk = 0;
            }
            if (!wrote)
            {
                snprintf(error, error_size, "write failed (is the disk full?)");
                ok = false;
                break;
            }
            done += CCI_SECTOR;
            if (progress && (done & 0x3FFFFF) == 0 && progress(done, total, user) != 0)
            {
                snprintf(error, error_size, "cancelled");
                ok = false;
            }
        }
        fclose(f);
    }
    free(index);
    free(win);
    if (fclose(out) != 0 && ok)
    {
        snprintf(error, error_size, "write failed (is the disk full?)");
        ok = false;
    }
    free(obuf);
    if (ok)
        chmod(out_path, 0666);
    else
        remove(out_path);
    return ok;
}
