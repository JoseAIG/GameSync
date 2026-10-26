#ifndef COMMON_H
#define COMMON_H

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// APP_VERSION is defined by the Makefile from the root VERSION file

// Max values
#define MAX_TITLES        256
#define MAX_PATH_LEN      256
#define MAX_URL_LEN       256
#define MAX_API_KEY_LEN   128
#define TITLE_ERROR_LEN   48   // TitleInfo.last_error

// Config file location on SD card
#define CONFIG_PATH "sdmc:/3ds/3dssync/config.txt"
#define BACKUP_DIR  "sdmc:/3ds/3dssync/backups"

// Title info for display and sync
typedef struct {
    u64 title_id;
    FS_MediaType media_type;
    char title_id_hex[17]; // 16 hex chars + null
    char product_code[16]; // Product code from AM (e.g., CTR-P-BRBE) or NDS game code
    char name[64];         // Game name (from server lookup or product code)
    bool has_save_data;
    bool in_conflict;      // Set after sync if this title has a conflict
    bool is_nds;           // NDS game (via nds-bootstrap on SD) vs 3DS title
    bool marked;           // User-selected for batch operations
    u8 sync_state;         // TitleSyncState: what this session last learned
    char last_error[TITLE_ERROR_LEN]; // why the last transfer failed (TSTATE_FAILED), "" otherwise
    u64 state_ms;          // gui_clock_ms() when sync_state was last set (0 = never): the row's ring
    u64 mark_ms;           // gui_clock_ms() of the last mark toggle (0 = never): the check's pop-in
    char sav_path[MAX_PATH_LEN]; // NDS only: path to .sav file on SD card
} TitleInfo;

// Per-title sync state shown in the save list (not persisted: it reflects
// what this session last learned about the title)
typedef enum {
    TSTATE_UNKNOWN = 0,
    TSTATE_SYNCED,          // hashes match
    TSTATE_UPLOADED,        // uploaded this session
    TSTATE_DOWNLOADED,      // downloaded this session
    TSTATE_NEEDS_UPLOAD,    // compared: only this console changed
    TSTATE_NEEDS_DOWNLOAD,  // compared: only the server changed
    TSTATE_CONFLICT,        // both changed
    TSTATE_FAILED,          // last transfer failed
    TSTATE_SKIPPED,         // game card: left out of Sync All
} TitleSyncState;

// Console ID file location
#define CONSOLE_ID_PATH  "sdmc:/3ds/3dssync/console_id.txt"
#define HASH_CACHE_FILE  "sdmc:/3ds/3dssync/hash_cache.dat"

// App configuration loaded from SD card
typedef struct {
    char server_url[MAX_URL_LEN];
    char server_url_alt[MAX_URL_LEN]; // Optional fallback, e.g. the server's IP on a second LAN
    bool use_alt_url;       // Runtime only (never saved): primary didn't answer, alt did
    char api_key[MAX_API_KEY_LEN];
    char console_id[17];  // 16 hex chars + null (generated on first run)
    char nds_dir[MAX_PATH_LEN]; // NDS ROM directory on SD (e.g., "sdmc:/roms/nds")
    int utc_offset_minutes; // Console clock offset from UTC (e.g. -240 for UTC-4)
    int local_backups;      // Copies of the previous save kept before overwriting (0 = off)
    bool tls_verify;        // Verify HTTPS server certificates (default true)
    bool update_check;      // Allow checking for / installing app updates (default true)
    bool reduce_motion;     // Fades and snaps instead of slides and springs (default false)
} AppConfig;

#endif // COMMON_H
