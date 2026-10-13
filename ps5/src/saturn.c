/*
 * GameSync PS5 client - a Saturn disc's product code, read from its IP.BIN.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A C port of shared/rom_id/saturn.py (read_saturn_product_code): sector 0
 * of a Saturn disc starts with "SEGA SEGASATURN " and holds the product
 * number at 0x20 (10 bytes, then the version). The id it makes,
 * SAT_<code>, is the one the server, Saroo and the Android client use.
 */
#include "saturn.h"

#include "util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char MAGIC[] = "SEGA SEGASATURN ";

bool saturn_clean_product_code(const char *raw, char *out, size_t size)
{
    char code[16];
    size_t n = 0;
    for (size_t i = 0; raw[i] && i < 10 && n + 1 < sizeof(code); ++i)
    {
        /* two spaces, or a space before the version ("V1.003"), end it */
        if (raw[i] == ' ' && (raw[i + 1] == ' ' || raw[i + 1] == 'V' || raw[i + 1] == 'v'))
            break;
        if ((raw[i] == 'V' || raw[i] == 'v') && i > 0 && isdigit((unsigned char)raw[i + 1]) &&
            raw[i - 1] == ' ')
            break;
        code[n++] = raw[i];
    }
    while (n && code[n - 1] == ' ')
        n--;
    code[n] = 0;
    /* "T-4507G-1": later discs carry a -N disc index */
    if (n >= 3 && code[n - 2] == '-' && isdigit((unsigned char)code[n - 1]) &&
        isalnum((unsigned char)code[n - 3]))
        code[n -= 2] = 0;
    /* spaces to '_', keep [A-Za-z0-9_-], upper case */
    size_t m = 0;
    for (size_t i = 0; code[i] && m + 1 < size; ++i)
    {
        char c = code[i] == ' ' ? '_' : code[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '-')
            out[m++] = (char)toupper((unsigned char)c);
    }
    out[m] = 0;
    return m > 0;
}

/* The first FILE of a .cue, next to it. */
static bool cue_first_file(const char *cue, char *out, size_t size)
{
    char *text = (char *)util_read_file(cue, NULL);
    if (!text)
        return false;
    bool found = false;
    for (char *line = text; line && *line && !found;)
    {
        char *end = strchr(line, '\n');
        if (end)
            *end = 0;
        while (*line == ' ' || *line == '\t')
            line++;
        if (strncmp(line, "FILE", 4) == 0 || strncmp(line, "file", 4) == 0)
        {
            const char *q = strchr(line, '"');
            const char *q2 = q ? strchr(q + 1, '"') : NULL;
            if (q && q2 && q2 > q + 1)
            {
                char name[256];
                size_t len = (size_t)(q2 - q - 1);
                if (len >= sizeof(name))
                    len = sizeof(name) - 1;
                memcpy(name, q + 1, len);
                name[len] = 0;
                char dir[PATH_LEN];
                util_copy(dir, sizeof(dir), cue);
                char *slash = strrchr(dir, '/');
                if (slash)
                {
                    *slash = 0;
                    snprintf(out, size, "%s/%s", dir, util_basename(name));
                }
                else
                    util_copy(out, size, util_basename(name));
                found = true;
            }
        }
        line = end ? end + 1 : NULL;
    }
    free(text);
    return found;
}

bool saturn_title_id(const char *path, char *out, size_t size)
{
    char image[PATH_LEN];
    if (util_has_suffix_i(path, ".cue"))
    {
        if (!cue_first_file(path, image, sizeof(image)))
            return false;
    }
    else if (util_has_suffix_i(path, ".bin") || util_has_suffix_i(path, ".img") ||
             util_has_suffix_i(path, ".iso"))
        util_copy(image, sizeof(image), path);
    else
        return false;
    FILE *f = fopen(image, "rb");
    if (!f)
        return false;
    uint8_t sector[0x80];
    size_t got = fread(sector, 1, sizeof(sector), f);
    fclose(f);
    if (got < 0x60)
        return false;
    /* raw 2352-byte sectors start with the sync pattern 00 FF*10 00; the
     * user data follows the header (MODE1) or the subheader too (MODE2) */
    size_t data = 0;
    static const uint8_t sync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    if (memcmp(sector, sync, sizeof(sync)) == 0)
        data = sector[15] == 2 ? 0x18 : 0x10;
    if (memcmp(sector + data, MAGIC, sizeof(MAGIC) - 1) != 0)
        return false;
    char raw[11];
    memcpy(raw, sector + data + 0x20, 10);
    raw[10] = 0;
    char code[16];
    if (!saturn_clean_product_code(raw, code, sizeof(code)))
        return false;
    snprintf(out, size, "SAT_%s", code);
    return true;
}
