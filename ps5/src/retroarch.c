/*
 * GameSync PS5 client - RetroArch (PS5_RetroArch, PPSA99169) saves.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Where a save lives follows the port's own WebUI (mihawk-99/PS5_RetroArch,
 * src/webui_ps5.cpp, game_files()), which mirrors RetroArch's rules:
 *   base   = savefile_directory, or <title>/savefiles ("default", ":/x" and
 *            "/app0/x" are relative to the title folder)
 *   savefiles_in_content_dir = true  -> the ROM's own folder
 *   sort_savefiles_by_content_enable -> base/<ROM's folder name>
 *   sort_savefiles_enable (on unless "false" in this port) -> .../<core name>
 * where the core name is the core's .info "corename" ("Snes9x", "mGBA").
 * An existing save anywhere one folder below base wins over the computed
 * path, so a change of settings or core doesn't hide a save.
 *
 * Games come from content/<system>/ (system from the folder) and from the
 * playlists RetroArch writes (playlists/<name>.lpl), which also cover games on
 * USB drives and say which core runs them.
 */
#include "retroarch.h"
#include "saturn.h"

#include "json.h"
#include "naming.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char root[PATH_LEN];
    char base[PATH_LEN];
    bool in_content_dir;
    bool by_content;
    bool by_core;
} RaSettings;

typedef struct
{
    char path[PATH_LEN]; /* the ROM */
    char system[12];
    char core_lib[64];   /* "snes9x_libretro", or "" */
} RaGame;

typedef struct
{
    RaGame *items;
    int count, cap;
} RaGames;

/* ------------------------------------------------------------ settings */

static void cfg_value(const char *text, const char *key, char *out, size_t size)
{
    out[0] = 0;
    size_t klen = strlen(key);
    for (const char *line = text; line && *line;)
    {
        while (*line == ' ' || *line == '\t')
            line++;
        if (strncmp(line, key, klen) == 0 && (line[klen] == ' ' || line[klen] == '='))
        {
            const char *v = strchr(line, '=');
            if (v)
            {
                v++;
                while (*v == ' ' || *v == '"')
                    v++;
                size_t n = 0;
                while (v[n] && v[n] != '"' && v[n] != '\n' && v[n] != '\r')
                    n++;
                if (n >= size)
                    n = size - 1;
                memcpy(out, v, n);
                out[n] = 0;
                return;
            }
        }
        line = strchr(line, '\n');
        if (line)
            line++;
    }
}

/* "/app0/x", ":/x", "x" relative -> under the title folder */
static void title_path(const char *root, const char *value, char *out, size_t size)
{
    if (strncmp(value, "/app0", 5) == 0)
        snprintf(out, size, "%s%s", root, value + 5);
    else if (strncmp(value, ":/", 2) == 0)
        snprintf(out, size, "%s/%s", root, value + 2);
    else if (value[0] != '/')
        snprintf(out, size, "%s/%s", root, value);
    else
        snprintf(out, size, "%s", value);
    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/')
        out[--len] = 0;
}

static void load_settings(const Config *config, RaSettings *s)
{
    memset(s, 0, sizeof(*s));
    util_copy(s->root, sizeof(s->root), config->retroarch_dir);
    s->by_core = true;
    snprintf(s->base, sizeof(s->base), "%s/savefiles", s->root);
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/config/retroarch.cfg", s->root);
    char *text = (char *)util_read_file(path, NULL);
    if (!text)
        return;
    char v[PATH_LEN];
    cfg_value(text, "savefile_directory", v, sizeof(v));
    if (v[0] && strcmp(v, "default") != 0)
        title_path(s->root, v, s->base, sizeof(s->base));
    cfg_value(text, "savefiles_in_content_dir", v, sizeof(v));
    s->in_content_dir = strcmp(v, "true") == 0;
    cfg_value(text, "sort_savefiles_by_content_enable", v, sizeof(v));
    s->by_content = strcmp(v, "true") == 0;
    cfg_value(text, "sort_savefiles_enable", v, sizeof(v));
    s->by_core = strcmp(v, "false") != 0;
    free(text);
}

/* ------------------------------------------------------------ cores */

typedef struct
{
    const char *system;
    const char *lib;
    const char *corename; /* the .info corename, if the file can't be read */
} DefaultCore;

