/*
 * GameSync PS5 client - comparing with the server and moving saves.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The three-way rule of server/app/routes/sync.py, run here because each kind
 * of save is hashed by a different endpoint:
 *   - RetroArch .srm: /titles' save_hash is sha256 of the file;
 *   - PS1 cards: /saves/{id}/ps1-card/meta (/titles hashes the PSP VMP set);
 *   - PS2 folders: sha256 of the folder's P2FD from /saves/{id}/ps2-files.
 * Unlike the server's /sync, a save that was never synced and differs from
 * the server's is a conflict, not a download: nothing is overwritten without
 * the user choosing.
 */
#include "sync.h"

#include "json.h"
#include "bundle.h"
#include "ps2mc.h"
#include "retroarch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PS1_CARD_SIZE 131072u
/* the id rules ids.txt was made under (bumped when they change) */
#define ID_RULES "2"

static void progress(SyncContext *ctx, const char *message, int done, int total)
{
    if (ctx->progress)
        ctx->progress(message, done, total, ctx->progress_user);
}

void sync_init(SyncContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    util_mkdirs(config_data_dir());
    config_load(&ctx->config);
    config_apply(&ctx->config, &ctx->server);
    kv_load(&ctx->state, data_path("state.txt"));
    kv_load(&ctx->ids, data_path("ids.txt"));
    /* ids.txt from before translations and hacks shared their game's slot
     * (and Saturn went by serial): resolved again */
    if (strcmp(kv_get(&ctx->ids, "_rules") ? kv_get(&ctx->ids, "_rules") : "", ID_RULES) != 0)
    {
        char path[PATH_LEN];
        util_copy(path, sizeof(path), ctx->ids.path);
        kv_free(&ctx->ids);
        memset(&ctx->ids, 0, sizeof(ctx->ids));
        util_copy(ctx->ids.path, sizeof(ctx->ids.path), path);
        kv_set(&ctx->ids, "_rules", ID_RULES);
        kv_save(&ctx->ids);
    }
    kv_load(&ctx->installed, data_path("installed.txt"));
}

void sync_shutdown(SyncContext *ctx)
{
    kv_save(&ctx->state);
    kv_save(&ctx->ids);
    kv_save(&ctx->installed);
    kv_free(&ctx->state);
    kv_free(&ctx->ids);
    kv_free(&ctx->installed);
    saves_free(&ctx->saves);
}

static void set_error(SyncContext *ctx, const char *what, int code)
{
    snprintf(ctx->error, sizeof(ctx->error), "%s: %s", what, http_error_text(code));
    app_log("%s", ctx->error);
}

/* ------------------------------------------------------------ title ids */

/* RetroArch ROMs get their save slot from the server's resolver, so the
 * client and server agree on slugs/serials without a C port of the rules in
 * shared/rom_id. Results are cached in ids.txt. */
static bool resolve_ids(SyncContext *ctx)
{
    /* the catalog already knows the id of a file it installed: a Saturn CHD
     * (no IP.BIN to read) or a translation keeps its game's serial there */
    for (int i = 0; ctx->catalog && i < ctx->saves.count; ++i)
    {
        SaveEntry *e = &ctx->saves.items[i];
        if (e->title_id[0] || e->emu != EMU_RETROARCH || !e->rom_file[0])
            continue;
        const RomRow *r = catalog_find_file(ctx->catalog, e->system, e->rom_file);
        if (!r)
        {
            /* a CUE/BIN install: the catalog row is the CHD of the same name */
            char chd[300];
            util_copy(chd, sizeof(chd), e->rom_file);
            util_strip_ext(chd);
            util_copy(chd + strlen(chd), sizeof(chd) - strlen(chd), ".chd");
            r = catalog_find_file(ctx->catalog, e->system, chd);
        }
        if (!r || !r->title_id[0])
            continue;
        util_copy(e->title_id, sizeof(e->title_id), r->title_id);
        char key[300];
        snprintf(key, sizeof(key), "%s|%s", e->system, e->rom_file);
        kv_set(&ctx->ids, key, r->title_id);
    }
    const int batch = 100;
    int pending = 0;
    for (int i = 0; i < ctx->saves.count; ++i)
        if (!ctx->saves.items[i].title_id[0])
            pending++;
    int done = 0;
    while (pending > 0)
    {
        StrBuf body;
        sb_init(&body);
        sb_append(&body, "{\"items\":[");
        int idx[100];
        int n = 0;
        for (int i = 0; i < ctx->saves.count && n < batch; ++i)
        {
            SaveEntry *e = &ctx->saves.items[i];
            if (e->title_id[0] || e->message[0])
                continue;
            if (n)
                sb_append(&body, ",");
            sb_append(&body, "{\"system\":");
            sb_append_json_str(&body, e->system);
            sb_append(&body, ",\"rom_filename\":");
            sb_append_json_str(&body, e->rom_file);
            sb_append(&body, "}");
            idx[n++] = i;
        }
        sb_append(&body, "]}");
        if (n == 0 || body.failed)
        {
            sb_free(&body);
            break;
        }
        progress(ctx, "Identifying RetroArch games", done, done + pending);
        HttpBuffer out = {0};
        int status = http_post(&ctx->server, "/api/v1/catalog/resolve/batch", body.data, body.len,
                               "application/json", &out, NULL);
        sb_free(&body);
        if (status != 200)
        {
            http_buffer_free(&out);
            set_error(ctx, "resolving game ids", status);
            return false;
        }
        JsonValue *root = json_parse((const char *)out.data);
        http_buffer_free(&out);
        JsonValue *results = json_get(root, "results");
        int k = 0;
        for (JsonValue *r = results ? results->child : NULL; r && k < n; r = r->next, ++k)
        {
            SaveEntry *e = &ctx->saves.items[idx[k]];
            const char *id = json_get_str(r, "sync_id", "");
            if (!id[0])
            {
                util_copy(e->message, sizeof(e->message), "no id from the server");
                continue;
            }
            util_copy(e->title_id, sizeof(e->title_id), id);
            char key[300];
            snprintf(key, sizeof(key), "%s|%s", e->system, e->rom_file);
            kv_set(&ctx->ids, key, id);
        }
        for (; k < n; ++k) /* a short answer: don't ask again this round */
            util_copy(ctx->saves.items[idx[k]].message, sizeof(ctx->saves.items[0].message),
                      "no id from the server");
        json_free(root);
        pending -= n;
        done += n;
    }
    kv_save(&ctx->ids);
    return true;
}

