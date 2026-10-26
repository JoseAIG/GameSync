// Host test for ui_gesture.c: touch filter and latch, accelerated repeat,
// tap recognizer, list controller
#include "ui_gesture.h"
#include "test_util.h"
#include <math.h>

#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

static u64 fake_now;
static u64 fake_clock(void) { return fake_now; }

// One frame of the touch tracker
static UiInput touch(UiTouchTrack *t, bool held, float x, float y) {
    UiInput in;
    memset(&in, 0, sizeof(in));
    in.t_ms = fake_now;
    ui_touch_update(t, held, x, y, &in);
    return in;
}

static void test_touch_edges(void) {
    UiTouchTrack t = { 0 };
    UiInput in = touch(&t, true, 100, 50);
    CHECK(in.t_down && in.t_held && !in.t_up);
    CHECK(in.tx == 100 && in.ty == 50 && in.tx0 == 100 && in.ty0 == 50 && in.moved == 0);
    in = touch(&t, true, 103, 54);
    CHECK(!in.t_down && in.t_held);
    CHECK(in.tx == 103 && in.ty == 54 && NEAR(in.moved, 5, 1e-4f));
    // hidTouchRead() reads (0, 0) on release: t_up carries the last held point
    in = touch(&t, false, 0, 0);
    CHECK(in.t_up && !in.t_held && !in.t_down);
    CHECK(in.tx == 103 && in.ty == 54);
    in = touch(&t, false, 0, 0);
    CHECK(!in.t_up && !in.t_held);
}

static void test_spike_filter(void) {
    UiTouchTrack t = { 0 };
    touch(&t, true, 100, 100);
    // A glitch: one far sample, then back near the old position
    UiInput in = touch(&t, true, 100, 200);
    CHECK(in.ty == 100);                    // held back a frame
    in = touch(&t, true, 101, 102);
    CHECK(in.ty == 102);                    // dropped, the finger never left
    CHECK(in.moved < 3);

    // A real fast flick: the next sample confirms the jump
    in = touch(&t, true, 101, 160);
    CHECK(in.ty == 102);
    in = touch(&t, true, 101, 170);
    CHECK(in.ty == 170);
    CHECK(in.moved > 60);

    // Ordinary fast motion (under 40 px a frame) passes with no delay
    in = touch(&t, true, 101, 205);
    CHECK(in.ty == 205);

    // A jump on the last frame before release is dropped, and the release
    // is no tap
    in = touch(&t, true, 101, 20);
    CHECK(in.ty == 205);
    in = touch(&t, false, 0, 0);
    CHECK(in.t_up && in.ty == 205 && in.t_cancel);
    // An ordinary release is not cancelled
    touch(&t, true, 50, 50);
    in = touch(&t, false, 0, 0);
    CHECK(in.t_up && !in.t_cancel);
}

// A finger moving faster than 40 px a frame: held back one frame, then
// tracked every frame (the confirmed step is the prediction)
static void test_spike_filter_fast_motion(void) {
    static const float speeds[] = { 42, 45, 50, 60, 80 };
    for (unsigned k = 0; k < sizeof(speeds) / sizeof(speeds[0]); k++) {
        float v = speeds[k];
        UiTouchTrack t = { 0 };
        touch(&t, true, 100, 230);
        UiInput in = touch(&t, true, 100, 230 - v);
        CHECK(in.ty == 230);                       // the first jump waits a frame
        for (int f = 2; 230 - f * v >= 0; f++) {
            in = touch(&t, true, 100, 230 - f * v);
            CHECK(in.ty == 230 - f * v);           // then every sample is kept
            CHECK(NEAR(in.moved, f * v, 1e-3f));
        }
        in = touch(&t, false, 0, 0);
        CHECK(in.t_up && !in.t_cancel);
    }

    // Decelerating or turning slightly still confirms; a glitch that turns
    // back does not
    UiTouchTrack t = { 0 };
    touch(&t, true, 100, 200);
    touch(&t, true, 100, 150);                     // held back
    UiInput in = touch(&t, true, 110, 120);        // further along, a bit off line
    CHECK(in.ty == 120 && in.tx == 110);
    UiTouchTrack g = { 0 };
    touch(&g, true, 100, 200);
    touch(&g, true, 100, 150);
    in = touch(&g, true, 100, 180);                // back toward the start: a glitch
    CHECK(in.ty == 180 && in.moved < 21);

    // A glitch in the middle of a fast flick costs at most a frame or two
    UiTouchTrack f = { 0 };
    touch(&f, true, 100, 230);
    touch(&f, true, 100, 185);
    touch(&f, true, 100, 140);                     // tracking at 45 px/frame
    in = touch(&f, true, 300, 20);                 // glitch
    CHECK(in.ty == 140);
    touch(&f, true, 100, 95);
    in = touch(&f, true, 100, 50);
    CHECK(in.ty == 50);
}

