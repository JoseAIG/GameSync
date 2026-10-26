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
    t->sx = x - t->ax;
    t->sy = y - t->ay;
    t->ax = x;
    t->ay = y;
    float d = dist(x, y, t->x0, t->y0);
    if (d > t->moved) t->moved = d;
}

// Far from the last accepted sample and from where the last step points
static bool track_far(const UiTouchTrack *t, float x, float y) {
    return dist(x, y, t->ax, t->ay) > UI_TOUCH_SPIKE_PX &&
           dist(x, y, t->ax + t->sx, t->ay + t->sy) > UI_TOUCH_SPIKE_PX;
}

// The sample after a held-back jump confirms it: it stays on the jump, or
// carries on the way the jump went (a finger still moving that fast). A
// glitch is followed by a sample back near where the finger really is,
// i.e. behind the jump.
static bool track_confirms(const UiTouchTrack *t, float x, float y) {
    if (dist(x, y, t->px, t->py) <= UI_TOUCH_CONFIRM_PX) return true;
    float jx = t->px - t->ax, jy = t->py - t->ay;
    float jl = sqrtf(jx * jx + jy * jy);
    if (jl <= 0) return false;
    float dx = x - t->px, dy = y - t->py;
    float along = (dx * jx + dy * jy) / jl;
    float across = fabsf(dx * jy - dy * jx) / jl;
    return along > 0 && across <= UI_TOUCH_CONFIRM_PX + 0.5f * along;
}

static void track_fill(const UiTouchTrack *t, UiInput *in) {
    in->tx = t->ax;
    in->ty = t->ay;
    in->tx0 = t->x0;
    in->ty0 = t->y0;
    in->moved = t->moved;
}

void ui_touch_update(UiTouchTrack *t, bool raw_held, float x, float y, UiInput *in) {
    in->t_down = in->t_held = in->t_up = in->t_cancel = false;
    if (!raw_held) {
        if (t->down) {
            // Released: report the last accepted sample. A jump still
            // waiting for confirmation was the panel glitching on release,
            // or a flick that left on its first fast frame: either way
            // this release is no tap.
            t->down = false;
            in->t_cancel = t->pending;
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
        t->sx = t->sy = 0;
        t->moved = 0;
        in->t_down = true;
    } else if (t->pending) {
        if (track_confirms(t, x, y)) {
            // The finger really went there: a fast flick, keep both
            t->pending = false;
            track_accept(t, t->px, t->py);
            track_accept(t, x, y);
        } else {
            // A glitch: drop it and judge this sample on its own
            t->pending = false;
            if (track_far(t, x, y)) {
                t->pending = true;
                t->px = x;
                t->py = y;
            } else {
                track_accept(t, x, y);
            }
        }
    } else if (track_far(t, x, y)) {
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
            if (in->t_up && !in->t_cancel && inside) committed = t->armed;
            t->armed = -1;
        }
    }
    if (pressed_out) *pressed_out = pressed;
    return committed;
}

// ---------------------------------------------------------------------------
// List controller
// ---------------------------------------------------------------------------
//
// Momentum projection and the rubber band follow Apple's "Designing Fluid
// Interfaces" (WWDC 2018): project(v) = (v / 1000) * d / (1 - d) with
// d = 0.998 per ms (v * 0.499 in v's own units), and
// rubberband(o, dim, 0.55). Written from the talk; no code copied.

// Distance a flick travels per unit of release velocity (s): 0.499
static float project_factor(void) {
    return UI_DECEL_RATE / (1.0f - UI_DECEL_RATE) / 1000.0f;
}

// The same deceleration as a decay rate (1/s): -ln(0.998) * 1000 = 2.002
static float decay_rate(void) {
    static float k;
    if (k == 0) k = -1000.0f * logf(UI_DECEL_RATE);
    return k;
}