/* A PS1 game in RetroArch and in SwanStation: SwanStation's card wins. */
static void drop_duplicates(SyncContext *ctx)
{
    SaveList *l = &ctx->saves;
    int w = 0;
    for (int i = 0; i < l->count; ++i)
    {
        bool dup = false;
        if (l->items[i].title_id[0])
            for (int j = 0; j < w; ++j)
                if (strcmp(l->items[j].title_id, l->items[i].title_id) == 0)
                {
                    dup = true;
                    break;
                }
        if (!dup)
            l->items[w++] = l->items[i];
    }
    l->count = w;
}

/* ------------------------------------------------------------ decisions */

SaveStatus sync_decide(const SaveEntry *e, const char *last)
{
    if (!e->title_id[0])
        return ST_ERROR;
    if (e->local && !e->server)
        return ST_LOCAL_ONLY;
    if (!e->local && e->server)
        return ST_SERVER_ONLY;
    if (!e->local && !e->server)
        return ST_NONE;
    if (strcmp(e->local_hash, e->server_hash) == 0)
        return ST_SYNCED;
    if (!last || !last[0])
        return ST_CONFLICT;
    if (strcmp(last, e->server_hash) == 0)
        return ST_UPLOAD;
    if (strcmp(last, e->local_hash) == 0)
        return ST_DOWNLOAD;
    return ST_CONFLICT;
}

static void record_synced(SyncContext *ctx, SaveEntry *e, const char *hash)
{
    kv_set(&ctx->state, e->title_id, hash);
    kv_save(&ctx->state);
}

static void decide(SyncContext *ctx, SaveEntry *e)
{
    const char *last = kv_get(&ctx->state, e->title_id);
    e->status = sync_decide(e, last);
    /* already equal: remember it, so a later one-sided change isn't a conflict */
    if (e->status == ST_SYNCED && (!last || strcmp(last, e->local_hash) != 0))
        record_synced(ctx, e, e->local_hash);
}

/* ------------------------------------------------------------ server state */

static bool fetch_server_hash(SyncContext *ctx, SaveEntry *e)
{
    char path[256];
    HttpBuffer out = {0};
    HttpResult res;
    int status;
    switch (e->kind)
    {
    case SAVE_PS1_CARD:
        snprintf(path, sizeof(path), "/api/v1/saves/%s/ps1-card/meta?slot=0", e->title_id);
        status = http_get(&ctx->server, path, &out, &res);
        if (status == 200)
        {
            JsonValue *root = json_parse((const char *)out.data);
            util_copy(e->server_hash, sizeof(e->server_hash), json_get_str(root, "save_hash", ""));
            e->server_size = (uint64_t)json_get_num(root, "save_size", 0);
            json_free(root);
        }
        break;
    case SAVE_PS2_FOLDER:
        snprintf(path, sizeof(path), "/api/v1/saves/%s/ps2-files", e->title_id);
        status = http_get(&ctx->server, path, &out, &res);
        if (status == 200)
        {
            util_sha256_hex(out.data, out.len, e->server_hash);
            e->server_size = out.len;
        }
        break;
    case SAVE_GCI:
        /* the stored save can be a whole card: the meta hash is of this game's GCI */
        snprintf(path, sizeof(path), "/api/v1/saves/%s/gc-card/meta", e->title_id);
        status = http_get(&ctx->server, path, &out, &res);
        if (status == 200)
        {
            JsonValue *root = json_parse((const char *)out.data);
            util_copy(e->server_hash, sizeof(e->server_hash), json_get_str(root, "save_hash", ""));
            e->server_size = (uint64_t)json_get_num(root, "save_size", 0);
            json_free(root);
        }
        break;
    default:
        return true; /* /titles already gave it */
    }
    http_buffer_free(&out);
    if (status == 404)
    {
        /* on the server, but not as this kind of save (a PSP-only VMP...) */
        e->server = false;
        return true;
    }
    if (status != 200)
    {
        e->status = ST_ERROR;
        snprintf(e->message, sizeof(e->message), "server: %s", http_error_text(status));
        return false;
    }
    return true;
}

