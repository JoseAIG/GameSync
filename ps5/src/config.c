/*
 * GameSync PS5 client - configuration and small key=value stores.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char data_dir[PATH_LEN] = DEFAULT_DATA_DIR;

void config_set_data_dir(const char *dir)
{
    util_copy(data_dir, sizeof(data_dir), dir);
}

const char *config_data_dir(void)
{
    return data_dir;
}

const char *data_path(const char *name)
{
    static char buffers[4][PATH_LEN];
    static int next;
    char *out = buffers[next];
    next = (next + 1) % 4;
    util_path_join(out, PATH_LEN, data_dir, name);
    return out;
}

/* SwanStationPS5 keeps its data in /data/SwanStationPS5; builds before
 * Oct 2026 used /data/PSXS5 (src/platform/plat_sdl.c plat_default_root). The
 * new one wins when both are there: an update leaves the old folder behind. */
static const char *swanstation_default(void)
{
    if (!util_dir_exists("/data/SwanStationPS5") && util_dir_exists("/data/PSXS5"))
        return "/data/PSXS5";
    return "/data/SwanStationPS5";
}

void config_defaults(Config *config)
{
    memset(config, 0, sizeof(*config));
    util_copy(config->retroarch_dir, sizeof(config->retroarch_dir), "/data/homebrew/PPSA99169");
    util_copy(config->swanstation_dir, sizeof(config->swanstation_dir), swanstation_default());
    util_copy(config->ps5sx2_dir, sizeof(config->ps5sx2_dir), "/data/PCSX2");
    util_copy(config->porpoise_dir, sizeof(config->porpoise_dir), "/data/porpoise");
    util_copy(config->eden_dir, sizeof(config->eden_dir), "/data/prosperoeden");
    util_copy(config->ps2_card, sizeof(config->ps2_card), "Mcd001.ps2");
    config->ps2_per_game = true;
    util_copy(config->cemu_dir, sizeof(config->cemu_dir), "/data/ps5cemu");
    util_copy(config->xemu_dir, sizeof(config->xemu_dir), "/data/xemu");
    config->auto_dirs = CFG_AUTO_RETROARCH | CFG_AUTO_SWANSTATION | CFG_AUTO_PS5SX2 |
                        CFG_AUTO_PORPOISE | CFG_AUTO_EDEN | CFG_AUTO_CEMU | CFG_AUTO_XEMU;
}

static const char *dir_value(const Config *config, unsigned flag, const char *dir)
{
    return (config->auto_dirs & flag) ? "auto" : dir;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t len = strlen(s);
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' ||
                   s[len - 1] == '\n'))
        s[--len] = 0;
    return s;
}

static void new_console_id(Config *config)
{
    /* No stable hardware id is reachable from a title: a random one, kept in
     * the config from then on. */
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)config;
    srand(seed);
    snprintf(config->console_id, sizeof(config->console_id), "ps5_%04x%04x",
             (unsigned)rand() & 0xFFFF, (unsigned)rand() & 0xFFFF);
}

/* "auto", empty, or the folder earlier versions wrote as the default -> found
 * on the console; anything else is the user's own choice. */
static void set_dir(Config *config, unsigned flag, char *field, const char *value,
                    const char *old_default)
{
    if (!*value || util_ieq(value, "auto") || strcmp(value, old_default) == 0)
    {
        config->auto_dirs |= flag;
        return;
    }
    config->auto_dirs &= ~flag;
    util_copy(field, PATH_LEN, value);
}

static bool is_title_dir(const char *dir, const char *title_id)
{
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/eboot.bin", dir);
    if (!util_file_exists(path))
        return false;
    snprintf(path, sizeof(path), "%s/sce_sys/param.json", dir);
    char *json = (char *)util_read_file(path, NULL);
    if (!json)
        return true; /* named after the title id and runnable: good enough */
    bool match = strstr(json, title_id) != NULL;
    free(json);
    return match;
}

