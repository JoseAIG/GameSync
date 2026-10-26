#include "folder_scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool ends_with_ci(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    if (m > n) return false;
    for (size_t i = 0; i < m; i++)
        if (lower(s[n - m + i]) != lower(suffix[i])) return false;
    return true;
}

bool folder_name_has_ext(const char *name, const char *const *exts) {
    if (!name || !exts) return false;
    for (int i = 0; exts[i]; i++)
        if (ends_with_ci(name, exts[i])) return true;
    return false;
}

// Length of `name` without its extension (up to the last '.')
static size_t stem_len(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot ? (size_t)(dot - name) : strlen(name);
}

bool folder_save_name(const FolderRules *rules, const char *name, char *out, size_t out_size) {
    if (!rules || !rules->save_ext || !name || out_size == 0) return false;
    int n = snprintf(out, out_size, "%.*s%s", (int)stem_len(name), name, rules->save_ext);
    return n > 0 && (size_t)n < out_size;
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

void folder_path_join(const char *dir, const char *name, char *out, size_t out_size) {
    if (out_size == 0) return;
    if (!dir) dir = "";
    if (!name) name = "";
    while (*name == '/') name++;
    size_t n = strlen(dir);
    if (n == 0) snprintf(out, out_size, "%s", name);
    else if (dir[n - 1] == '/') snprintf(out, out_size, "%s%s", dir, name);
    else snprintf(out, out_size, "%s/%s", dir, name);
}

// Where the path proper starts: after "<device>:" (letters only), else 0
static size_t device_len(const char *p) {
    size_t i = 0;
    while ((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= '0' && p[i] <= '9')) i++;
    return (i > 0 && p[i] == ':') ? i + 1 : 0;
}

void folder_path_normalize(const char *in, char *out, size_t out_size) {
    if (out_size == 0) return;
    char buf[FOLDER_PATH_MAX * 2];
    if (!in) in = "";
    while (*in == ' ' || *in == '\t') in++;
    size_t dev = device_len(in);
    if (dev) snprintf(buf, sizeof(buf), "%.*s/%s", (int)dev, in, in + dev);
    else snprintf(buf, sizeof(buf), "sdmc:/%s", in);
    // Trailing spaces, repeated and trailing slashes
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' || buf[len - 1] == '\r' ||
                       buf[len - 1] == '\n'))
        buf[--len] = '\0';
    size_t start = device_len(buf);   // index of the '/' after the device
    size_t w = start;
    for (size_t r = start; buf[r]; r++) {
        if (buf[r] == '/' && w > start && buf[w - 1] == '/') continue;
        buf[w++] = buf[r];
    }
    buf[w] = '\0';
    if (w > start + 1 && buf[w - 1] == '/') buf[--w] = '\0';
    snprintf(out, out_size, "%s", buf);
}

bool folder_path_is_root(const char *path) {
    if (!path) return false;
    size_t dev = device_len(path);
    return dev > 0 && path[dev] == '/' && path[dev + 1] == '\0';
}

bool folder_path_parent(const char *path, char *out, size_t out_size) {
    if (!path || folder_path_is_root(path)) return false;
    const char *slash = strrchr(path, '/');
    if (!slash) return false;
    size_t keep = (size_t)(slash - path);
    // "sdmc:/roms" -> "sdmc:/" (the root keeps its slash)
    if (keep > 0 && path[keep - 1] == ':') keep++;
    if (keep == 0) return false;
    snprintf(out, out_size, "%.*s", (int)keep, path);
    return true;
}