/* the cores PS5_RetroArch ships (tools/build-*.sh) */
static const DefaultCore CORES[] = {
    {"NES", "fceumm_libretro", "FCEUmm"},
    {"SNES", "snes9x_libretro", "Snes9x"},
    {"GB", "mgba_libretro", "mGBA"},
    {"GBC", "mgba_libretro", "mGBA"},
    {"GBA", "mgba_libretro", "mGBA"},
    {"N64", "mupen64plus_next_libretro", "Mupen64Plus-Next"},
    {"MD", "genesis_plus_gx_libretro", "Genesis Plus GX"},
    {"SMS", "genesis_plus_gx_libretro", "Genesis Plus GX"},
    {"GG", "genesis_plus_gx_libretro", "Genesis Plus GX"},
    {"SG1000", "genesis_plus_gx_libretro", "Genesis Plus GX"},
    {"32X", "picodrive_libretro", "PicoDrive"},
    {"PCE", "mednafen_pce_libretro", "Beetle PCE"},
    {"TG16", "mednafen_pce_libretro", "Beetle PCE"},
    {"PCSG", "mednafen_pce_libretro", "Beetle PCE"},
    {"NGP", "mednafen_ngp_libretro", "Beetle NeoPop"},
    {"NGPC", "mednafen_ngp_libretro", "Beetle NeoPop"},
    {"WSWAN", "mednafen_wswan_libretro", "Beetle WonderSwan"},
    {"WSWANC", "mednafen_wswan_libretro", "Beetle WonderSwan"},
    {"VB", "mednafen_vb_libretro", "Beetle VB"},
    {"LYNX", "handy_libretro", "Handy"},
    {"POKEMINI", "pokemini_libretro", "PokeMini"},
    {"JAGUAR", "virtualjaguar_libretro", "Virtual Jaguar"},
    {"SAT", "mednafen_saturn_libretro", "Beetle Saturn"},
    {"PS1", "mednafen_psx_hw_libretro", "Beetle PSX HW"},
};

static const DefaultCore *default_core(const char *system)
{
    for (size_t i = 0; i < sizeof(CORES) / sizeof(CORES[0]); ++i)
        if (util_ieq(CORES[i].system, system))
            return &CORES[i];
    return NULL;
}

/* "snes9x_libretro" -> "Snes9x" from <root>/info/snes9x_libretro.info */
static void core_name(const RaSettings *s, const char *lib, const char *system, char *out,
                      size_t size)
{
    out[0] = 0;
    const DefaultCore *def = default_core(system);
    if (!lib || !lib[0])
        lib = def ? def->lib : "";
    if (lib[0])
    {
        char path[PATH_LEN];
        snprintf(path, sizeof(path), "%s/info/%s.info", s->root, lib);
        char *text = (char *)util_read_file(path, NULL);
        if (text)
        {
            cfg_value(text, "corename", out, size);
            free(text);
        }
    }
    if (!out[0] && def && strcmp(lib, def->lib) == 0)
        util_copy(out, size, def->corename);
}

/* ------------------------------------------------------------ games */

static void add_game(RaGames *g, const char *path, const char *system, const char *core_lib)
{
    for (int i = 0; i < g->count; ++i)
        if (strcmp(g->items[i].path, path) == 0)
        {
            if (core_lib && core_lib[0] && !g->items[i].core_lib[0])
                util_copy(g->items[i].core_lib, sizeof(g->items[i].core_lib), core_lib);
            return;
        }
    if (g->count == g->cap)
    {
        int cap = g->cap ? g->cap * 2 : 256;
        RaGame *grown = realloc(g->items, (size_t)cap * sizeof(RaGame));
        if (!grown)
            return;
        g->items = grown;
        g->cap = cap;
    }
    RaGame *it = &g->items[g->count++];
    memset(it, 0, sizeof(*it));
    util_copy(it->path, sizeof(it->path), path);
    util_copy(it->system, sizeof(it->system), system);
    if (core_lib)
        util_copy(it->core_lib, sizeof(it->core_lib), core_lib);
}

static void walk_content(RaGames *g, const char *dir, const char *system, int depth)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (de->d_name[0] == '.')
            continue;
        char path[PATH_LEN];
        util_path_join(path, sizeof(path), dir, de->d_name);
        bool is_dir = de->d_type == DT_DIR || (de->d_type == DT_UNKNOWN && util_dir_exists(path));
        if (is_dir)
        {
            if (depth < 3)
                walk_content(g, path, system, depth + 1);
        }
        else if (is_launchable_rom(de->d_name, system))
            add_game(g, path, system, NULL);
    }
    closedir(d);
}