// A button slid off fast is never committed: neither a tracked slide nor
// one whose only far sample was the last before release
static void test_tap_fast_slide_off(void) {
    GuiRect r[1] = { { 10, 200, 100, 38 } };
    UiTap tap = UI_TAP_INIT;
    UiTouchTrack t = { 0 };
    int pressed;
    UiInput in = touch(&t, true, 50, 215);
    ui_tap_buttons(&tap, &in, r, 1, &pressed);
    CHECK(pressed == 0);
    for (float y = 215 - 45; y >= 80; y -= 45) {
        in = touch(&t, true, 50, y);
        ui_tap_buttons(&tap, &in, r, 1, &pressed);
    }
    in = touch(&t, false, 0, 0);
    CHECK(ui_tap_buttons(&tap, &in, r, 1, &pressed) == -1);

    in = touch(&t, true, 50, 215);
    ui_tap_buttons(&tap, &in, r, 1, &pressed);
    in = touch(&t, true, 50, 150);                 // one fast frame, then gone
    ui_tap_buttons(&tap, &in, r, 1, &pressed);
    in = touch(&t, false, 0, 0);
    CHECK(in.t_cancel);
    CHECK(ui_tap_buttons(&tap, &in, r, 1, &pressed) == -1);

    // An ordinary tap still commits
    in = touch(&t, true, 50, 215);
    ui_tap_buttons(&tap, &in, r, 1, &pressed);
    in = touch(&t, false, 0, 0);
    CHECK(ui_tap_buttons(&tap, &in, r, 1, &pressed) == 0);
}

static void test_latch(void) {
    UiTouchTrack t = { 0 };
    touch(&t, true, 50, 50);
    // A dialog opens with the finger still down: no t_up, ignored until lifted
    ui_touch_latch(&t, true);
    UiInput in = touch(&t, true, 60, 60);
    CHECK(!in.t_down && !in.t_held && !in.t_up);
    in = touch(&t, false, 0, 0);
    CHECK(!in.t_up);
    in = touch(&t, true, 70, 70);
    CHECK(in.t_down);                       // a fresh touch counts
    // Latch with nothing on the screen: the next touch is fresh
    touch(&t, false, 0, 0);
    ui_touch_latch(&t, false);
    in = touch(&t, true, 10, 10);
    CHECK(in.t_down);
}

