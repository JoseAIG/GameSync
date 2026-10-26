#include "history_parse.h"

// Value of `"key"` inside [start, end): the first character after the colon
// (whitespace skipped), or NULL
static const char *find_value(const char *start, const char *end, const char *key) {
    size_t klen = strlen(key);
    for (const char *p = start; p && p + klen + 2 <= end; p++) {
        p = strchr(p, '"');
        if (!p || p + klen + 2 > end) return NULL;
        if (strncmp(p + 1, key, klen) != 0 || p[klen + 1] != '"') continue;
        const char *v = p + klen + 2;
        while (v < end && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) v++;
        if (v >= end || *v != ':') continue;
        v++;
        while (v < end && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) v++;
        return v < end ? v : NULL;
    }
    return NULL;
}

// A string or number value copied into out; false if neither
static bool copy_scalar(const char *v, const char *end, char *out, size_t size) {
    size_t n = 0;
    if (*v == '"') {
        v++;
        while (v < end && *v != '"' && n + 1 < size) out[n++] = *v++;
        if (v >= end || *v != '"') return false;  // cut short, or too long
    } else {
        while (v < end && ((*v >= '0' && *v <= '9') || *v == '-') && n + 1 < size) out[n++] = *v++;
    }
    out[n] = '\0';
    return n > 0;
}

static long parse_long(const char *v, const char *end) {
    long value = 0;
    bool neg = false;
    if (v < end && *v == '-') { neg = true; v++; }
    while (v < end && *v >= '0' && *v <= '9') value = value * 10 + (*v++ - '0');
    return neg ? -value : value;
}

int history_parse_versions(const char *json, HistoryVersion *versions, int max_versions) {
    if (!json || !versions || max_versions <= 0) return 0;
    const char *json_end = json + strlen(json);
    const char *arr = find_value(json, json_end, "versions");
    if (!arr || *arr != '[') return 0;
    arr++;

    int count = 0;
    while (*arr && *arr != ']' && count < max_versions) {
        const char *obj = strchr(arr, '{');
        if (!obj) break;
        const char *close = strchr(arr, ']');
        if (close && close < obj) break;   // the list ended first
        obj++;
        const char *obj_end = strchr(obj, '}');
        if (!obj_end) break;

        HistoryVersion v;
        memset(&v, 0, sizeof(v));
        const char *ts = find_value(obj, obj_end, "timestamp");
        if (ts && copy_scalar(ts, obj_end, v.timestamp, sizeof(v.timestamp))) {
            const char *size = find_value(obj, obj_end, "size");
            const char *files = find_value(obj, obj_end, "file_count");
            long s = size ? parse_long(size, obj_end) : 0;
            v.size = s > 0 ? (u32)s : 0;
            v.file_count = files ? (int)parse_long(files, obj_end) : 0;
            versions[count++] = v;
        }
        arr = obj_end + 1;
    }
    return count;
}