/* A RetroArch-system save (SNES_..., GBA_...) the console has no ROM for.
 * The .srm must be named after the ROM, so the name comes from the catalog
 * row with the same id: the file the catalog would install. Without one the
 * row still shows, but can't be downloaded. */
static void add_server_only_retroarch(SyncContext *ctx, const JsonValue *t, const char *tid)
{
    const char *us = strchr(tid, '_');
    if (!us || us == tid || us - tid >= 12)
        return;
    char system[12];
    memcpy(system, tid, (size_t)(us - tid));
    system[us - tid] = 0;
    if (!retroarch_syncs_system(system))
        return;
    SaveEntry *e = saves_add(&ctx->saves);
    if (!e)
        return;
    e->emu = EMU_RETROARCH;
    e->kind = SAVE_SRM;
    util_copy(e->system, sizeof(e->system), system);
    util_copy(e->title_id, sizeof(e->title_id), tid);
    e->server = true;
    e->status = ST_SERVER_ONLY;
    util_copy(e->server_hash, sizeof(e->server_hash), json_get_str(t, "save_hash", ""));
    e->server_size = (uint64_t)json_get_num(t, "save_size", 0);
    const Catalog *c = ctx->catalog;
    for (int i = 0; c && i < c->rom_count; ++i)
    {
        const RomRow *r = &c->roms[i];
        if (strcmp(r->title_id, tid) != 0)
            continue;
        InstallPlan plan;
        catalog_plan(&ctx->config, r, &plan);
        if (!plan.supported)
            continue;
        util_copy(e->rom_file, sizeof(e->rom_file), r->filename);
        retroarch_save_path(&ctx->config, system, plan.dest, e->path, sizeof(e->path));
        break;
    }
    if (!e->path[0])
        util_copy(e->message, sizeof(e->message),
                  "no catalog ROM with this id: install the game to name its save");
}