static void test_repeat(void) {
    UiRepeat r = { { 0, 0 }, { 0, 0 } };
    u64 now = 5000;
    int extra = 0;
    // Under a second: only the hardware repeat
    for (int f = 0; f < 59; f++, now += 16)
        if (ui_repeat_accel(&r, KEY_DOWN, 0, now) & KEY_DOWN) extra++;
    CHECK(extra == 0);
    // Then every second frame, on top of the hardware repeat
    extra = 0;
    now = 5000 + 1000;
    for (int f = 0; f < 60; f++, now += 16)
        if (ui_repeat_accel(&r, KEY_DOWN, 0, now) & KEY_DOWN) extra++;
    CHECK(extra == 30);
    // The hardware repeat is kept as is
    CHECK(ui_repeat_accel(&r, KEY_DOWN, KEY_A, now) & KEY_A);
    // Released: back to normal
    CHECK(ui_repeat_accel(&r, 0, 0, now) == 0);
    CHECK(ui_repeat_accel(&r, KEY_DOWN, 0, now + 16) == 0);
    CHECK(ui_repeat_accel(&r, KEY_DOWN, 0, now + 32) == 0);
    // UP is tracked on its own; LEFT / RIGHT are never accelerated
    UiRepeat q = { { 0, 0 }, { 0, 0 } };
    u32 seen = 0;
    for (u64 ms = 100; ms < 3000; ms += 16) seen |= ui_repeat_accel(&q, KEY_UP | KEY_RIGHT, 0, ms);
    CHECK((seen & KEY_UP) && !(seen & KEY_RIGHT));
}

static UiInput frame(bool t_down, bool t_held, bool t_up, float x, float y) {
    UiInput in;
    memset(&in, 0, sizeof(in));
    in.t_down = t_down;
    in.t_held = t_held;
    in.t_up = t_up;
    in.tx = x;
    in.ty = y;
    return in;
}

static void test_tap(void) {
    GuiRect r[2] = { { 10, 200, 100, 38 }, { 120, 200, 100, 38 } };
    UiTap tap = UI_TAP_INIT;
    int pressed;
    // Pressed on the touch-down frame, committed on touch-up only
    UiInput in = frame(true, true, false, 50, 210);
    CHECK(ui_tap_buttons(&tap, &in, r, 2, &pressed) == -1 && pressed == 0);
    in = frame(false, true, false, 52, 212);
    CHECK(ui_tap_buttons(&tap, &in, r, 2, &pressed) == -1 && pressed == 0);
    in = frame(false, false, true, 52, 212);
    CHECK(ui_tap_buttons(&tap, &in, r, 2, &pressed) == 0 && pressed == -1);

    // Slide off (more than 10 px out): released; back on: pressed again
    in = frame(true, true, false, 150, 210);
    ui_tap_buttons(&tap, &in, r, 2, &pressed);
    CHECK(pressed == 1);
    in = frame(false, true, false, 150, 150);
    ui_tap_buttons(&tap, &in, r, 2, &pressed);
    CHECK(pressed == -1);
    in = frame(false, true, false, 225, 215);   // within the 10 px slop
    ui_tap_buttons(&tap, &in, r, 2, &pressed);
    CHECK(pressed == 1);
    // Released off it: nothing
    in = frame(false, true, false, 150, 150);
    ui_tap_buttons(&tap, &in, r, 2, &pressed);
    in = frame(false, false, true, 150, 150);
    CHECK(ui_tap_buttons(&tap, &in, r, 2, &pressed) == -1);

    // Down outside every button, released on one: nothing
    in = frame(true, true, false, 300, 100);
    ui_tap_buttons(&tap, &in, r, 2, &pressed);
    in = frame(false, false, true, 50, 210);
    CHECK(ui_tap_buttons(&tap, &in, r, 2, &pressed) == -1);

    // A touch dropped by the latch (no t_up) never commits
    in = frame(true, true, false, 50, 210);
    ui_tap_buttons(&tap, &in, r, 2, &pressed);
    in = frame(false, false, false, 50, 210);
    CHECK(ui_tap_buttons(&tap, &in, r, 2, &pressed) == -1 && pressed == -1);
    in = frame(false, false, true, 50, 210);
    CHECK(ui_tap_buttons(&tap, &in, r, 2, &pressed) == -1);

    // A rect with no area (a disabled button) never arms
    GuiRect off[1] = { { 10, 200, 0, 38 } };
    in = frame(true, true, false, 10, 210);
    ui_tap_buttons(&tap, &in, off, 1, &pressed);
    CHECK(pressed == -1);
}


// ---------------------------------------------------------------------------
// List controller
// ---------------------------------------------------------------------------