bool config_find_title(const char *title_id, char *out, size_t size)
{
    char roots[40][64];
    int n = 0;
    snprintf(roots[n++], 64, "/data/etaHEN/games");
    snprintf(roots[n++], 64, "/data/homebrew");
    for (int i = 0; i < 8; ++i)
    {
        snprintf(roots[n++], 64, "/mnt/usb%d/etaHEN/games", i);
        snprintf(roots[n++], 64, "/mnt/usb%d/homebrew", i);
    }
    for (int i = 0; i < 2; ++i)
    {
        snprintf(roots[n++], 64, "/mnt/ext%d/etaHEN/games", i);
        snprintf(roots[n++], 64, "/mnt/ext%d/homebrew", i);
    }
    for (int r = 0; r < n; ++r)
    {
        char dir[PATH_LEN];
        snprintf(dir, sizeof(dir), "%s/%s", roots[r], title_id);
        if (is_title_dir(dir, title_id))
        {
            util_copy(out, size, dir);
            return true;
        }
        /* a wrapper folder from a release zip: <root>/<anything>/<title id> */
        DIR *d = opendir(roots[r]);
        if (!d)
            continue;
        struct dirent *de;
        bool found = false;
        while (!found && (de = readdir(d)) != NULL)
        {
            if (de->d_name[0] == '.')
                continue;
            snprintf(dir, sizeof(dir), "%s/%s/%s", roots[r], de->d_name, title_id);
            found = is_title_dir(dir, title_id);
        }
        closedir(d);
        if (found)
        {
            util_copy(out, size, dir);
            return true;
        }
    }
    return false;
}

void config_resolve(Config *config)
{
    config->retroarch_title[0] = config->porpoise_title[0] = 0;
    config_find_title("PPSA99169", config->retroarch_title, sizeof(config->retroarch_title));
    config_find_title("PPSA99764", config->porpoise_title, sizeof(config->porpoise_title));
    /* RetroArch keeps content, saves and settings in its own title folder */
    if (config->auto_dirs & CFG_AUTO_RETROARCH)
        util_copy(config->retroarch_dir, sizeof(config->retroarch_dir),
                  config->retroarch_title[0] ? config->retroarch_title : "/data/homebrew/PPSA99169");
    /* Porpoise uses /data/porpoise, or its title folder while it can't reach /data */
    if (config->auto_dirs & CFG_AUTO_PORPOISE)
    {
        char inside[PATH_LEN];
        snprintf(inside, sizeof(inside), "%s/porpoise", config->porpoise_title);
        util_copy(config->porpoise_dir, sizeof(config->porpoise_dir),
                  !util_dir_exists("/data/porpoise") && config->porpoise_title[0] &&
                          util_dir_exists(inside)
                      ? inside
                      : "/data/porpoise");
    }
    /* the others keep their data at a fixed /data path */
    if (config->auto_dirs & CFG_AUTO_SWANSTATION)
        util_copy(config->swanstation_dir, sizeof(config->swanstation_dir), swanstation_default());
    if (config->auto_dirs & CFG_AUTO_PS5SX2)
        util_copy(config->ps5sx2_dir, sizeof(config->ps5sx2_dir), "/data/PCSX2");
    if (config->auto_dirs & CFG_AUTO_EDEN)
        util_copy(config->eden_dir, sizeof(config->eden_dir), "/data/prosperoeden");
    if (config->auto_dirs & CFG_AUTO_CEMU)
        util_copy(config->cemu_dir, sizeof(config->cemu_dir), "/data/ps5cemu");
    if (config->auto_dirs & CFG_AUTO_XEMU)
        util_copy(config->xemu_dir, sizeof(config->xemu_dir), "/data/xemu");
}