/* libretro database names (playlist db_name / file name) -> system code */
static const struct
{
    const char *db;
    const char *system;
} DATABASES[] = {
    {"Nintendo - Nintendo Entertainment System", "NES"},
    {"Nintendo - Family Computer Disk System", "FDS"},
    {"Nintendo - Super Nintendo Entertainment System", "SNES"},
    {"Nintendo - Game Boy Advance", "GBA"},
    {"Nintendo - Game Boy Color", "GBC"},
    {"Nintendo - Game Boy", "GB"},
    {"Nintendo - Nintendo 64", "N64"},
    {"Nintendo - Virtual Boy", "VB"},
    {"Nintendo - Pokemon Mini", "POKEMINI"},
    {"Sega - Mega Drive - Genesis", "MD"},
    {"Sega - Master System - Mark III", "SMS"},
    {"Sega - Game Gear", "GG"},
    {"Sega - SG-1000", "SG1000"},
    {"Sega - 32X", "32X"},
    {"NEC - PC Engine - TurboGrafx 16", "PCE"},
    {"NEC - PC Engine SuperGrafx", "PCSG"},
    {"SNK - Neo Geo Pocket Color", "NGPC"},
    {"SNK - Neo Geo Pocket", "NGP"},
    {"Bandai - WonderSwan Color", "WSWANC"},
    {"Bandai - WonderSwan", "WSWAN"},
    {"Atari - Lynx", "LYNX"},
    {"Atari - Jaguar", "JAGUAR"},
    {"Sony - PlayStation", "PS1"},
};

static const char *system_from_db(const char *db)
{
    for (size_t i = 0; i < sizeof(DATABASES) / sizeof(DATABASES[0]); ++i)
        if (strncmp(db, DATABASES[i].db, strlen(DATABASES[i].db)) == 0 &&
            (db[strlen(DATABASES[i].db)] == 0 || db[strlen(DATABASES[i].db)] == '.'))
            return DATABASES[i].system;
    return NULL;
}

/* the system from a ROM path under content/<system>/ */
static const char *system_from_path(const RaSettings *s, const char *path)
{
    char prefix[PATH_LEN];
    snprintf(prefix, sizeof(prefix), "%s/content/", s->root);
    size_t n = strlen(prefix);
    if (strncmp(path, prefix, n) != 0)
        return NULL;
    char folder[64];
    size_t i = 0;
    for (const char *p = path + n; *p && *p != '/' && i + 1 < sizeof(folder); ++p)
        folder[i++] = *p;
    folder[i] = 0;
    return system_from_folder(folder);
}

static void read_playlists(const RaSettings *s, RaGames *g)
{
    char dir[PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/playlists", s->root);
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (!util_has_suffix_i(de->d_name, ".lpl"))
            continue;
        char path[PATH_LEN];
        util_path_join(path, sizeof(path), dir, de->d_name);
        char *text = (char *)util_read_file(path, NULL);
        if (!text)
            continue;
        JsonValue *root = json_parse(text);
        free(text);
        const char *list_system = system_from_db(de->d_name);
        JsonValue *items = json_get(root, "items");
        for (JsonValue *it = items ? items->child : NULL; it; it = it->next)
        {
            const char *rom = json_get_str(it, "path", "");
            if (!rom[0])
                continue;
            char real[PATH_LEN];
            /* "archive.zip#member.sfc": RetroArch names the save after the archive */
            util_copy(real, sizeof(real), rom);
            char *hash = strchr(real, '#');
            if (hash)
                *hash = 0;
            char mapped[PATH_LEN];
            title_path(s->root, real, mapped, sizeof(mapped));
            const char *system = system_from_db(json_get_str(it, "db_name", ""));
            if (!system)
                system = list_system;
            if (!system)
                system = system_from_path(s, mapped);
            if (!system)
                continue;
            /* core_path ".../cores/snes9x_libretro.elf" -> "snes9x_libretro" */
            char lib[64] = "";
            const char *core = json_get_str(it, "core_path", "");
            if (core[0] && strcmp(core, "DETECT") != 0)
            {
                util_copy(lib, sizeof(lib), util_basename(core));
                util_strip_ext(lib);
            }
            add_game(g, mapped, system, lib);
        }
        json_free(root);
    }
    closedir(d);
}

/* ------------------------------------------------------------ save paths */

/* <base>/<stem>.srm or <base>/<any folder>/<stem>.srm that exists */
static bool find_existing(const char *base, const char *srm, char *out, size_t size)
{
    char path[PATH_LEN];
    util_path_join(path, sizeof(path), base, srm);
    if (util_file_exists(path))
    {
        util_copy(out, size, path);
        return true;
    }
    DIR *d = opendir(base);
    if (!d)
        return false;
    bool found = false;
    struct dirent *de;
    while (!found && (de = readdir(d)) != NULL)
    {
        if (de->d_name[0] == '.')
            continue;
        char sub[PATH_LEN];
        util_path_join(sub, sizeof(sub), base, de->d_name);
        util_path_join(path, sizeof(path), sub, srm);
        if (util_file_exists(path))
        {
            util_copy(out, size, path);
            found = true;
        }
    }
    closedir(d);
    return found;
}

