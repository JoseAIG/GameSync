#include "sync.h"
#include "archive.h"
#include "backup.h"
#include "bundle.h"
#include "config.h"
#include "nds.h"
#include "network.h"
#include "sha256.h"

#include <inttypes.h>
#include <sys/stat.h>

#define MAX_SAVE_FILES 256
#define STATE_DIR "sdmc:/3ds/3dssync/state"
// The hash Sync All sends for a title with no save on this console
#define ZERO_HASH "0000000000000000000000000000000000000000000000000000000000000000"
#define MAX_UPLOAD_SIZE 0x70000  // 448KB compressed - bundles are zlib compressed

// Stringify a numeric macro for messages
#define STR_(x) #x
#define STR(x) STR_(x)

// HTTP status of the last request that ended in SYNC_ERR_SERVER
static u32 last_server_status;

static SyncResult server_error(u32 status) {
    last_server_status = status;
    return SYNC_ERR_SERVER;
}

const char *sync_result_str(SyncResult result) {
    static char detail[128];
    switch (result) {
        case SYNC_OK:           return "OK";
        case SYNC_ERR_NETWORK:
            if (!network_last_error()[0]) return "Network error";
            snprintf(detail, sizeof(detail), "Network error: %s", network_last_error());
            return detail;
        case SYNC_ERR_SERVER:
            if (!last_server_status) return "Server error";
            snprintf(detail, sizeof(detail), "Server error (HTTP %lu)",
                     (unsigned long)last_server_status);
            return detail;
        case SYNC_ERR_ARCHIVE:  return "Save read/write error";
        case SYNC_ERR_BUNDLE:   return "Bundle format error";
        case SYNC_ERR_TOO_LARGE: return "Save too large (over 448 KB compressed)";
        case SYNC_ERR_TOO_MANY_FILES: return "Save has too many files (max " STR(MAX_SAVE_FILES) ")";
        case SYNC_ERR_INTEGRITY: return "SHA-256 mismatch in download";
        case SYNC_ERR_BACKUP:   return "Backup failed, save untouched";
        case SYNC_ERR_RESTORED: return "Write failed, old save restored";
        case SYNC_ERR_RESTORE_FAILED: return "Write and restore failed!";
        case SYNC_ERR_LOCAL_UNREADABLE: return "Can't read this console's save, left untouched";
        default:                return "Unknown error";
    }
}

