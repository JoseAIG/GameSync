// Host test for history_parse.c: GET /saves/{id}/history as the server sends it
#include "history_parse.h"
#include "test_util.h"

static void test_numeric_timestamps(void) {
    // FastAPI's compact JSON: Unix seconds as a number
    const char *json =
        "{\"title_id\":\"0004000000055D00\",\"versions\":["
        "{\"timestamp\":1735787045,\"display\":\"2025-01-02T03 04 05\",\"size\":131072,\"file_count\":2},"
        "{\"timestamp\":1735700000,\"display\":\"2025-01-01T03 33 20\",\"size\":512,\"file_count\":1}]}";
    HistoryVersion v[4];
    int n = history_parse_versions(json, v, 4);
    CHECK(n == 2);
    CHECK_STR(v[0].timestamp, "1735787045");
    CHECK(v[0].size == 131072);
    CHECK(v[0].file_count == 2);
    CHECK_STR(v[1].timestamp, "1735700000");
    CHECK(v[1].size == 512);
    CHECK(v[1].file_count == 1);
}

static void test_spaced_and_string_timestamps(void) {
    // Pretty-printed, and an ISO string from an older server
    const char *json =
        "{ \"versions\" : [ { \"timestamp\" : \"2026-01-02T03:04:05+00:00\" , \"size\" : 7 ,"
        " \"file_count\" : 3 } ] }";
    HistoryVersion v[2];
    int n = history_parse_versions(json, v, 2);
    CHECK(n == 1);
    CHECK_STR(v[0].timestamp, "2026-01-02T03:04:05+00:00");
    CHECK(v[0].size == 7);
    CHECK(v[0].file_count == 3);
}

static void test_edges(void) {
    HistoryVersion v[2];
    CHECK(history_parse_versions("{\"versions\":[]}", v, 2) == 0);
    CHECK(history_parse_versions("{\"title_id\":\"x\"}", v, 2) == 0);
    CHECK(history_parse_versions("", v, 2) == 0);
    CHECK(history_parse_versions(NULL, v, 2) == 0);
    // An entry without a timestamp is skipped, not an empty key
    CHECK(history_parse_versions("{\"versions\":[{\"size\":1},{\"timestamp\":5,\"size\":2}]}", v, 2) == 1);
    CHECK_STR(v[0].timestamp, "5");
    CHECK(v[0].size == 2);
    // A key that only appears as a value doesn't count
    CHECK(history_parse_versions("{\"versions\":[{\"display\":\"timestamp\",\"size\":1}]}", v, 2) == 0);
    // Capped at max_versions
    const char *three = "{\"versions\":[{\"timestamp\":1},{\"timestamp\":2},{\"timestamp\":3}]}";
    CHECK(history_parse_versions(three, v, 2) == 2);
    CHECK_STR(v[1].timestamp, "2");
    // Objects after the list belong to something else
    CHECK(history_parse_versions("{\"versions\":[],\"x\":{\"timestamp\":9}}", v, 2) == 0);
    // A string too long for the field is dropped rather than cut
    CHECK(history_parse_versions("{\"versions\":[{\"timestamp\":\"0123456789012345678901234567890123456789\"}]}",
                                 v, 2) == 0);
}

int main(void) {
    test_numeric_timestamps();
    test_spaced_and_string_timestamps();
    test_edges();
    TEST_DONE();
}