bool config_load(Config *config)
{
    config_defaults(config);
    size_t size;
    char *text = (char *)util_read_file(data_path("config.txt"), &size);
    if (!text)
    {
        new_console_id(config);
        config_save(config);
        config_resolve(config);
        return false;
    }
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
    {
        char *l = trim(line);
        if (!*l || *l == '#')
            continue;
        char *eq = strchr(l, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = trim(l), *value = trim(eq + 1);
        if (strcmp(key, "server_url") == 0)
            util_copy(config->server_url, sizeof(config->server_url), value);
        else if (strcmp(key, "api_key") == 0)
            util_copy(config->api_key, sizeof(config->api_key), value);
        else if (strcmp(key, "console_id") == 0)
            util_copy(config->console_id, sizeof(config->console_id), value);
        else if (strcmp(key, "retroarch_dir") == 0)
            set_dir(config, CFG_AUTO_RETROARCH, config->retroarch_dir, value,
                    "/data/homebrew/PPSA99169");
        else if (strcmp(key, "swanstation_dir") == 0)
        {
            /* either default means auto, so an update can move it */
            set_dir(config, CFG_AUTO_SWANSTATION, config->swanstation_dir, value, "/data/PSXS5");
            if (strcmp(value, "/data/SwanStationPS5") == 0)
                config->auto_dirs |= CFG_AUTO_SWANSTATION;
        }
        else if (strcmp(key, "ps5sx2_dir") == 0)
            set_dir(config, CFG_AUTO_PS5SX2, config->ps5sx2_dir, value, "/data/PCSX2");
        else if (strcmp(key, "porpoise_dir") == 0)
            set_dir(config, CFG_AUTO_PORPOISE, config->porpoise_dir, value, "/data/porpoise");
        else if (strcmp(key, "eden_dir") == 0)
            set_dir(config, CFG_AUTO_EDEN, config->eden_dir, value, "/data/prosperoeden");
        else if (strcmp(key, "cemu_dir") == 0)
            set_dir(config, CFG_AUTO_CEMU, config->cemu_dir, value, "/data/ps5cemu");
        else if (strcmp(key, "xemu_dir") == 0)
            set_dir(config, CFG_AUTO_XEMU, config->xemu_dir, value, "/data/xemu");
        else if (strcmp(key, "ps2_cards") == 0)
            config->ps2_per_game = !util_ieq(value, "shared");
        else if (strcmp(key, "ps2_card") == 0 && *value)
            util_copy(config->ps2_card, sizeof(config->ps2_card), value);
        else if (strcmp(key, "ps1_target") == 0)
            config->ps1_in_retroarch = util_ieq(value, "retroarch");
    }
    free(text);
    if (!config->console_id[0])
    {
        new_console_id(config);
        config_save(config);
    }
    config_resolve(config);
    return config_ready(config);
}

bool config_save(const Config *config)
{
    char text[4096];
    int n = snprintf(text, sizeof(text),
                     "# GameSync PS5 client\n"
                     "# server_url: the GameSync server, e.g. http://192.168.1.10:8000\n"
                     "server_url=%s\n"
                     "api_key=%s\n"
                     "console_id=%s\n"
                     "\n"
                     "# Emulator folders. auto = found on the console (RetroArch's title folder\n"
                     "# under /data/homebrew, /data/etaHEN/games, USB...); or a path of your own.\n"
                     "retroarch_dir=%s\n"
                     "swanstation_dir=%s\n"
                     "ps5sx2_dir=%s\n"
                     "porpoise_dir=%s\n"
                     "eden_dir=%s\n"
                     "cemu_dir=%s\n"
                     "xemu_dir=%s\n"
                     "# PS5SX2 cards: per_game = memcards/<game file>.ps2 for each game, set as\n"
                     "# its Slot 1 card in settings/<game file>.ini; shared = every game on ps2_card\n"
                     "ps2_cards=%s\n"
                     "ps2_card=%s\n"
                     "# Where PS1 games from the catalog go: swanstation or retroarch\n"
                     "ps1_target=%s\n",
                     config->server_url, config->api_key, config->console_id,
                     dir_value(config, CFG_AUTO_RETROARCH, config->retroarch_dir),
                     dir_value(config, CFG_AUTO_SWANSTATION, config->swanstation_dir),
                     dir_value(config, CFG_AUTO_PS5SX2, config->ps5sx2_dir),
                     dir_value(config, CFG_AUTO_PORPOISE, config->porpoise_dir),
                     dir_value(config, CFG_AUTO_EDEN, config->eden_dir),
                     dir_value(config, CFG_AUTO_CEMU, config->cemu_dir),
                     dir_value(config, CFG_AUTO_XEMU, config->xemu_dir),
                     config->ps2_per_game ? "per_game" : "shared", config->ps2_card,
                     config->ps1_in_retroarch ? "retroarch" : "swanstation");
    if (n < 0 || (size_t)n >= sizeof(text))
        return false;
    return util_write_file_atomic(data_path("config.txt"), text, (size_t)n);
}

bool config_ready(const Config *config)
{
    return config->server_url[0] && config->api_key[0];
}

void config_apply(const Config *config, HttpServer *server)
{
    http_server_set_url(server, config->server_url);
    util_copy(server->api_key, sizeof(server->api_key), config->api_key);
    util_copy(server->console_id, sizeof(server->console_id), config->console_id);
}

/* ------------------------------------------------------------ KvStore */

static int kv_index(const KvStore *kv, const char *key)
{
    for (int i = 0; i < kv->count; ++i)
        if (strcmp(kv->items[i].key, key) == 0)
            return i;
    return -1;
}

void kv_load(KvStore *kv, const char *path)
{
    memset(kv, 0, sizeof(*kv));
    util_copy(kv->path, sizeof(kv->path), path);
    char *text = (char *)util_read_file(path, NULL);
    if (!text)
        return;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
    {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r')
            line[len - 1] = 0;
        /* keys may hold '=' (ROM file names); the value never does */
        char *eq = strrchr(line, '=');
        if (!eq || eq == line)
            continue;
        *eq = 0;
        kv_set(kv, line, eq + 1);
    }
    free(text);
    kv->dirty = false;
}

const char *kv_get(const KvStore *kv, const char *key)
{
    int i = kv_index(kv, key);
    return i >= 0 ? kv->items[i].value : NULL;
}

void kv_set(KvStore *kv, const char *key, const char *value)
{
    if (strchr(key, '\n') || strchr(value, '\n') || strchr(value, '='))
        return;
    int i = kv_index(kv, key);
    if (i >= 0)
    {
        if (strcmp(kv->items[i].value, value) == 0)
            return;
        char *v = strdup(value);
        if (!v)
            return;
        free(kv->items[i].value);
        kv->items[i].value = v;
        kv->dirty = true;
        return;
    }
    if (kv->count == kv->cap)
    {
        int cap = kv->cap ? kv->cap * 2 : 64;
        KvItem *items = realloc(kv->items, (size_t)cap * sizeof(KvItem));
        if (!items)
            return;
        kv->items = items;
        kv->cap = cap;
    }
    char *k = strdup(key), *v = strdup(value);
    if (!k || !v)
    {
        free(k);
        free(v);
        return;
    }
    kv->items[kv->count].key = k;
    kv->items[kv->count].value = v;
    kv->count++;
    kv->dirty = true;
}

void kv_remove(KvStore *kv, const char *key)
{
    int i = kv_index(kv, key);
    if (i < 0)
        return;
    free(kv->items[i].key);
    free(kv->items[i].value);
    kv->items[i] = kv->items[--kv->count];
    kv->dirty = true;
}

bool kv_save(KvStore *kv)
{
    if (!kv->dirty)
        return true;
    size_t cap = 1024, len = 0;
    for (int i = 0; i < kv->count; ++i)
        cap += strlen(kv->items[i].key) + strlen(kv->items[i].value) + 2;
    char *text = malloc(cap);
    if (!text)
        return false;
    for (int i = 0; i < kv->count; ++i)
        len += (size_t)snprintf(text + len, cap - len, "%s=%s\n", kv->items[i].key,
                                kv->items[i].value);
    bool ok = util_write_file_atomic(kv->path, text, len);
    free(text);
    if (ok)
        kv->dirty = false;
    return ok;
}

void kv_free(KvStore *kv)
{
    for (int i = 0; i < kv->count; ++i)
    {
        free(kv->items[i].key);
        free(kv->items[i].value);
    }
    free(kv->items);
    kv->items = NULL;
    kv->count = kv->cap = 0;
}

void app_log(const char *fmt, ...)
{
    FILE *f = fopen(data_path("gamesync.log"), "a");
    if (!f)
        return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm)
        fprintf(f, "%02d:%02d:%02d ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
