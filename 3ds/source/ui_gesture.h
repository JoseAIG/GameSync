#ifndef UI_GESTURE_H
#define UI_GESTURE_H

// Input state machines of the 3DS client's GUI: the per-frame input
// snapshot with its touch filter and latch, accelerated key repeat, the tap
// recognizer behind every touch button, and the list controller (drag,
// momentum, rubber band). Plain C on libctru's integer types and KEY_*
// masks only: no citro2d, no HID calls, no clock reads (the time comes in
// UiInput.t_ms or from ui_gesture_set_clock), so it is host-tested
// (tests/test_ui_gesture.c). ui.c feeds it from hidScanInput().

#include "common.h"
#include "gui_motion.h"

// Time source for the few functions that need "now" without an input
// (edge feedback). ui.c sets gui_clock_ms; tests set a fake clock.
void ui_gesture_set_clock(u64 (*now_ms)(void));
u64 ui_gesture_now(void);

// ---------------------------------------------------------------------------
// Per-frame input snapshot
// ---------------------------------------------------------------------------

typedef struct {
    u32 down, held, up, rep;     // hidKeysDown / Held / Up / DownRepeat (+ accelerated repeat)
    bool t_down, t_held, t_up;   // touch edges, after the latch and the spike filter
    float tx, ty;                // current point; on t_up the LAST HELD sample
    float tx0, ty0;              // point at touch-down
    float moved;                 // max distance from (tx0, ty0) during this touch
    u64 t_ms;                    // monotonic ms at scan
} UiInput;

// Touch tracker: one finger, raw samples in, filtered edges out.
//
// Spike rejection (not a speed limit): a held sample more than 40 px from
// the last accepted one is held back a frame. The next sample within 16 px
// of it confirms it (a real fast flick); otherwise it was a resistive-panel
// glitch and is dropped. A jump on the last frame before release is
// dropped. Ordinary samples pass with no delay.
//
// Latch: after ui_touch_latch() a finger already on the screen is ignored
// until it lifts, so the touch that opened or closed a dialog never taps or
// drags what is underneath.
typedef struct {
    bool down;          // a touch is being tracked
    bool latched;       // ignore the screen until it is released
    float ax, ay;       // last accepted sample
    bool pending;       // a far jump waiting for confirmation
    float px, py;
    float x0, y0, moved;
} UiTouchTrack;

#define UI_TOUCH_SPIKE_PX 40.0f
#define UI_TOUCH_CONFIRM_PX 16.0f

// One frame: `raw_held` = the panel reports a touch, (x, y) its sample.
// Fills the t_* fields, tx/ty, tx0/ty0 and moved of `in`.
void ui_touch_update(UiTouchTrack *t, bool raw_held, float x, float y, UiInput *in);
// Drop the current touch (no t_up for it); `raw_held_now` = a finger is on
// the screen now, which is then ignored until it lifts.
void ui_touch_latch(UiTouchTrack *t, bool raw_held_now);

// Accelerated repeat: keeps the hardware repeat (hidSetRepeatParameters)
// and, once UP or DOWN has been held for UI_REPEAT_ACCEL_MS, also reports
// that direction every second frame (30 rows/s). Same keys, same action.
typedef struct {
    u64 since[2];      // UP, DOWN: when the hold started (0 = not held)
    u32 frames[2];
} UiRepeat;

#define UI_REPEAT_ACCEL_MS 1000

u32 ui_repeat_accel(UiRepeat *r, u32 held, u32 rep, u64 now_ms);

// ---------------------------------------------------------------------------
// Tap recognizer (buttons)
// ---------------------------------------------------------------------------

// A touch-down inside rect i arms it; it shows pressed while the finger is
// inside rect i grown by UI_TAP_SLOP_PX (leave = released, come back =
// pressed again) and commits on touch-up while pressed. No double-tap.
typedef struct {
    int armed;   // armed index or -1
} UiTap;

#define UI_TAP_INIT { -1 }
#define UI_TAP_SLOP_PX 10.0f

// Returns the index committed this frame or -1. *pressed_out (may be NULL)
// = the index to draw pressed or -1. A rect with no area never arms.
int ui_tap_buttons(UiTap *t, const UiInput *in, const GuiRect *r, int n, int *pressed_out);

#endif // UI_GESTURE_H
