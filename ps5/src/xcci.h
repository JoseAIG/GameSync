/*
 * GameSync PS5 client - Xbox CCI images to XISO.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_XCCI_H
#define GAMESYNC_XCCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decodes one LZ4 block (the raw block format, no frame) into dst. Returns
 * the bytes written, or -1 for a damaged block. */
int lz4_block_decode(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap);

/* The XISO inside a CCI image (one file, or the slices of one: x.1.cci,
 * x.2.cci, in order), written to out_path. progress(done, total, user) is
 * told the bytes written; a nonzero return cancels. */
typedef int (*XcciProgressFn)(uint64_t done, uint64_t total, void *user);
bool xcci_to_iso(const char *const *slices, int count, const char *out_path,
                 XcciProgressFn progress, void *user, char *error, size_t error_size);

#endif