static void add_server_only(SyncContext *ctx, const JsonValue *t, const char *tid)
{
    const char *console = json_get_str(t, "console_type", json_get_str(t, "system", ""));
    bool ps1 = util_ieq(console, "PS1") || util_ieq(console, "PSX");
    bool ps2 = util_ieq(console, "PS2");
    bool here = false; /* a PS2 game file with this serial is in PS5SX2 */
    bool gc = strncmp(tid, "GC_", 3) == 0 && strlen(tid) == 7;
    bool wii = strncmp(tid, "WII_", 4) == 0 && strlen(tid) == 8;
    bool nsw = strncmp(tid, "NSW_", 4) == 0 && strlen(tid) == 20;
    char path[PATH_LEN] = "";
    if (wii && !saves_porpoise_wii_dir(&ctx->config, tid, path, sizeof(path)))
        return;
    if (nsw)
    {
        char user[PATH_LEN];
        if (!saves_eden_user_dir(&ctx->config, user, sizeof(user)))
            return; /* no ProsperoEden profile yet */
        snprintf(path, sizeof(path), "%s/%s", user, tid + 4);
    }
    char cemu_path[PATH_LEN];
    if (saves_cemu_dir(&ctx->config, tid, cemu_path, sizeof(cemu_path)))
    {
        /* a Wii U or 3DS save: PS5CEMU's Cemu / Azahar folder */
        SaveEntry *e = saves_add(&ctx->saves);
        if (!e)
            return;
        e->emu = EMU_CEMU;
        e->kind = SAVE_FOLDER;
        util_copy(e->system, sizeof(e->system), strncmp(tid, "00050000", 8) == 0 ? "WIIU" : "3DS");
        util_copy(e->title_id, sizeof(e->title_id), tid);
        util_copy(e->path, sizeof(e->path), cemu_path);
        e->server = true;
        e->status = ST_SERVER_ONLY;
        util_copy(e->server_hash, sizeof(e->server_hash), json_get_str(t, "save_hash", ""));
        e->server_size = (uint64_t)json_get_num(t, "save_size", 0);
        e->installed = kv_get(&ctx->installed, tid) != NULL;
        return;
    }
    if (!gc && !wii && !nsw && ((!ps1 && !ps2) || !is_ps_serial(tid)))
    {
        add_server_only_retroarch(ctx, t, tid);
        return;
    }
    SaveEntry *e = saves_add(&ctx->saves);
    if (!e)
        return;
    util_copy(e->title_id, sizeof(e->title_id), tid);
    e->server = true;
    e->status = ST_SERVER_ONLY;
    util_copy(e->server_hash, sizeof(e->server_hash), json_get_str(t, "save_hash", ""));
    e->server_size = (uint64_t)json_get_num(t, "save_size", 0);
    if (gc || wii || nsw)
    {
        e->emu = nsw ? EMU_EDEN : EMU_PORPOISE;
        e->kind = gc ? SAVE_GCI : SAVE_FOLDER;
        util_copy(e->system, sizeof(e->system), gc ? "GC" : wii ? "WII" : "NSW");
        if (gc)
        {
            saves_porpoise_card_dir(&ctx->config, tid + 3, e->path, sizeof(e->path));
            e->path_is_dir = true;
        }
        else
            util_copy(e->path, sizeof(e->path), path);
        e->installed = kv_get(&ctx->installed, tid) != NULL;
        return;
    }
    if (ps1)
    {
        e->emu = EMU_SWANSTATION;
        e->kind = SAVE_PS1_CARD;
        util_copy(e->system, sizeof(e->system), "PS1");
        /* the card name depends on SwanStation's card type; the title mode
         * names it after the game, whose file name the catalog knows */
        char stem[200] = "";
        for (int i = 0; ctx->catalog && i < ctx->catalog->rom_count; ++i)
        {
            const RomRow *r = &ctx->catalog->roms[i];
            if (util_ieq(r->system, "PS1") && strcmp(r->title_id, tid) == 0)
            {
                util_safe_filename(stem, sizeof(stem), r->filename);
                util_strip_ext(stem);
                break;
            }
        }
        saves_swanstation_card_path(&ctx->config, tid, stem, e->path, sizeof(e->path));
    }
    else
    {
        e->emu = EMU_PS5SX2;
        e->kind = SAVE_PS2_FOLDER;
        util_copy(e->system, sizeof(e->system), "PS2");
        /* per game: the card of the game file here with that serial (PS5SX2
         * knows it, also for CHDs the catalog only has a name slug for),
         * else of the file the catalog installs */
        char stem[200] = "";
        if (ctx->config.ps2_per_game)
            here = saves_ps5sx2_game_for_serial(&ctx->config, tid, stem, sizeof(stem));
        for (int i = 0; ctx->config.ps2_per_game && !stem[0] && ctx->catalog && i < ctx->catalog->rom_count; ++i)
        {
            const RomRow *r = &ctx->catalog->roms[i];
            if (util_ieq(r->system, "PS2") && strcmp(r->title_id, tid) == 0)
            {
                util_safe_filename(stem, sizeof(stem), r->filename);
                util_strip_ext(stem);
                break;
            }
        }
        /* the game's settings name the card once its save is downloaded */
        util_copy(e->rom_file, sizeof(e->rom_file), stem);
        if (!stem[0] || !saves_ps5sx2_game_card(&ctx->config, stem, false, e->path, sizeof(e->path)))
        {
            char dir[PATH_LEN];
            saves_ps5sx2_card_dir(&ctx->config, false, dir, sizeof(dir));
            snprintf(e->path, sizeof(e->path), "%s/%s", dir, ctx->config.ps2_card);
        }
    }
    /* installed.txt has the catalog's id, a name slug for some PS2 CHDs */
    e->installed = here || kv_get(&ctx->installed, tid) != NULL;
    e->status = ST_SERVER_ONLY;
}