static const UiListGeom LG = { 0, 26, 320, 8, 22, false, true };

typedef struct {
    UiListAnim a;
    UiTouchTrack t;
    int count, selected, scroll;
    u64 now;
} Sim;

static void sim_init(Sim *s, int count) {
    memset(s, 0, sizeof(*s));
    s->count = count;
    s->now = 1000;
    s->a.pressed_row = -1;
}

// One 60 Hz frame: touch sample (held / not), keys, input, then the step
static UiListEvent sim_frame(Sim *s, bool held, float y, u32 keys) {
    UiInput in;
    memset(&in, 0, sizeof(in));
    s->now += 16;
    fake_now = s->now;
    in.t_ms = s->now;
    in.rep = keys;
    in.down = keys;
    ui_touch_update(&s->t, held, 100, y, &in);
    UiListEvent ev = ui_list_input(&s->a, &LG, s->count, &s->selected, &s->scroll, &in, keys & UI_LIST_KEYS);
    if (ev.kind != UI_LIST_REATTACHED && (keys & UI_LIST_KEYS)) {
        ui_list_keys(&s->a, keys, s->count, LG.rows, &s->selected);
        if (s->selected < s->scroll) s->scroll = s->selected;
        if (s->selected >= s->scroll + LG.rows) s->scroll = s->selected - LG.rows + 1;
    }
    ui_list_step(&s->a, LG.rows, LG.row_h, s->count, s->selected, s->scroll, 1.0f / 60);
    return ev;
}

// Run frames with no input until the list settles (max `frames`)
static int sim_settle(Sim *s, int frames) {
    int n = 0;
    while (n < frames && !ui_list_settled(&s->a)) {
        sim_frame(s, false, 0, 0);
        n++;
    }
    sim_frame(s, false, 0, 0);   // write-back happens at the next input
    return n;
}

static void test_list_tap(void) {
    Sim s;
    sim_init(&s, 40);
    sim_frame(&s, false, 0, 0);
    // Row 2 is at y 26 + 2 * 22 = 70..92
    UiListEvent ev = sim_frame(&s, true, 80, 0);
    CHECK(ev.kind == UI_LIST_NONE);
    CHECK(ui_list_pressed_row(&s.a) == 2);          // pressed on the touch-down frame
    ev = sim_frame(&s, true, 83, 0);
    CHECK(ui_list_pressed_row(&s.a) == 2);
    ev = sim_frame(&s, false, 0, 0);                 // committed on touch-up
    CHECK(ev.kind == UI_LIST_TAP && ev.index == 2 && s.selected == 2);
    CHECK(ui_list_pressed_row(&s.a) == -1);
    // Tapping the selected row again
    sim_frame(&s, true, 80, 0);
    ev = sim_frame(&s, false, 0, 0);
    CHECK(ev.kind == UI_LIST_TAP_SELECTED && ev.index == 2);
    // Outside the list: nothing
    sim_frame(&s, true, 215, 0);
    ev = sim_frame(&s, false, 0, 0);
    CHECK(ev.kind == UI_LIST_NONE && s.selected == 2);
}

// A flick faster than 40 px a frame is a drag with momentum, never a tap,
// whichever row it starts on (the selected one included)
static void test_list_fast_flick(void) {
    static const float speeds[] = { 42, 45, 50, 60 };
    for (unsigned k = 0; k < sizeof(speeds) / sizeof(speeds[0]); k++) {
        float v = speeds[k];
        Sim s;
        sim_init(&s, 40);
        s.selected = 7;                            // the row under y 195
        sim_frame(&s, false, 0, 0);
        UiListEvent ev = sim_frame(&s, true, 195, 0);
        CHECK(ev.kind == UI_LIST_NONE);
        bool dragged = false;
        for (int f = 1; f <= 4; f++) {
            ev = sim_frame(&s, true, 195 - f * v, 0);
            CHECK(ev.kind == UI_LIST_NONE);
            dragged |= s.a.dragging;
        }
        CHECK(dragged && s.a.dragging);
        ev = sim_frame(&s, false, 0, 0);
        CHECK(ev.kind == UI_LIST_NONE && s.selected == 7);
        CHECK(s.a.release_v > 0 && s.a.coasting);
        sim_settle(&s, 600);
        CHECK(s.scroll > 0);
    }

    // Touch-down, one fast frame, release: the jump is dropped and the
    // release is no tap (not of the row under the touch-down either)
    Sim s;
    sim_init(&s, 40);
    sim_frame(&s, false, 0, 0);
    sim_frame(&s, true, 80, 0);
    sim_frame(&s, true, 20, 0);
    UiListEvent ev = sim_frame(&s, false, 0, 0);
    CHECK(ev.kind == UI_LIST_NONE && s.selected == 0);
}