// Load the last synced hash for a title from the state file.
// Returns true and fills hash_out (65 bytes) on success.
static bool load_last_synced_hash(const char *title_id_hex, char *hash_out) {
    char path[256];
    snprintf(path, sizeof(path), STATE_DIR "/%s.txt", title_id_hex);

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char buf[65] = {0};
    size_t rd = fread(buf, 1, 64, f);
    fclose(f);

    if (rd != 64) return false;

    // Validate hex characters
    for (int i = 0; i < 64; i++) {
        char c = buf[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }

    memcpy(hash_out, buf, 64);
    hash_out[64] = '\0';
    return true;
}

// Save the current hash as the last synced hash for a title.
static bool save_last_synced_hash(const char *title_id_hex, const char *hash) {
    // Ensure directories exist
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/3dssync", 0777);
    mkdir(STATE_DIR, 0777);

    char path[256];
    snprintf(path, sizeof(path), STATE_DIR "/%s.txt", title_id_hex);

    FILE *f = fopen(path, "w");
    if (!f) return false;

    size_t written = fwrite(hash, 1, 64, f);
    fclose(f);

    return written == 64;
}

// Minimal JSON string search - find value for a key in a JSON string.
// Returns pointer to the start of the value (after the colon and quote).
// Only handles simple string/number values, not nested objects.
static const char *json_find_key(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return NULL;
    pos += strlen(search);
    // Skip : and whitespace
    while (*pos == ':' || *pos == ' ' || *pos == '\t') pos++;
    return pos;
}

// Parse a JSON array of strings, returns count. Fills out_items.
static int json_parse_string_array(const char *json, const char *key,
                                   char out_items[][17], int max_items) {
    const char *arr = json_find_key(json, key);
    if (!arr || *arr != '[') return 0;
    arr++; // skip '['

    int count = 0;
    while (*arr && *arr != ']' && count < max_items) {
        // Find next quoted string
        const char *q1 = strchr(arr, '"');
        if (!q1) break;
        q1++;
        const char *q2 = strchr(q1, '"');
        if (!q2) break;

        int len = (int)(q2 - q1);
        if (len > 0 && len <= 16) {
            memcpy(out_items[count], q1, len);
            out_items[count][len] = '\0';
            count++;
        }
        arr = q2 + 1;
    }
    return count;
}

// Seconds between 1900-01-01 (osGetTime() epoch) and 1970-01-01 (Unix epoch)
#define SECONDS_1900_TO_1970 2208988800ULL

// Current time as a Unix timestamp (UTC).
// osGetTime() returns milliseconds since 1900-01-01 on the console clock, which
// is local time with no time zone, so the configured UTC offset is removed.
static u32 unix_timestamp_now(const AppConfig *config) {
    u64 secs = osGetTime() / 1000ULL;
    if (secs < SECONDS_1900_TO_1970) return 0;
    s64 unix_secs = (s64)(secs - SECONDS_1900_TO_1970)
                  - (s64)config->utc_offset_minutes * 60;
    if (unix_secs < 0) return 0;
    if (unix_secs > 0xFFFFFFFFLL) return 0xFFFFFFFFu;
    return (u32)unix_secs;
}

// Build JSON metadata for one title
static int build_title_json(char *buf, int buf_size, const AppConfig *config,
                            const TitleInfo *title,
                            const char *hash, u32 total_size,
                            const char *last_synced_hash) {
    u32 timestamp = unix_timestamp_now(config);

    if (last_synced_hash && last_synced_hash[0] != '\0') {
        return snprintf(buf, buf_size,
            "{\"title_id\":\"%s\",\"save_hash\":\"%s\","
            "\"timestamp\":%lu,\"size\":%lu,\"last_synced_hash\":\"%s\"}",
            title->title_id_hex, hash,
            (unsigned long)timestamp, (unsigned long)total_size,
            last_synced_hash);
    } else {
        return snprintf(buf, buf_size,
            "{\"title_id\":\"%s\",\"save_hash\":\"%s\","
            "\"timestamp\":%lu,\"size\":%lu}",
            title->title_id_hex, hash,
            (unsigned long)timestamp, (unsigned long)total_size);
    }
}

static SyncResult upload_title_with_hash(const AppConfig *config, const TitleInfo *title,
                                         SyncProgressCb progress, const char *save_hash) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Reading save: %s", title->title_id_hex);
    if (progress) progress(msg);

    // Heap-allocate to avoid stack overflow
    ArchiveFile *files = (ArchiveFile *)malloc(MAX_SAVE_FILES * sizeof(ArchiveFile));
    if (!files) return SYNC_ERR_ARCHIVE;

    int file_count;
    if (title->is_nds && title->media_type == MEDIATYPE_GAME_CARD)
        file_count = nds_cart_read_save(files, MAX_SAVE_FILES);
    else if (title->is_nds)
        file_count = nds_read_save(title->sav_path, files, MAX_SAVE_FILES);
    else
        file_count = archive_read(title->title_id, title->media_type,
                                  files, MAX_SAVE_FILES);
    if (file_count < 0) {
        free(files);
        return file_count == ARCHIVE_ERR_TOO_MANY_FILES ? SYNC_ERR_TOO_MANY_FILES
                                                        : SYNC_ERR_ARCHIVE;
    }
    if (file_count == 0) { free(files); return SYNC_OK; }

    // Compute hash if not provided
    char computed_hash[65] = {0};
    const char *hash_to_save = save_hash;
    if (!hash_to_save || hash_to_save[0] == '\0') {
        bundle_compute_save_hash(files, file_count, computed_hash);
        hash_to_save = computed_hash;
    }

    snprintf(msg, sizeof(msg), "Uploading: %s (%d files)", title->title_id_hex, file_count);
    if (progress) progress(msg);

    // Create bundle
    u32 timestamp = unix_timestamp_now(config);
    u32 bundle_size;
    u8 *bundle = bundle_create(title->title_id, timestamp,
                               files, file_count, &bundle_size);
    archive_free_files(files, file_count);
    free(files);

    if (!bundle) return SYNC_ERR_BUNDLE;

    // Check size before attempting upload
    if (bundle_size > MAX_UPLOAD_SIZE) {
        free(bundle);
        return SYNC_ERR_TOO_LARGE;
    }

    // POST to server — include product code so the server can store the game name.
    // Every upload is either directed by the three-way hash sync plan or
    // explicitly confirmed by the user, so skip the server's timestamp check
    // with force=true (as the Android client does). Otherwise a save uploaded
    // by another device with a newer clock would reject this upload with 409.
    char path[128];
    if (title->product_code[0]) {
        snprintf(path, sizeof(path), "/saves/%s?force=true&game_code=%s",
                 title->title_id_hex, title->product_code);
    } else {
        snprintf(path, sizeof(path), "/saves/%s?force=true", title->title_id_hex);
    }

    u32 resp_size, status;
    u8 *resp = network_post(config, path, bundle, bundle_size, &resp_size, &status);
    free(bundle);

    if (!resp) return SYNC_ERR_NETWORK;
    free(resp);

    if (status == 200) {
        // Upload succeeded - save this hash as last synced state
        save_last_synced_hash(title->title_id_hex, hash_to_save);
        return SYNC_OK;
    }
    return server_error(status);
}

// Map a bundle_parse error code to a SyncResult
static SyncResult bundle_error(int code) {
    if (code == -2) return SYNC_ERR_TOO_MANY_FILES;
    if (code == -3) return SYNC_ERR_INTEGRITY;
    return SYNC_ERR_BUNDLE;
}

// Read the current save of any kind of title (3DS archive, NDS .sav, NDS cart)
static int read_title_save(const TitleInfo *title, ArchiveFile *files) {
    if (title->is_nds && title->media_type == MEDIATYPE_GAME_CARD)
        return nds_cart_read_save(files, MAX_SAVE_FILES);
    if (title->is_nds)
        return nds_read_save(title->sav_path, files, MAX_SAVE_FILES);
    return archive_read(title->title_id, title->media_type, files, MAX_SAVE_FILES);
}