static bool compare_with_server(SyncContext *ctx)
{
    progress(ctx, "Reading the server's saves", 0, 0);
    HttpBuffer out = {0};
    int status = http_get(&ctx->server, "/api/v1/titles", &out, NULL);
    if (status != 200)
    {
        http_buffer_free(&out);
        set_error(ctx, "listing server saves", status);
        return false;
    }
    JsonValue *root = json_parse((const char *)out.data);
    http_buffer_free(&out);
    JsonValue *titles = json_get(root, "titles");
    if (!titles)
    {
        json_free(root);
        snprintf(ctx->error, sizeof(ctx->error), "unexpected /titles response");
        return false;
    }
    int local_count = ctx->saves.count;
    for (JsonValue *t = titles->child; t; t = t->next)
    {
        const char *tid = json_get_str(t, "title_id", "");
        if (!tid[0])
            continue;
        SaveEntry *e = NULL;
        for (int i = 0; i < local_count; ++i)
            if (strcmp(ctx->saves.items[i].title_id, tid) == 0)
            {
                e = &ctx->saves.items[i];
                break;
            }
        if (!e)
        {
            add_server_only(ctx, t, tid);
            continue;
        }
        e->server = true;
        util_copy(e->server_hash, sizeof(e->server_hash), json_get_str(t, "save_hash", ""));
        e->server_size = (uint64_t)json_get_num(t, "save_size", 0);
        const char *name = json_get_str(t, "game_name", json_get_str(t, "name", ""));
        /* RetroArch rows keep the ROM's own name; cards only know a serial */
        if (e->emu != EMU_RETROARCH && name[0] && strcmp(name, tid) != 0)
            util_copy(e->name, sizeof(e->name), name);
    }
    /* names for server-only rows (added after local_count) */
    for (int i = local_count; i < ctx->saves.count; ++i)
    {
        SaveEntry *e = &ctx->saves.items[i];
        util_copy(e->name, sizeof(e->name), e->title_id);
        for (JsonValue *t = titles->child; t; t = t->next)
            if (strcmp(json_get_str(t, "title_id", ""), e->title_id) == 0)
            {
                const char *name = json_get_str(t, "game_name", json_get_str(t, "name", ""));
                if (name[0])
                    util_copy(e->name, sizeof(e->name), name);
                break;
            }
    }
    json_free(root);

    int need = 0;
    for (int i = 0; i < local_count; ++i)
        if (ctx->saves.items[i].server && ctx->saves.items[i].kind != SAVE_SRM)
            need++;
    int done = 0;
    for (int i = 0; i < local_count; ++i)
    {
        SaveEntry *e = &ctx->saves.items[i];
        if (e->server && e->kind != SAVE_SRM)
        {
            progress(ctx, "Comparing memory cards", done++, need);
            if (!fetch_server_hash(ctx, e))
                continue;
        }
        if (e->status != ST_ERROR)
            decide(ctx, e);
    }
    ctx->compared = true;
    return true;
}

static int compare_entries(const void *a, const void *b)
{
    const SaveEntry *x = a, *y = b;
    /* what needs attention first, then by name */
    static const int rank[] = {
        [ST_CONFLICT] = 0, [ST_ERROR] = 1,       [ST_UPLOAD] = 2, [ST_DOWNLOAD] = 2,
        [ST_LOCAL_ONLY] = 3, [ST_SERVER_ONLY] = 4, [ST_SYNCED] = 5, [ST_UNKNOWN] = 6,
        [ST_NONE] = 7,
    };
    if (rank[x->status] != rank[y->status])
        return rank[x->status] - rank[y->status];
    return strcasecmp(x->name, y->name);
}

bool sync_refresh(SyncContext *ctx)
{
    ctx->error[0] = 0;
    ctx->compared = false;
    progress(ctx, "Looking for saves", 0, 0);
    saves_scan(&ctx->config, &ctx->ids, &ctx->installed, &ctx->saves);
    bool ok = config_ready(&ctx->config);
    if (!ok)
        snprintf(ctx->error, sizeof(ctx->error), "set the server URL and API key in Settings");
    if (ok)
        ok = resolve_ids(ctx);
    drop_duplicates(ctx);
    if (ok)
        ok = compare_with_server(ctx);
    /* RetroArch ROMs that have no save anywhere: nothing to show */
    int w = 0;
    for (int i = 0; i < ctx->saves.count; ++i)
    {
        SaveEntry *e = &ctx->saves.items[i];
        if (e->status == ST_NONE || (!ctx->compared && !e->local))
            continue;
        ctx->saves.items[w++] = *e;
    }
    ctx->saves.count = w;
    /* names the server doesn't know (a bare id) or only knows as a slug:
     * the catalog's own name for the game, when it has one */
    for (int i = 0; ctx->catalog && i < ctx->saves.count; ++i)
    {
        SaveEntry *e = &ctx->saves.items[i];
        bool bare = !e->name[0] || strcmp(e->name, e->title_id) == 0;
        if (!bare && !(e->emu == EMU_RETROARCH && !e->local))
            continue;
        for (int k = 0; k < ctx->catalog->rom_count; ++k)
            if (strcmp(ctx->catalog->roms[k].title_id, e->title_id) == 0)
            {
                util_copy(e->name, sizeof(e->name), ctx->catalog->roms[k].name);
                break;
            }
    }
    qsort(ctx->saves.items, (size_t)ctx->saves.count, sizeof(SaveEntry), compare_entries);
    return ok;
}

/* ------------------------------------------------------------ transfers */

static bool upload_ps2(SyncContext *ctx, SaveEntry *e, char *hash)
{
    Ps2CardFile card;
    char error[96];
    if (!ps2mc_load_file(e->path, &card, error, sizeof(error)))
    {
        util_copy(e->message, sizeof(e->message), error);
        return false;
    }
    int i = ps2mc_find_dir(&card.card, e->ps2_dir);
    size_t size = 0;
    uint8_t *p2fd = i >= 0 ? ps2mc_build_p2fd(&card.card.dirs[i], &size) : NULL;
    ps2mc_free(&card.card);
    if (!p2fd)
    {
        util_copy(e->message, sizeof(e->message), "the folder is gone from the card");
        return false;
    }
    char path[256];
    snprintf(path, sizeof(path), "/api/v1/saves/%s/ps2-files", e->title_id);
    int status = http_post(&ctx->server, path, p2fd, size, "application/octet-stream", NULL, NULL);
    util_sha256_hex(p2fd, size, hash);
    free(p2fd);
    if (status != 200)
    {
        snprintf(e->message, sizeof(e->message), "upload: %s", http_error_text(status));
        return false;
    }
    return true;
}

