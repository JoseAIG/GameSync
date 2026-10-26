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

int main(void) {
    test_parse();
    test_when();
    test_labels();
    TEST_DONE();
}
