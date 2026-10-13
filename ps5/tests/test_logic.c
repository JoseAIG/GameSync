/*
 * Host tests for the PS5 client's pure logic: JSON, naming, URL parsing and
 * the three-way sync decision, plus PS5SX2's per-game cards on a temp folder.
 * Run through ps5/tests/run_host_tests.sh.
 */
#include "http.h"
#include "json.h"
#include "naming.h"
#include "ps2mc.h"
#include "saturn.h"
#include "saves.h"
#include "sync.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                  \
    do                                                            \
    {                                                             \
        if (!(c))                                                 \
        {                                                         \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);    \
            failures++;                                           \
        }                                                         \
    } while (0)

static void test_json(void)
{
    JsonValue *v = json_parse("{\"roms\":[{\"name\":\"A \\\"q\\\" \\u00e9\",\"size\":12345678901,"
                              "\"is_bundle\":true},{\"name\":\"B\"}],\"has_more\":false,"
                              "\"n\":null}");
    CHECK(v != NULL);
    JsonValue *roms = json_get(v, "roms");
    CHECK(json_count(roms) == 2);
    CHECK(strcmp(json_get_str(roms->child, "name", ""), "A \"q\" \xc3\xa9") == 0);
    CHECK(json_get_num(roms->child, "size", 0) == 12345678901.0);
    CHECK(json_get_bool(roms->child, "is_bundle", false));
    CHECK(!json_get_bool(v, "has_more", true));
    CHECK(json_get(v, "n") && json_get(v, "n")->type == JSON_NULL);
    json_free(v);
    CHECK(json_parse("{\"a\":}") == NULL);
    CHECK(json_parse("[1,2") == NULL);

    StrBuf sb;
    sb_init(&sb);
    sb_append_json_str(&sb, "a\"b\\c\n");
    CHECK(strcmp(sb.data, "\"a\\\"b\\\\c\\n\"") == 0);
    sb_free(&sb);
}

static void test_naming(void)
{
    char s[64];
    CHECK(swanstation_card_serial("SLUS-01041_1.mcd", s, sizeof(s)) && strcmp(s, "SLUS01041") == 0);
    CHECK(swanstation_card_serial("slus-01041_1.mcd", s, sizeof(s)) && strcmp(s, "SLUS01041") == 0);
    CHECK(!swanstation_card_serial("Some Game_1.mcd", s, sizeof(s)));
    CHECK(!swanstation_card_serial("SLUS-01041.mcd", s, sizeof(s)));
    swanstation_card_name("SCES00344", s, sizeof(s));
    CHECK(strcmp(s, "SCES-00344_1.mcd") == 0);

    CHECK(strcmp(system_from_folder("SNES"), "SNES") == 0);
    CHECK(strcmp(system_from_folder("megadrive"), "MD") == 0);
    CHECK(strcmp(system_from_folder("Saturn"), "SAT") == 0);
    CHECK(system_from_folder("roms") == NULL);
    CHECK(strcmp(retroarch_folder_for_system("MD"), "genesis") == 0);
    CHECK(retroarch_syncs_system("GBA") && !retroarch_syncs_system("PS2"));
    CHECK(retroarch_syncs_system("SAT")); /* Beetle Saturn .bkr */
    CHECK(install_emulator_for_system("PS2", false) == EMU_PS5SX2);
    CHECK(install_emulator_for_system("PS1", false) == EMU_SWANSTATION);
    CHECK(install_emulator_for_system("PS1", true) == EMU_RETROARCH);
    CHECK(install_emulator_for_system("XBOX", false) == EMU_XEMU);
    CHECK(install_emulator_for_system("WIIU", false) == EMU_CEMU);
    CHECK(install_emulator_for_system("3DS", false) == EMU_CEMU);
    CHECK(install_emulator_for_system("NSW", false) == EMU_EDEN);
    CHECK(install_emulator_for_system("X360", false) == EMU_NONE);

    CHECK(is_launchable_rom("Sonic (USA).bin", "MD"));
    CHECK(!is_launchable_rom("Track 01.bin", "PS1"));
    CHECK(is_launchable_rom("Game.cue", "PS1"));
    CHECK(!is_launchable_rom("Game.srm", "SNES"));
    CHECK(!is_launchable_rom(".hidden.sfc", "SNES"));
}