static bool write_title_save(const TitleInfo *title, const ArchiveFile *files,
                             int file_count) {
    if (title->is_nds && title->media_type == MEDIATYPE_GAME_CARD)
        return nds_cart_write_save(files, file_count);
    if (title->is_nds)
        return nds_write_save(title->sav_path, files, file_count);
    return archive_write(title->title_id, title->media_type, files, file_count);
}

// Replace a title's save with `files`. The current save is kept in memory to
// restore it if the write fails, and copied to the SD card first
// (config->local_backups copies are kept) so it can be recovered by hand.
static SyncResult write_save_safely(const AppConfig *config, const TitleInfo *title,
                                    const ArchiveFile *files, int file_count,
                                    SyncProgressCb progress) {
    char msg[128];

    // A bundle with no files would wipe the save; never treat it as a save
    if (file_count <= 0) return SYNC_ERR_BUNDLE;

    ArchiveFile *old_files = (ArchiveFile *)malloc(MAX_SAVE_FILES * sizeof(ArchiveFile));
    if (!old_files) return SYNC_ERR_BACKUP;

    int old_count = read_title_save(title, old_files);
    if (old_count == ARCHIVE_ERR_TOO_MANY_FILES || old_count == ARCHIVE_ERR_READ) {
        // A save exists but can't be copied completely: don't overwrite it
        // (whatever Local backups is set to: it couldn't be put back either)
        free(old_files);
        return SYNC_ERR_LOCAL_UNREADABLE;
    }
    if (old_count < 0) old_count = 0;  // No current save, nothing to lose

    if (old_count > 0 && config->local_backups > 0) {
        snprintf(msg, sizeof(msg), "Backing up: %s", title->title_id_hex);
        if (progress) progress(msg);
        if (!backup_save(title->title_id_hex, old_files, old_count,
                         config->local_backups, NULL, 0)) {
            archive_free_files(old_files, old_count);
            free(old_files);
            return SYNC_ERR_BACKUP;
        }
    }

    snprintf(msg, sizeof(msg), "Writing save: %s (%d files)", title->title_id_hex, file_count);
    if (progress) progress(msg);

    SyncResult result = SYNC_OK;
    if (!write_title_save(title, files, file_count)) {
        result = SYNC_ERR_ARCHIVE;
        if (old_count > 0) {
            snprintf(msg, sizeof(msg), "Write failed, restoring: %s", title->title_id_hex);
            if (progress) progress(msg);
            result = write_title_save(title, old_files, old_count)
                   ? SYNC_ERR_RESTORED : SYNC_ERR_RESTORE_FAILED;
        }
    }

    archive_free_files(old_files, old_count);
    free(old_files);
    return result;
}

static SyncResult download_title(const AppConfig *config, const TitleInfo *title,
                                 SyncProgressCb progress) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Downloading: %s", title->title_id_hex);
    if (progress) progress(msg);

    char path[64];
    snprintf(path, sizeof(path), "/saves/%s", title->title_id_hex);

    u32 resp_size, status;
    u8 *resp = network_get(config, path, &resp_size, &status);
    if (!resp) return SYNC_ERR_NETWORK;
    if (status != 200) { free(resp); return server_error(status); }

    // Heap-allocate to avoid stack overflow
    ArchiveFile *files = (ArchiveFile *)malloc(MAX_SAVE_FILES * sizeof(ArchiveFile));
    if (!files) { free(resp); return SYNC_ERR_BUNDLE; }

    u64 tid;
    u32 ts;
    u8 *decompressed = NULL;
    int file_count = bundle_parse(resp, resp_size, &tid, &ts, files, MAX_SAVE_FILES, &decompressed);
    if (file_count < 0) {
        free(files);
        if (decompressed) free(decompressed);
        free(resp);
        return bundle_error(file_count);
    }

    // Compute hash of downloaded save (before write, while data is valid)
    char new_hash[65];
    bundle_compute_save_hash(files, file_count, new_hash);

    // Write save data (backs up and restores the current save on failure)
    SyncResult result = write_save_safely(config, title, files, file_count, progress);
    free(files);
    // Free decompressed buffer if we had a compressed bundle
    // (file data pointers point into decompressed, not resp)
    if (decompressed) free(decompressed);
    free(resp);

    if (result == SYNC_OK) {
        // Download and write succeeded - save this hash as last synced state
        save_last_synced_hash(title->title_id_hex, new_hash);
    }
    return result;
}

SyncResult sync_title(const AppConfig *config, const TitleInfo *title,
                      SyncProgressCb progress) {
    // For single-title sync: always upload (the user chose to upload this save)
    // Pass NULL for hash - upload_title_with_hash will compute it
    return upload_title_with_hash(config, title, progress, NULL);
}

SyncResult sync_download_title(const AppConfig *config, const TitleInfo *title,
                               SyncProgressCb progress) {
    // Force download from server, ignoring local state
    return download_title(config, title, progress);
}