/* Wii / Switch: the whole folder as a 3DSS v4 bundle. The server hashes the
 * files in bundle order, which is the order tree_hash uses. */
static bool upload_folder(SyncContext *ctx, SaveEntry *e, char *hash)
{
    SaveTree tree;
    if (!tree_read(e->path, "nocopy", &tree) || !tree.count)
    {
        tree_free(&tree);
        util_copy(e->message, sizeof(e->message), "can't read the save folder");
        return false;
    }
    size_t size = 0;
    uint8_t *bundle = bundle_build(e->title_id, &tree, (uint32_t)time(NULL), &size);
    tree_hash(&tree, hash, NULL);
    tree_free(&tree);
    if (!bundle)
    {
        util_copy(e->message, sizeof(e->message), "save too big to bundle");
        return false;
    }
    char path[256];
    snprintf(path, sizeof(path), "/api/v1/saves/%s?force=true&source=ps5", e->title_id);
    int status = http_post(&ctx->server, path, bundle, size, "application/octet-stream", NULL, NULL);
    free(bundle);
    if (status != 200)
    {
        snprintf(e->message, sizeof(e->message), "upload: %s", http_error_text(status));
        return false;
    }
    return true;
}

bool sync_upload(SyncContext *ctx, SaveEntry *e)
{
    e->message[0] = 0;
    if (!e->title_id[0])
        return false;
    char hash[65] = "";
    bool ok;
    if (e->kind == SAVE_PS2_FOLDER)
        ok = upload_ps2(ctx, e, hash);
    else if (e->kind == SAVE_FOLDER)
        ok = upload_folder(ctx, e, hash);
    else
    {
        size_t size;
        uint8_t *data = util_read_file(e->path, &size);
        if (!data)
        {
            util_copy(e->message, sizeof(e->message), "can't read the save");
            return false;
        }
        if (e->kind == SAVE_PS1_CARD && size != PS1_CARD_SIZE)
        {
            free(data);
            util_copy(e->message, sizeof(e->message), "not a 128 KB PS1 card");
            return false;
        }
        char path[256];
        if (e->kind == SAVE_PS1_CARD)
            snprintf(path, sizeof(path), "/api/v1/saves/%s/ps1-card?slot=0", e->title_id);
        else if (e->kind == SAVE_GCI)
            snprintf(path, sizeof(path), "/api/v1/saves/%s/gc-card?format=gci", e->title_id);
        else
            snprintf(path, sizeof(path), "/api/v1/saves/%s/raw?force=true", e->title_id);
        int status = http_post(&ctx->server, path, data, size, "application/octet-stream", NULL,
                               NULL);
        util_sha256_hex(data, size, hash);
        free(data);
        ok = status == 200;
        if (!ok)
            snprintf(e->message, sizeof(e->message), "upload: %s", http_error_text(status));
    }
    if (!ok)
    {
        app_log("upload %s failed: %s", e->title_id, e->message);
        e->status = ST_ERROR;
        return false;
    }
    app_log("uploaded %s (%s)", e->title_id, emulator_tag(e->emu));
    record_synced(ctx, e, hash);
    util_copy(e->local_hash, sizeof(e->local_hash), hash);
    util_copy(e->server_hash, sizeof(e->server_hash), hash);
    e->local = e->server = true;
    e->status = ST_SYNCED;
    return true;
}

static bool download_ps2(SaveEntry *e, const uint8_t *data, size_t size)
{
    Ps2Dir dir;
    if (!ps2mc_parse_p2fd(data, size, &dir))
    {
        util_copy(e->message, sizeof(e->message), "bad folder data from the server");
        return false;
    }
    Ps2CardFile card;
    char error[96] = "";
    if (!util_file_exists(e->path))
    {
        /* no card yet: a blank one in PCSX2's own .ps2 form (a game's own
         * card can be the first in a memcards/ folder not made yet) */
        memset(&card, 0, sizeof(card));
        util_mkdirs_parent(e->path);
        card.ecc = !util_has_suffix_i(e->path, ".mc2");
    }
    else if (!ps2mc_load_file(e->path, &card, error, sizeof(error)))
    {
        ps2mc_free_dir(&dir);
        util_copy(e->message, sizeof(e->message), error);
        return false;
    }
    /* the server's folder name can differ from ours (BASLUS-20312 vs
     * BASLUS-20312SAVE): replace the folder this entry is bound to */
    if (e->ps2_dir[0] && strcmp(e->ps2_dir, dir.name) != 0)
    {
        int old = ps2mc_find_dir(&card.card, e->ps2_dir);
        if (old >= 0)
        {
            ps2mc_free_dir(&card.card.dirs[old]);
            card.card.dirs[old] = card.card.dirs[--card.card.count];
        }
    }
    char name[33];
    util_copy(name, sizeof(name), dir.name);
    bool ok = ps2mc_put_dir(&card.card, &dir);
    ps2mc_free_dir(&dir);
    if (ok)
        ok = ps2mc_save_file(e->path, &card, (uint32_t)time(NULL), error, sizeof(error));
    ps2mc_free(&card.card);
    if (!ok)
    {
        util_copy(e->message, sizeof(e->message), error[0] ? error : "can't update the card");
        return false;
    }
    util_copy(e->ps2_dir, sizeof(e->ps2_dir), name);
    return true;
}