static void test_list_drag(void) {
    Sim s;
    sim_init(&s, 40);
    sim_frame(&s, false, 0, 0);
    sim_frame(&s, true, 150, 0);
    // Under 8 px: still a press, the content doesn't move
    sim_frame(&s, true, 143, 0);
    CHECK(ui_list_pressed_row(&s.a) >= 0 && s.a.scroll.x == 0);
    // Past 8 px: a drag, re-anchored there, then 1:1 by the delta
    sim_frame(&s, true, 141, 0);
    CHECK(s.a.dragging && ui_list_pressed_row(&s.a) == -1);
    CHECK(s.a.scroll.x == 0);                        // no 8 px jump
    sim_frame(&s, true, 141 - 22, 0);
    sim_frame(&s, true, 141 - 44, 0);
    CHECK(NEAR(s.a.scroll.x, 2.0f, 1e-4f));          // 44 px = 2 rows
    sim_frame(&s, true, 141 - 66, 0);
    CHECK(NEAR(ui_list_view(&s.a), 3.0f, 1e-4f));
    // Held still, then released: no velocity, no tap, lands on a whole row
    for (int i = 0; i < 8; i++) sim_frame(&s, true, 141 - 66, 0);
    UiListEvent ev = sim_frame(&s, false, 0, 0);
    CHECK(ev.kind == UI_LIST_NONE);
    CHECK(s.selected == 0);                          // a drag never selects
    sim_settle(&s, 120);
    CHECK(s.scroll == 3 && s.a.scroll.x == 3.0f);   // written back to the caller
}

static void test_list_rubber_band(void) {
    Sim s;
    sim_init(&s, 40);
    sim_frame(&s, false, 0, 0);
    sim_frame(&s, true, 40, 0);
    sim_frame(&s, true, 50, 0);                      // commits the drag at 50
    sim_frame(&s, true, 90, 0);
    sim_frame(&s, true, 130, 0);
    sim_frame(&s, true, 150, 0);                     // 100 px past the top
    float view_px = ui_list_view(&s.a) * 22;
    float expect = -gui_rubberband(100, 8 * 22, 0.55f);
    CHECK(NEAR(view_px, expect, 0.01f));
    CHECK(view_px > -100 * 0.55f && view_px < 0);    // resists
    // Released over the edge: straight back (damping 1), never past the top
    sim_frame(&s, true, 150, 0);
    sim_frame(&s, true, 150, 0);
    sim_frame(&s, true, 150, 0);
    sim_frame(&s, true, 150, 0);
    sim_frame(&s, false, 0, 0);
    CHECK(s.a.coast_mode == UI_COAST_RETURN);
    float hi = -1e9f;
    while (!ui_list_settled(&s.a)) {
        sim_frame(&s, false, 0, 0);
        if (s.a.scroll.x > hi) hi = s.a.scroll.x;
    }
    CHECK(hi <= 0.0f);
    CHECK(s.a.scroll.x == 0.0f);

    // A list that fits on one screen still drags and bands
    Sim f;
    sim_init(&f, 5);
    sim_frame(&f, false, 0, 0);
    sim_frame(&f, true, 100, 0);
    sim_frame(&f, true, 80, 0);
    sim_frame(&f, true, 40, 0);
    CHECK(ui_list_view(&f.a) > 0.5f);
    sim_frame(&f, false, 0, 0);
    sim_settle(&f, 200);
    CHECK(f.a.scroll.x == 0.0f && f.scroll == 0);
}