// titles[i] failed with `r`: count it, and record how and why (the text
// for the save list, when the caller asked for it)
static void note_title_failed(SyncSummary *s, int i, SyncResult r, bool upload, const char *why) {
    s->failed++;
    if (i < 0 || i >= MAX_TITLES) return;
    s->title_state[i] = TSTATE_FAILED;
    s->title_result[i] = (u8)r;
    s->title_upload[i] = upload;
    if (s->title_error) snprintf(s->title_error[i], TITLE_ERROR_LEN, "%s", why);
}

bool sync_all(const AppConfig *config, const TitleInfo *titles, int title_count,
              SyncProgressCb progress, SyncSummary *summary) {
    // Initialize summary
    SyncSummary local_summary = {0};
    local_summary.title_error = summary ? summary->title_error : NULL;

    if (progress) progress("Preparing sync metadata...");

    // Cache for computed hashes (needed for upload later)
    char (*hash_cache)[65] = (char (*)[65])malloc(title_count * 65);
    if (!hash_cache) return false;
    // Titles sent with a real save but no sync history (never synced from
    // this console). The server plans "download" for these when its copy
    // differs, which would overwrite progress made here: they become
    // conflicts instead, as in sync_decide().
    bool *no_history = (bool *)calloc(title_count > 0 ? title_count : 1, sizeof(bool));
    if (!no_history) { free(hash_cache); return false; }

    // Heap-allocate files array to avoid stack overflow
    ArchiveFile *files = (ArchiveFile *)malloc(MAX_SAVE_FILES * sizeof(ArchiveFile));
    if (!files) { free(no_history); free(hash_cache); return false; }

    // Build JSON for sync request
    // Largest entry: 73 bytes of keys/punctuation + 16 (title id) + 2 * 64
    // (hashes) + 2 * 10 (timestamp, size) = 237, plus the separating comma.
    // Header and "]}" are under 64 bytes.
    int json_cap = title_count * 256 + 64;
    char *json = (char *)malloc(json_cap);
    if (!json) { free(files); free(no_history); free(hash_cache); return false; }

    int pos = snprintf(json, json_cap, "{\"console_id\":\"%s\",\"titles\":[", config->console_id);
    bool first_title = true;

    for (int i = 0; i < title_count; i++) {
        // Skip cartridge games in automatic sync (use manual A/B buttons instead)
        if (titles[i].media_type == MEDIATYPE_GAME_CARD) {
            hash_cache[i][0] = '\0';  // Mark as skipped
            if (i < MAX_TITLES) local_summary.title_state[i] = TSTATE_SKIPPED;
            continue;
        }

        char msg[128];

        // Get fingerprint cheaply without reading content.
        // mtime is only available for NDS .sav files on the real filesystem;
        // archive saves have no mtime, so mtime=0 disables caching for them.
        int stat_fc = 0;
        u32 stat_sz = 0;
        u32 stat_mtime = 0;
        bool has_stat = false;
        if (titles[i].is_nds && titles[i].media_type != MEDIATYPE_GAME_CARD) {
            struct stat st;
            if (stat(titles[i].sav_path, &st) == 0) {
                stat_fc = 1;
                stat_sz = (u32)st.st_size;
                stat_mtime = (u32)st.st_mtime;
                has_stat = true;
            }
        } else if (!titles[i].is_nds) {
            // archive_stat gives no mtime — stat_mtime stays 0 (cache disabled)
            has_stat = (archive_stat(titles[i].title_id, titles[i].media_type,
                                     &stat_fc, &stat_sz) == 0);
        }

        char current_hash[65] = {0};
        u32 total_size = 0;

        // Try hash cache first (only for NDS saves where mtime is reliable)
        char cached_hash[65];
        if (has_stat && stat_sz > 0 &&
                config_get_cached_hash(titles[i].title_id_hex, stat_fc, stat_sz, stat_mtime, cached_hash)) {
            snprintf(msg, sizeof(msg), "Cached %d/%d: %s",
                i + 1, title_count, titles[i].title_id_hex);
            if (progress) progress(msg);
            strcpy(current_hash, cached_hash);
            total_size = stat_sz;
        } else {
            snprintf(msg, sizeof(msg), "Hashing save %d/%d: %s",
                i + 1, title_count, titles[i].title_id_hex);
            if (progress) progress(msg);

            // Read save to compute current hash
            int fc;
            if (titles[i].is_nds && titles[i].media_type == MEDIATYPE_GAME_CARD)
                fc = nds_cart_read_save(files, MAX_SAVE_FILES);
            else if (titles[i].is_nds)
                fc = nds_read_save(titles[i].sav_path, files, MAX_SAVE_FILES);
            else
                fc = archive_read(titles[i].title_id, titles[i].media_type,
                                  files, MAX_SAVE_FILES);
            if (fc == ARCHIVE_ERR_TOO_MANY_FILES || fc == ARCHIVE_ERR_READ) {
                // Never report a partial save: leave this title out of the
                // sync (like a cartridge) and count it as failed.
                snprintf(msg, sizeof(msg), "Cannot read save: %s (%s)",
                    titles[i].title_id_hex,
                    fc == ARCHIVE_ERR_TOO_MANY_FILES ? "too many files" : "read error");
                if (progress) progress(msg);
                hash_cache[i][0] = '\0';
                note_title_failed(&local_summary, i,
                                  fc == ARCHIVE_ERR_TOO_MANY_FILES ? SYNC_ERR_TOO_MANY_FILES : SYNC_ERR_ARCHIVE,
                                  true, fc == ARCHIVE_ERR_TOO_MANY_FILES
                                  ? "Can't read the save: too many files" : "Can't read the save");
                continue;
            }
            if (fc < 0) fc = 0;

            if (fc > 0) {
                bundle_compute_save_hash(files, fc, current_hash);
                for (int j = 0; j < fc; j++) total_size += files[j].size;
                archive_free_files(files, fc);
                // Store in cache for next run (skipped when mtime == 0)
                if (has_stat && stat_sz > 0)
                    config_set_cached_hash(titles[i].title_id_hex, stat_fc, stat_sz, stat_mtime, current_hash);
            } else {
                strcpy(current_hash, ZERO_HASH);
            }
        }

        // Cache this hash for potential upload later
        strcpy(hash_cache[i], current_hash);

        // Load last synced hash (if exists)
        char last_synced[65] = {0};
        bool has_last_synced = load_last_synced_hash(titles[i].title_id_hex, last_synced);
        no_history[i] = !has_last_synced && strcmp(current_hash, ZERO_HASH) != 0;

        if (!first_title) pos += snprintf(json + pos, json_cap - pos, ",");
        first_title = false;
        pos += build_title_json(json + pos, json_cap - pos, config, &titles[i],
                               current_hash, total_size,
                               has_last_synced ? last_synced : NULL);
    }

    pos += snprintf(json + pos, json_cap - pos, "]}");

    // Done with files array for hashing phase
    free(files);

    // Send sync request
    if (progress) progress("Sending sync request...");

    u32 resp_size, status;
    u8 *resp = network_post_json(config, "/sync", json, &resp_size, &status);
    free(json);

    if (!resp) { free(no_history); free(hash_cache); return false; }
    if (status != 200) { free(resp); free(no_history); free(hash_cache); return false; }

    // Null-terminate response for string parsing
    u8 *resp_str = (u8 *)realloc(resp, resp_size + 1);
    if (!resp_str) { free(resp); free(no_history); free(hash_cache); return false; }
    resp_str[resp_size] = '\0';
    char *plan = (char *)resp_str;

    // Parse sync plan - heap allocate to avoid stack overflow (256 * 17 * 5 = 21KB)
    char (*upload_ids)[17] = (char (*)[17])malloc(MAX_TITLES * 17);
    char (*download_ids)[17] = (char (*)[17])malloc(MAX_TITLES * 17);
    char (*server_only_ids)[17] = (char (*)[17])malloc(MAX_TITLES * 17);
    char (*conflict_ids)[17] = (char (*)[17])malloc(MAX_TITLES * 17);
    char (*up_to_date_ids)[17] = (char (*)[17])malloc(MAX_TITLES * 17);

    if (!upload_ids || !download_ids || !server_only_ids || !conflict_ids || !up_to_date_ids) {
        free(upload_ids);
        free(download_ids);
        free(server_only_ids);
        free(conflict_ids);
        free(up_to_date_ids);
        free(resp_str);
        free(no_history);
        free(hash_cache);
        return false;
    }

    int upload_count = json_parse_string_array(plan, "upload", upload_ids, MAX_TITLES);
    int download_count = json_parse_string_array(plan, "download", download_ids, MAX_TITLES);
    int server_only_count = json_parse_string_array(plan, "server_only", server_only_ids, MAX_TITLES);
    int conflict_count = json_parse_string_array(plan, "conflict", conflict_ids, MAX_TITLES);
    int up_to_date_count = json_parse_string_array(plan, "up_to_date", up_to_date_ids, MAX_TITLES);

    free(resp_str);

    // Auto-resolve conflicts for titles without local saves -> download
    // (no local save means nothing to lose, safe to download from server)
    for (int i = 0; i < conflict_count; ) {
        bool resolved = false;
        for (int j = 0; j < title_count; j++) {
            if (strcmp(titles[j].title_id_hex, conflict_ids[i]) == 0 &&
                !titles[j].has_save_data) {
                // Move to download list
                if (download_count < MAX_TITLES) {
                    strcpy(download_ids[download_count], conflict_ids[i]);
                    download_count++;
                }
                // Remove from conflict list (shift remaining)
                for (int k = i; k < conflict_count - 1; k++)
                    strcpy(conflict_ids[k], conflict_ids[k + 1]);
                conflict_count--;
                resolved = true;
                break;
            }
        }
        if (!resolved) i++;
    }

    // A "download" for a title this console has never synced but has a save
    // for means the server's copy differs and nobody knows which is newer
    // (the server prefers its own copy then). Downloading would silently
    // replace this console's progress: ask instead, like Smart Sync does.
    for (int i = 0; i < download_count; ) {
        bool guarded = false;
        for (int j = 0; j < title_count; j++) {
            if (strcmp(titles[j].title_id_hex, download_ids[i]) != 0) continue;
            if (hash_cache[j][0] == '\0') continue;  // Not part of this sync
            if (no_history[j] && conflict_count < MAX_TITLES) {
                strcpy(conflict_ids[conflict_count++], download_ids[i]);
                for (int k = i; k < download_count - 1; k++)
                    strcpy(download_ids[k], download_ids[k + 1]);
                download_count--;
                guarded = true;
            }
            break;
        }
        if (!guarded) i++;
    }
    free(no_history);

    // Record counts in summary
    local_summary.up_to_date = up_to_date_count;
    local_summary.conflicts = conflict_count;
    local_summary.skipped = server_only_count; // Will reduce as we download

    // Copy conflict title IDs for UI display (up to MAX_CONFLICT_DISPLAY)
    int copy_count = conflict_count < MAX_CONFLICT_DISPLAY ? conflict_count : MAX_CONFLICT_DISPLAY;
    for (int i = 0; i < copy_count; i++) {
        strncpy(local_summary.conflict_titles[i], conflict_ids[i], 16);
        local_summary.conflict_titles[i][16] = '\0';
    }
    // Clear remaining slots
    for (int i = copy_count; i < MAX_CONFLICT_DISPLAY; i++) {
        local_summary.conflict_titles[i][0] = '\0';
    }

    // Per-title state for the save list
    for (int j = 0; j < title_count && j < MAX_TITLES; j++) {
        // A title left out of the request (a cartridge, or a save that
        // couldn't be read) keeps its own state even when an installed copy
        // with the same title ID is up to date or in conflict
        if (hash_cache[j][0] == '\0') continue;
        for (int i = 0; i < up_to_date_count; i++)
            if (strcmp(titles[j].title_id_hex, up_to_date_ids[i]) == 0)
                local_summary.title_state[j] = TSTATE_SYNCED;
        for (int i = 0; i < conflict_count; i++)
            if (strcmp(titles[j].title_id_hex, conflict_ids[i]) == 0)
                local_summary.title_state[j] = TSTATE_CONFLICT;
    }

    char msg[128];

    // Process uploads
    for (int i = 0; i < upload_count; i++) {
        // Find the title and its cached hash
        for (int j = 0; j < title_count; j++) {
            if (strcmp(titles[j].title_id_hex, upload_ids[i]) == 0) {
                if (hash_cache[j][0] == '\0') continue;  // Not part of this sync
                // No save here (e.g. a DS ROM that was never played): there
                // is nothing to upload, and it isn't a failure
                if (strcmp(hash_cache[j], ZERO_HASH) == 0) break;
                snprintf(msg, sizeof(msg), "Uploading %d/%d: %s",
                    i + 1, upload_count, upload_ids[i]);
                if (progress) progress(msg);

                SyncResult r = upload_title_with_hash(config, &titles[j], NULL, hash_cache[j]);
                if (r == SYNC_OK) {
                    local_summary.uploaded++;
                    if (j < MAX_TITLES) local_summary.title_state[j] = TSTATE_UPLOADED;
                } else {
                    note_title_failed(&local_summary, j, r, true, sync_result_str(r));
                }
                break;
            }
        }
    }

    // Process downloads (both "download" and "server_only")
    int total_dl = download_count + server_only_count;
    int dl_done = 0;

    for (int i = 0; i < download_count; i++) {
        for (int j = 0; j < title_count; j++) {
            if (strcmp(titles[j].title_id_hex, download_ids[i]) == 0) {
                if (hash_cache[j][0] == '\0') continue;  // Not part of this sync
                snprintf(msg, sizeof(msg), "Downloading %d/%d: %s",
                    ++dl_done, total_dl, download_ids[i]);
                if (progress) progress(msg);

                SyncResult r = download_title(config, &titles[j], NULL);
                if (r == SYNC_OK) {
                    local_summary.downloaded++;
                    if (j < MAX_TITLES) local_summary.title_state[j] = TSTATE_DOWNLOADED;
                } else {
                    note_title_failed(&local_summary, j, r, false, sync_result_str(r));
                }
                break;
            }
        }
    }

    // server_only titles: download if title exists locally.
    // Titles left out of the sync request (cartridges, marked with an empty
    // hash_cache entry) always come back as server_only; never touch them here.
    // The same check above keeps a cartridge from being picked instead of an
    // installed copy that shares its title ID.
    for (int i = 0; i < server_only_count; i++) {
        for (int j = 0; j < title_count; j++) {
            if (strcmp(titles[j].title_id_hex, server_only_ids[i]) == 0) {
                if (hash_cache[j][0] == '\0') continue;  // Not part of this sync
                snprintf(msg, sizeof(msg), "Downloading %d/%d: %s",
                    ++dl_done, total_dl, server_only_ids[i]);
                if (progress) progress(msg);

                SyncResult r = download_title(config, &titles[j], NULL);
                local_summary.skipped--; // Was counted as skipped, now downloaded or failed
                if (r == SYNC_OK) {
                    local_summary.downloaded++;
                    if (j < MAX_TITLES) local_summary.title_state[j] = TSTATE_DOWNLOADED;
                } else {
                    note_title_failed(&local_summary, j, r, false, sync_result_str(r));
                }
                break;
            }
        }
        // If not found locally, remains in skipped count
    }

    free(upload_ids);
    free(download_ids);
    free(server_only_ids);
    free(conflict_ids);
    free(up_to_date_ids);
    free(hash_cache);

    // Send product codes for all titles so the server can resolve game names
    // for saves that were already up-to-date and never went through an upload.
    // ~40 bytes per entry: "\"0004000000161E00\":\"CTR-P-A22J\","
    int hints_cap = title_count * 45 + 16;
    char *hints_json = (char *)malloc(hints_cap);
    if (hints_json) {
        int pos = snprintf(hints_json, hints_cap, "{\"codes\":{");
        bool first_hint = true;
        for (int i = 0; i < title_count; i++) {
            if (!titles[i].product_code[0]) continue;
            if (!first_hint)
                pos += snprintf(hints_json + pos, hints_cap - pos, ",");
            pos += snprintf(hints_json + pos, hints_cap - pos,
                            "\"%s\":\"%s\"",
                            titles[i].title_id_hex, titles[i].product_code);
            first_hint = false;
        }
        snprintf(hints_json + pos, hints_cap - pos, "}}");

        if (!first_hint) {  /* at least one entry */
            u32 resp_size, status;
            u8 *resp = network_post_json(config, "/titles/update_names",
                                         hints_json, &resp_size, &status);
            if (resp) free(resp);
        }
        free(hints_json);
    }

    if (summary) *summary = local_summary;
    return true;
}

