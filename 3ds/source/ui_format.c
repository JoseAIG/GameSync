#include "ui_format.h"
#include <stdio.h>
#include <string.h>

void ui_format_size_str(uint32_t bytes, char *out, int out_size) {
    if (bytes >= 1024 * 1024)
        snprintf(out, out_size, "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(out, out_size, "%.1f KB", bytes / 1024.0);
    else
        snprintf(out, out_size, "%lu B", (unsigned long)bytes);
}

void ui_format_date_str(const char *iso, char *out, int out_size) {
    if (!iso) iso = "";
    if (strlen(iso) >= 16 && iso[10] == 'T')
        snprintf(out, out_size, "%.10s %.5s", iso, iso + 11);
    else if (iso[0])
        snprintf(out, out_size, "%.19s", iso);
    else
        snprintf(out, out_size, "-");
}

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's algorithm)
static int64_t days_from_civil(int64_t y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

// `n` digits at p as a number, or -1
static int digits(const char *p, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return -1;
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

bool ui_parse_timestamp(const char *s, int64_t *unix_out) {
    if (!s || !*s) return false;

    // All digits: Unix seconds
    size_t len = strlen(s);
    bool all_digits = len <= 12;
    for (size_t i = 0; i < len && all_digits; i++)
        if (s[i] < '0' || s[i] > '9') all_digits = false;
    if (all_digits) {
        int64_t v = 0;
        for (size_t i = 0; i < len; i++) v = v * 10 + (s[i] - '0');
        *unix_out = v;
        return true;
    }

    // YYYY-MM-DD[T ]HH:MM[:SS[.frac]] then Z / +HH:MM / -HHMM / +HH
    if (len < 16 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') || s[13] != ':')
        return false;
    int y = digits(s, 4), mo = digits(s + 5, 2), d = digits(s + 8, 2);
    int h = digits(s + 11, 2), mi = digits(s + 14, 2), sec = 0;
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59)
        return false;
    const char *p = s + 16;
    if (*p == ':') {
        sec = digits(p + 1, 2);
        if (sec < 0 || sec > 60) return false;
        p += 3;
        if (*p == '.' || *p == ',') {
            p++;
            while (*p >= '0' && *p <= '9') p++;
        }
    }
    int offset_s;
    if (*p == 'Z' || *p == 'z') {
        offset_s = 0;
        p++;
    } else if (*p == '+' || *p == '-') {
        int sign = *p == '-' ? -1 : 1;
        int oh = digits(p + 1, 2), om = 0;
        if (oh < 0 || oh > 23) return false;
        p += 3;
        if (*p == ':') p++;
        if (*p >= '0' && *p <= '9') {
            om = digits(p, 2);
            if (om < 0 || om > 59) return false;
            p += 2;
        }
        offset_s = sign * (oh * 3600 + om * 60);
    } else {
        return false;   // no zone: the instant isn't known
    }
    if (*p) return false;

    int64_t days = days_from_civil(y, mo, d);
    *unix_out = days * 86400 + h * 3600 + mi * 60 + sec - offset_s;
    return true;
}

static const char *const month_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

void ui_format_when_at(const char *iso, int utc_offset_minutes, int64_t now_local,
                       char *out, int out_size) {
    int64_t utc;
    if (!ui_parse_timestamp(iso, &utc)) {
        ui_format_date_str(iso, out, out_size);
        return;
    }
    int64_t local = utc + (int64_t)utc_offset_minutes * 60;
    int64_t day = floor_div(local, 86400);
    int64_t secs = local - day * 86400;
    int y, m, d;
    civil_from_days(day, &y, &m, &d);
    int hh = (int)(secs / 3600), mm = (int)(secs / 60 % 60);

    // Only trust the console clock if it's plausible: it has been seen
    // stuck in 1990, and a server time a day "in the future" means it's off
    bool clock_ok = false;
    int64_t today = 0;
    int now_y = 0;
    if (now_local != UI_NOW_UNKNOWN) {
        int nm, nd;
        today = floor_div(now_local, 86400);
        civil_from_days(today, &now_y, &nm, &nd);
        clock_ok = now_y >= 2024 && local <= now_local + 86400;
    }

    if (clock_ok && day == today)
        snprintf(out, out_size, "Today %02d:%02d", hh, mm);
    else if (clock_ok && day == today - 1)
        snprintf(out, out_size, "Yesterday %02d:%02d", hh, mm);
    else if (clock_ok && y == now_y)
        snprintf(out, out_size, "%s %d %02d:%02d", month_names[m - 1], d, hh, mm);
    else
        snprintf(out, out_size, "%s %d %d %02d:%02d", month_names[m - 1], d, y, hh, mm);
}

void ui_device_label_for(const char *console_id, const char *local_id, char *out, int out_size) {
    if (console_id && console_id[0] && local_id && local_id[0] && strcmp(console_id, local_id) == 0)
        snprintf(out, out_size, "This console");
    else if (console_id && console_id[0])
        snprintf(out, out_size, "Another device \xC2\xB7 %.4s", console_id);
    else
        snprintf(out, out_size, "Another device");
}

const char *ui_text_field_error(const char *text, bool url, bool allow_empty) {
    if (!text) text = "";
    if (!text[0]) return allow_empty ? NULL : "Enter a value";
    if (url && strncmp(text, "http://", 7) != 0 && strncmp(text, "https://", 8) != 0)
        return "Start with http:// or https://";
    return NULL;
}

int ui_utf8_copy(const char *src, char *out, int out_size) {
    if (out_size <= 0) return 0;
    if (!src) src = "";
    int n = (int)strlen(src);
    if (n > out_size - 1) {
        n = out_size - 1;
        // Back off continuation bytes (10xxxxxx) and the lead byte they follow
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(out, src, (size_t)n);
    out[n] = '\0';
    return n;
}

// Base letters of U+00C0..U+00FF ("" = not a letter: kept as is)
static const char *const latin1_fold[64] = {
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",      // C0-CF
    "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "ss",     // D0-DF
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",      // E0-EF
    "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "y",      // F0-FF
};

// Lower-case ASCII, Latin-1 letters folded to their base; any other UTF-8
// sequence is copied as is (so it still matches itself)
static void fold_name(const char *s, char *out, int out_size) {
    int pos = 0;
    while (s && *s && pos < out_size - 3) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            out[pos++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : (char)c;
            s++;
        } else if ((c == 0xC3) && ((unsigned char)s[1] & 0xC0) == 0x80) {
            unsigned cp = 0xC0 + ((unsigned char)s[1] & 0x3F);
            const char *base = latin1_fold[cp - 0xC0];
            if (base[0]) {
                for (const char *b = base; *b && pos < out_size - 1; b++) out[pos++] = *b;
            } else {
                out[pos++] = s[0];
                out[pos++] = s[1];
            }
            s += 2;
        } else {
            out[pos++] = *s++;
        }
    }
    out[pos] = '\0';
}

bool ui_name_matches(const char *name, const char *query) {
    if (!query || !query[0]) return true;
    char n[256], q[256];
    fold_name(name, n, sizeof(n));
    fold_name(query, q, sizeof(q));
    return strstr(n, q) != NULL;
}

bool ui_utf8_prefix(const char *s, int max_chars, char *out, int out_size) {
    if (!s) s = "";
    int i = 0, chars = 0;
    while (s[i] && chars < max_chars) {
        int len = 1;
        while (((unsigned char)s[i + len] & 0xC0) == 0x80) len++;
        if (i + len > out_size - 1) break;
        i += len;
        chars++;
    }
    ui_utf8_copy(s, out, i + 1);
    return s[i] != '\0';
}
