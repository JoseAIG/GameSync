// Plain-C helpers of background jobs (see ui_job.h)
#include "ui_job.h"
#include <stdio.h>
#include <string.h>

// Copy `s` into `buf`, cutting at a character boundary; NULL stays NULL
static const char *copy_str(const char *s, char *buf, size_t size) {
    if (!s) return NULL;
    size_t n = strlen(s);
    if (n >= size) {
        n = size - 1;
        // Don't leave half a UTF-8 sequence: back up over continuation
        // bytes, then over the lead byte they belonged to
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    return buf;
}

void ui_progress_copy(UiProgressCopy *dst, const UiProgress *src) {
    UiProgress empty = { 0 };
    if (!src) src = &empty;
    dst->p = *src;
    dst->p.title = copy_str(src->title, dst->title, sizeof(dst->title));
    dst->p.name = copy_str(src->name, dst->name, sizeof(dst->name));
    dst->p.status = copy_str(src->status, dst->status, sizeof(dst->status));
    dst->p.left = copy_str(src->left, dst->left, sizeof(dst->left));
    dst->p.right = copy_str(src->right, dst->right, sizeof(dst->right));
    dst->p.info1 = copy_str(src->info1, dst->info1, sizeof(dst->info1));
    dst->p.info2 = copy_str(src->info2, dst->info2, sizeof(dst->info2));
}

UiJobWait ui_job_wait_state(unsigned long long now_ms, unsigned long long news_ms, bool cancellable,
                            bool cancel_hint) {
    UiJobWait w = { false, 0, false };
    unsigned long long quiet = now_ms > news_ms ? now_ms - news_ms : 0;
    w.seconds = (unsigned)(quiet / 1000);
    w.waiting = quiet >= UI_JOB_WAIT_MS;
    w.cancel = cancellable && (cancel_hint || w.waiting);
    return w;
}

void ui_format_wait(unsigned seconds, char *out, size_t size) {
    unsigned h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    if (h) snprintf(out, size, "%u:%02u:%02u", h, m, s);
    else snprintf(out, size, "%u:%02u", m, s);
}