// Parse a JSON string value (no escapes, simple case)
static bool json_parse_string(const char *json, const char *key, char *out, int out_size) {
    const char *pos = json_find_key(json, key);
    if (!pos || *pos != '"') return false;
    pos++; // skip opening quote

    const char *end = strchr(pos, '"');
    if (!end) return false;

    int len = (int)(end - pos);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, pos, len);
    out[len] = '\0';
    return true;
}

// Parse a JSON integer value
static bool json_parse_int(const char *json, const char *key, int *out) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;
    *out = atoi(pos);
    return true;
}

bool sync_get_save_details(const AppConfig *config, const TitleInfo *title,
                           SaveDetails *details) {
    memset(details, 0, sizeof(SaveDetails));

    // --- Get local info ---
    ArchiveFile *files = (ArchiveFile *)malloc(MAX_SAVE_FILES * sizeof(ArchiveFile));
    if (!files) return false;

    int file_count;
    if (title->is_nds && title->media_type == MEDIATYPE_GAME_CARD)
        file_count = nds_cart_read_save(files, MAX_SAVE_FILES);
    else if (title->is_nds)
        file_count = nds_read_save(title->sav_path, files, MAX_SAVE_FILES);
    else
        file_count = archive_read(title->title_id, title->media_type,
                                  files, MAX_SAVE_FILES);
    if (file_count > 0) {
        details->local_exists = true;
        details->local_file_count = file_count;

        // Compute hash and total size
        bundle_compute_save_hash(files, file_count, details->local_hash);
        for (int i = 0; i < file_count; i++) {
            details->local_size += files[i].size;
        }
        archive_free_files(files, file_count);
    } else if (file_count == ARCHIVE_ERR_TOO_MANY_FILES || file_count == ARCHIVE_ERR_READ) {
        // A save exists but can't be read completely: don't let it look
        // missing (that would suggest a download over it)
        details->local_exists = true;
        details->local_file_count = 0;
        details->local_size = 0;
        strcpy(details->local_hash, file_count == ARCHIVE_ERR_TOO_MANY_FILES
               ? "ERROR: too many files" : "ERROR: read failed");
    } else {
        details->local_exists = (file_count == 0);  // 0 files = empty save, -1 = error/no save
        details->local_file_count = 0;
        details->local_size = 0;
        strcpy(details->local_hash, "N/A");
    }
    free(files);

    // --- Load last synced hash ---
    details->has_last_synced = load_last_synced_hash(title->title_id_hex, details->last_synced_hash);

    // --- Fetch server info ---
    char path[64];
    snprintf(path, sizeof(path), "/saves/%s/meta", title->title_id_hex);

    u32 resp_size = 0, status = 0;
    u8 *resp = network_get(config, path, &resp_size, &status);
    details->server_error = false;
    details->server_status = status;
    snprintf(details->server_error_msg, sizeof(details->server_error_msg), "%s", network_last_error());

    if (resp && status == 200) {
        // Null-terminate for string parsing
        u8 *resp_str = (u8 *)realloc(resp, resp_size + 1);
        if (resp_str) {
            resp_str[resp_size] = '\0';
            char *json = (char *)resp_str;

            details->server_exists = true;

            json_parse_string(json, "save_hash", details->server_hash, sizeof(details->server_hash));
            json_parse_string(json, "last_sync", details->server_last_sync, sizeof(details->server_last_sync));
            json_parse_string(json, "console_id", details->server_console_id, sizeof(details->server_console_id));

            int size = 0, fc = 0;
            if (json_parse_int(json, "save_size", &size)) details->server_size = (u32)size;
            if (json_parse_int(json, "file_count", &fc)) details->server_file_count = fc;

            free(resp_str);
        } else {
            free(resp);
        }
    } else if (resp && status == 404) {
        // The server answered and has no save for this title.
        free(resp);
        details->server_exists = false;
    } else {
        // No response (timeout, Wi-Fi drop) or an unexpected status: the
        // server state is unknown.
        if (resp) free(resp);
        details->server_exists = false;
        details->server_error = true;
    }

    // --- Determine sync status ---
    if (details->local_exists && details->server_exists) {
        details->is_synced = (strcmp(details->local_hash, details->server_hash) == 0);
    } else {
        details->is_synced = false;
    }

    return true;
}

