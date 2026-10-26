#ifndef UI_JOB_H
#define UI_JOB_H

// The plain-C parts of background jobs (ui_run_job in ui.c): the copy of a
// progress report the worker hands to the main thread, and when the card
// starts saying it is waiting. No libctru or citro2d, so it is host-tested
// (tests/test_ui_job.c).

#include "ui_types.h"
#include <stddef.h>

// A UiProgress whose strings live inside it, so it outlives the worker's
// stack frame that reported it. p's pointers point into the buffers (or
// are NULL); copy one with ui_progress_copy(&dst, &src.p), never with `=`.
typedef struct {
    UiProgress p;
    char title[48], name[96], status[128], left[48], right[24], info1[96], info2[96];
} UiProgressCopy;

// Deep copy of `src` (NULL = an empty report); strings too long are cut
// at a UTF-8 character boundary
void ui_progress_copy(UiProgressCopy *dst, const UiProgress *src);

// After this long without news from the worker, the card says it is
// waiting (and a cancellable job shows its Cancel pill)
#define UI_JOB_WAIT_MS 5000

typedef struct {
    bool waiting;       // say "Waiting for the server... 0:07"
    unsigned seconds;   // since the last news
    bool cancel;        // show the Cancel pill
} UiJobWait;

// `news_ms` = when the worker last reported (or the job started)
UiJobWait ui_job_wait_state(unsigned long long now_ms, unsigned long long news_ms, bool cancellable,
                            bool cancel_hint);

// "0:07", "12:30", "1:02:03"
void ui_format_wait(unsigned seconds, char *out, size_t size);

#endif // UI_JOB_H