// Flick: the finger moves `px_per_frame` up for `frames` frames, then lifts
static float flick(Sim *s, float y0, float px_per_frame, int frames) {
    float y = y0;
    sim_frame(s, true, y, 0);
    for (int i = 0; i < frames; i++) {
        y -= px_per_frame;
        sim_frame(s, true, y, 0);
    }
    sim_frame(s, false, 0, 0);
    return s->a.release_v;
}

static void test_list_flick(void) {
    gui_set_reduce_motion(false);
    Sim s;
    sim_init(&s, 200);
    sim_frame(&s, false, 0, 0);
    float v = flick(&s, 190, 12, 6);                 // 12 px / 16 ms = 34 rows/s
    CHECK(NEAR(v, 12.0f / 0.016f / 22, 0.5f));
    CHECK(s.a.coast_mode == UI_COAST_EXP);
    float x0 = s.a.scroll.x;
    float target = s.a.coast_target;
    // Lands where the 0.998 projection says, on a whole row
    CHECK(NEAR(target, roundf(x0 + v * 0.998f / (1 - 0.998f) / 1000), 1e-3f));
    CHECK(target == floorf(target));
    // Never faster than the release, monotonic, no overshoot
    bool slower = true, monotonic = true;
    float prev = x0;
    int frames = 0;
    while (!ui_list_settled(&s.a) && frames < 600) {
        sim_frame(&s, false, 0, 0);
        if (fabsf(s.a.scroll.v) > v + 1e-3f) slower = false;
        if (s.a.scroll.x < prev - 1e-4f || s.a.scroll.x > target + 1e-4f) monotonic = false;
        prev = s.a.scroll.x;
        frames++;
    }
    CHECK(slower && monotonic);
    CHECK(s.a.coast_overspeed == 0);
    CHECK(s.a.scroll.x == target);
    sim_frame(&s, false, 0, 0);
    CHECK(s.scroll == (int)target);
    CHECK(frames > 30);                               // a coast, not a jump
    CHECK(s.selected == 0);                           // the selection stayed
}

static void test_list_flick_into_edge(void) {
    gui_set_reduce_motion(false);
    Sim s;
    sim_init(&s, 30);                                 // max = 22
    sim_frame(&s, false, 0, 0);
    flick(&s, 190, 30, 5);                            // ~85 rows/s: projects far past the end
    CHECK(s.a.coast_to_edge && s.a.coast_mode == UI_COAST_DECAY);
    bool bounced = false, over = false;
    float hi = 0;
    for (int i = 0; i < 600 && !ui_list_settled(&s.a); i++) {
        sim_frame(&s, false, 0, 0);
        if (s.a.coast_mode == UI_COAST_BOUNCE) bounced = true;
        if (s.a.scroll.x > 22) over = true;
        float view = ui_list_view(&s.a);
        if (view > hi) hi = view;
    }
    CHECK(bounced && over);
    CHECK(hi > 22 && hi < 22 + 8 * 0.55f);            // soft, through the rubber band
    CHECK(s.a.scroll.x == 22.0f);
    sim_frame(&s, false, 0, 0);
    CHECK(s.scroll == 22);

    // A flick that stays inside never bounces
    Sim t;
    sim_init(&t, 300);
    sim_frame(&t, false, 0, 0);
    flick(&t, 190, 10, 5);
    bool any_bounce = false;
    for (int i = 0; i < 600 && !ui_list_settled(&t.a); i++) {
        sim_frame(&t, false, 0, 0);
        if (t.a.coast_mode == UI_COAST_BOUNCE || t.a.coast_mode == UI_COAST_DECAY) any_bounce = true;
    }
    CHECK(!any_bounce);
}