/* The save's extension: .srm (RetroArch's save RAM), except Beetle Saturn,
 * which writes the Saturn's internal backup RAM itself as <game>.bkr - the
 * 32 KB Mednafen image the server keeps for SAT_ ids (shared/saturn_format.py). */
static const char *save_ext(const char *system)
{
    return util_ieq(system, "SAT") ? ".bkr" : ".srm";
}

static void save_path(const RaSettings *s, const RaGame *game, char *out, size_t size)
{
    char stem[256], srm[300];
    util_copy(stem, sizeof(stem), util_basename(game->path));
    util_strip_ext(stem);
    snprintf(srm, sizeof(srm), "%s%s", stem, save_ext(game->system));
    char content_dir[PATH_LEN];
    util_copy(content_dir, sizeof(content_dir), game->path);
    char *slash = strrchr(content_dir, '/');
    if (slash)
        *slash = 0;

    if (s->in_content_dir)
    {
        util_path_join(out, size, content_dir, srm);
        return;
    }
    char folder[PATH_LEN];
    util_copy(folder, sizeof(folder), s->base);
    if (s->by_content)
    {
        char tmp[PATH_LEN];
        util_path_join(tmp, sizeof(tmp), folder, util_basename(content_dir));
        util_copy(folder, sizeof(folder), tmp);
    }
    if (s->by_core)
    {
        char name[96];
        core_name(s, game->core_lib, game->system, name, sizeof(name));
        if (name[0])
        {
            char tmp[PATH_LEN];
            util_path_join(tmp, sizeof(tmp), folder, name);
            util_copy(folder, sizeof(folder), tmp);
        }
    }
    util_path_join(out, size, folder, srm);
    if (util_file_exists(out))
        return;
    /* a save from other settings or another core still counts */
    char found[PATH_LEN];
    if (find_existing(s->base, srm, found, sizeof(found)))
        util_copy(out, size, found);
}

void retroarch_scan(const Config *config, const KvStore *ids, SaveList *list)
{
    RaSettings s;
    load_settings(config, &s);
    RaGames games = {0};
    char content[PATH_LEN];
    snprintf(content, sizeof(content), "%s/content", s.root);
    DIR *d = opendir(content);
    if (d)
    {
        struct dirent *de;
        while ((de = readdir(d)) != NULL)
        {
            const char *system = de->d_name[0] != '.' ? system_from_folder(de->d_name) : NULL;
            if (!system)
                continue;
            char path[PATH_LEN];
            util_path_join(path, sizeof(path), content, de->d_name);
            walk_content(&games, path, system, 1);
        }
        closedir(d);
    }
    read_playlists(&s, &games);

    for (int i = 0; i < games.count; ++i)
    {
        const RaGame *g = &games.items[i];
        bool ps1 = util_ieq(g->system, "PS1");
        if (!ps1 && !retroarch_syncs_system(g->system))
            continue;
        const char *file = util_basename(g->path);
        if (!is_launchable_rom(file, g->system))
            continue;
        SaveEntry *e = saves_add(list);
        if (!e)
            break;
        e->emu = EMU_RETROARCH;
        e->kind = ps1 ? SAVE_PS1_CARD : SAVE_SRM;
        util_copy(e->system, sizeof(e->system), g->system);
        util_copy(e->rom_file, sizeof(e->rom_file), file);
        util_copy(e->name, sizeof(e->name), file);
        util_strip_ext(e->name);
        save_path(&s, g, e->path, sizeof(e->path));
        char key[300];
        snprintf(key, sizeof(key), "%s|%s", g->system, file);
        const char *id = kv_get(ids, key);
        char sat[64];
        if (id)
            util_copy(e->title_id, sizeof(e->title_id), id);
        /* a Saturn disc says which game it is (a translation keeps the
         * original's IP.BIN), whatever the file is called */
        else if (util_ieq(g->system, "SAT") && saturn_title_id(g->path, sat, sizeof(sat)))
            util_copy(e->title_id, sizeof(e->title_id), sat);
        e->installed = true; /* the ROM is here: Sync all may fetch its save */
        saves_hash_local(e);
    }
    free(games.items);
}

void retroarch_save_path(const Config *config, const char *system, const char *rom_path, char *out,
                         size_t size)
{
    RaSettings s;
    load_settings(config, &s);
    RaGame g;
    memset(&g, 0, sizeof(g));
    util_copy(g.path, sizeof(g.path), rom_path);
    util_copy(g.system, sizeof(g.system), system);
    save_path(&s, &g, out, size);
}
