#ifndef SYNC_H
#define SYNC_H

#include "common.h"
#include "title.h"

// Sync result for UI feedback
typedef enum {
    SYNC_OK,
    SYNC_ERR_NETWORK,
    SYNC_ERR_SERVER,
    SYNC_ERR_ARCHIVE,
    SYNC_ERR_BUNDLE,
    SYNC_ERR_TOO_LARGE,
    SYNC_ERR_TOO_MANY_FILES,
    SYNC_ERR_INTEGRITY,      // Downloaded file failed its SHA-256 check
    SYNC_ERR_BACKUP,         // Current save couldn't be backed up; nothing written
    SYNC_ERR_RESTORED,       // Write failed; the previous save was put back
    SYNC_ERR_RESTORE_FAILED, // Write failed and so did restoring the previous save
} SyncResult;

// Sync action decided by smart sync
typedef enum {
    SYNC_ACTION_UPLOAD,     // Client newer than server -> upload
    SYNC_ACTION_DOWNLOAD,  // Server newer than client -> download
    SYNC_ACTION_UP_TO_DATE, // Hashes match
    SYNC_ACTION_CONFLICT,   // Both changed -> user needs to decide
    SYNC_ACTION_SERVER_ERROR, // Server state unknown (network/HTTP error) -> do nothing
} SyncAction;

// Summary of sync_all operation
#define MAX_CONFLICT_DISPLAY 8  // Max conflicts to report for UI

typedef struct {
    int uploaded;
    int downloaded;
    int up_to_date;
    int conflicts;
    int failed;
    int skipped;       // server_only titles not on this device
    // First few conflicting title IDs for display (null-terminated strings)
    char conflict_titles[MAX_CONFLICT_DISPLAY][17];
    // TitleSyncState for each entry of the titles[] passed to sync_all
    u8 title_state[MAX_TITLES];
} SyncSummary;

// Callback for progress updates during sync
typedef void (*SyncProgressCb)(const char *message);

// Get human-readable error message for a SyncResult
const char *sync_result_str(SyncResult result);

// Sync a single title with the server.
SyncResult sync_title(const AppConfig *config, const TitleInfo *title,
                      SyncProgressCb progress);

// Sync all titles: sends metadata to /sync endpoint, then
// uploads/downloads as directed by the sync plan.
// Returns true on success (even if some titles failed), false on fatal error.
// Fills summary with counts if non-NULL.
bool sync_all(const AppConfig *config, const TitleInfo *titles, int title_count,
              SyncProgressCb progress, SyncSummary *summary);

// Download a specific title from the server (force download, ignore local state)
SyncResult sync_download_title(const AppConfig *config, const TitleInfo *title,
                               SyncProgressCb progress);

// Save details info (for details dialog)
typedef struct {
    // Local info
    int local_file_count;
    u32 local_size;
    char local_hash[65];
    bool local_exists;

    // Server info (fetched from server)
    bool server_exists;
    int server_file_count;
    u32 server_size;
    char server_hash[65];
    char server_last_sync[32];      // ISO 8601 date
    char server_console_id[17];     // Which console uploaded
    // True when the server could not be queried (no response, or an HTTP
    // status other than 200/404). server_exists is then meaningless: we must
    // not treat "couldn't ask" as "server has nothing" and offer an upload.
    bool server_error;
    u32 server_status;              // HTTP status, 0 if there was no response
    char server_error_msg[96];      // network_last_error() of that query, "" if none

    // Sync status
    bool is_synced;           // local_hash == server_hash
    bool has_last_synced;     // Whether we have a sync state file
    char last_synced_hash[65];
} SaveDetails;

// Get detailed info about a save (local + server).
// Fills details struct with information. Returns true on success.
bool sync_get_save_details(const AppConfig *config, const TitleInfo *title,
                           SaveDetails *details);

// Decide the sync action based on SaveDetails (hash-only three-way comparison).
// Returns the suggested action (upload/download/up_to_date/conflict).
SyncAction sync_decide(const SaveDetails *details);

// History version info
#define MAX_HISTORY_VERSIONS 20

typedef struct {
    char timestamp[32];  // ISO 8601 timestamp
    u32 size;
    int file_count;
} HistoryVersion;

// Get list of history versions for a title.
// Returns number of versions found, or -1 on error.
int sync_get_history(const AppConfig *config, const char *title_id_hex,
                     HistoryVersion *versions, int max_versions);

// Download a specific history version.
// Returns SYNC_OK on success, error code on failure.
SyncResult sync_download_history(const AppConfig *config, const TitleInfo *title,
                                 const char *timestamp, SyncProgressCb progress);

#endif // SYNC_H
