// Host test for ui_job.c (progress copies, waiting state, wait clock)
#include "ui_job.h"
#include "test_util.h"
#include <stdlib.h>

static void test_copy_owns_strings(void) {
    char name[32] = "Zelda", status[32] = "Uploading 1/3";
    UiProgress src = { 0 };
    src.title = "Syncing";
    src.name = name;
    src.status = status;
    src.frac = 0.5f;
    src.cancel_hint = true;
    src.tone = UI_TONE_WARN;
    UiProgressCopy c;
    ui_progress_copy(&c, &src);
    // The worker's buffers change (or go away) after the call: the copy doesn't
    strcpy(name, "XXXXX");
    strcpy(status, "YYYY");
    CHECK_STR(c.p.title, "Syncing");
    CHECK_STR(c.p.name, "Zelda");
    CHECK_STR(c.p.status, "Uploading 1/3");
    CHECK(c.p.name == c.name && c.p.status == c.status);
    CHECK(c.p.left == NULL && c.p.info1 == NULL && c.p.info2 == NULL);
    CHECK(c.p.frac == 0.5f && c.p.cancel_hint && c.p.tone == UI_TONE_WARN);

    // A copy of a copy points into its own buffers
    UiProgressCopy d;
    ui_progress_copy(&d, &c.p);
    CHECK(d.p.name == d.name);
    CHECK_STR(d.p.name, "Zelda");

    // NULL = empty
    ui_progress_copy(&d, NULL);
    CHECK(d.p.title == NULL && d.p.name == NULL && d.p.frac == 0);
}

static void test_copy_cuts_utf8(void) {
    // 60 x "é" (2 bytes each) is longer than the 48-byte title buffer
    char longs[200] = "";
    for (int i = 0; i < 60; i++) strcat(longs, "\xC3\xA9");
    UiProgress src = { 0 };
    src.title = longs;
    UiProgressCopy c;
    ui_progress_copy(&c, &src);
    size_t n = strlen(c.p.title);
    CHECK(n < sizeof(c.title));
    CHECK(n % 2 == 0);   // never half a character
    CHECK(n == 46);
    // ASCII just fills the buffer
    char ascii[100];
    memset(ascii, 'a', 99);
    ascii[99] = '\0';
    src.title = ascii;
    ui_progress_copy(&c, &src);
    CHECK(strlen(c.p.title) == sizeof(c.title) - 1);
}

static void test_wait_state(void) {
    UiJobWait w = ui_job_wait_state(1000, 1000, true, false);
    CHECK(!w.waiting && !w.cancel && w.seconds == 0);
    w = ui_job_wait_state(5999, 1000, true, false);
    CHECK(!w.waiting && !w.cancel && w.seconds == 4);
    w = ui_job_wait_state(6000, 1000, true, false);
    CHECK(w.waiting && w.cancel && w.seconds == 5);
    // Not cancellable: says it waits, never offers a Cancel
    w = ui_job_wait_state(60000, 1000, false, true);
    CHECK(w.waiting && !w.cancel && w.seconds == 59);
    // A report that asks for the pill gets it at once when cancellable
    w = ui_job_wait_state(1200, 1000, true, true);
    CHECK(!w.waiting && w.cancel);
    // News "from the future" (clock read before the worker's stamp)
    w = ui_job_wait_state(900, 1000, true, false);
    CHECK(!w.waiting && w.seconds == 0);
}

static void test_format_wait(void) {
    char b[16];
    ui_format_wait(0, b, sizeof(b));
    CHECK_STR(b, "0:00");
    ui_format_wait(7, b, sizeof(b));
    CHECK_STR(b, "0:07");
    ui_format_wait(750, b, sizeof(b));
    CHECK_STR(b, "12:30");
    ui_format_wait(3723, b, sizeof(b));
    CHECK_STR(b, "1:02:03");
}

int main(void) {
    test_copy_owns_strings();
    test_copy_cuts_utf8();
    test_wait_state();
    test_format_wait();
    TEST_DONE();
}