static void test_url(void)
{
    HttpServer s;
    memset(&s, 0, sizeof(s));
    CHECK(http_server_set_url(&s, "http://192.168.1.10:8000/"));
    CHECK(strcmp(s.host, "192.168.1.10") == 0 && s.port == 8000 && s.base_path[0] == 0);
    CHECK(http_server_set_url(&s, "10.0.0.2/gamesync"));
    CHECK(s.port == 80 && strcmp(s.base_path, "/gamesync") == 0);
    CHECK(!http_server_set_url(&s, "https://10.0.0.2"));
    CHECK(!http_server_set_url(&s, "http://:8000"));
}

static SaveStatus decide(bool local, const char *lh, bool server, const char *sh, const char *last)
{
    SaveEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.title_id, sizeof(e.title_id), "SLUS01041");
    e.local = local;
    e.server = server;
    snprintf(e.local_hash, sizeof(e.local_hash), "%s", lh);
    snprintf(e.server_hash, sizeof(e.server_hash), "%s", sh);
    return sync_decide(&e, last);
}

static void test_decide(void)
{
    CHECK(decide(true, "a", true, "a", NULL) == ST_SYNCED);
    CHECK(decide(true, "a", false, "", NULL) == ST_LOCAL_ONLY);
    CHECK(decide(false, "", true, "b", NULL) == ST_SERVER_ONLY);
    CHECK(decide(false, "", false, "", NULL) == ST_NONE);
    CHECK(decide(true, "b", true, "a", "a") == ST_UPLOAD);
    CHECK(decide(true, "a", true, "b", "a") == ST_DOWNLOAD);
    CHECK(decide(true, "b", true, "c", "a") == ST_CONFLICT);
    /* never synced and different: the user picks, nothing is overwritten */
    CHECK(decide(true, "a", true, "b", NULL) == ST_CONFLICT);
    CHECK(decide(true, "a", true, "b", "") == ST_CONFLICT);
}

/* A card holding one folder with one file. */
static bool make_card(const char *path, const char *folder)
{
    Ps2CardFile f;
    memset(&f, 0, sizeof(f));
    f.ecc = true;
    Ps2Dir dir;
    memset(&dir, 0, sizeof(dir));
    snprintf(dir.name, sizeof(dir.name), "%s", folder);
    dir.files = calloc(1, sizeof(Ps2File));
    dir.file_count = 1;
    snprintf(dir.files[0].name, sizeof(dir.files[0].name), "%s", folder);
    dir.files[0].size = 3000;
    dir.files[0].data = malloc(3000);
    memset(dir.files[0].data, 0x5A, 3000);
    char error[96];
    bool ok = ps2mc_put_dir(&f.card, &dir) && ps2mc_save_file(path, &f, 1, error, sizeof(error));
    ps2mc_free_dir(&dir);
    ps2mc_free(&f.card);
    return ok;
}

static bool card_has(const char *path, const char *folder)
{
    Ps2CardFile f;
    char error[96];
    if (!ps2mc_load_file(path, &f, error, sizeof(error)))
        return false;
    bool found = ps2mc_find_dir(&f.card, folder) >= 0;
    ps2mc_free(&f.card);
    return found;
}

