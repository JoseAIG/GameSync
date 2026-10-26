/*
 * GameSync PS5 client - PS2 memory card (8 MB) parser/builder.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_PS2MC_H
#define GAMESYNC_PS2MC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PS2MC_MC2_SIZE 8388608u /* 16384 pages x 512 */
#define PS2MC_PS2_SIZE 8650752u /* 16384 pages x (512 + 16 ECC spare) */

typedef struct
{
    char name[33];
    uint8_t *data;
    uint32_t size;
    uint16_t mode;        /* 0 = default file mode */
    uint8_t created[8];   /* raw PS2 timestamps; all zero = "now" */
    uint8_t modified[8];
} Ps2File;

typedef struct
{
    char name[33];
    Ps2File *files;
    int file_count;
    uint16_t mode;        /* 0 = default directory mode */
    uint8_t created[8];
    uint8_t modified[8];
} Ps2Dir;

typedef struct
{
    Ps2Dir *dirs;
    int count;
} Ps2Card;

/* Splits a canonical .mc2 image into its top-level save folders. */
bool ps2mc_parse(const uint8_t *mc2, Ps2Card *out, char *error, size_t error_size);
/* Builds a fresh canonical .mc2 image holding card's folders (malloc'd,
 * PS2MC_MC2_SIZE bytes). NULL when it doesn't fit. */
uint8_t *ps2mc_build(const Ps2Card *card, uint32_t now, char *error, size_t error_size);
void ps2mc_free(Ps2Card *card);
void ps2mc_free_dir(Ps2Dir *dir);

/* .ps2 (ECC) <-> .mc2 (canonical). */
void ps2mc_strip_ecc(const uint8_t *ps2, uint8_t *mc2);
void ps2mc_add_ecc(const uint8_t *mc2, uint8_t *ps2);

/* P2FD: the server's one-folder wire format (server/app/services/ps2mc.py). */
uint8_t *ps2mc_build_p2fd(const Ps2Dir *dir, size_t *size);
bool ps2mc_parse_p2fd(const uint8_t *data, size_t size, Ps2Dir *out);

/* "BASLUS-20312" -> "SLUS20312". false for folders without a game serial. */
bool ps2mc_serial_from_dirname(const char *name, char *out, size_t size);

/* Replaces (or adds) a folder by name; takes ownership of dir's contents. */
bool ps2mc_put_dir(Ps2Card *card, Ps2Dir *dir);
int ps2mc_find_dir(const Ps2Card *card, const char *name);

/* Card files on disk, in either format. */
typedef struct
{
    Ps2Card card;
    bool ecc; /* the file is a .ps2 image with ECC spare bytes */
} Ps2CardFile;

bool ps2mc_load_file(const char *path, Ps2CardFile *out, char *error, size_t error_size);
/* Rebuilds and writes the card in its original format, keeping the
 * previous file as path.bak. */
bool ps2mc_save_file(const char *path, const Ps2CardFile *file, uint32_t now, char *error,
                     size_t error_size);
/* A blank formatted card, ECC form when ecc. */
bool ps2mc_create_file(const char *path, bool ecc);

#endif
