/*
 * Host tests for the PS5 client's pure logic: JSON, naming, URL parsing and
 * the three-way sync decision. Run through ps5/tests/run_host_tests.sh.
 */
#include "http.h"
#include "json.h"
#include "naming.h"
#include "sync.h"

#include <stdio.h>
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

int main(void)
{
    test_json();
    test_naming();
    test_url();
    test_decide();
    printf("%s (%d failures)\n", failures ? "FAILED" : "logic ok", failures);
    return failures ? 1 : 0;
}
