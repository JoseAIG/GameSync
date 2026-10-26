/*
 * GameSync PS5 client - Xbox saves inside XPSemu's virtual hard disk.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_XBOX_HDD_H
#define GAMESYNC_XBOX_HDD_H

#include "bundle.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct XboxHdd XboxHdd;

/* Opens a qcow2 Xbox disk image read-only and mounts its FATX E: partition.
 * NULL on failure, with the reason in err. */
XboxHdd *xhdd_open(const char *path, char *err, size_t err_size);
/* The same, for xhdd_write_title. Refuses images with snapshots or not
 * closed cleanly. */
XboxHdd *xhdd_open_rw(const char *path, char *err, size_t err_size);
void xhdd_close(XboxHdd *hdd);

/* An E:\UDATA folder name that is a game's save: 8 hex digits whose first
 * two bytes are uppercase letters (the publisher), as the Xbox client
 * (xbox/source/saves.c) accepts. */
bool xhdd_is_game_title_id(const char *name);

/* The game save folders in E:\UDATA, as uppercase 8-hex title ids. */
int xhdd_titles(XboxHdd *hdd, char (*ids)[9], int max);

/* Every file under E:\UDATA\<title_id> (any case), paths relative to it with
 * '\' separators - what the Xbox client bundles - sorted by strcmp. A title
 * holding more than max_bytes fails. */
bool xhdd_read_title(XboxHdd *hdd, const char *title_id, uint64_t max_bytes, SaveTree *out,
                     char *err, size_t err_size);

/* Replaces E:\UDATA\<title_id> with tree's files ('\' or '/' paths). */
bool xhdd_write_title(XboxHdd *hdd, const char *title_id, const SaveTree *tree, char *err,
                      size_t err_size);

/* Copies the image to backup (through backup.tmp). */
bool xhdd_backup(const char *path, const char *backup);

/* XPSemu's disk image: xemu.toml's hdd_path, else <xemu_dir>/xbox_hdd.qcow2. */
void xhdd_image_path(const char *xemu_dir, char *out, size_t size);

#endif