/* A new .gci in a Dolphin card folder: "<maker>-<code>-<file name>.gci",
 * as Dolphin names them, from the GCI's own directory entry. */
static bool gci_file_name(SaveEntry *e, const uint8_t *gci, size_t size)
{
    if (size < 0x40 + 0x2000)
        return false;
    char maker[3] = {(char)gci[4], (char)gci[5], 0};
    char code[5] = {(char)gci[0], (char)gci[1], (char)gci[2], (char)gci[3], 0};
    char file[33];
    memcpy(file, gci + 8, 32);
    file[32] = 0;
    char safe[33];
    util_safe_filename(safe, sizeof(safe), file);
    for (char *p = safe; *p; ++p)
        if (*p == ' ')
            *p = '_';
    char name[96];
    snprintf(name, sizeof(name), "%s-%s-%s.gci", maker, code, safe[0] ? safe : "save");
    char dir[PATH_LEN];
    util_copy(dir, sizeof(dir), e->path);
    util_path_join(e->path, sizeof(e->path), dir, name);
    e->path_is_dir = false;
    return true;
}

static bool download_folder(SaveEntry *e, const uint8_t *data, size_t size, char *hash)
{
    SaveTree tree;
    if (!bundle_parse(data, size, &tree))
    {
        util_copy(e->message, sizeof(e->message), "bad bundle from the server");
        return false;
    }
    bool ok = tree_write_replace(e->path, &tree);
    if (ok)
        tree_hash(&tree, hash, &e->local_size);
    else
        util_copy(e->message, sizeof(e->message), "can't write the save folder");
    tree_free(&tree);
    return ok;
}

bool sync_download(SyncContext *ctx, SaveEntry *e)
{
    if (!e->path[0])
    {
        util_copy(e->message, sizeof(e->message),
                  "no catalog ROM with this id: install the game to name its save");
        return false;
    }
    e->message[0] = 0;
    if (!e->title_id[0])
        return false;
    char path[256];
    switch (e->kind)
    {
    case SAVE_PS1_CARD:
        snprintf(path, sizeof(path), "/api/v1/saves/%s/ps1-card?slot=0", e->title_id);
        break;
    case SAVE_PS2_FOLDER:
        snprintf(path, sizeof(path), "/api/v1/saves/%s/ps2-files", e->title_id);
        break;
    case SAVE_GCI:
        snprintf(path, sizeof(path), "/api/v1/saves/%s/gc-card?format=gci", e->title_id);
        break;
    case SAVE_FOLDER:
        snprintf(path, sizeof(path), "/api/v1/saves/%s", e->title_id);
        break;
    default:
        snprintf(path, sizeof(path), "/api/v1/saves/%s/raw", e->title_id);
        break;
    }
    HttpBuffer out = {0};
    int status = http_get(&ctx->server, path, &out, NULL);
    bool ok = status == 200;
    if (!ok)
        snprintf(e->message, sizeof(e->message), "download: %s", http_error_text(status));
    else if (e->kind == SAVE_PS1_CARD && out.len != PS1_CARD_SIZE)
    {
        ok = false;
        util_copy(e->message, sizeof(e->message), "server card isn't 128 KB");
    }
    char hash[65] = "";
    if (ok)
    {
        util_sha256_hex(out.data, out.len, hash);
        if (e->kind == SAVE_PS2_FOLDER)
            ok = download_ps2(e, out.data, out.len);
        else if (e->kind == SAVE_FOLDER)
            ok = download_folder(e, out.data, out.len, hash);
        else
        {
            if (e->kind == SAVE_GCI && e->path_is_dir && !gci_file_name(e, out.data, out.len))
            {
                ok = false;
                util_copy(e->message, sizeof(e->message), "the server's GCI is damaged");
            }
            /* keep the replaced save once, next to it */
            if (ok && util_file_exists(e->path))
            {
                char bak[PATH_LEN + 8];
                snprintf(bak, sizeof(bak), "%s.bak", e->path);
                util_copy_file(e->path, bak);
            }
            if (ok)
                ok = util_write_file_atomic(e->path, out.data, out.len);
            if (!ok && !e->message[0])
                util_copy(e->message, sizeof(e->message), "can't write the save");
        }
    }
    uint64_t size = out.len;
    http_buffer_free(&out);
    if (!ok)
    {
        app_log("download %s failed: %s", e->title_id, e->message);
        e->status = ST_ERROR;
        return false;
    }
    app_log("downloaded %s (%s)", e->title_id, emulator_tag(e->emu));
    /* quick resume would load the old card back out of the game's state */
    if (e->emu == EMU_SWANSTATION && e->kind == SAVE_PS1_CARD)
        saves_swanstation_drop_resume(&ctx->config, e->path, e->title_id);
    if (e->emu == EMU_PS5SX2 && ctx->config.ps2_per_game && e->rom_file[0])
    {
        char card[PATH_LEN];
        saves_ps5sx2_game_card(&ctx->config, e->rom_file, true, card, sizeof(card));
    }
    record_synced(ctx, e, hash);
    util_copy(e->local_hash, sizeof(e->local_hash), hash);
    util_copy(e->server_hash, sizeof(e->server_hash), hash);
    if (e->kind != SAVE_FOLDER)
        e->local_size = size;
    e->local = e->server = true;
    e->status = ST_SYNCED;
    return true;
}