static void test_list_reduced_motion(void) {
    gui_set_reduce_motion(true);
    Sim s;
    sim_init(&s, 200);
    sim_frame(&s, false, 0, 0);
    flick(&s, 190, 12, 6);
    // Still coasts (direct manipulation), it doesn't snap to the target
    CHECK(s.a.coasting);
    float target = s.a.coast_target;
    sim_frame(&s, false, 0, 0);
    CHECK(s.a.scroll.x < target - 1);
    sim_settle(&s, 600);
    CHECK(s.a.scroll.x == target);

    // Into an edge: no bounce past it (damping 1 instead of 0.8)
    Sim e;
    sim_init(&e, 30);
    sim_frame(&e, false, 0, 0);
    flick(&e, 190, 30, 5);
    float hi = 0;
    bool past_after_bounce = false;
    for (int i = 0; i < 600 && !ui_list_settled(&e.a); i++) {
        sim_frame(&e, false, 0, 0);
        if (e.a.coast_mode == UI_COAST_BOUNCE && e.a.scroll.v < 0 && e.a.scroll.x < 22 - 1e-3f)
            past_after_bounce = true;
        if (e.a.scroll.x > hi) hi = e.a.scroll.x;
    }
    CHECK(!past_after_bounce);
    CHECK(e.a.scroll.x == 22.0f);
    gui_set_reduce_motion(false);
}

static void test_list_stop_and_velocity_window(void) {
    Sim s;
    sim_init(&s, 300);
    sim_frame(&s, false, 0, 0);
    flick(&s, 190, 12, 6);
    sim_frame(&s, false, 0, 0);
    CHECK(fabsf(s.a.scroll.v) > 2);
    // A touch while it coasts stops it there, and is not a tap
    float at = s.a.scroll.x;
    sim_frame(&s, true, 100, 0);
    CHECK(!s.a.coasting && NEAR(s.a.scroll.x, at, 0.5f));
    sim_frame(&s, true, 100, 0);
    UiListEvent ev = sim_frame(&s, false, 0, 0);
    CHECK(ev.kind == UI_LIST_NONE && s.selected == 0);
    sim_settle(&s, 200);
    CHECK(s.a.scroll.x == floorf(s.a.scroll.x));      // settled on a whole row

    // Moved, stopped for 100 ms, lifted: no momentum (only the last 60 ms count)
    Sim q;
    sim_init(&q, 300);
    sim_frame(&q, false, 0, 0);
    sim_frame(&q, true, 190, 0);
    for (int i = 0; i < 5; i++) sim_frame(&q, true, 190 - 15.0f * (i + 1), 0);
    for (int i = 0; i < 7; i++) sim_frame(&q, true, 115, 0);
    sim_frame(&q, false, 0, 0);
    CHECK(q.a.release_v == 0);
}

static void test_list_detached(void) {
    Sim s;
    sim_init(&s, 100);
    sim_frame(&s, false, 0, 0);
    s.selected = 3;
    sim_frame(&s, false, 0, 0);
    // Drag the list down by ~20 rows: the selection scrolls off the top
    sim_frame(&s, true, 190, 0);
    float y = 190;
    for (int i = 0; i < 40; i++) {
        y -= 11;
        if (y < 30) {
            // regrab lower
            for (int k = 0; k < 6; k++) sim_frame(&s, true, y, 0);
            sim_frame(&s, false, 0, 0);
            y = 190;
            sim_frame(&s, true, y, 0);
        }
        sim_frame(&s, true, y, 0);
    }
    for (int k = 0; k < 6; k++) sim_frame(&s, true, y, 0);
    sim_frame(&s, false, 0, 0);
    sim_settle(&s, 600);
    CHECK(s.scroll > 10);
    CHECK(s.selected == 3);
    // DOWN while the selection is above the viewport: the first visible row
    int first = s.scroll;
    UiListEvent ev = sim_frame(&s, false, 0, KEY_DOWN);
    CHECK(ev.kind == UI_LIST_REATTACHED && s.selected == first);
    // Then the keys work as usual
    ev = sim_frame(&s, false, 0, KEY_DOWN);
    CHECK(ev.kind == UI_LIST_NONE && s.selected == first + 1);

    // ui_list_reveal brings a far selection back into view
    s.a.detached = true;
    s.selected = 90;
    ui_list_reveal(&s.a, s.count, s.selected, &s.scroll);
    CHECK(s.scroll == 90 - 8 + 1 && !s.a.detached);
}

