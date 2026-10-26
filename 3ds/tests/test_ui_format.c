// Host test for ui_format.c (sizes, timestamps, "Today 21:14", device labels)
#include "ui_format.h"
#include "test_util.h"

static void test_parse(void) {
    int64_t t = 0;
    CHECK(ui_parse_timestamp("2026-01-02T03:04:05.123456+00:00", &t) && t == 1767323045);
    CHECK(ui_parse_timestamp("2026-01-02T03:04:05Z", &t) && t == 1767323045);
    CHECK(ui_parse_timestamp("2026-01-02T05:04:05+02:00", &t) && t == 1767323045);
    CHECK(ui_parse_timestamp("2025-12-31T22:04:05-0500", &t) && t == 1767236645);
    CHECK(ui_parse_timestamp("2026-01-02 03:04Z", &t) && t == 1767323040);
    CHECK(ui_parse_timestamp("2024-02-29T23:59:59+00:00", &t) && t == 1709251199);
    CHECK(ui_parse_timestamp("1970-01-01T00:00:00Z", &t) && t == 0);
    // History lists Unix seconds
    CHECK(ui_parse_timestamp("1767323045", &t) && t == 1767323045);
    // No zone, or not a date: unknown instant
    CHECK(!ui_parse_timestamp("2026-01-02T03:04:05", &t));
    CHECK(!ui_parse_timestamp("2026-01-02T03:04:05+", &t));
    CHECK(!ui_parse_timestamp("2026-13-02T03:04:05Z", &t));
    CHECK(!ui_parse_timestamp("yesterday", &t));
    CHECK(!ui_parse_timestamp("", &t));
    CHECK(!ui_parse_timestamp(NULL, &t));
    CHECK(!ui_parse_timestamp("2026-01-02T03:04:05Zjunk", &t));
}

static void test_when(void) {
    char out[48];
    // Console clock: 2026-10-26 08:00 local, UTC-4 (12:00 UTC)
    const int64_t now = 1793016000 - 4 * 3600;
    const int off = -240;

    ui_format_when_at("2026-10-26T11:30:00+00:00", off, now, out, sizeof(out));
    CHECK_STR(out, "Today 07:30");
    // 02:30 UTC on the 26th is 22:30 on the 25th in UTC-4
    ui_format_when_at("2026-10-26T02:30:00Z", off, now, out, sizeof(out));
    CHECK_STR(out, "Yesterday 22:30");
    ui_format_when_at("2026-10-12T21:14:00-04:00", off, now, out, sizeof(out));
    CHECK_STR(out, "Oct 12 21:14");
    ui_format_when_at("2024-10-12T21:14:00-04:00", off, now, out, sizeof(out));
    CHECK_STR(out, "Oct 12 2024 21:14");
    // Unix seconds (History) the same way: 2026-10-26 11:30 UTC
    ui_format_when_at("1793014200", off, now, out, sizeof(out));
    CHECK_STR(out, "Today 07:30");
    // Later today (still in the past for the server, an hour ahead here) is fine
    ui_format_when_at("2026-10-26T13:00:00Z", off, now, out, sizeof(out));
    CHECK_STR(out, "Today 09:00");

    // A console clock stuck in 1990: no relative words, the year is shown
    const int64_t y1990 = 631152000;   // 1990-01-01
    ui_format_when_at("2026-10-26T11:30:00Z", off, y1990, out, sizeof(out));
    CHECK_STR(out, "Oct 26 2026 07:30");
    // Server time more than a day ahead of the console: the clock is wrong
    ui_format_when_at("2026-10-28T11:30:00Z", off, now, out, sizeof(out));
    CHECK_STR(out, "Oct 28 2026 07:30");
    ui_format_when_at("2026-10-26T11:30:00Z", 0, UI_NOW_UNKNOWN, out, sizeof(out));
    CHECK_STR(out, "Oct 26 2026 11:30");

    // Fallback: v055's absolute form
    ui_format_when_at("2026-10-12T21:14:00", off, now, out, sizeof(out));
    CHECK_STR(out, "2026-10-12 21:14");
    ui_format_when_at("", off, now, out, sizeof(out));
    CHECK_STR(out, "-");
}

static void test_labels(void) {
    char out[48];
    ui_device_label_for("1A2B3C4D5E6F7788", "1A2B3C4D5E6F7788", out, sizeof(out));
    CHECK_STR(out, "This console");
    ui_device_label_for("99AA3C4D5E6F7788", "1A2B3C4D5E6F7788", out, sizeof(out));
    CHECK_STR(out, "Another device \xC2\xB7 99AA");
    ui_device_label_for("", "1A2B3C4D5E6F7788", out, sizeof(out));
    CHECK_STR(out, "Another device");

    ui_format_size_str(512, out, sizeof(out));
    CHECK_STR(out, "512 B");
    ui_format_size_str(12 * 1024, out, sizeof(out));
    CHECK_STR(out, "12.0 KB");
    ui_format_size_str(3 * 1024 * 1024 / 2, out, sizeof(out));
    CHECK_STR(out, "1.5 MB");
    ui_format_date_str("2026-10-12T21:14:03.5+00:00", out, sizeof(out));
    CHECK_STR(out, "2026-10-12 21:14");
}

