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

// An HTTP Date header ("Sun, 06 Nov 1994 08:49:37 GMT") as Unix seconds.
// False for any other form.
bool ui_parse_http_date(const char *s, int64_t *unix_out);

// The console clock's offset from UTC, in minutes, from one reading of both
// clocks (`console_local` = console clock as seconds since 1970, as if UTC;
// `server_utc` = the server's time): rounded to the nearest quarter hour.
// False when it isn't a real zone (-12:00 .. +14:00): the console clock is
// then simply wrong, and no offset fixes it.
bool ui_clock_offset_minutes(int64_t console_local, int64_t server_utc, int *minutes_out);

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

// How long ago, from a monotonic delta: "just now" (under a minute),
// "1 min ago", "12 min ago", "3 h ago", "2 days ago"
void ui_format_ago(uint64_t ms, char *out, int out_size);

// ---------------------------------------------------------------------------
// Text entry
// ---------------------------------------------------------------------------

// Why `text` can't be accepted by a text field, or NULL when it can: empty
// text needs `allow_empty`; a URL field takes only text starting with
// "http://" or "https://" (empty is then judged by `allow_empty` alone).
const char *ui_text_field_error(const char *text, bool url, bool allow_empty);

// Copy UTF-8 `src` into `out` (out_size bytes with the NUL), cut at a
// character boundary when it doesn't fit, so no half character is left.
// Returns the bytes copied.
int ui_utf8_copy(const char *src, char *out, int out_size);

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

// Does `name` contain `query`? Case-insensitive, and Latin-1 accents fold
// to their base letter (U+00C0-U+00FF: "Pokemon" finds "Pok\xC3\xA9mon",
// "AE" finds the ligature). An empty query matches everything.
bool ui_name_matches(const char *name, const char *query);

// The first `max_chars` UTF-8 characters of `s` into `out`; returns true
// when `s` was longer (the caller adds an ellipsis)
bool ui_utf8_prefix(const char *s, int max_chars, char *out, int out_size);

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// Local backups as a value: "Off", "1 copy", "3 copies"
void ui_format_copies(int n, char *out, int out_size);

// The Local backups chooser: Off, 1, 3, 5 and 10 copies. A `current` value
// that isn't one of them (config.txt takes 0-50) is inserted in order as
// "7 copies (current)", so opening and closing the chooser never changes
// it. Fills values[] and labels[] and returns the count; *initial = the
// index of `current`.
#define UI_BACKUP_CHOICES_MAX 6
#define UI_BACKUP_LABEL_LEN 32
int ui_backup_choices(int current, int *values, char labels[][UI_BACKUP_LABEL_LEN], int *initial);

#endif // UI_FORMAT_H
