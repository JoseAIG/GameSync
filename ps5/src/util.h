/*
 * GameSync PS5 client - small file and string helpers.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_UTIL_H
#define GAMESYNC_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PATH_LEN 512

/* Reads a whole file into a malloc'd buffer (NUL-terminated, not counted in
 * *size). Returns NULL when it can't be opened or memory runs out. */
uint8_t *util_read_file(const char *path, size_t *size);
/* Writes path.tmp, then renames it over path, so a crash can't leave half a
 * save behind. Files are left world read/writable: the emulators run as a
 * different sandboxed title and must still open what we write. */
bool util_write_file_atomic(const char *path, const void *data, size_t size);
bool util_copy_file(const char *from, const char *to);
bool util_file_exists(const char *path);
bool util_dir_exists(const char *path);
int64_t util_file_size(const char *path);
/* mkdir -p, every level 0777. */
bool util_mkdirs(const char *path);
/* mkdir -p of the folder holding path. */
bool util_mkdirs_parent(const char *path);

/* sha256 of a file / of a buffer, as 64 lowercase hex chars + NUL. */
bool util_sha256_file(const char *path, char out[65]);
void util_sha256_hex(const void *data, size_t size, char out[65]);

void util_path_join(char *out, size_t size, const char *dir, const char *name);
/* "Game (USA).sfc" -> "Game (USA)"; only the last extension. */
void util_strip_ext(char *name);
const char *util_basename(const char *path);
const char *util_ext(const char *path); /* ".sfc" or "" */
bool util_ieq(const char *a, const char *b);
bool util_has_suffix_i(const char *s, const char *suffix);
void util_copy(char *out, size_t size, const char *s);
/* Upper-case and drop everything outside A-Z0-9: "SLUS-010.41" -> "SLUS01041". */
void util_compact_serial(char *out, size_t size, const char *s);
/* A string safe as a file name on the PS5's UFS: no '/', no control chars. */
void util_safe_filename(char *out, size_t size, const char *s);
/* Human size: "1.4 MB". */
void util_format_size(char *out, size_t size, uint64_t bytes);
/* Percent-encodes everything outside unreserved chars (for query strings). */
void util_url_encode(char *out, size_t size, const char *s);

#endif
