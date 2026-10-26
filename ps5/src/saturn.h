/*
 * GameSync PS5 client - a Saturn disc's product code, read from its IP.BIN.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_SATURN_H
#define GAMESYNC_SATURN_H

#include <stdbool.h>
#include <stddef.h>

/* "SAT_T-4507G" for a .cue (its first FILE), .bin/.img (raw 2352-byte
 * sectors, MODE1 or MODE2) or .iso (2048-byte sectors): the product code in
 * the disc's IP.BIN, the id every Saturn save is stored under, whatever the
 * file is called (a fan translation keeps the original's IP.BIN). Later discs
 * drop their "-N" so all discs share one slot. false for anything else - a
 * .chd is compressed - or a disc without a Saturn header. */
bool saturn_title_id(const char *path, char *out, size_t size);

/* "T-4507G  V1.003" / "T-4507G-1" -> "T-4507G" (exposed for tests). */
bool saturn_clean_product_code(const char *raw, char *out, size_t size);

#endif
