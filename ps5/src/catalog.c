/*
 * GameSync PS5 client - the server's ROM catalog and installing from it.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Caching follows the MiSTer / Steam Deck clients (shared/catalog_cache.py):
 * GET /roms/fingerprints names a fingerprint per system; a system whose
 * fingerprint is unchanged loads from <data>/catalog/<SYS>.bin, the others
 * are refetched. Offline, every cached system is shown; a server without the
 * route is browsed live, uncached. The fingerprint covers the RA fields too,
 * so new RetroAchievements badges refresh the cache by themselves.
 *
 * RetroAchievements levels as in shared/ra_variants.py game_ra_verified():
 * verified = achievements and (ra_match "hash" or ra_dump); a title-only
 * match is the weak "RA?".
 *
 * Install targets:
 *  - PS1 -> SwanStationPS5: <dir>/games/<game>/<file> (one folder per game,
 *    as PSXS5's shelf expects). Single-file images only (.chd/.pbp/.iso).
 *  - PS2 -> PS5SX2: <dir>/games/<file> (.iso/.chd/.cso/.zso).
 *  - everything RetroArch plays -> <dir>/content/<system>/<file>, the
 *    folders the PS5 port's library groups by.
 * Multi-file bundles (zipped .cue/.bin sets, MSU packs...) aren't installed
 * by this version.
 */
#include "catalog.h"

#include "json.h"
#include "unzip.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CACHE_MAGIC "GSC1"
#define CACHE_VERSION 4u

void catalog_free(Catalog *catalog)
{
    free(catalog->systems);
    free(catalog->roms);
    memset(catalog, 0, sizeof(*catalog));
}

static void http_fail(char *error, size_t size, const char *what, int code)
{
    snprintf(error, size, "%s: %s", what, http_error_text(code));
}

/* ------------------------------------------------------------ rows */

static void parse_row(const JsonValue *r, const char *system, RomRow *row)
{
    memset(row, 0, sizeof(*row));
    util_copy(row->rom_id, sizeof(row->rom_id), json_get_str(r, "rom_id", ""));
    util_copy(row->name, sizeof(row->name), json_get_str(r, "name", ""));
    util_copy(row->filename, sizeof(row->filename), json_get_str(r, "filename", ""));
    util_copy(row->system, sizeof(row->system), system);
    util_copy(row->title_id, sizeof(row->title_id), json_get_str(r, "title_id", ""));
    util_copy(row->bundle_kind, sizeof(row->bundle_kind), json_get_str(r, "bundle_kind", ""));
    row->size = (uint64_t)json_get_num(r, "size", 0);
    row->is_bundle = json_get_bool(r, "is_bundle", false);
    row->ra_game_id = (int)json_get_num(r, "ra_game_id", 0);
    row->ra_achievements = (int)json_get_num(r, "ra_achievements", 0);
    const char *match = json_get_str(r, "ra_match", "");
    row->ra_match = row->ra_game_id ? (util_ieq(match, "title") ? 2 : 1) : 0;
    row->ra_dump = json_get_bool(r, "ra_dump", false);
    JsonValue *formats = json_get(r, "extract_formats");
    for (JsonValue *f = formats ? formats->child : NULL; f; f = f->next)
    {
        if (f->type == JSON_STRING && util_ieq(f->str, "cue"))
            row->has_cue = true;
        if (f->type == JSON_STRING && util_ieq(f->str, "iso"))
            row->has_iso = true;
        if (f->type == JSON_STRING && util_ieq(f->str, "decrypted_cci"))
            row->has_dcci = true;
    }
    row->disc_index = (uint8_t)json_get_num(r, "disc_index", 0);
    row->disc_total = (uint8_t)json_get_num(r, "disc_total", 0);
    if (!row->name[0])
        util_copy(row->name, sizeof(row->name), row->filename);
}

typedef struct
{
    RomRow *rows;
    int count;
    int cap;
} RowList;

static bool rows_push(RowList *l, const RomRow *row)
{
    if (l->count == l->cap)
    {
        int cap = l->cap ? l->cap * 2 : 1024;
        RomRow *grown = realloc(l->rows, (size_t)cap * sizeof(RomRow));
        if (!grown)
            return false;
        l->rows = grown;
        l->cap = cap;
    }
    l->rows[l->count++] = *row;
    return true;
}

/* All of one system's rows from the server. Only rows whose system is
 * exactly this one: the server also answers for alias systems. */
static bool fetch_system(const HttpServer *server, const char *system, RowList *out, char *error,
                         size_t size)
{
    int offset = 0;
    for (;;)
    {
        char path[160];
        snprintf(path, sizeof(path), "/api/v1/roms?system=%s&limit=2000&offset=%d", system, offset);
        HttpBuffer buf = {0};
        int status = http_get(server, path, &buf, NULL);
        if (status != 200)
        {
            http_buffer_free(&buf);
            http_fail(error, size, "listing ROMs", status);
            return false;
        }
        JsonValue *root = json_parse((const char *)buf.data);
        http_buffer_free(&buf);
        JsonValue *roms = json_get(root, "roms");
        int page = 0;
        for (JsonValue *r = roms ? roms->child : NULL; r; r = r->next, ++page)
        {
            if (!util_ieq(json_get_str(r, "system", system), system))
                continue;
            RomRow row;
            parse_row(r, system, &row);
            if (!rows_push(out, &row))
                break;
        }
        bool more = json_get_bool(root, "has_more", false);
        json_free(root);
        offset += page;
        if (!more || page == 0)
            return true;
    }
}

