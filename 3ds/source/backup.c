#include "backup.h"

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_BACKUP_DIRS 64

static bool make_dir(const char *path) {
    if (mkdir(path, 0777) == 0 || errno == EEXIST) return true;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Create every missing directory in an SD path (the last component included)
static bool mkdir_p(const char *path) {
    char buf[MAX_PATH_LEN * 2];
    snprintf(buf, sizeof(buf), "%s", path);

    // Skip the "sdmc:/" device prefix
    char *p = strchr(buf, '/');
    if (!p) return false;

    for (p = p + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!make_dir(buf)) return false;
            *p = '/';
        }
    }
    return make_dir(buf);
}

// Delete a directory tree on the SD card
static void remove_tree(const char *path) {
    DIR *dp = opendir(path);
    if (dp) {
        struct dirent *ent;
        while ((ent = readdir(dp)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            char child[MAX_PATH_LEN * 2];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            struct stat st;
            if (stat(child, &st) == 0 && S_ISDIR(st.st_mode))
                remove_tree(child);
            else
                unlink(child);
        }
        closedir(dp);
    }
    rmdir(path);
}

static int compare_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

// Keep only the newest `keep` backups of a title. Backup folder names are
// timestamps (YYYYMMDD-HHMMSS), so name order is age order.
static void prune_backups(const char *title_dir, int keep) {
    DIR *dp = opendir(title_dir);
    if (!dp) return;

    char (*names)[32] = (char (*)[32])malloc(MAX_BACKUP_DIRS * 32);
    if (!names) { closedir(dp); return; }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL && count < MAX_BACKUP_DIRS) {
        if (ent->d_name[0] == '.' || strlen(ent->d_name) >= 32) continue;
        snprintf(names[count++], 32, "%s", ent->d_name);
    }
    closedir(dp);

    qsort(names, count, 32, compare_names);
    for (int i = 0; i < count - keep; i++) {
        char path[MAX_PATH_LEN * 2];
        snprintf(path, sizeof(path), "%s/%s", title_dir, names[i]);
        remove_tree(path);
    }
    free(names);
}

bool backup_save(const char *title_id_hex, const ArchiveFile *files, int file_count,
                 int keep, char *out_dir, int out_size) {
    if (out_dir && out_size > 0) out_dir[0] = '\0';
    if (keep <= 0 || file_count <= 0) return true;

    char title_dir[MAX_PATH_LEN];
    snprintf(title_dir, sizeof(title_dir), BACKUP_DIR "/%s", title_id_hex);

    // Console clock time (local time, which is what the user sees)
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char stamp[32];
    if (tm)
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm);
    else
        snprintf(stamp, sizeof(stamp), "backup");

    // Avoid reusing a folder if two writes happen within the same second
    char dir[MAX_PATH_LEN + 40];
    snprintf(dir, sizeof(dir), "%s/%s", title_dir, stamp);
    struct stat st;
    for (int n = 1; stat(dir, &st) == 0 && n < 100; n++)
        snprintf(dir, sizeof(dir), "%s/%s_%02d", title_dir, stamp, n);

    if (!mkdir_p(dir)) return false;

    for (int i = 0; i < file_count; i++) {
        char path[MAX_PATH_LEN * 2];
        snprintf(path, sizeof(path), "%s/%s", dir, files[i].path);

        // Create subdirectories of the save path
        char *slash = strrchr(path, '/');
        if (slash && slash > path + strlen(dir)) {
            *slash = '\0';
            bool ok = mkdir_p(path);
            *slash = '/';
            if (!ok) { remove_tree(dir); return false; }
        }

        FILE *f = fopen(path, "wb");
        if (!f) { remove_tree(dir); return false; }
        size_t written = files[i].size ? fwrite(files[i].data, 1, files[i].size, f) : 0;
        bool ok = (written == files[i].size);
        if (fclose(f) != 0) ok = false;
        if (!ok) { remove_tree(dir); return false; }
    }

    prune_backups(title_dir, keep);

    if (out_dir && out_size > 0) snprintf(out_dir, out_size, "%s", dir);
    return true;
}
