/*
 * GameSync PS5 client - save folders and the 3DSS bundle format.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_BUNDLE_H
#define GAMESYNC_BUNDLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    char path[256]; /* relative, '/' separators */
    uint8_t *data;
    uint32_t size;
} TreeFile;

typedef struct
{
    TreeFile *files; /* sorted by strcmp on path: the order hashes and bundles use */
    int count;
} SaveTree;

/* Every file under dir, recursively. skip_top names a top-level folder left
 * out (Wii "nocopy"), or NULL. false when dir can't be read. */
bool tree_read(const char *dir, const char *skip_top, SaveTree *out);
void tree_free(SaveTree *tree);
/* Puts files in bundle order (strcmp on path). */
void tree_sort(SaveTree *tree);
/* The server's hash of a multi-file save: sha256 over every file's bytes,
 * no paths, in bundle order (server/app/services/storage.py). */
void tree_hash(const SaveTree *tree, char out[65], uint64_t *total_size);
/* Replaces dir's contents with tree: written to <dir>.gs_new first, the old
 * folder kept as <dir>.bak (the previous .bak is dropped). */
bool tree_write_replace(const char *dir, const SaveTree *tree);

/* A 3DSS v4 bundle (string title id <= 31 chars, zlib payload). */
uint8_t *bundle_build(const char *title_id, const SaveTree *tree, uint32_t timestamp, size_t *size);
/* Any version 1-5; files are verified against their sha256. */
bool bundle_parse(const uint8_t *data, size_t size, SaveTree *out);

/* rm -rf (only below a path with at least two components). */
bool remove_tree(const char *path);

#endif