/* ------------------------------------------------------------ cache */

static void cache_path(char *out, size_t size, const char *system)
{
    char name[32];
    snprintf(name, sizeof(name), "catalog/%s.bin", system);
    util_copy(out, size, data_path(name));
}

/* Appends a cached system's rows; fills fingerprint. false when absent or stale-format. */
static bool cache_read(const char *system, RowList *out, char *fingerprint, size_t fp_size)
{
    char path[PATH_LEN];
    cache_path(path, sizeof(path), system);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char magic[4];
    uint32_t version = 0, row_size = 0, count = 0;
    char fp[96];
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, CACHE_MAGIC, 4) == 0 &&
              fread(&version, 4, 1, f) == 1 && version == CACHE_VERSION &&
              fread(&row_size, 4, 1, f) == 1 && row_size == sizeof(RomRow) &&
              fread(fp, 1, sizeof(fp), f) == sizeof(fp) && fread(&count, 4, 1, f) == 1 &&
              count < 1000000;
    for (uint32_t i = 0; ok && i < count; ++i)
    {
        RomRow row;
        ok = fread(&row, sizeof(row), 1, f) == 1;
        if (ok)
        {
            row.name[sizeof(row.name) - 1] = 0;
            row.filename[sizeof(row.filename) - 1] = 0;
            ok = rows_push(out, &row);
        }
    }
    fclose(f);
    if (ok)
    {
        fp[sizeof(fp) - 1] = 0;
        util_copy(fingerprint, fp_size, fp);
    }
    return ok;
}

static void cache_write(const char *system, const RomRow *rows, int count, const char *fingerprint)
{
    char path[PATH_LEN];
    cache_path(path, sizeof(path), system);
    size_t bytes = 4 + 4 + 4 + 96 + 4 + (size_t)count * sizeof(RomRow);
    uint8_t *buf = malloc(bytes);
    if (!buf)
        return;
    uint8_t *p = buf;
    uint32_t version = CACHE_VERSION, row_size = sizeof(RomRow), n = (uint32_t)count;
    char fp[96] = {0};
    util_copy(fp, sizeof(fp), fingerprint);
    memcpy(p, CACHE_MAGIC, 4);
    p += 4;
    memcpy(p, &version, 4);
    p += 4;
    memcpy(p, &row_size, 4);
    p += 4;
    memcpy(p, fp, sizeof(fp));
    p += sizeof(fp);
    memcpy(p, &n, 4);
    p += 4;
    memcpy(p, rows, (size_t)count * sizeof(RomRow));
    util_write_file_atomic(path, buf, bytes);
    free(buf);
}

void catalog_reset(const HttpServer *server)
{
    HttpBuffer buf = {0};
    http_get(server, "/api/v1/roms/scan", &buf, NULL); /* refused is fine */
    http_buffer_free(&buf);
    char dir[PATH_LEN];
    util_copy(dir, sizeof(dir), data_path("catalog"));
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (!util_has_suffix_i(de->d_name, ".bin"))
            continue;
        char path[PATH_LEN];
        util_path_join(path, sizeof(path), dir, de->d_name);
        remove(path);
    }
    closedir(d);
}

/* ------------------------------------------------------------ load */

static int compare_systems(const void *a, const void *b)
{
    const SystemRow *x = a, *y = b;
    return strcasecmp(system_display_name(x->system), system_display_name(y->system));
}

static int compare_rows(const void *a, const void *b)
{
    const RomRow *x = a, *y = b;
    int c = strcasecmp(system_display_name(x->system), system_display_name(y->system));
    return c ? c : strcasecmp(x->name, y->name);
}

static bool add_system(Catalog *c, const char *system, int count, const char *fingerprint)
{
    SystemRow *grown = realloc(c->systems, (size_t)(c->system_count + 1) * sizeof(SystemRow));
    if (!grown)
        return false;
    c->systems = grown;
    SystemRow *s = &c->systems[c->system_count++];
    memset(s, 0, sizeof(*s));
    util_copy(s->system, sizeof(s->system), system);
    util_copy(s->fingerprint, sizeof(s->fingerprint), fingerprint);
    s->count = count;
    return true;
}

/* Offline: every system with a cache file. */
static void load_offline(Catalog *c, RowList *rows)
{
    char dir[PATH_LEN];
    util_copy(dir, sizeof(dir), data_path("catalog"));
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (!util_has_suffix_i(de->d_name, ".bin"))
            continue;
        char system[16];
        util_copy(system, sizeof(system), de->d_name);
        util_strip_ext(system);
        int before = rows->count;
        char fp[96];
        if (cache_read(system, rows, fp, sizeof(fp)))
            add_system(c, system, rows->count - before, fp);
    }
    closedir(d);
}

