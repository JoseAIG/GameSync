/*
 * GameSync PS5 client - extracting the server's ZIP_STORED archives.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_UNZIP_H
#define GAMESYNC_UNZIP_H

#include <stdbool.h>
#include <stddef.h>

/* Extracts every member of a stored (uncompressed) zip into out_dir, ZIP64
 * sizes included. Member files are left world-writable for the emulators. */
bool unzip_stored(const char *zip_path, const char *out_dir, char *error, size_t size);

#endif