SyncAction sync_decide(const SaveDetails *details) {
    // Never suggest an upload or download when we couldn't read the server.
    if (details->server_error) {
        return SYNC_ACTION_SERVER_ERROR;
    }

    // No local and no server -> up to date (nothing to sync)
    if (!details->local_exists && !details->server_exists) {
        return SYNC_ACTION_UP_TO_DATE;
    }

    // Only local exists -> upload
    if (details->local_exists && !details->server_exists) {
        return SYNC_ACTION_UPLOAD;
    }

    // Only server exists -> download
    if (!details->local_exists && details->server_exists) {
        return SYNC_ACTION_DOWNLOAD;
    }

    // Both exist - compare hashes
    if (details->is_synced) {
        return SYNC_ACTION_UP_TO_DATE;
    }

    // Hashes differ - three-way comparison
    if (details->has_last_synced) {
        // last_synced == server -> only client changed -> upload
        if (strcmp(details->last_synced_hash, details->server_hash) == 0) {
            return SYNC_ACTION_UPLOAD;
        }
        // last_synced == local -> only server changed -> download
        if (strcmp(details->last_synced_hash, details->local_hash) == 0) {
            return SYNC_ACTION_DOWNLOAD;
        }
        // All three differ -> conflict
        return SYNC_ACTION_CONFLICT;
    }

    // No sync history - server will decide based on console_id
    // For now, treat as conflict (user needs to decide)
    return SYNC_ACTION_CONFLICT;
}