bool catalog_load(const HttpServer *server, Catalog *c, CatalogProgressFn progress, void *user,
                  char *error, size_t size)
{
    catalog_free(c);
    util_mkdirs(data_path("catalog"));
    RowList rows = {0};
    HttpBuffer buf = {0};
    if (progress)
        progress("Checking the catalog", 0, 0, user);
    int status = http_get(server, "/api/v1/roms/fingerprints", &buf, NULL);
    JsonValue *root = status == 200 ? json_parse((const char *)buf.data) : NULL;
    http_buffer_free(&buf);

    if (status == 200 && root)
    {
        c->cached = true;
        JsonValue *systems = json_get(root, "systems");
        int total = 0, done = 0;
        for (JsonValue *s = systems ? systems->child : NULL; s; s = s->next)
            if (s->key && install_emulator_for_system(s->key, false) != EMU_NONE)
                total++;
        for (JsonValue *s = systems ? systems->child : NULL; s; s = s->next)
        {
            if (!s->key || install_emulator_for_system(s->key, false) == EMU_NONE)
                continue;
            const char *fp = json_get_str(s, "fingerprint", "");
            int before = rows.count;
            char cached_fp[96] = "";
            bool fresh = cache_read(s->key, &rows, cached_fp, sizeof(cached_fp)) &&
                         strcmp(cached_fp, fp) == 0;
            if (!fresh)
            {
                rows.count = before; /* drop a stale copy */
                if (progress)
                {
                    char msg[96];
                    snprintf(msg, sizeof(msg), "Updating %s", system_display_name(s->key));
                    progress(msg, done, total, user);
                }
                if (!fetch_system(server, s->key, &rows, error, size))
                {
                    rows.count = before;
                    continue;
                }
                cache_write(s->key, rows.rows + before, rows.count - before, fp);
            }
            add_system(c, s->key, rows.count - before, fp);
            done++;
        }
        json_free(root);
    }
    else if (status == 404)
    {
        /* an older server: live, uncached */
        json_free(root);
        HttpBuffer sys = {0};
        status = http_get(server, "/api/v1/roms/systems", &sys, NULL);
        JsonValue *sroot = status == 200 ? json_parse((const char *)sys.data) : NULL;
        http_buffer_free(&sys);
        JsonValue *stats = json_get(sroot, "stats");
        for (JsonValue *s = stats ? stats->child : NULL; s; s = s->next)
        {
            if (!s->key || install_emulator_for_system(s->key, false) == EMU_NONE)
                continue;
            int before = rows.count;
            if (fetch_system(server, s->key, &rows, error, size))
                add_system(c, s->key, rows.count - before, "");
        }
        json_free(sroot);
    }
    else
    {
        json_free(root);
        c->offline = true;
        load_offline(c, &rows);
        if (!c->system_count)
        {
            http_fail(error, size, "the catalog", status);
            free(rows.rows);
            return false;
        }
    }
    c->roms = rows.rows;
    c->rom_count = rows.count;
    if (c->system_count > 1)
        qsort(c->systems, (size_t)c->system_count, sizeof(SystemRow), compare_systems);
    if (c->rom_count > 1)
        qsort(c->roms, (size_t)c->rom_count, sizeof(RomRow), compare_rows);
    if (!c->system_count)
    {
        snprintf(error, size, "the server has no ROMs for systems the PS5 emulators play");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------ queries */

RaLevel catalog_ra_level(const RomRow *rom)
{
    if (rom->ra_achievements <= 0)
        return RA_NONE;
    return rom->ra_match == 1 || rom->ra_dump ? RA_VERIFIED : RA_TITLE;
}

/* "vii" <-> "7", "ii" <-> "2" ... for 1-20 */
static const char *const ROMAN[] = {"",   "i",   "ii",   "iii", "iv", "v",   "vi",
                                    "vii", "viii", "ix", "x",   "xi", "xii", "xiii",
                                    "xiv", "xv",  "xvi", "xvii", "xviii", "xix", "xx"};

static void lower_alnum(char *out, size_t size, const char *s)
{
    size_t o = 0;
    for (; *s && o + 1 < size; ++s)
    {
        unsigned char ch = (unsigned char)tolower((unsigned char)*s);
        out[o++] = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ? (char)ch : ' ';
    }
    out[o] = 0;
}

/* token as a whole word or a substring of the joined text */
static bool has_token(const char *text, const char *joined, const char *token)
{
    if (strstr(joined, token))
        return true;
    size_t n = strlen(token);
    for (const char *p = text; (p = strstr(p, token)) != NULL; p += n)
        if ((p == text || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0))
            return true;
    return false;
}

bool catalog_matches(const RomRow *rom, const char *query)
{
    char q[160];
    lower_alnum(q, sizeof(q), query);
    char text[600], joined[600];
    char raw[600];
    snprintf(raw, sizeof(raw), "%s %s %s", rom->name, rom->filename, rom->title_id);
    lower_alnum(text, sizeof(text), raw);
    size_t j = 0;
    for (const char *p = text; *p && j + 1 < sizeof(joined); ++p)
        if (*p != ' ')
            joined[j++] = *p;
    joined[j] = 0;
    char *save = NULL;
    for (char *tok = strtok_r(q, " ", &save); tok; tok = strtok_r(NULL, " ", &save))
    {
        bool found = has_token(text, joined, tok);
        for (int n = 1; !found && n <= 20; ++n)
        {
            char digits[4];
            snprintf(digits, sizeof(digits), "%d", n);
            if (strcmp(tok, digits) == 0)
                found = has_token(text, "", ROMAN[n]);
            else if (strcmp(tok, ROMAN[n]) == 0)
                found = has_token(text, "", digits);
        }
        if (!found)
            return false;
    }
    return true;
}

const RomRow *catalog_find_file(const Catalog *catalog, const char *system, const char *filename)
{
    for (int i = 0; i < catalog->rom_count; ++i)
    {
        const RomRow *r = &catalog->roms[i];
        if (util_ieq(r->system, system) && util_ieq(r->filename, filename))
            return r;
    }
    return NULL;
}

/* ------------------------------------------------------------ installs */

static bool ext_in(const char *filename, const char *const *exts, size_t n)
{
    const char *ext = util_ext(filename);
    for (size_t i = 0; i < n; ++i)
        if (util_ieq(ext, exts[i]))
            return true;
    return false;
}

void catalog_plan(const Config *config, const RomRow *rom, InstallPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->emu = install_emulator_for_system(rom->system, config->ps1_in_retroarch);
    char file[200];
    util_safe_filename(file, sizeof(file), rom->filename);
    if (plan->emu == EMU_NONE)
    {
        snprintf(plan->reason, sizeof(plan->reason), "no PS5 emulator for %s", rom->system);
        return;
    }
    if (rom->is_bundle)
    {
        char game[200];
        util_copy(game, sizeof(game), file);
        util_strip_ext(game); /* bundles are listed as <name>.zip */
        if (plan->emu == EMU_XEMU && rom->has_iso)
        {
            /* the server turns the Xbox folder (.cci + default.xbe) into one XISO */
            snprintf(plan->dest, sizeof(plan->dest), "%s/games/%s.iso", config->xemu_dir, game);
            plan->extract = "iso";
        }
        else if (plan->emu == EMU_CEMU && util_ieq(rom->system, "WIIU"))
        {
            /* a WUP/NUS set (title.tmd + .app + title.tik) or a loadiine tree,
             * which Cemu runs as it is from its games folder */
            snprintf(plan->dest, sizeof(plan->dest), "%s/games/%s", config->cemu_dir, game);
            snprintf(plan->check, sizeof(plan->check), "%s/.gamesync-complete", plan->dest);
            plan->folder = true;
            plan->supported = true;
            return;
        }
        else
        {
            snprintf(plan->reason, sizeof(plan->reason), "multi-file %s games aren't supported yet",
                     rom->system);
            return;
        }
        plan->supported = true;
        util_copy(plan->check, sizeof(plan->check), plan->dest);
        return;
    }
    if (plan->emu == EMU_SWANSTATION)
    {
        static const char *const ok[] = {".chd", ".pbp", ".iso"};
        if (!ext_in(file, ok, 3))
        {
            snprintf(plan->reason, sizeof(plan->reason), "SwanStation install needs .chd/.pbp/.iso");
            return;
        }
        char game[200];
        util_copy(game, sizeof(game), file);
        util_strip_ext(game);
        snprintf(plan->dest, sizeof(plan->dest), "%s/games/%s/%s", config->swanstation_dir, game,
                 file);
    }
    else if (plan->emu == EMU_PORPOISE)
    {
        static const char *const ok[] = {".iso", ".gcm", ".rvz", ".ciso", ".gcz", ".wbfs", ".wia", ".wad"};
        if (util_has_suffix_i(file, ".zip"))
        {
            /* the .rvz (or .iso, .wad...) inside, served out of the zip */
            char game[200];
            util_copy(game, sizeof(game), file);
            util_strip_ext(game);
            snprintf(plan->dest, sizeof(plan->dest), "%s/games/%s", config->porpoise_dir, game);
            util_copy(plan->check, sizeof(plan->check), plan->dest);
            plan->zip_member = true;
            plan->supported = true;
            return;
        }
        if (!ext_in(file, ok, 8))
        {
            snprintf(plan->reason, sizeof(plan->reason), "Porpoise needs .iso/.rvz/.gcm/.wbfs...");
            return;
        }
        snprintf(plan->dest, sizeof(plan->dest), "%s/games/%s", config->porpoise_dir, file);
    }
    else if (plan->emu == EMU_CEMU)
    {
        /* PS5CEMU-HAR: Wii U files in games/, 3DS in azahar/games/ (both flat) */
        bool wiiu = util_ieq(rom->system, "WIIU");
        static const char *const wiiu_ok[] = {".wua", ".wud", ".wux", ".rpx", ".wuhb"};
        static const char *const ds_ok[] = {".3ds", ".cci", ".cxi", ".3dsx", ".app",
                                            ".z3ds", ".zcci", ".zcxi", ".z3dsx"};
        if (!wiiu && !ext_in(file, ds_ok, 9) && rom->has_dcci)
        {
            /* a zipped or encrypted dump: the server decrypts it into the
             * CCI Azahar loads (SYNC_ROM_3DS_DECRYPTED_CCI_COMMAND) */
            char game[200];
            util_copy(game, sizeof(game), file);
            util_strip_ext(game);
            snprintf(plan->dest, sizeof(plan->dest), "%s/azahar/games/%s.cci", config->cemu_dir, game);
            plan->extract = "decrypted_cci";
            plan->supported = true;
            util_copy(plan->check, sizeof(plan->check), plan->dest);
            return;
        }
        if (wiiu ? !ext_in(file, wiiu_ok, 5) : !ext_in(file, ds_ok, 9))
        {
            snprintf(plan->reason, sizeof(plan->reason),
                     wiiu ? "PS5CEMU needs a .wua/.wud/.wux/.rpx or a WUP folder"
                          : "PS5CEMU's Azahar needs .3ds/.cci/.cxi/.3dsx (the server can't decrypt this one)");
            return;
        }
        snprintf(plan->dest, sizeof(plan->dest), "%s/%s/%s", config->cemu_dir,
                 wiiu ? "games" : "azahar/games", file);
    }
    else if (plan->emu == EMU_XEMU)
    {
        static const char *const ok[] = {".iso", ".xiso"};
        if (!ext_in(file, ok, 2))
        {
            snprintf(plan->reason, sizeof(plan->reason), "XPSemu needs an XISO (.iso/.xiso)");
            return;
        }
        snprintf(plan->dest, sizeof(plan->dest), "%s/games/%s", config->xemu_dir, file);
    }
    else if (plan->emu == EMU_EDEN)
    {
        static const char *const ok[] = {".nsp", ".xci"};
        if (!ext_in(file, ok, 2))
        {
            snprintf(plan->reason, sizeof(plan->reason), "ProsperoEden needs .nsp/.xci");
            return;
        }
        snprintf(plan->dest, sizeof(plan->dest), "%s/roms/%s", config->eden_dir, file);
    }
    else if (plan->emu == EMU_PS5SX2)
    {
        static const char *const ok[] = {".iso", ".chd", ".cso", ".zso"};
        if (!ext_in(file, ok, 4))
        {
            snprintf(plan->reason, sizeof(plan->reason), "PS5SX2 needs .iso/.chd/.cso/.zso");
            return;
        }
        snprintf(plan->dest, sizeof(plan->dest), "%s/games/%s", config->ps5sx2_dir, file);
    }
    else
    {
        snprintf(plan->dest, sizeof(plan->dest), "%s/content/%s/%s", config->retroarch_dir,
                 retroarch_folder_for_system(rom->system), file);
    }
    plan->supported = true;
    util_copy(plan->check, sizeof(plan->check), plan->dest);
}

void catalog_plan_cue(const Config *config, const RomRow *rom, InstallPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->cue = true;
    plan->emu = install_emulator_for_system(rom->system, config->ps1_in_retroarch);
    if (!rom->has_cue)
    {
        snprintf(plan->reason, sizeof(plan->reason), "the server has no CUE/BIN version of it");
        return;
    }
    char game[200];
    util_safe_filename(game, sizeof(game), rom->filename);
    util_strip_ext(game);
    if (plan->emu == EMU_SWANSTATION)
        snprintf(plan->unzip_dir, sizeof(plan->unzip_dir), "%s/games/%s", config->swanstation_dir,
                 game);
    else if (plan->emu == EMU_PS5SX2)
        snprintf(plan->unzip_dir, sizeof(plan->unzip_dir), "%s/games/%s", config->ps5sx2_dir, game);
    else if (plan->emu == EMU_RETROARCH)
        snprintf(plan->unzip_dir, sizeof(plan->unzip_dir), "%s/content/%s/%s",
                 config->retroarch_dir, retroarch_folder_for_system(rom->system), game);
    else
    {
        snprintf(plan->reason, sizeof(plan->reason), "no CUE/BIN install for %s", rom->system);
        return;
    }
    snprintf(plan->dest, sizeof(plan->dest), "%s/%s.cue.zip", plan->unzip_dir, game);
    snprintf(plan->check, sizeof(plan->check), "%s/%s.cue", plan->unzip_dir, game);
    plan->supported = true;
}

/* any .cue in the folder: the set's own name can differ from the CHD's */
static bool folder_has_cue(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    bool found = false;
    struct dirent *de;
    while (!found && (de = readdir(d)) != NULL)
        found = util_has_suffix_i(de->d_name, ".cue");
    closedir(d);
    return found;
}

/* What Porpoise loads, in the order a zip's members are preferred. */
static const char *const DOLPHIN_EXTS[] = {".rvz", ".iso", ".wbfs", ".gcm", ".ciso",
                                           ".gcz", ".wia", ".wad"};
#define DOLPHIN_EXT_COUNT (sizeof(DOLPHIN_EXTS) / sizeof(DOLPHIN_EXTS[0]))

bool catalog_installed(const InstallPlan *plan)
{
    if (!plan->supported)
        return false;
    if (plan->zip_member)
    {
        for (size_t i = 0; i < DOLPHIN_EXT_COUNT; ++i)
        {
            char path[PATH_LEN + 8];
            snprintf(path, sizeof(path), "%s%s", plan->dest, DOLPHIN_EXTS[i]);
            if (util_file_exists(path))
                return true;
        }
        return false;
    }
    return plan->cue ? folder_has_cue(plan->unzip_dir) : util_file_exists(plan->check);
}

typedef struct
{
    FILE *f;
    double disk;      /* seconds spent writing, for the speed log */
    uint64_t pending; /* bytes written since the last flush to disk */
} FileSink;

/* Written data is pushed to the disk every 128 MB: left to pile up in the
 * system's cache, gigabytes of it made every write slower and slower (a
 * download or an Xbox conversion started fast and nearly stopped, and
 * pausing - which closes the file - made it fast again). */
#define FLUSH_EVERY ((uint64_t)128 << 20)

static void flush_to_disk(FILE *f)
{
    fflush(f);
    fsync(fileno(f));
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int file_sink(const void *data, size_t len, void *user)
{
    FileSink *s = user;
    double t = now_sec();
    bool ok = fwrite(data, 1, len, s->f) == len;
    s->pending += len;
    if (ok && s->pending >= FLUSH_EVERY)
    {
        flush_to_disk(s->f);
        s->pending = 0;
    }
    s->disk += now_sec() - t;
    return ok ? 0 : -1;
}

/* Large downloads come in segments, each on a fresh connection: over Wi-Fi a
 * long-lived connection keeps slowing down (pausing and resuming made it fast
 * again), and a new one starts at full speed. */
#define SEGMENT ((uint64_t)128 << 20)

typedef struct
{
    HttpProgressFn progress;
    void *user;
    uint64_t base;  /* bytes of this attempt before the segment */
    uint64_t total; /* bytes this attempt fetches */
} SegProgress;

static int seg_progress(uint64_t done, uint64_t total, void *user)
{
    (void)total;
    SegProgress *s = user;
    return s->progress ? s->progress(s->base + done, s->total, s->user) : 0;
}

/* GET path into sink->f, bytes [have, total): segment by segment when the
 * size is known, else one request from have. *res is the last response; a
 * server that ignores Range answers 200 with the whole file, which ends it. */
static int ranged_get(const HttpServer *server, const char *path, FileSink *sink, uint64_t have,
                      uint64_t total, int timeout, HttpProgressFn progress, void *user,
                      HttpResult *res, const char *label)
{
    HttpRequest req = {0};
    req.method = "GET";
    req.path = path;
    req.timeout_sec = timeout;
    req.sink = file_sink;
    req.sink_user = sink;
    bool discover = total == UINT64_MAX; /* size unknown: learnt from the first reply */
    if (discover)
        total = have + SEGMENT;
    if (!total || total <= have)
    {
        req.range_start = have;
        req.progress = progress;
        req.progress_user = user;
        return http_request(server, &req, res);
    }
    SegProgress sp = {progress, user, 0, total - have};
    req.progress = seg_progress;
    req.progress_user = &sp;
    uint64_t pos = have, logged = have;
    double t0 = now_sec(), disk0 = sink->disk;
    int rc = HTTP_OK;
    while (pos < total)
    {
        uint64_t end = (pos + SEGMENT < total ? pos + SEGMENT : total) - 1;
        req.range_start = pos;
        req.range_end = end;
        sp.base = pos - have;
        rc = http_request(server, &req, res);
        if (rc != HTTP_OK || res->status != 206)
            break; /* an error, or a 200 that carried the whole file */
        /* the file's real size, from "Content-Range: bytes S-E/TOTAL" */
        char range[96];
        const char *slash = http_header(res, "Content-Range", range, sizeof(range))
                                ? strrchr(range, '/')
                                : NULL;
        if (slash && slash[1] >= '0' && slash[1] <= '9')
        {
            uint64_t real = strtoull(slash + 1, NULL, 10);
            if (real > have)
            {
                total = real;
                sp.total = total - have;
            }
        }
        else if (discover)
            break; /* no size to go on: what came is what there is */
        pos = end + 1;
        if (pos - logged >= ((uint64_t)1 << 30) || pos == total)
        {
            double t = now_sec(), secs = t - t0 > 0.001 ? t - t0 : 0.001;
            app_log("download %s: %.2f GB of %.2f, last %.0f MB at %.1f MB/s (disk %.0f%%)", label,
                    pos / 1e9, total / 1e9, (pos - logged) / 1e6, (pos - logged) / 1e6 / secs,
                    100.0 * (sink->disk - disk0) / secs);
            logged = pos;
            t0 = t;
            disk0 = sink->disk;
        }
    }
    return rc;
}

/* Progress over a whole bundle: each file's bytes on top of the ones before. */
typedef struct
{
    HttpProgressFn progress;
    void *user;
    uint64_t base;
    uint64_t total;
} BundleProgress;

static int bundle_progress(uint64_t done, uint64_t total, void *user)
{
    (void)total;
    BundleProgress *b = user;
    return b->progress ? b->progress(b->base + done, b->total, b->user) : 0;
}

/* "content/0010/a b.bin" -> "content/0010/a%20b.bin": each part encoded, the
 * slashes kept. false for a path that could leave the game's folder. */
static bool encode_member(char *out, size_t size, const char *name)
{
    if (!name[0] || name[0] == '/' || strstr(name, "..") || strchr(name, '\\'))
        return false;
    size_t n = 0;
    out[0] = 0;
    char part[256];
    for (const char *p = name; *p;)
    {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len >= sizeof(part))
            return false;
        memcpy(part, p, len);
        part[len] = 0;
        char enc[768];
        util_url_encode(enc, sizeof(enc), part);
        int w = snprintf(out + n, size - n, "%s%s", n ? "/" : "", enc);
        if (w < 0 || (size_t)w >= size - n)
            return false;
        n += (size_t)w;
        p = slash ? slash + 1 : p + len;
    }
    return n > 0;
}

/* A bundle's file list from /roms/<id>/manifest (NULL on failure). */
static JsonValue *fetch_manifest(const HttpServer *server, const char *id, char *error, size_t size)
{
    char path[400];
    snprintf(path, sizeof(path), "/api/v1/roms/%s/manifest", id);
    HttpBuffer out = {0};
    int status = http_get(server, path, &out, NULL);
    if (status != 200)
    {
        http_buffer_free(&out);
        http_fail(error, size, "file list", status);
        return NULL;
    }
    JsonValue *root = json_parse((const char *)out.data);
    http_buffer_free(&out);
    JsonValue *files = json_get(root, "files");
    if (!files || !files->child)
    {
        json_free(root);
        snprintf(error, size, "empty file list");
        return NULL;
    }
    return root;
}

/* One member of a bundle, /roms/<id>/file/<name>, into dest: resumed from
 * dest.part, kept when it is already there at its size. */
static bool fetch_member(const HttpServer *server, const char *id, const char *name, uint64_t want,
                         const char *dest, BundleProgress *bp, char *buffer, char *error,
                         size_t size)
{
    char member[1024], path[1200], part[PATH_LEN + 8];
    if (!encode_member(member, sizeof(member), name))
    {
        snprintf(error, size, "bad file name in the set: %.60s", name);
        return false;
    }
    if (util_file_size(dest) == (int64_t)want)
    {
        bp->base += want; /* already here from an earlier attempt */
        return true;
    }
    snprintf(part, sizeof(part), "%s.part", dest);
    int64_t have = util_file_size(part);
    if (have < 0 || (uint64_t)have >= want)
        have = 0;
    if (!util_mkdirs_parent(dest))
    {
        snprintf(error, size, "can't create %s", name);
        return false;
    }
    FileSink sink = {fopen(part, have > 0 ? "ab" : "wb"), 0, 0};
    if (!sink.f)
    {
        snprintf(error, size, "can't write %s", util_basename(part));
        return false;
    }
    if (buffer)
        setvbuf(sink.f, buffer, _IOFBF, 1024 * 1024);
    snprintf(path, sizeof(path), "/api/v1/roms/%s/file/%s", id, member);
    bp->base += (uint64_t)have;
    HttpResult res;
    int rc = ranged_get(server, path, &sink, (uint64_t)have, want, 120, bundle_progress, bp, &res,
                        util_basename(name));
    bool closed = fclose(sink.f) == 0;
    bp->base -= (uint64_t)have;
    if (rc == HTTP_OK && res.status == 200 && have > 0)
    {
        snprintf(error, size, "resume refused, try again");
        remove(part);
        return false;
    }
    if (rc != HTTP_OK || (res.status != 200 && res.status != 206) || !closed)
    {
        if (rc == HTTP_OK && res.status >= 300)
        {
            remove(part);
            http_fail(error, size, name, res.status);
        }
        else
            snprintf(error, size, "%s: %s", util_basename(name),
                     rc == HTTP_OK ? "write failed" : http_error_text(rc));
        return false;
    }
    if (util_file_size(part) != (int64_t)want)
    {
        snprintf(error, size, "%s came out the wrong size", util_basename(name));
        remove(part);
        return false;
    }
    remove(dest);
    if (rename(part, dest) != 0)
    {
        snprintf(error, size, "can't rename %s", util_basename(name));
        return false;
    }
    chmod(dest, 0666); /* the emulator is another sandboxed title */
    bp->base += want;
    return true;
}

/* A zipped game (a Wii zip holding an .rvz): only the game file comes down,
 * out of the zip on the server, to dest + its extension. */
static bool download_zip_member(const HttpServer *server, const RomRow *rom,
                                const InstallPlan *plan, HttpProgressFn progress, void *user,
                                char *error, size_t size)
{
    char id[256];
    util_url_encode(id, sizeof(id), rom->rom_id);
    JsonValue *root = fetch_manifest(server, id, error, size);
    if (!root)
        return false;
    JsonValue *members = json_get(root, "members");
    const char *pick = NULL;
    uint64_t want = 0;
    size_t best = DOLPHIN_EXT_COUNT;
    for (JsonValue *m = members ? members->child : NULL; m; m = m->next)
    {
        const char *name = json_get_str(m, "name", "");
        for (size_t i = 0; i < best; ++i)
            if (util_has_suffix_i(name, DOLPHIN_EXTS[i]))
            {
                pick = name;
                want = (uint64_t)json_get_num(m, "size", 0);
                best = i;
                break;
            }
    }
    if (!pick)
    {
        json_free(root);
        snprintf(error, size, members ? "no game file in the zip" : "the server can't open zips (update it)");
        return false;
    }
    char dest[PATH_LEN + 8];
    snprintf(dest, sizeof(dest), "%s%s", plan->dest, DOLPHIN_EXTS[best]);
    char *buffer = malloc(1024 * 1024);
    BundleProgress bp = {progress, user, 0, want};
    bool ok = util_mkdirs_parent(dest) &&
              fetch_member(server, id, pick, want, dest, &bp, buffer, error, size);
    free(buffer);
    json_free(root);
    return ok;
}

/* A bundle (a Wii U WUP folder): every file of the manifest into the
 * folder. A marker file says the set is complete. */
static bool download_folder(const HttpServer *server, const RomRow *rom, const InstallPlan *plan,
                            HttpProgressFn progress, void *user, char *error, size_t size)
{
    char id[256];
    util_url_encode(id, sizeof(id), rom->rom_id);
    JsonValue *root = fetch_manifest(server, id, error, size);
    if (!root)
        return false;
    if (!util_mkdirs(plan->dest))
    {
        json_free(root);
        snprintf(error, size, "can't create the game folder");
        return false;
    }
    BundleProgress bp = {progress, user, 0, (uint64_t)json_get_num(root, "total_size", 0)};
    remove(plan->check);
    bool ok = true;
    char *buffer = malloc(1024 * 1024);
    for (JsonValue *f = json_get(root, "files")->child; f && ok; f = f->next)
    {
        const char *name = json_get_str(f, "name", "");
        char dest[PATH_LEN];
        if (snprintf(dest, sizeof(dest), "%s/%s", plan->dest, name) >= (int)sizeof(dest))
        {
            snprintf(error, size, "file name too long: %.60s", name);
            ok = false;
            break;
        }
        ok = fetch_member(server, id, name, (uint64_t)json_get_num(f, "size", 0), dest, &bp,
                          buffer, error, size);
    }
    free(buffer);
    json_free(root);
    if (ok)
        ok = util_write_file_atomic(plan->check, "complete\n", 9);
    return ok;
}

bool catalog_download(const HttpServer *server, const RomRow *rom, const InstallPlan *plan,
                      HttpProgressFn progress, void *user, char *error, size_t size)
{
    if (!plan->supported)
    {
        snprintf(error, size, "%s", plan->reason);
        return false;
    }
    if (plan->folder)
        return download_folder(server, rom, plan, progress, user, error, size);
    if (plan->zip_member)
        return download_zip_member(server, rom, plan, progress, user, error, size);
    /* Xbox: the server's XISO (?extract=iso), not the .cci decoded here - an
     * app's writes on the PS5 are throttled to ~2 MB/s after a ~1.3 GB burst,
     * and a local conversion can't be paused to let that recover. The decoder
     * (xcci.c) is kept for a helper process that isn't throttled. */
    if (!util_mkdirs_parent(plan->dest))
    {
        snprintf(error, size, "can't create the game folder");
        return false;
    }
    char part[PATH_LEN + 8];
    snprintf(part, sizeof(part), "%s.part", plan->dest);
    int64_t have = util_file_size(part);
    /* a CUE/BIN set is zipped afresh on each request: no Range on it. A
     * converted ISO/CCI is cached by the server and served with Range, so it
     * resumes; rom->size is the source's size, not the output's. */
    if (have < 0 || plan->cue || (!plan->extract && rom->size && (uint64_t)have >= rom->size))
        have = 0;
    FileSink sink = {fopen(part, have > 0 ? "ab" : "wb"), 0, 0};
    if (!sink.f)
    {
        snprintf(error, size, "can't write %s", util_basename(part));
        return false;
    }
    char *buffer = malloc(1024 * 1024);
    if (buffer)
        setvbuf(sink.f, buffer, _IOFBF, 1024 * 1024);
    char id[256], path[320];
    util_url_encode(id, sizeof(id), rom->rom_id);
    snprintf(path, sizeof(path), "/api/v1/roms/%s%s%s", id,
             plan->cue ? "?extract=cue" : plan->extract ? "?extract=" : "",
             plan->extract && !plan->cue ? plan->extract : "");
    HttpResult res;
    /* CUE/BIN: one request. A conversion: segments once its size is known. */
    int rc = ranged_get(server, path, &sink, (uint64_t)have,
                        plan->cue ? 0 : plan->extract ? UINT64_MAX : rom->size,
                        20 * 60, /* a server-side conversion can take a while to start */
                        progress, user, &res, rom->name);
    bool closed = fclose(sink.f) == 0;
    free(buffer);
    if (rc == HTTP_OK && res.status == 200 && have > 0)
    {
        /* the server ignored the Range: what we appended is a full copy */
        snprintf(error, size, "resume refused, try again");
        remove(part);
        return false;
    }
    if (rc != HTTP_OK || (res.status != 200 && res.status != 206) || !closed)
    {
        if (rc == HTTP_OK && res.status >= 300)
        {
            remove(part); /* an error page, not game data */
            http_fail(error, size, "download", res.status);
        }
        else
            snprintf(error, size, "download: %s",
                     rc == HTTP_OK ? "write failed" : http_error_text(rc));
        return false;
    }
    remove(plan->dest);
    if (rename(part, plan->dest) != 0)
    {
        snprintf(error, size, "can't rename the download");
        return false;
    }
    chmod(plan->dest, 0666); /* the emulator is another sandboxed title */
    if (plan->cue)
    {
        char why[128] = "";
        bool ok = unzip_stored(plan->dest, plan->unzip_dir, why, sizeof(why));
        remove(plan->dest);
        if (!ok || !folder_has_cue(plan->unzip_dir))
        {
            snprintf(error, size, "unpacking the CUE/BIN set: %s", why[0] ? why : "no .cue in it");
            return false;
        }
    }
    return true;
}
