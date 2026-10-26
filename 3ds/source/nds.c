#include "nds.h"
#include "card_spi.h"
#include "title.h"

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#define NDS_GAMECODE_OFFSET 0x0C
#define NDS_TITLE_ID_PREFIX 0x00048000ULL

// Read the 4-char game code from an NDS ROM header on the SD card.
// Returns true and fills code_out (5 bytes) on success.
static bool nds_read_gamecode(const char *rom_path, char *code_out) {
    FILE *f = fopen(rom_path, "rb");
    if (!f) return false;

    fseek(f, NDS_GAMECODE_OFFSET, SEEK_SET);
    char code[4];
    size_t rd = fread(code, 1, 4, f);
    fclose(f);

    if (rd != 4) return false;

    // Validate printable ASCII
    for (int i = 0; i < 4; i++) {
        if (code[i] < 0x20 || code[i] > 0x7E)
            return false;
    }

    memcpy(code_out, code, 4);
    code_out[4] = '\0';
    return true;
}

// Convert a 4-char NDS game code to a u64 title ID.
// Format: 00048000 (high 32) + ASCII hex of game code (low 32)
// Example: "A2DE" -> 0x0004800041324445
static u64 nds_gamecode_to_title_id(const char *code) {
    u64 tid = NDS_TITLE_ID_PREFIX << 32;
    tid |= ((u64)(u8)code[0]) << 24;
    tid |= ((u64)(u8)code[1]) << 16;
    tid |= ((u64)(u8)code[2]) << 8;
    tid |= ((u64)(u8)code[3]);
    return tid;
}

// What counts as a DS ROM and where its save lives. One source of truth:
// the folder picker's preview counts with the same rules (folder_scan.c).
static const char *const nds_exts[] = { ".nds", NULL };
const FolderRules nds_folder_rules = { nds_exts, ".sav", "saves" };

// Find the .sav file for an NDS ROM `rom_name` in folder `dir`. Checks:
// 1. Same directory: <dir>/<stem>.sav
// 2. saves/ subfolder: <dir>/saves/<stem>.sav
// Returns true and fills sav_path_out on success.
static bool find_sav_for_rom(const char *dir, const char *rom_name, char *sav_path_out, int out_size) {
    char sav_name[MAX_PATH_LEN], sub[MAX_PATH_LEN];
    if (!folder_save_name(&nds_folder_rules, rom_name, sav_name, sizeof(sav_name))) return false;

    // Check same directory
    folder_path_join(dir, sav_name, sav_path_out, out_size);
    struct stat st;
    if (stat(sav_path_out, &st) == 0 && S_ISREG(st.st_mode))
        return true;

    // Check saves/ subfolder
    folder_path_join(dir, nds_folder_rules.save_subdir, sub, sizeof(sub));
    folder_path_join(sub, sav_name, sav_path_out, out_size);
    if (stat(sav_path_out, &st) == 0 && S_ISREG(st.st_mode))
        return true;

    return false;
}

