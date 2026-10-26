#ifndef HISTORY_PARSE_H
#define HISTORY_PARSE_H

// Parsing of GET /saves/{title_id}/history. Plain C (host-tested in
// tests/test_history_parse.c).

#include "common.h"

#define MAX_HISTORY_VERSIONS 20

typedef struct {
    // The version's key as the server lists it. The server sends Unix
    // seconds as a JSON number (kept here as its digits), which is also what
    // GET /saves/{id}/history/{timestamp} takes back; an older server's ISO
    // 8601 string is kept verbatim.
    char timestamp[32];
    u32 size;
    int file_count;
} HistoryVersion;

// Fill `versions` from the response body (NUL-terminated). Returns the
// number of versions read (0 when the list is missing or empty).
int history_parse_versions(const char *json, HistoryVersion *versions, int max_versions);

#endif // HISTORY_PARSE_H