static float list_max(int count, int rows) {
    return count > rows ? (float)(count - rows) : 0.0f;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void sync_count(UiListAnim *a, int count, int selected, int scroll) {
    if (a->init && a->count == count) return;
    gui_spring_set(&a->sel, (float)selected);
    gui_spring_set(&a->scroll, (float)scroll);
    a->count = count;
    a->init = true;
    a->tracking = a->dragging = a->coasting = a->detached = false;
    a->coast_mode = UI_COAST_NONE;
    a->wb_pending = false;
    a->nsamples = 0;
    a->pressed_row = -1;
}

void ui_list_reset(UiListAnim *a) {
    a->init = false;
    a->tracking = a->dragging = a->coasting = a->detached = false;
    a->coast_mode = UI_COAST_NONE;
    a->wb_pending = false;
    a->snap = false;
    a->pressed_row = -1;
    gui_spring_set(&a->nudge, 0);
}

void ui_list_edge(UiListAnim *a, UiListEdge edge) {
    u64 now = ui_gesture_now();
    if (now == 0) now = 1;   // 0 means "never"
    a->flash_ms = now;
    if (edge == UI_EDGE_WRAP) {
        a->wrap_ms = now;
        a->snap = true;
    } else if (!gui_reduce_motion()) {
        // The sign follows the press: up for LEFT / UP, down for RIGHT / DOWN
        a->nudge.x = edge == UI_EDGE_CLAMP_TOP ? -6.0f : 6.0f;
        a->nudge.v = 0;
    }
}

void ui_list_keys(UiListAnim *a, u32 rep, int count, int page, int *selected) {
    if (count <= 0) return;
    int s = *selected;
    if (rep & KEY_UP) {
        if (s == 0) ui_list_edge(a, count > 1 ? UI_EDGE_WRAP : UI_EDGE_CLAMP_TOP);
        s = (s - 1 + count) % count;
    }
    if (rep & KEY_DOWN) {
        if (s == count - 1) ui_list_edge(a, count > 1 ? UI_EDGE_WRAP : UI_EDGE_CLAMP_BOTTOM);
        s = (s + 1) % count;
    }
    if (rep & KEY_LEFT) {
        if (s == 0) ui_list_edge(a, UI_EDGE_CLAMP_TOP);
        s = s - page < 0 ? 0 : s - page;
    }
    if (rep & KEY_RIGHT) {
        if (s == count - 1) ui_list_edge(a, UI_EDGE_CLAMP_BOTTOM);
        s = s + page >= count ? count - 1 : s + page;
    }
    *selected = s;
}

float ui_list_view(const UiListAnim *a) {
    float x = a->scroll.x, row_h = a->row_h > 0 ? a->row_h : 1;
    float max = list_max(a->count, a->rows);
    float dim = (float)a->rows * row_h;
    if (x < 0) return -gui_rubberband(-x * row_h, dim, UI_LIST_RUBBER) / row_h;
    if (x > max) return max + gui_rubberband((x - max) * row_h, dim, UI_LIST_RUBBER) / row_h;
    return x;
}

int ui_list_first_visible(const UiListAnim *a) {
    int f = (int)floorf(ui_list_view(a));
    return f < 0 ? 0 : f;
}

bool ui_list_settled(const UiListAnim *a) {
    return !a->tracking && !a->coasting;
}

int ui_list_pressed_row(const UiListAnim *a) {
    return (a->tracking && !a->dragging) ? a->pressed_row : -1;
}

void ui_list_set_range(UiListAnim *a, int lo, int hi) {
    a->range_on = true;
    a->range_lo = lo;
    a->range_hi = hi < lo ? lo : hi;
}

void ui_list_clear_range(UiListAnim *a) {
    a->range_on = false;
}

// Viewport top as a whole row inside the list
static int resting_top(const UiListAnim *a) {
    return (int)lroundf(clampf(ui_list_view(a), 0, list_max(a->count, a->rows)));
}

static void stop_coast(UiListAnim *a) {
    a->coasting = false;
    a->coast_mode = UI_COAST_NONE;
    a->coast_to_edge = false;
}

void ui_list_reveal(UiListAnim *a, int count, int selected, int *scroll) {
    if (a->wb_pending) {
        *scroll = a->wb;
        a->wb_pending = false;
    }
    if (a->coasting) {
        *scroll = resting_top(a);
        stop_coast(a);
    }
    a->detached = false;
    int rows = a->rows;
    if (rows <= 0 || count <= 0) return;
    if (selected < *scroll) *scroll = selected;
    if (selected >= *scroll + rows) *scroll = selected - rows + 1;
    if (*scroll < 0) *scroll = 0;
}

static void add_sample(UiListAnim *a, float y, u64 ms) {
    if (a->nsamples == UI_LIST_SAMPLES) {
        memmove(&a->samples[0], &a->samples[1], sizeof(a->samples[0]) * (UI_LIST_SAMPLES - 1));
        a->nsamples--;
    }
    a->samples[a->nsamples].y = y;
    a->samples[a->nsamples].ms = ms;
    a->nsamples++;
}

// End-to-start slope of the samples of the last 60 ms, in rows/s, signed
// like the content's motion (a finger moving up scrolls forward)
static float release_velocity(const UiListAnim *a, u64 now) {
    int first = -1;
    for (int i = 0; i < a->nsamples; i++) {
        if (now >= a->samples[i].ms && now - a->samples[i].ms <= UI_LIST_VELOCITY_MS) {
            first = i;
            break;
        }
    }
    int last = a->nsamples - 1;
    if (first < 0 || last - first < 1) return 0;
    float dt = (float)(a->samples[last].ms - a->samples[first].ms) / 1000.0f;
    if (dt <= 0) return 0;
    float dy = a->samples[last].y - a->samples[first].y;
    return -(dy / dt) / a->row_h;
}

// The finger let go of a list it moved (or a coast it stopped): pick where
// it lands and how it gets there
static void release(UiListAnim *a, float v) {
    float max = list_max(a->count, a->rows);
    float x = a->scroll.x;
    a->release_v = fabsf(v);
    a->coasting = true;
    a->coast_to_edge = false;
    if (x < 0 || x > max) {
        // Pulled past an end: no momentum earned, straight back (damping 1)
        a->coast_mode = UI_COAST_RETURN;
        a->coast_target = x < 0 ? 0 : max;
        a->scroll.v = v;
        return;
    }
    float lo = 0, hi = max;
    if (a->range_on) {
        lo = clampf((float)a->range_lo, 0, max);
        hi = clampf((float)a->range_hi, lo, max);
    }
    float proj = x + v * project_factor();
    bool edge_reachable = (proj < 0 && lo <= 0) || (proj > max && hi >= max);
    if (edge_reachable) {
        // The flick carries past an end: decelerate to it, then bounce
        a->coast_mode = UI_COAST_DECAY;
        a->coast_to_edge = true;
        a->coast_target = proj < 0 ? 0 : max;
        a->scroll.v = v;
        return;
    }
    float target = clampf(roundf(proj), lo, hi);
    float d = target - x;
    float lambda = d != 0 ? v / d : 0;
    a->coast_target = target;
    a->scroll.v = v;
    if (fabsf(v) < 1.0f || lambda < 1.0f || lambda > 6.0f) {
        // Tiny flick, or the rounding landed behind the finger: settle
        a->coast_mode = UI_COAST_SETTLE;
    } else {
        // Deceleration adjusted to land on the row, from the release speed
        a->coast_mode = UI_COAST_EXP;
        a->coast_lambda = lambda;
    }
}

static void finish_coast(UiListAnim *a, float target) {
    gui_spring_set(&a->scroll, target);
    stop_coast(a);
    a->wb_pending = true;
    a->wb = (int)lroundf(target);
}

static void coast_step(UiListAnim *a, float dt) {
    float max = list_max(a->count, a->rows);
    float t = a->coast_target;
    switch ((UiCoastMode)a->coast_mode) {
        case UI_COAST_EXP: {
            // x(t) = target - (target - x0) e^(-lambda t): v starts at the
            // release velocity and only ever slows
            float x = t - (t - a->scroll.x) * expf(-a->coast_lambda * dt);
            a->scroll.x = x;
            a->scroll.v = a->coast_lambda * (t - x);
            if (fabsf(t - x) < 0.02f) finish_coast(a, t);
            break;
        }
        case UI_COAST_SETTLE:
            gui_spring_always(&a->scroll, t, GUI_RESP_SETTLE, dt);
            if (a->scroll.x == t && a->scroll.v == 0) finish_coast(a, t);
            break;
        case UI_COAST_DECAY:
            a->scroll.x += a->scroll.v * dt;
            a->scroll.v *= expf(-decay_rate() * dt);
            if (a->scroll.x < 0 || a->scroll.x > max) {
                // Crossed the end: the rest of the momentum goes into the
                // bounce (damping 0.8; 1 under reduced motion)
                a->coast_mode = UI_COAST_BOUNCE;
                a->coast_target = a->scroll.x < 0 ? 0 : max;
            } else if (fabsf(a->scroll.v) < 0.5f) {
                // Ran out of speed just short of it
                a->coast_mode = UI_COAST_SETTLE;
                a->coast_target = clampf(roundf(a->scroll.x), 0, max);
            }
            break;
        case UI_COAST_BOUNCE:
            gui_spring_bouncy(&a->scroll, t, GUI_RESP_EDGE, 0.8f, dt);
            if (a->scroll.x == t && a->scroll.v == 0) finish_coast(a, t);
            break;
        case UI_COAST_RETURN:
            gui_spring_always(&a->scroll, t, GUI_RESP_EDGE, dt);
            if (a->scroll.x == t && a->scroll.v == 0) finish_coast(a, t);
            break;
        default:
            stop_coast(a);
            break;
    }
    if ((a->coast_mode == UI_COAST_EXP || a->coast_mode == UI_COAST_DECAY) &&
        fabsf(a->scroll.v) > a->release_v + 1e-3f)
        a->coast_overspeed++;
}

void ui_list_step(UiListAnim *a, int rows, float row_h, int count, int selected, int scroll, float dt) {
    a->rows = rows;
    a->row_h = row_h;
    sync_count(a, count, selected, scroll);
    float target = a->wb_pending ? (float)a->wb : (float)scroll;
    // A wrap jumps (gliding across the whole list would sweep every row)
    if (a->snap) {
        gui_spring_set(&a->sel, (float)selected);
        if (!a->tracking && !a->coasting) gui_spring_set(&a->scroll, target);
        a->snap = false;
    }
    // Long jumps snap, short moves glide; the springs keep their velocity
    // across retargets, so a held D-pad glides continuously
    if (fabsf(a->sel.x - selected) > rows) gui_spring_set(&a->sel, (float)selected);
    gui_spring(&a->sel, (float)selected, GUI_RESP_SELECT, dt);
    gui_spring(&a->nudge, 0, GUI_RESP_NUDGE, dt);

    if (a->tracking) return;   // the finger owns the viewport (dragging or holding it)
    if (a->coasting) {
        coast_step(a, dt);
        return;
    }
    // Following the keys
    if (fabsf(a->scroll.x - target) > rows) gui_spring_set(&a->scroll, target);
    gui_spring(&a->scroll, target, GUI_RESP_SELECT, dt);
}

UiListEvent ui_list_input(UiListAnim *a, const UiListGeom *g, int count, int *selected, int *scroll,
                          const UiInput *in, u32 keys_moved) {
    UiListEvent ev = { UI_LIST_NONE, -1, 0 };
    a->rows = g->rows;
    a->row_h = g->row_h;
    sync_count(a, count, *selected, *scroll);
    if (a->wb_pending) {
        *scroll = a->wb;
        a->wb_pending = false;
    }

    // A move key: the keys take the viewport back
    if (keys_moved && !a->tracking && count > 0) {
        if (a->coasting) {
            *scroll = resting_top(a);
            stop_coast(a);
        }
        if (a->detached) {
            a->detached = false;
            int first = *scroll, last = *scroll + g->rows - 1;
            if (last > count - 1) last = count - 1;
            if (*selected < first || *selected > last) {
                // Off screen: the nearest visible row instead of the move
                *selected = *selected < first ? first : last;
                ev.kind = UI_LIST_REATTACHED;
                return ev;
            }
        }
    }

    GuiRect rect = gui_rect_make(g->x, g->y, g->w, g->rows * g->row_h);
    if (in->t_down && count > 0 && gui_rect_contains(rect, in->tx, in->ty)) {
        a->tracking = true;
        a->dragging = false;
        a->no_tap = a->froze = false;
        if (a->coasting) {
            // A touch stops the momentum where it is; a fast one isn't a tap
            a->no_tap = fabsf(a->scroll.v) > 2.0f;
            a->froze = true;
            stop_coast(a);
        }
        a->scroll.v = 0;   // hold the content under the finger
        a->grab_y = in->ty;
        a->grab_scroll = a->scroll.x;
        a->nsamples = 0;
        add_sample(a, in->ty, in->t_ms);
        int row = (int)floorf(ui_list_view(a) + (in->ty - g->y) / g->row_h);
        a->pressed_row = (!a->no_tap && row >= 0 && row < count) ? row : -1;
        a->sel_at_down = *selected;
    }
    if (!a->tracking) return ev;

    if (in->t_held) {
        if (!a->dragging && fabsf(in->ty - in->ty0) > UI_LIST_DRAG_PX) {
            // A drag: the tap is off for good. Re-anchor here, so the
            // content starts from where it is rather than jumping 8 px.
            a->dragging = true;
            a->detached = true;
            a->pressed_row = -1;
            a->grab_y = in->ty;
            a->grab_scroll = a->scroll.x;
        }
        if (a->dragging) {
            // 1:1, keeping the grab offset; past an end ui_list_view()
            // shows it through the rubber band
            float pos_px = a->grab_scroll * g->row_h - (in->ty - a->grab_y);
            a->scroll.x = pos_px / g->row_h;
            a->scroll.v = 0;
        }
        add_sample(a, in->ty, in->t_ms);
        return ev;
    }

    // Released (t_up), or the touch was dropped by a latch
    a->tracking = false;
    float v = (in->t_up && a->dragging) ? release_velocity(a, in->t_ms) : 0;
    if (in->t_up && !in->t_cancel && !a->dragging && !a->no_tap && a->pressed_row >= 0 &&
        in->moved < UI_LIST_DRAG_PX &&
        gui_rect_contains(rect, in->tx, in->ty)) {
        int row = (int)floorf(ui_list_view(a) + (in->ty - g->y) / g->row_h);
        if (row == a->pressed_row) {
            ev.kind = row == a->sel_at_down ? UI_LIST_TAP_SELECTED : UI_LIST_TAP;
            ev.index = row;
            ev.px = in->tx;
            *selected = row;
        }
    }
    if (a->dragging || a->froze) release(a, v);
    a->dragging = a->froze = false;
    a->pressed_row = -1;
    return ev;
}
