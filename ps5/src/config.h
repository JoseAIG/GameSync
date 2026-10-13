/*
 * GameSync PS5 client - configuration and small key=value stores.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_CONFIG_H
#define GAMESYNC_CONFIG_H

#include "http.h"
#include "util.h"

#include <stdbool.h>

/* Everything the client keeps: /data/gamesync on the PS5 (a host build can
 * point it elsewhere with config_set_data_dir). Files in it:
 *   config.txt     settings
 *   state.txt      title_id=hash of the last synced version
 *   ids.txt        SYSTEM|rom file=title_id (RetroArch ROMs, from the server)
 *   installed.txt  title_id=emulator tag (games installed from the catalog)
 *   gamesync.log */
#define DEFAULT_DATA_DIR "/data/gamesync"
void config_set_data_dir(const char *dir);
const char *config_data_dir(void);
/* <data dir>/<name> in a static buffer (one of a few, rotating). */
const char *data_path(const char *name);

typedef struct
{
    char server_url[256];
    char api_key[160];
    char console_id[64];
    /* Emulator roots; the defaults are where each project installs. */
    char retroarch_dir[PATH_LEN];   /* /data/homebrew/PPSA99169 */
    char swanstation_dir[PATH_LEN]; /* /data/SwanStationPS5 (older builds: /data/PSXS5) */
    char ps5sx2_dir[PATH_LEN];      /* /data/PCSX2 */
    char porpoise_dir[PATH_LEN];    /* /data/porpoise (Dolphin) */
    char eden_dir[PATH_LEN];        /* /data/prosperoeden (Switch) */
    char cemu_dir[PATH_LEN];        /* /data/ps5cemu (Wii U, 3DS in azahar/) */
    char xemu_dir[PATH_LEN];        /* /data/xemu (Xbox) */
    char ps2_card[64];              /* card new PS2 saves go to: Mcd001.ps2 */
    bool ps1_in_retroarch;          /* PS1 catalog installs: RetroArch, not SwanStation */
    unsigned auto_dirs;             /* CFG_AUTO_*: found on the console, "auto" in the file */
    char retroarch_title[PATH_LEN]; /* where the RetroArch title folder was found, "" if not */
    char porpoise_title[PATH_LEN];  /* the Porpoise title folder, "" if not found */
} Config;

enum
{
    CFG_AUTO_RETROARCH = 1u << 0,
    CFG_AUTO_SWANSTATION = 1u << 1,
    CFG_AUTO_PS5SX2 = 1u << 2,
    CFG_AUTO_PORPOISE = 1u << 3,
    CFG_AUTO_EDEN = 1u << 4,
    CFG_AUTO_CEMU = 1u << 5,
    CFG_AUTO_XEMU = 1u << 6,
};

/* An installed title's folder (eboot.bin / sce_sys beside it), wherever
 * ShadowMountPlus finds titles: /data/homebrew, /data/etaHEN/games (where
 * ProsperoStore installs), the same on USB (/mnt/usbN) and extended storage
 * (/mnt/extN), and one folder deeper (a release zip's wrapper folder). */
bool config_find_title(const char *title_id, char *out, size_t size);
/* Fills the "auto" emulator folders from what's installed. */
void config_resolve(Config *config);

void config_defaults(Config *config);
/* Loads config.txt over the defaults; creates the file (and a console id)
 * when missing. false when the file was missing or incomplete. */
bool config_load(Config *config);
bool config_save(const Config *config);
bool config_ready(const Config *config); /* server_url and api_key set */
void config_apply(const Config *config, HttpServer *server);

typedef struct
{
    char *key;
    char *value;
} KvItem;

typedef struct
{
    char path[PATH_LEN];
    KvItem *items;
    int count;
    int cap;
    bool dirty;
} KvStore;

void kv_load(KvStore *kv, const char *path);
const char *kv_get(const KvStore *kv, const char *key);
void kv_set(KvStore *kv, const char *key, const char *value);
void kv_remove(KvStore *kv, const char *key);
bool kv_save(KvStore *kv);
void kv_free(KvStore *kv);

/* A line in gamesync.log (best effort). */
void app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