static void test_text_entry(void) {
    CHECK(ui_text_field_error("http://192.168.1.100:8000", true, false) == NULL);
    CHECK(ui_text_field_error("https://example.invalid", true, false) == NULL);
    CHECK_STR(ui_text_field_error("192.168.1.100:8000", true, false), "Start with http:// or https://");
    CHECK_STR(ui_text_field_error("ftp://x", true, true), "Start with http:// or https://");
    CHECK_STR(ui_text_field_error("http:/x", true, true), "Start with http:// or https://");
    // Empty: only when allowed (an empty alternate URL turns it off)
    CHECK(ui_text_field_error("", true, true) == NULL);
    CHECK(ui_text_field_error("", true, false) != NULL);
    CHECK(ui_text_field_error("", false, false) != NULL);
    CHECK(ui_text_field_error(NULL, false, true) == NULL);
    CHECK(ui_text_field_error("anything", false, false) == NULL);

    char out[8];
    CHECK(ui_utf8_copy("abc", out, sizeof(out)) == 3);
    CHECK_STR(out, "abc");
    CHECK(ui_utf8_copy("abcdefghij", out, sizeof(out)) == 7);
    CHECK_STR(out, "abcdefg");
    // "abcde" + U+00E9 (2 bytes) + "f": the cut would split the e-acute
    CHECK(ui_utf8_copy("abcdef\xC3\xA9g", out, sizeof(out)) == 6);
    CHECK_STR(out, "abcdef");
    CHECK(ui_utf8_copy("abcde\xC3\xA9g", out, sizeof(out)) == 7);
    CHECK_STR(out, "abcde\xC3\xA9");
    // A 3-byte character cut after its first or second byte
    CHECK(ui_utf8_copy("abcde\xE2\x80\xA6", out, sizeof(out)) == 5);
    CHECK_STR(out, "abcde");
    CHECK(ui_utf8_copy("abcdef\xE2\x80\xA6", out, sizeof(out)) == 6);
    CHECK(ui_utf8_copy(NULL, out, sizeof(out)) == 0 && out[0] == '\0');
}

static void test_search(void) {
    CHECK(ui_name_matches("The Legend of Zelda", "zel"));
    CHECK(ui_name_matches("The Legend of Zelda", "ZELDA"));
    CHECK(ui_name_matches("The Legend of Zelda", ""));
    CHECK(ui_name_matches("The Legend of Zelda", NULL));
    CHECK(!ui_name_matches("The Legend of Zelda", "mario"));
    // Accents fold both ways
    CHECK(ui_name_matches("Pok\xC3\xA9mon Y", "pokemon"));
    CHECK(ui_name_matches("Pokemon Y", "Pok\xC3\xA9"));
    CHECK(ui_name_matches("\xC3\x89TRANGE", "etrange"));
    CHECK(ui_name_matches("Stra\xC3\x9F" "e", "strasse"));
    CHECK(ui_name_matches("\xC3\x86on", "aeon"));
    CHECK(ui_name_matches("Ni\xC3\xB1o", "nino"));
    // Other UTF-8 still matches itself
    CHECK(ui_name_matches("\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3", "\xE3\x82\xB1"));
    CHECK(!ui_name_matches("abc", "abcd"));
    CHECK(!ui_name_matches(NULL, "a"));

    char out[32];
    CHECK(!ui_utf8_prefix("zel", 10, out, sizeof(out)));
    CHECK_STR(out, "zel");
    CHECK(ui_utf8_prefix("abcdefghijkl", 10, out, sizeof(out)));
    CHECK_STR(out, "abcdefghij");
    CHECK(!ui_utf8_prefix("abcdefghij", 10, out, sizeof(out)));
    // Characters, not bytes
    CHECK(ui_utf8_prefix("\xC3\xA9\xC3\xA9\xC3\xA9", 2, out, sizeof(out)));
    CHECK_STR(out, "\xC3\xA9\xC3\xA9");
    char small[4];
    CHECK(ui_utf8_prefix("\xC3\xA9\xC3\xA9\xC3\xA9", 10, small, sizeof(small)));
    CHECK_STR(small, "\xC3\xA9");
}

int main(void) {
    test_search();
    test_text_entry();
    test_parse();
    test_when();
    test_labels();
    TEST_DONE();
}