int sync_get_history(const AppConfig *config, const char *title_id_hex,
                     HistoryVersion *versions, int max_versions) {
    char path[64];
    snprintf(path, sizeof(path), "/saves/%s/history", title_id_hex);

    u32 resp_size, status;
    u8 *resp = network_get(config, path, &resp_size, &status);

    if (!resp || status != 200) {
        if (resp) free(resp);
        return -1;
    }

    // Null-terminate for string parsing
    u8 *resp_str = (u8 *)realloc(resp, resp_size + 1);
    if (!resp_str) {
        free(resp);
        return -1;
    }
    resp_str[resp_size] = '\0';
    // The server lists each version's "timestamp" as Unix seconds (a JSON
    // number), the key GET /saves/{id}/history/{timestamp} takes back
    int count = history_parse_versions((const char *)resp_str, versions, max_versions);

    free(resp_str);
    return count;
}

SyncResult sync_download_history(const AppConfig *config, const TitleInfo *title,
                                 const char *timestamp, const char *server_hash,
                                 SyncProgressCb progress) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Downloading history version...");
    if (progress) progress(msg);

    char path[64];
    snprintf(path, sizeof(path), "/saves/%s/history/%s", title->title_id_hex, timestamp);

    u32 resp_size, status;
    u8 *resp = network_get(config, path, &resp_size, &status);
    if (!resp) return SYNC_ERR_NETWORK;
    if (status != 200) { free(resp); return server_error(status); }

    // Parse bundle
    ArchiveFile *files = (ArchiveFile *)malloc(MAX_SAVE_FILES * sizeof(ArchiveFile));
    if (!files) { free(resp); return SYNC_ERR_BUNDLE; }

    u64 tid;
    u32 ts;
    u8 *decompressed = NULL;
    int file_count = bundle_parse(resp, resp_size, &tid, &ts, files, MAX_SAVE_FILES, &decompressed);
    if (file_count < 0) {
        free(files);
        if (decompressed) free(decompressed);
        free(resp);
        return bundle_error(file_count);
    }

    // Compute hash of the restored version while the file data is still valid
    char hash[65];
    bundle_compute_save_hash(files, file_count, hash);

    // Write save data (backs up and restores the current save on failure)
    SyncResult result = write_save_safely(config, title, files, file_count, progress);

    free(files);
    if (decompressed) free(decompressed);
    free(resp);

    if (result != SYNC_OK) return result;

    // Never record the restored version as last synced: the server's current
    // copy is still the other one, so the next sync would see "only the
    // server changed" and download it back over the restore. Record the
    // server's current copy instead: the restore is then a change made on
    // this console, and the next sync uploads it (or reports a conflict if
    // another device uploaded meanwhile).
    if (server_hash && strlen(server_hash) == 64) {
        save_last_synced_hash(title->title_id_hex, server_hash);
    } else {
        // Server copy unknown: keep the old record, unless it is the restored
        // version itself (that would read "only the server changed" too).
        // With no record the next sync asks instead of downloading.
        char last[65];
        if (load_last_synced_hash(title->title_id_hex, last) && strcmp(last, hash) == 0) {
            char path[256];
            snprintf(path, sizeof(path), STATE_DIR "/%s.txt", title->title_id_hex);
            remove(path);
        }
    }

    return SYNC_OK;
}
