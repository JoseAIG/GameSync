// Host test for config_file.c: every key survives load -> save -> load
#include "config_file.h"
#include "test_util.h"

static void parse_text(AppConfig *c, const char *text) {
    FILE *f = tmpfile();
    fputs(text, f);
    rewind(f);
    config_set_defaults(c);
    config_parse_stream(c, f);
    fclose(f);
}

static void round_trip(const AppConfig *in, AppConfig *out) {
    FILE *f = tmpfile();
    CHECK(config_write_stream(in, f));
    rewind(f);
    config_set_defaults(out);
    config_parse_stream(out, f);
    fclose(f);
}

static void check_same(const AppConfig *a, const AppConfig *b) {
    CHECK_STR(a->server_url, b->server_url);
    CHECK_STR(a->server_url_alt, b->server_url_alt);
    CHECK_STR(a->api_key, b->api_key);
    CHECK_STR(a->nds_dir, b->nds_dir);
    CHECK(a->utc_offset_minutes == b->utc_offset_minutes);
    CHECK(a->local_backups == b->local_backups);
    CHECK(a->tls_verify == b->tls_verify);
    CHECK(a->update_check == b->update_check);
    CHECK(a->reduce_motion == b->reduce_motion);
}

static void test_defaults(void) {
    AppConfig c;
    parse_text(&c, "server_url=http://192.168.1.100:8000\napi_key=k\n");
    CHECK(c.local_backups == 3);
    CHECK(c.tls_verify);
    CHECK(c.update_check);
    CHECK(!c.reduce_motion);
    CHECK(c.utc_offset_minutes == 0);
    CHECK(c.server_url_alt[0] == '\0');
}

static void test_every_key_round_trips(void) {
    // A config with every fork key set away from its default
    const char *text =
        "\xEF\xBB\xBF# comment\r\n"
        "server_url = http://192.168.1.100:8000 \r\n"
        "server_url_alt=http://10.0.0.2:8000\n"
        "api_key=secret-key\n"
        "nds_dir=sdmc:/roms/nds\n"
        "utc_offset_minutes=-240\n"
        "local_backups=7\n"
        "tls_verify=0\n"
        "update_check=0\n"
        "reduce_motion=1\n"
        "unknown_key=ignored\n";
    AppConfig a, b, c;
    parse_text(&a, text);
    CHECK_STR(a.server_url, "http://192.168.1.100:8000");
    CHECK_STR(a.server_url_alt, "http://10.0.0.2:8000");
    CHECK_STR(a.api_key, "secret-key");
    CHECK_STR(a.nds_dir, "sdmc:/roms/nds");
    CHECK(a.utc_offset_minutes == -240);
    CHECK(a.local_backups == 7);
    CHECK(!a.tls_verify);
    CHECK(!a.update_check);
    CHECK(a.reduce_motion);

    round_trip(&a, &b);   // load -> save -> load
    check_same(&a, &b);
    round_trip(&b, &c);   // and once more: stable
    check_same(&b, &c);

    // The defaults (and the switches back on) round-trip too
    AppConfig d, e;
    parse_text(&d, "server_url=http://x\napi_key=k\nreduce_motion=0\nlocal_backups=0\n");
    round_trip(&d, &e);
    check_same(&d, &e);
    CHECK(e.local_backups == 0);
    CHECK(e.tls_verify && e.update_check && !e.reduce_motion);
}

static void test_written_keys(void) {
    AppConfig a;
    parse_text(&a, "server_url=http://x\napi_key=k\nreduce_motion=1\n");
    FILE *f = tmpfile();
    CHECK(config_write_stream(&a, f));
    rewind(f);
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CHECK(strstr(buf, "reduce_motion=1\n") != NULL);
    CHECK(strstr(buf, "local_backups=3\n") != NULL);
    CHECK(strstr(buf, "tls_verify=1\n") != NULL);
    CHECK(strstr(buf, "update_check=1\n") != NULL);
    CHECK(strstr(buf, "utc_offset_minutes=0\n") != NULL);
}

static void test_booleans(void) {
    AppConfig c;
    // Words, any case
    parse_text(&c, "tls_verify=true\nupdate_check=Yes\nreduce_motion=ON\n");
    CHECK(c.tls_verify && c.update_check && c.reduce_motion);
    parse_text(&c, "tls_verify=false\nupdate_check=no\nreduce_motion=Off\n");
    CHECK(!c.tls_verify && !c.update_check && !c.reduce_motion);
    // Anything else keeps the default: a typo never turns certificate
    // checks off
    parse_text(&c, "tls_verify=ture\nupdate_check=\nreduce_motion=2\n");
    CHECK(c.tls_verify && c.update_check && !c.reduce_motion);
}

static void test_clamps(void) {
    AppConfig c;
    parse_text(&c, "local_backups=99\nutc_offset_minutes=9999\n");
    CHECK(c.local_backups == 50);
    CHECK(c.utc_offset_minutes == 14 * 60);
    parse_text(&c, "local_backups=-3\nutc_offset_minutes=-9999\n");
    CHECK(c.local_backups == 0);
    CHECK(c.utc_offset_minutes == -14 * 60);
}

int main(void) {
    test_booleans();
    test_defaults();
    test_every_key_round_trips();
    test_written_keys();
    test_clamps();
    TEST_DONE();
}
