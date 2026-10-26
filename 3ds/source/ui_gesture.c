// Input state machines of the GUI (see ui_gesture.h). Host-tested: keep it
// free of citro2d and of libctru calls.
#include "ui_gesture.h"
#include <math.h>

static u64 (*clock_fn)(void);

void ui_gesture_set_clock(u64 (*now_ms)(void)) { clock_fn = now_ms; }
u64 ui_gesture_now(void) { return clock_fn ? clock_fn() : 0; }

static float dist(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

// ---------------------------------------------------------------------------
// Touch tracker
// ---------------------------------------------------------------------------

static void track_accept(UiTouchTrack *t, float x, float y) {
    t->ax = x;
    t->ay = y;
    float d = dist(x, y, t->x0, t->y0);
    if (d > t->moved) t->moved = d;
}

static void track_fill(const UiTouchTrack *t, UiInput *in) {
    in->tx = t->ax;
    in->ty = t->ay;
    in->tx0 = t->x0;
    in->ty0 = t->y0;
    in->moved = t->moved;
}

void ui_touch_update(UiTouchTrack *t, bool raw_held, float x, float y, UiInput *in) {
    in->t_down = in->t_held = in->t_up = false;
    if (!raw_held) {
        if (t->down) {
            // Released: report the last accepted sample; a jump still
            // waiting for confirmation was the panel glitching on release
            t->down = false;
            t->pending = false;
            in->t_up = true;
        }
        t->latched = false;
        track_fill(t, in);
        return;
    }
    if (t->latched) {
        track_fill(t, in);
        return;
    }
    if (!t->down) {
        t->down = true;
        t->pending = false;
        t->x0 = t->ax = x;
        t->y0 = t->ay = y;
        t->moved = 0;
        in->t_down = true;
    } else if (t->pending) {
        if (dist(x, y, t->px, t->py) <= UI_TOUCH_CONFIRM_PX) {
            // The finger really is there: a fast flick, keep it
            t->pending = false;
            track_accept(t, x, y);
        } else {
            // A glitch: drop it and judge this sample on its own
            t->pending = false;
            if (dist(x, y, t->ax, t->ay) > UI_TOUCH_SPIKE_PX) {
                t->pending = true;
                t->px = x;
                t->py = y;
            } else {
                track_accept(t, x, y);
            }
        }
    } else if (dist(x, y, t->ax, t->ay) > UI_TOUCH_SPIKE_PX) {
        t->pending = true;
        t->px = x;
        t->py = y;
    } else {
        track_accept(t, x, y);
    }
    in->t_held = true;
    track_fill(t, in);
}

void ui_touch_latch(UiTouchTrack *t, bool raw_held_now) {
    t->down = false;
    t->pending = false;
    t->latched = raw_held_now;
}

// ---------------------------------------------------------------------------
// Accelerated repeat
// ---------------------------------------------------------------------------

u32 ui_repeat_accel(UiRepeat *r, u32 held, u32 rep, u64 now_ms) {
    static const u32 dirs[2] = { KEY_UP, KEY_DOWN };
    for (int i = 0; i < 2; i++) {
        if (!(held & dirs[i])) {
            r->since[i] = 0;
            r->frames[i] = 0;
            continue;
        }
        if (r->since[i] == 0) {
            r->since[i] = now_ms ? now_ms : 1;
            r->frames[i] = 0;
            continue;
        }
        if (now_ms - r->since[i] >= UI_REPEAT_ACCEL_MS) {
            if (r->frames[i]++ % 2 == 0) rep |= dirs[i];
        }
    }
    return rep;
}

// ---------------------------------------------------------------------------
// Tap recognizer
// ---------------------------------------------------------------------------

static int hit(const GuiRect *r, int n, float x, float y) {
    for (int i = 0; i < n; i++)
        if (gui_rect_contains(r[i], x, y)) return i;
    return -1;
}

int ui_tap_buttons(UiTap *t, const UiInput *in, const GuiRect *r, int n, int *pressed_out) {
    int committed = -1, pressed = -1;
    if (in->t_down) t->armed = hit(r, n, in->tx, in->ty);
    if (t->armed >= n) t->armed = -1;
    if (t->armed >= 0) {
        bool inside = gui_rect_contains(gui_rect_grow(r[t->armed], UI_TAP_SLOP_PX), in->tx, in->ty);
        if (in->t_held) {
            if (inside) pressed = t->armed;
        } else {
            // Released (commit if still on it), or the touch was dropped
            if (in->t_up && inside) committed = t->armed;
            t->armed = -1;
        }
    }
    if (pressed_out) *pressed_out = pressed;
    return committed;
}
