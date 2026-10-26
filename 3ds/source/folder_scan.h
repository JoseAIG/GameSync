#ifndef FOLDER_SCAN_H
#define FOLDER_SCAN_H

// Folders on the SD card, for the folder picker and the NDS scanner: path
// helpers that never produce "//", the "is this a ROM / where is its save"
// rules nds_scan() uses, a folder listing, and an incremental counter that
// says how many ROMs (and saves beside them) a folder holds without ever
// stalling a frame. Plain C on POSIX dirent (no libctru), so it is
// host-tested (tests/test_folder_scan.c).
//
// Folder vs file comes from dirent.d_type (libctru's sdmc device fills it
// in): nothing here calls stat(), which costs an SD round trip per entry.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <dirent.h>

#define FOLDER_PATH_MAX 256
#define FOLDER_NAME_MAX 256
// Entries read from one directory when counting, folders kept by a listing
#define FOLDER_SCAN_CAP 512

// What a folder should hold, e.g. NDS ROMs: { {".nds", NULL}, ".sav", "saves" }
typedef struct {
    const char *const *exts;    // item extensions, case-insensitive (NULL-terminated)
    const char *save_ext;       // save beside an item ("<stem>.sav"), or NULL
    const char *save_subdir;    // ...or inside this subfolder ("saves"), or NULL
} FolderRules;

// ---------------------------------------------------------------------------
// Rules (shared with nds_scan)
// ---------------------------------------------------------------------------

// Does `name` end with one of `exts` (case-insensitive)?
bool folder_name_has_ext(const char *name, const char *const *exts);
// The save file name of item `name`: its stem (up to the last '.') plus
// rules->save_ext. False without a save_ext or when it doesn't fit.
bool folder_save_name(const FolderRules *rules, const char *name, char *out, size_t out_size);

// ---------------------------------------------------------------------------
// Paths ("sdmc:/roms/nds": no trailing '/', except the root "sdmc:/")
// ---------------------------------------------------------------------------

// dir + '/' + name with exactly one '/' between them ("sdmc:/" + "roms" =
// "sdmc:/roms", "sdmc:" + "x.sav" = "sdmc:/x.sav")
void folder_path_join(const char *dir, const char *name, char *out, size_t out_size);
// Tidy a typed path: "/roms/" -> "sdmc:/roms", "roms//nds/" ->
// "sdmc:/roms/nds", "sdmc:" -> "sdmc:/". Surrounding spaces are dropped.
void folder_path_normalize(const char *in, char *out, size_t out_size);
// "sdmc:/" (any "<device>:/" with nothing after it)
bool folder_path_is_root(const char *path);
// The folder above a normalized path; false at the root
bool folder_path_parent(const char *path, char *out, size_t out_size);
// The last part of a normalized path ("nds"); the root itself for the root
const char *folder_path_name(const char *path);

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

typedef struct {
    char (*names)[FOLDER_NAME_MAX];   // caller-owned, `cap` rows
    int cap;
    int count;
    bool truncated;                   // more folders than `cap`: the first ones read are kept
} FolderList;

// The subfolders of `path` (files and names starting with '.' are left
// out), sorted with strcasecmp. `tick` (may be NULL) is called every 32
// entries read, so a slow folder can show it is busy. False (and the list
// left empty) when the folder can't be opened.
bool folder_list_read(const char *path, FolderList *list, void (*tick)(void *ctx), void *ctx);
// Index of the folder called `name` (case-insensitive), -1 if none
int folder_list_find(const FolderList *list, const char *name);

// ---------------------------------------------------------------------------
// Counting
// ---------------------------------------------------------------------------

typedef struct {
    int items;          // files matching rules->exts, top level only
    int saves;          // items with a save beside them or in save_subdir
    int saves_in_sub;   // ...of which in save_subdir
    int folders;        // subfolders (not starting with '.')
    bool opened;        // the folder could be read
    bool capped;        // a directory had more than FOLDER_SCAN_CAP entries: counts are of the first ones
} FolderCount;

// A count that can stop after any entry and carry on later: an open DIR*
// cursor over the folder, then over its save_subdir. About 6 KB.
typedef struct {
    FolderRules rules;
    char path[FOLDER_PATH_MAX];
    DIR *dir;
    int phase;          // 0 idle, 1 the folder, 2 its save_subdir, 3 done
    int entries;        // read from the current directory
    uint32_t item_hash[FOLDER_SCAN_CAP];
    uint32_t save_hash[FOLDER_SCAN_CAP];
    uint32_t sub_hash[FOLDER_SCAN_CAP];
    int nitem, nsave, nsub;
    FolderCount result;
} FolderCounter;

// Start counting `path` (stops any count in progress)
void folder_count_start(FolderCounter *c, const FolderRules *rules, const char *path);
// Read entries until done or until now_ms() reaches deadline_ms (checked
// after every entry; now_ms NULL = no deadline). True once the result is
// final.
bool folder_count_step(FolderCounter *c, uint64_t (*now_ms)(void), uint64_t deadline_ms);
// Abandon a count (closes its directory). Safe on an idle counter.
void folder_count_stop(FolderCounter *c);
bool folder_count_busy(const FolderCounter *c);   // started, not done
bool folder_count_done(const FolderCounter *c);

// "12 .nds ROMs · 10 saves (4 in saves/)", "1 .nds ROM · no saves", or
// "No .nds ROMs in this folder" (`item_label` is the plural, ".nds ROMs")
void folder_count_summary(const FolderCount *c, const FolderRules *rules, const char *item_label,
                          char *out, size_t out_size);
// "14 .nds ROMs", "no .nds ROMs · 3 folders", "can't be read"
void folder_count_brief(const FolderCount *c, const char *item_label, char *out, size_t out_size);

// Counts by path, least recently used dropped first
#define FOLDER_CACHE_SIZE 16
typedef struct {
    struct {
        char path[FOLDER_PATH_MAX];
        FolderCount count;
        uint32_t used;   // 0 = empty slot
    } e[FOLDER_CACHE_SIZE];
    uint32_t clock;
} FolderCache;

bool folder_cache_get(FolderCache *cache, const char *path, FolderCount *out);
void folder_cache_put(FolderCache *cache, const char *path, const FolderCount *count);

#endif // FOLDER_SCAN_H
