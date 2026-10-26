#ifndef UI_TYPES_H
#define UI_TYPES_H

// Plain types shared by ui.h and the host-tested ui_job.c (no citro2d).

#include <stdbool.h>

typedef enum {
    UI_TONE_ACCENT = 0,
    UI_TONE_OK,
    UI_TONE_WARN,
    UI_TONE_INFO,
    UI_TONE_ERR,
} UiTone;

typedef struct {
    const char *title;    // card title
    const char *name;     // what's being worked on (bold line)
    const char *status;   // line under the name
    float frac;           // 0..1; < 0 = indeterminate; > 1 = no bar
    const char *left;     // under the bar, left ("12 MB / 64 MB")
    const char *right;    // under the bar, right ("18%")
    const char *info1;    // extra lines (speed, time)
    const char *info2;
    bool cancel_hint;     // a "B Cancel" pill (see ui_progress_cancel_poll)
    UiTone tone;
} UiProgress;

#endif // UI_TYPES_H