const char *folder_path_name(const char *path) {
    if (!path) return "";
    if (folder_path_is_root(path)) return path;
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

static int name_cmp(const void *a, const void *b) {
    return strcasecmp((const char *)a, (const char *)b);
}

bool folder_list_read(const char *path, FolderList *list, void (*tick)(void *ctx), void *ctx) {
    list->count = 0;
    list->truncated = false;
    DIR *dp = opendir(path);
    if (!dp) return false;
    struct dirent *e;
    int read = 0;
    while ((e = readdir(dp)) != NULL) {
        if (tick && ++read % 32 == 0) tick(ctx);
        if (e->d_type != DT_DIR || e->d_name[0] == '.') continue;
        if (list->count >= list->cap) {
            list->truncated = true;
            continue;   // keep reading: only the folders kept are capped
        }
        snprintf(list->names[list->count++], FOLDER_NAME_MAX, "%s", e->d_name);
    }
    closedir(dp);
    qsort(list->names, (size_t)list->count, FOLDER_NAME_MAX, name_cmp);
    return true;
}

int folder_list_find(const FolderList *list, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < list->count; i++)
        if (strcasecmp(list->names[i], name) == 0) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Counting
// ---------------------------------------------------------------------------

// FNV-1a of the stem, ASCII case folded (the SD card's FAT is
// case-insensitive, so "Game.SAV" is the save of "game.nds")
static uint32_t stem_hash(const char *name, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)lower(name[i]);
        h *= 16777619u;
    }
    return h;
}