static void test_ps5sx2_cards(void)
{
    char root[] = "/tmp/gs_ps5sx2_XXXXXX";
    if (!mkdtemp(root))
    {
        CHECK(!"mkdtemp");
        return;
    }
    Config c;
    config_defaults(&c);
    snprintf(c.ps5sx2_dir, sizeof(c.ps5sx2_dir), "%s", root);
    char path[PATH_LEN], ini[PATH_LEN], shared[PATH_LEN];
    snprintf(path, sizeof(path), "%s/memcards", root);
    util_mkdirs(path);
    snprintf(path, sizeof(path), "%s/settings", root);
    util_mkdirs(path);
    snprintf(shared, sizeof(shared), "%s/memcards/Mcd001.ps2", root);
    CHECK(make_card(shared, "BASLUS-20963"));
    char before[65], after[65];
    util_sha256_file(shared, before);

    /* the game's settings keep their lines; the shared card is replaced */
    snprintf(ini, sizeof(ini), "%s/settings/Final Fantasy X (USA).ini", root);
    const char *old = "upscale_multiplier=2\nMemoryCards/Slot1_Filename = Mcd001.ps2\n";
    util_write_file_atomic(ini, old, strlen(old));
    CHECK(saves_ps5sx2_setup_game(&c, "Final Fantasy X (USA)", "SLUS-20963"));
    char *text = (char *)util_read_file(ini, NULL);
    CHECK(text && strcmp(text, "upscale_multiplier=2\nMemoryCards/Slot1_Filename=Final Fantasy X (USA).ps2\n"
                                  "MemoryCards/Slot1_Enable=true\n") == 0);
    free(text);
    char own[PATH_LEN];
    snprintf(own, sizeof(own), "%s/memcards/Final Fantasy X (USA).ps2", root);
    CHECK(util_file_size(own) == PS2MC_PS2_SIZE);
    CHECK(card_has(own, "BASLUS-20963"));
    util_sha256_file(shared, after);
    CHECK(strcmp(before, after) == 0);
    /* nothing left to do the second time */
    CHECK(!saves_ps5sx2_setup_game(&c, "Final Fantasy X (USA)", "SLUS-20963"));

    /* the shelf's file with the slot off and no last newline: no save
     * anywhere, so a blank card, and the slot back on */
    snprintf(ini, sizeof(ini), "%s/settings/Okami (USA).ini", root);
    const char *off = "# Okami (SLUS-21115): written from the PS5SX2 shelf\nMemoryCards/Slot1_Enable=false";
    util_write_file_atomic(ini, off, strlen(off));
    CHECK(saves_ps5sx2_setup_game(&c, "Okami (USA)", "SLUS-21115"));
    text = (char *)util_read_file(ini, NULL);
    CHECK(text && strcmp(text, "# Okami (SLUS-21115): written from the PS5SX2 shelf\n"
                               "MemoryCards/Slot1_Enable=true\n"
                               "MemoryCards/Slot1_Filename=Okami (USA).ps2\n") == 0);
    free(text);
    snprintf(own, sizeof(own), "%s/memcards/Okami (USA).ps2", root);
    CHECK(util_file_size(own) == PS2MC_PS2_SIZE && !card_has(own, "BASLUS-21115"));

    /* settings only: the card folder doesn't exist yet and is made */
    snprintf(path, sizeof(path), "%s/fresh", root);
    util_mkdirs(path);
    Config fresh = c;
    snprintf(fresh.ps5sx2_dir, sizeof(fresh.ps5sx2_dir), "%s", path);
    CHECK(saves_ps5sx2_setup_game(&fresh, "Ico (USA)", "SCUS-97113"));
    snprintf(own, sizeof(own), "%s/memcards/Ico (USA).ps2", path);
    CHECK(util_file_size(own) == PS2MC_PS2_SIZE);
    /* cards kept in the PCSX2 folder itself (no memcards/): used there */
    snprintf(path, sizeof(path), "%s/rootcards", root);
    util_mkdirs(path);
    snprintf(shared, sizeof(shared), "%s/Mcd001.ps2", path);
    CHECK(make_card(shared, "BASLUS-20963"));
    snprintf(fresh.ps5sx2_dir, sizeof(fresh.ps5sx2_dir), "%s", path);
    CHECK(saves_ps5sx2_setup_game(&fresh, "Final Fantasy X (USA)", "SLUS20963"));
    snprintf(own, sizeof(own), "%s/Final Fantasy X (USA).ps2", path);
    CHECK(card_has(own, "BASLUS-20963"));

    /* a card the player picked stays */
    snprintf(ini, sizeof(ini), "%s/settings/Ico (USA).ini", root);
    const char *mine = "MemoryCards/Slot1_Filename=Card 1.ps2";
    util_write_file_atomic(ini, mine, strlen(mine));
    CHECK(saves_ps5sx2_game_card(&c, "Ico (USA)", true, path, sizeof(path)));
    CHECK(util_has_suffix_i(path, "/memcards/Card 1.ps2"));

    /* PS5SX2's serial cache, as the console has it (no cache/ folder) */
    char game[PATH_LEN], cache[PATH_LEN], line[PATH_LEN + 64], serial[32], stem[200];
    snprintf(game, sizeof(game), "%s/games/Star Ocean - Till the End of Time (USA).chd", root);
    util_mkdirs_parent(game);
    util_write_file_atomic(game, "x", 1);
    snprintf(cache, sizeof(cache), "%s/chd-serials.txt", root);
    snprintf(line, sizeof(line), "/elsewhere/Other.chd\t1\t2\tSLUS-99999\n%s\t3915421814\t1791908768\tSLUS-20488\n",
             game);
    util_write_file_atomic(cache, line, strlen(line));
    CHECK(saves_ps5sx2_serial(&c, game, serial, sizeof(serial)) && strcmp(serial, "SLUS20488") == 0);
    CHECK(saves_ps5sx2_game_for_serial(&c, "SLUS20488", stem, sizeof(stem)) &&
          strcmp(stem, "Star Ocean - Till the End of Time (USA)") == 0);
    CHECK(!saves_ps5sx2_game_for_serial(&c, "SLUS99999", stem, sizeof(stem))); /* file not here */
    CHECK(!saves_ps5sx2_serial(&c, "/nope.chd", serial, sizeof(serial)));

    /* the scan prefers the game's own card over the shared copy */
    SaveList list = {0};
    KvStore ids = {0}, installed = {0};
    c.retroarch_dir[0] = c.swanstation_dir[0] = c.porpoise_dir[0] = c.eden_dir[0] = 0;
    c.cemu_dir[0] = c.xemu_dir[0] = 0;
    saves_scan(&c, &ids, &installed, &list);
    SaveEntry *e = saves_find(&list, "SLUS20963");
    CHECK(e && util_has_suffix_i(e->path, "Final Fantasy X (USA).ps2"));
    CHECK(!list.warning[0]);
    saves_free(&list);

    char cmd[PATH_LEN + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

/* sync_ps5sx2_game_index against game_index_fixture.py on url, serving body. */
static void test_game_index(const char *url, const char *body_file)
{
    char root[] = "/tmp/gs_gameindex_XXXXXX";
    if (!mkdtemp(root))
    {
        CHECK(!"mkdtemp");
        return;
    }
    SyncContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    config_defaults(&ctx.config);
    snprintf(ctx.config.server_url, sizeof(ctx.config.server_url), "%s", url);
    snprintf(ctx.config.api_key, sizeof(ctx.config.api_key), "key");
    char dir[PATH_LEN], file[PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/PCSX2", root);
    snprintf(ctx.config.ps5sx2_dir, sizeof(ctx.config.ps5sx2_dir), "%s", dir);
    config_apply(&ctx.config, &ctx.server);
    snprintf(file, sizeof(file), "%s/resources/GameIndex.yaml", dir);

    CHECK(sync_ps5sx2_game_index(&ctx) == 0); /* no PS5SX2 here */
    util_mkdirs(dir);
    CHECK(sync_ps5sx2_game_index(&ctx) == 1);
    char want[65], got[65];
    CHECK(util_sha256_file(body_file, want) && util_sha256_file(file, got) && strcmp(want, got) == 0);
    CHECK(sync_ps5sx2_game_index(&ctx) == 0); /* there: PS5SX2's own stays */

    /* a download that doesn't match its hash is not written */
    remove(file);
    snprintf(ctx.config.api_key, sizeof(ctx.config.api_key), "bad");
    config_apply(&ctx.config, &ctx.server);
    CHECK(sync_ps5sx2_game_index(&ctx) == -1);
    CHECK(!util_file_exists(file));

    char cmd[PATH_LEN + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

/* A Saturn sector 0 (IP.BIN) with product code `code`, as an .iso (2048-byte
 * sectors) or a raw 2352-byte MODE1/MODE2 sector. */
static void write_saturn_disc(const char *path, const char *code, int mode)
{
    unsigned char sector[2352];
    memset(sector, 0, sizeof(sector));
    size_t data = 0;
    if (mode)
    {
        memset(sector + 1, 0xFF, 10);
        sector[15] = (unsigned char)mode;
        data = mode == 2 ? 0x18 : 0x10;
    }
    memcpy(sector + data, "SEGA SEGASATURN ", 16);
    memcpy(sector + data + 0x10, "SEGA ENTERPRISES", 16);
    memset(sector + data + 0x20, ' ', 10);
    memcpy(sector + data + 0x20, code, strlen(code) < 10 ? strlen(code) : 10);
    memcpy(sector + data + 0x2A, "V1.003", 6);
    util_write_file_atomic(path, sector, mode ? 2352 : 2048);
}

static void test_saturn(void)
{
    char out[64];
    CHECK(saturn_clean_product_code("T-4507G   ", out, sizeof(out)) && strcmp(out, "T-4507G") == 0);
    CHECK(saturn_clean_product_code("T-4507G-1 ", out, sizeof(out)) && strcmp(out, "T-4507G") == 0);
    CHECK(saturn_clean_product_code("MK-81307 V", out, sizeof(out)) && strcmp(out, "MK-81307") == 0);
    CHECK(saturn_clean_product_code("81014     ", out, sizeof(out)) && strcmp(out, "81014") == 0);

    char root[] = "/tmp/gs_saturn_XXXXXX";
    if (!mkdtemp(root))
    {
        CHECK(!"mkdtemp");
        return;
    }
    char iso[PATH_LEN], bin[PATH_LEN], cue[PATH_LEN], m2[PATH_LEN], chd[PATH_LEN];
    snprintf(iso, sizeof(iso), "%s/Radiant Silvergun (Japan).iso", root);
    write_saturn_disc(iso, "T-32902G", 0);
    CHECK(saturn_title_id(iso, out, sizeof(out)) && strcmp(out, "SAT_T-32902G") == 0);
    /* a translation keeps the original's IP.BIN: the file name doesn't matter */
    snprintf(bin, sizeof(bin), "%s/Grandia (Japan) (Disc 2) [T-En by X v1.1] (Track 01).bin", root);
    write_saturn_disc(bin, "T-4507G-1", 1);
    snprintf(cue, sizeof(cue), "%s/Grandia (Japan) (Disc 2) [T-En by X v1.1].cue", root);
    const char *sheet = "FILE \"Grandia (Japan) (Disc 2) [T-En by X v1.1] (Track 01).bin\" BINARY\r\n"
                        "  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n";
    util_write_file_atomic(cue, sheet, strlen(sheet));
    CHECK(saturn_title_id(cue, out, sizeof(out)) && strcmp(out, "SAT_T-4507G") == 0);
    snprintf(m2, sizeof(m2), "%s/mode2.bin", root);
    write_saturn_disc(m2, "GS-9003", 2);
    CHECK(saturn_title_id(m2, out, sizeof(out)) && strcmp(out, "SAT_GS-9003") == 0);
    /* not a Saturn disc, and a CHD (compressed): no id */
    snprintf(chd, sizeof(chd), "%s/game.chd", root);
    util_write_file_atomic(chd, "MComprHD", 8);
    CHECK(!saturn_title_id(chd, out, sizeof(out)));
    util_write_file_atomic(iso, "not a disc", 10);
    CHECK(!saturn_title_id(iso, out, sizeof(out)));

    char cmd[PATH_LEN + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

int main(int argc, char **argv)
{
    test_saturn();
    if (argc == 3)
        test_game_index(argv[1], argv[2]);
    test_json();
    test_naming();
    test_url();
    test_decide();
    test_ps5sx2_cards();
    printf("%s (%d failures)\n", failures ? "FAILED" : "logic ok", failures);
    return failures ? 1 : 0;
}
