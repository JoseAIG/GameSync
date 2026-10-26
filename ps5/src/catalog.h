/*
 * GameSync PS5 client - the server's ROM catalog and installing from it.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_CATALOG_H
#define GAMESYNC_CATALOG_H

#include "config.h"
#include "http.h"
#include "naming.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    RA_NONE,     /* RetroAchievements has no set for it */
    RA_TITLE,    /* matched by name only: "RA?" */
    RA_VERIFIED, /* this very dump: exact hash, or a file name RA registered */
} RaLevel;

typedef struct
{
    char rom_id[128];
    char name[160];
    char filename[200];
    char system[12];
    char title_id[64];
    char bundle_kind[16];
    uint64_t size;
    int ra_game_id;
    int ra_achievements;
    uint8_t ra_match; /* 0 none, 1 hash, 2 title */
    bool ra_dump;
    bool is_bundle;
    bool has_cue;     /* the server can convert it to a CUE/BIN set (?extract=cue) */
    bool has_iso;     /* an Xbox bundle the server can turn into one XISO (?extract=iso) */
    bool has_dcci;    /* a 3DS game the server can serve as a decrypted CCI (?extract=decrypted_cci) */
    uint8_t disc_index;
    uint8_t disc_total;
} RomRow;

typedef struct
{
    char system[12];
    int count;
    char fingerprint[96];
} SystemRow;

typedef struct
{
    SystemRow *systems; /* playable here, sorted by display name */
    int system_count;
    RomRow *roms;       /* every playable system, sorted by system then name */
    int rom_count;
    bool offline;       /* the server couldn't be reached: cached copy */
    bool cached;        /* the server has /roms/fingerprints */
} Catalog;

typedef void (*CatalogProgressFn)(const char *message, int done, int total, void *user);

void catalog_free(Catalog *catalog);
/* Loads every system an emulator here can play: unchanged systems from the
 * cache in <data>/catalog/, changed ones from the server (the MiSTer /
 * Steam Deck strategy). Offline, the cache alone. */
bool catalog_load(const HttpServer *server, Catalog *catalog, CatalogProgressFn progress,
                  void *user, char *error, size_t size);
/* Settings > Refresh catalog: asks the server to rescan, wipes the cache. */
void catalog_reset(const HttpServer *server);

RaLevel catalog_ra_level(const RomRow *rom);
/* Every query token (roman numerals and digits interchangeable) appears in
 * the name, file name or title id. */
bool catalog_matches(const RomRow *rom, const char *query);
/* A catalog row for an installed file: same system and file name. */
const RomRow *catalog_find_file(const Catalog *catalog, const char *system, const char *filename);

typedef struct
{
    Emulator emu;
    bool supported;
    char dest[PATH_LEN];   /* the file the emulator will load */
    char reason[96];       /* why not, when !supported */
    bool cue;              /* the CUE/BIN variant: dest is a temporary .zip */
    char unzip_dir[PATH_LEN]; /* cue: the game's folder the set goes into */
    char check[PATH_LEN];  /* the file that says it's installed (dest, or the .cue) */
    const char *extract;   /* ?extract= for a server-side conversion ("iso"), or NULL */
    bool folder;           /* a bundle fetched file by file into dest, a folder */
    bool zip_member;       /* a zipped game: just the game file out of the zip, to dest + its
                              extension (the server's /roms/<id>/file/<member>) */
} InstallPlan;

void catalog_plan(const Config *config, const RomRow *rom, InstallPlan *plan);
/* The CUE/BIN variant of a disc image (rom->has_cue): the server converts it,
 * the set is unzipped into <system folder>/<game>/. */
void catalog_plan_cue(const Config *config, const RomRow *rom, InstallPlan *plan);
bool catalog_installed(const InstallPlan *plan);

/* Downloads to dest (resuming a .part left by an earlier try). progress may
 * return non-zero to cancel. Thread-safe. */
bool catalog_download(const HttpServer *server, const RomRow *rom, const InstallPlan *plan,
                      HttpProgressFn progress, void *user, char *error, size_t size);

#endif