static void test_list_range_and_edges(void) {
    // Live window: a coast never lands outside [lo, hi]
    Sim s;
    sim_init(&s, 1000);
    sim_frame(&s, false, 0, 0);
    ui_list_set_range(&s.a, 0, 40);
    flick(&s, 190, 30, 6);
    sim_settle(&s, 600);
    CHECK(s.scroll <= 40 && s.scroll >= 0);
    ui_list_clear_range(&s.a);

    // Edge feedback is stamped with the clock
    UiListAnim a;
    memset(&a, 0, sizeof(a));
    fake_now = 5000;
    gui_set_reduce_motion(false);
    ui_list_edge(&a, UI_EDGE_WRAP);
    CHECK(a.wrap_ms == 5000 && a.flash_ms == 5000 && a.snap);
    ui_list_edge(&a, UI_EDGE_CLAMP_BOTTOM);
    CHECK(a.nudge.x == 6.0f);
    gui_set_reduce_motion(true);
    a.nudge.x = 0;
    ui_list_edge(&a, UI_EDGE_CLAMP_TOP);
    CHECK(a.nudge.x == 0 && a.flash_ms == 5000);      // no nudge, the flash stays
    gui_set_reduce_motion(false);

    // Keys: wrap and clamp as v055
    int sel = 0;
    ui_list_keys(&a, KEY_UP, 10, 8, &sel);
    CHECK(sel == 9);
    ui_list_keys(&a, KEY_RIGHT, 10, 8, &sel);
    CHECK(sel == 9);
    ui_list_keys(&a, KEY_LEFT, 10, 8, &sel);
    CHECK(sel == 1);
}

static void test_rubberband(void) {
    CHECK(gui_rubberband(0, 176, 0.55f) == 0);
    // Slope c at the bound, never reaching dim, symmetric
    CHECK(NEAR(gui_rubberband(1, 176, 0.55f), 0.55f, 0.01f));
    CHECK(gui_rubberband(10000, 176, 0.55f) < 176);
    CHECK(gui_rubberband(176, 176, 0.55f) < 176 * 0.55f);
    CHECK(gui_rubberband(-50, 176, 0.55f) == -gui_rubberband(50, 176, 0.55f));
    CHECK(gui_rubberband(100, 176, 0.55f) < gui_rubberband(200, 176, 0.55f));
}

// A new filter fades the rows in without moving, snapping or nudging
static void test_list_crossfade(void) {
    UiListAnim a = { 0 };
    fake_now = 7000;
    ui_list_crossfade(&a);
    CHECK(a.wrap_ms == 7000);
    CHECK(!a.snap && a.flash_ms == 0 && a.nudge.x == 0);
    fake_now = 0;
    ui_list_crossfade(&a);
    CHECK(a.wrap_ms != 0);   // 0 would mean "never"
}

int main(void) {
    ui_gesture_set_clock(fake_clock);
    test_touch_edges();
    test_spike_filter();
    test_spike_filter_fast_motion();
    test_latch();
    test_repeat();
    test_tap();
    test_tap_fast_slide_off();
    test_rubberband();
    test_list_tap();
    test_list_fast_flick();
    test_list_drag();
    test_list_rubber_band();
    test_list_flick();
    test_list_flick_into_edge();
    test_list_reduced_motion();
    test_list_stop_and_velocity_window();
    test_list_detached();
    test_list_range_and_edges();
    test_list_crossfade();
    TEST_DONE();
}