static int hash_cmp(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static bool has_hash(const uint32_t *v, int n, uint32_t h) {
    return bsearch(&h, v, (size_t)n, sizeof(uint32_t), hash_cmp) != NULL;
}

void folder_count_stop(FolderCounter *c) {
    if (c->dir) closedir(c->dir);
    c->dir = NULL;
    c->phase = 0;
}

void folder_count_start(FolderCounter *c, const FolderRules *rules, const char *path) {
    folder_count_stop(c);
    c->rules = *rules;
    snprintf(c->path, sizeof(c->path), "%s", path);
    c->entries = 0;
    c->nitem = c->nsave = c->nsub = 0;
    memset(&c->result, 0, sizeof(c->result));
    c->phase = 1;
}

bool folder_count_busy(const FolderCounter *c) {
    return c->phase == 1 || c->phase == 2;
}

bool folder_count_done(const FolderCounter *c) {
    return c->phase == 3;
}

// nds_scan's rule: a save beside the item wins, else one in save_subdir
static void count_finish(FolderCounter *c) {
    qsort(c->save_hash, (size_t)c->nsave, sizeof(uint32_t), hash_cmp);
    qsort(c->sub_hash, (size_t)c->nsub, sizeof(uint32_t), hash_cmp);
    for (int i = 0; i < c->nitem; i++) {
        if (has_hash(c->save_hash, c->nsave, c->item_hash[i])) {
            c->result.saves++;
        } else if (has_hash(c->sub_hash, c->nsub, c->item_hash[i])) {
            c->result.saves++;
            c->result.saves_in_sub++;
        }
    }
    c->phase = 3;
}

// One entry of the folder itself
static void count_top_entry(FolderCounter *c, const struct dirent *e) {
    if (e->d_name[0] == '.') return;
    if (e->d_type == DT_DIR) {
        c->result.folders++;
        return;
    }
    if (folder_name_has_ext(e->d_name, c->rules.exts)) {
        c->result.items++;
        c->item_hash[c->nitem++] = stem_hash(e->d_name, stem_len(e->d_name));
    } else if (c->rules.save_ext && ends_with_ci(e->d_name, c->rules.save_ext)) {
        c->save_hash[c->nsave++] = stem_hash(e->d_name, strlen(e->d_name) - strlen(c->rules.save_ext));
    }
}

// One entry of save_subdir
static void count_sub_entry(FolderCounter *c, const struct dirent *e) {
    if (e->d_name[0] == '.' || e->d_type == DT_DIR) return;
    if (ends_with_ci(e->d_name, c->rules.save_ext))
        c->sub_hash[c->nsub++] = stem_hash(e->d_name, strlen(e->d_name) - strlen(c->rules.save_ext));
}

bool folder_count_step(FolderCounter *c, uint64_t (*now_ms)(void), uint64_t deadline_ms) {
    while (c->phase == 1 || c->phase == 2) {
        if (!c->dir) {
            char sub[FOLDER_PATH_MAX];
            const char *path = c->path;
            if (c->phase == 2) {
                folder_path_join(c->path, c->rules.save_subdir, sub, sizeof(sub));
                path = sub;
            }
            c->dir = opendir(path);
            c->entries = 0;
            if (!c->dir) {
                // The folder: unreadable. Its save_subdir: simply none.
                if (c->phase == 1) {
                    c->phase = 3;
                    return true;
                }
                count_finish(c);
                return true;
            }
            if (c->phase == 1) c->result.opened = true;
        }
        struct dirent *e = c->entries < FOLDER_SCAN_CAP ? readdir(c->dir) : NULL;
        if (e) {
            c->entries++;
            if (c->phase == 1) count_top_entry(c, e);
            else count_sub_entry(c, e);
        } else {
            // This directory is done (or capped: is there more past the cap?)
            if (c->entries >= FOLDER_SCAN_CAP && readdir(c->dir)) c->result.capped = true;
            closedir(c->dir);
            c->dir = NULL;
            if (c->phase == 1 && c->rules.save_ext && c->rules.save_subdir && c->nitem > 0) {
                c->phase = 2;
            } else {
                count_finish(c);
                return true;
            }
        }
        if (now_ms && now_ms() >= deadline_ms) return false;
    }
    return c->phase == 3;
}

static void plural(int n, const char *label, char *out, size_t size) {
    // The label is the plural (".nds ROMs"): one drops its final 's'
    size_t len = strlen(label);
    if (n == 1 && len > 1 && label[len - 1] == 's') snprintf(out, size, "1 %.*s", (int)len - 1, label);
    else snprintf(out, size, "%d %s", n, label);
}

#define DOT " \xC2\xB7 "

void folder_count_summary(const FolderCount *c, const FolderRules *rules, const char *item_label,
                          char *out, size_t out_size) {
    if (!c->opened) {
        snprintf(out, out_size, "This folder can't be read");
        return;
    }
    if (c->items == 0) {
        snprintf(out, out_size, "No %s in this folder", item_label);
        return;
    }
    char items[48];
    plural(c->items, item_label, items, sizeof(items));
    if (!rules || !rules->save_ext) {
        snprintf(out, out_size, "%s", items);
        return;
    }
    char saves[32];
    if (c->saves == 0) snprintf(saves, sizeof(saves), "no saves");
    else plural(c->saves, "saves", saves, sizeof(saves));
    if (c->saves_in_sub > 0 && rules->save_subdir)
        snprintf(out, out_size, "%s" DOT "%s (%d in %s/)", items, saves, c->saves_in_sub, rules->save_subdir);
    else
        snprintf(out, out_size, "%s" DOT "%s", items, saves);
}

void folder_count_brief(const FolderCount *c, const char *item_label, char *out, size_t out_size) {
    if (!c->opened) {
        snprintf(out, out_size, "can't be read");
        return;
    }
    if (c->items > 0) {
        plural(c->items, item_label, out, out_size);
        return;
    }
    if (c->folders > 0) {
        char folders[24];
        plural(c->folders, "folders", folders, sizeof(folders));
        snprintf(out, out_size, "no %s" DOT "%s", item_label, folders);
    } else {
        snprintf(out, out_size, "no %s", item_label);
    }
}

bool folder_cache_get(FolderCache *cache, const char *path, FolderCount *out) {
    for (int i = 0; i < FOLDER_CACHE_SIZE; i++) {
        if (cache->e[i].used && strcmp(cache->e[i].path, path) == 0) {
            cache->e[i].used = ++cache->clock;
            if (out) *out = cache->e[i].count;
            return true;
        }
    }
    return false;
}

void folder_cache_put(FolderCache *cache, const char *path, const FolderCount *count) {
    int slot = 0;
    for (int i = 0; i < FOLDER_CACHE_SIZE; i++) {
        if (cache->e[i].used && strcmp(cache->e[i].path, path) == 0) {
            slot = i;
            break;
        }
        // An empty slot, else the least recently used
        if (cache->e[i].used < cache->e[slot].used) slot = i;
    }
    snprintf(cache->e[slot].path, sizeof(cache->e[slot].path), "%s", path);
    cache->e[slot].count = *count;
    cache->e[slot].used = ++cache->clock;
}