int nds_scan(const char *nds_dir, TitleInfo *titles, int offset, int max_titles) {
    if (!nds_dir || !nds_dir[0])
        return 0;

    DIR *dp = opendir(nds_dir);
    if (!dp) return 0;

    int added = 0;
    struct dirent *entry;

    while ((entry = readdir(dp)) != NULL && (offset + added) < max_titles) {
        // Skip . and ..
        if (entry->d_name[0] == '.') continue;

        // Only process .nds files
        if (!folder_name_has_ext(entry->d_name, nds_folder_rules.exts)) continue;

        // Build full ROM path (a folder at the root, "sdmc:/", gets no "//")
        char rom_path[MAX_PATH_LEN];
        folder_path_join(nds_dir, entry->d_name, rom_path, sizeof(rom_path));

        // Read game code from ROM header
        char code[5];
        if (!nds_read_gamecode(rom_path, code))
            continue;

        // Check for duplicate game codes (already in title list)
        bool dup = false;
        for (int j = 0; j < offset + added; j++) {
            if (titles[j].is_nds && strcmp(titles[j].product_code, code) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        // Find matching .sav file (or set default path for downloads)
        char sav_path[MAX_PATH_LEN];
        bool has_save = find_sav_for_rom(nds_dir, entry->d_name, sav_path, sizeof(sav_path));
        if (!has_save) {
            // Default sav_path: prefer the saves/ subfolder if it exists
            char sav_name[MAX_PATH_LEN], saves_sub[MAX_PATH_LEN];
            folder_save_name(&nds_folder_rules, entry->d_name, sav_name, sizeof(sav_name));
            folder_path_join(nds_dir, nds_folder_rules.save_subdir, saves_sub, sizeof(saves_sub));
            struct stat st;
            if (stat(saves_sub, &st) == 0) {
                folder_path_join(saves_sub, sav_name, sav_path, sizeof(sav_path));
            } else {
                folder_path_join(nds_dir, sav_name, sav_path, sizeof(sav_path));
            }
        }

        // Build TitleInfo
        TitleInfo *t = &titles[offset + added];
        memset(t, 0, sizeof(TitleInfo));

        t->title_id = nds_gamecode_to_title_id(code);
        t->media_type = MEDIATYPE_SD;  // NDS ROMs are on SD card
        t->is_nds = true;
        t->has_save_data = has_save;
        t->in_conflict = false;

        title_id_to_hex(t->title_id, t->title_id_hex);
        strncpy(t->product_code, code, sizeof(t->product_code) - 1);
        strncpy(t->sav_path, sav_path, MAX_PATH_LEN - 1);

        // Set initial name to ROM filename (will be updated by server lookup)
        // Strip .nds extension for display
        char display_name[64];
        strncpy(display_name, entry->d_name, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';
        char *ext = strrchr(display_name, '.');
        if (ext) *ext = '\0';
        snprintf(t->name, sizeof(t->name), "%s", display_name);

        added++;
    }

    closedir(dp);
    return added;
}

int nds_read_save(const char *sav_path, ArchiveFile *files, int max_files) {
    if (max_files < 1) return ARCHIVE_ERR_TOO_MANY_FILES;

    // A missing .sav means "no save yet"; any other failure is a read error,
    // so an unreadable save is never mistaken for an empty one
    FILE *f = fopen(sav_path, "rb");
    if (!f) return errno == ENOENT ? ARCHIVE_ERR_OPEN : ARCHIVE_ERR_READ;

    // Get file size
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return ARCHIVE_ERR_READ;
    }

    u8 *data = (u8 *)malloc(size ? (size_t)size : 1);
    if (!data) {
        fclose(f);
        return ARCHIVE_ERR_READ;
    }

    size_t rd = fread(data, 1, (size_t)size, f);
    fclose(f);

    if ((long)rd != size) {
        free(data);
        return ARCHIVE_ERR_READ;
    }

    // Store as single file "save.dat" (matches ds_sync.py bundle format)
    strncpy(files[0].path, "save.dat", MAX_PATH_LEN - 1);
    files[0].path[MAX_PATH_LEN - 1] = '\0';
    files[0].size = (u32)size;
    files[0].data = data;

    return 1;
}

bool nds_write_save(const char *sav_path, const ArchiveFile *files, int file_count) {
    if (file_count < 1) return false;

    // Use the first file's data (bundles from ds_sync.py have one "save.dat" file)
    const ArchiveFile *sav = &files[0];

    // Ensure parent directory exists (for first-time downloads to saves/ subfolder)
    char dir[MAX_PATH_LEN];
    strncpy(dir, sav_path, MAX_PATH_LEN - 1);
    dir[MAX_PATH_LEN - 1] = '\0';
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir, 0777);
    }

    FILE *f = fopen(sav_path, "wb");
    if (!f) return false;

    size_t written = fwrite(sav->data, 1, sav->size, f);
    // fclose flushes; a full SD card can fail here
    if (fclose(f) != 0) return false;

    return written == sav->size;
}

int nds_cart_read_save(ArchiveFile *files, int max_files) {
    if (max_files < 1) return ARCHIVE_ERR_TOO_MANY_FILES;

    // No detectable save chip: nothing to read (and nothing can be written)
    CardSaveType type = card_spi_detect();
    if (type == SAVE_TYPE_UNKNOWN) return ARCHIVE_ERR_OPEN;

    u32 save_size = card_spi_get_size(type);
    if (save_size == 0) return ARCHIVE_ERR_OPEN;

    // From here on a save exists, so failures are read errors
    u8 *data = (u8 *)malloc(save_size);
    if (!data) return ARCHIVE_ERR_READ;

    if (!card_spi_read_save(type, data, save_size)) {
        free(data);
        return ARCHIVE_ERR_READ;
    }

    strncpy(files[0].path, "save.dat", MAX_PATH_LEN - 1);
    files[0].path[MAX_PATH_LEN - 1] = '\0';
    files[0].size = save_size;
    files[0].data = data;

    return 1;
}

bool nds_cart_write_save(const ArchiveFile *files, int file_count) {
    if (file_count < 1) return false;

    CardSaveType type = card_spi_detect();
    if (type == SAVE_TYPE_UNKNOWN) return false;

    const ArchiveFile *sav = &files[0];
    u32 save_size = card_spi_get_size(type);

    // If download is smaller than cart, pad with 0xFF
    // If download is larger, write only what fits
    u32 write_size = sav->size < save_size ? save_size : sav->size;
    if (write_size > save_size) write_size = save_size;

    u8 *buf;
    if (sav->size < save_size) {
        // Pad with 0xFF
        buf = (u8 *)malloc(save_size);
        if (!buf) return false;
        memset(buf, 0xFF, save_size);
        memcpy(buf, sav->data, sav->size);
        bool ok = card_spi_write_save(type, buf, save_size);
        free(buf);
        return ok;
    }

    return card_spi_write_save(type, sav->data, write_size);
}