bool sync_apply(SyncContext *ctx, SaveEntry *e)
{
    switch (e->status)
    {
    case ST_UPLOAD:
    case ST_LOCAL_ONLY: return sync_upload(ctx, e);
    case ST_DOWNLOAD:
    case ST_SERVER_ONLY: return sync_download(ctx, e);
    default: return true;
    }
}

SyncSummary sync_all(SyncContext *ctx)
{
    SyncSummary s = {0};
    int total = 0, done = 0;
    for (int i = 0; i < ctx->saves.count; ++i)
        if (ctx->saves.items[i].status != ST_SYNCED)
            total++;
    for (int i = 0; i < ctx->saves.count; ++i)
    {
        SaveEntry *e = &ctx->saves.items[i];
        if (e->status == ST_CONFLICT)
        {
            s.conflicts++;
            continue;
        }
        /* a save on the server for a game this console doesn't have: left for
         * the user to pull by hand (a shared PS2 card fills up, a RetroArch
         * save without its ROM is clutter) */
        if (e->status == ST_SERVER_ONLY && !e->installed)
            continue;
        bool up = e->status == ST_UPLOAD || e->status == ST_LOCAL_ONLY;
        bool down = e->status == ST_DOWNLOAD || e->status == ST_SERVER_ONLY;
        if (!up && !down)
            continue;
        char message[160];
        snprintf(message, sizeof(message), "%s %s", up ? "Uploading" : "Downloading", e->name);
        progress(ctx, message, done++, total);
        if (sync_apply(ctx, e))
            up ? s.uploaded++ : s.downloaded++;
        else
            s.failed++;
    }
    return s;
}

/* PCSX2's game database, which PS5SX2 reads from resources/GameIndex.yaml
 * (OrbisDir("resources")): every game's fixes come from it, and Star Ocean 3
 * doesn't even boot without its VuAddSubHack. PS5SX2's installer copies it;
 * a store install of just the app doesn't, so it comes from the server
 * (server/app/routes/emulators.py, the latest PS5SX2 release's copy). Only
 * written when missing: a copy that's there is PS5SX2's own. */
int sync_ps5sx2_game_index(SyncContext *ctx)
{
    const Config *c = &ctx->config;
    if (!c->ps5sx2_dir[0] || !util_dir_exists(c->ps5sx2_dir))
        return 0;
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/resources/GameIndex.yaml", c->ps5sx2_dir);
    if (util_file_exists(path))
        return 0;
    HttpBuffer out = {0};
    HttpResult res = {0};
    int status = http_get(&ctx->server, "/api/v1/emulators/ps5sx2/game-index", &out, &res);
    char want[65] = "", got[65] = "", tag[64] = "";
    http_header(&res, "X-File-Hash", want, sizeof(want));
    http_header(&res, "X-Source-Tag", tag, sizeof(tag));
    if (status == 200)
        util_sha256_hex(out.data, out.len, got);
    int result = -1;
    if (status != 200)
        app_log("PS5SX2 GameIndex.yaml: the server answered %s", http_error_text(status));
    else if (!want[0] || strcmp(want, got) != 0)
        app_log("PS5SX2 GameIndex.yaml: damaged download");
    else if (util_mkdirs_parent(path) && util_write_file_atomic(path, out.data, out.len))
    {
        app_log("PS5SX2 GameIndex.yaml from %s installed (%zu bytes)", tag, out.len);
        result = 1;
    }
    else
        app_log("PS5SX2 GameIndex.yaml: can't write %s", path);
    http_buffer_free(&out);
    return result;
}
