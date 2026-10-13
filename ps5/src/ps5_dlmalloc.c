/*
 * GameSync PS5 client - dlmalloc 2.8.6 (Doug Lea, public domain / CC0) built
 * as a single mspace for ps5_heap.c: no system memory of its own,
 * thread-safe spin locks. Settings as in PSXS5 / SwanStationPS5 by darkxex
 * (src/platform/ps5_dlmalloc.c, GPL-3.0-or-later).
 *
 * malloc.c itself is not kept in this repository: ps5/build.sh fetches the
 * pinned copy and checks its SHA-256.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if defined(__PROSPERO__)
#define ONLY_MSPACES 1
#define MSPACES 1
#define USE_LOCKS 1
#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define HAVE_MORECORE 0
#define LACKS_SYS_MMAN_H 1
#define NO_MALLOC_STATS 1
#define NO_MALLINFO 1
#define malloc_getpagesize ((size_t)16384U)
#define MALLOC_ALIGNMENT ((size_t)32U)
#include "../third_party/dlmalloc/malloc.c"
#endif
