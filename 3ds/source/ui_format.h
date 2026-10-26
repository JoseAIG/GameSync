#ifndef UI_FORMAT_H
#define UI_FORMAT_H

// Text formatting for the screens: sizes, dates, device names. Plain C with
// no libctru dependency (host-tested in tests/test_ui_format.c); ui.h has
// the wrappers that know the console's clock and id.

#include <stdbool.h>
#include <stdint.h>

// "512 B", "12.0 KB", "1.5 MB"
void ui_format_size_str(uint32_t bytes, char *out, int out_size);

// v055's absolute form: ISO 8601 "YYYY-MM-DDTHH:MM..." -> "YYYY-MM-DD HH:MM"
// (other text verbatim, "" -> "-")
void ui_format_date_str(const char *iso, char *out, int out_size);

// A server timestamp as Unix seconds (UTC): ISO 8601 with a zone designator
// ("...Z", "...+02:00", "...-0500"), or all digits (Unix seconds, as History
// lists them). False when it can't be parsed or has no zone: the instant is
// then unknown.
bool ui_parse_timestamp(const char *s, int64_t *unix_out);

#define UI_NOW_UNKNOWN INT64_MIN

// "Today 21:14", "Yesterday 21:14", "Oct 12 21:14" (the year is added when
// it isn't the console's current one), in the console's local time:
// UTC + utc_offset_minutes. `now_local` is the console clock as seconds since
// 1970 in local time (osGetTime() based), or UI_NOW_UNKNOWN. A console clock
// before 2024, or a time more than a day ahead of it, means the clock is
// wrong: then no relative word is used and the year is always shown.
// Falls back to ui_format_date_str() for text it can't parse.
void ui_format_when_at(const char *iso, int utc_offset_minutes, int64_t now_local,
                       char *out, int out_size);

// "This console" when `console_id` is `local_id`, else "Another device ·
// XXXX" (its first 4 characters), or "Another device" without an id
void ui_device_label_for(const char *console_id, const char *local_id, char *out, int out_size);

#endif // UI_FORMAT_H
