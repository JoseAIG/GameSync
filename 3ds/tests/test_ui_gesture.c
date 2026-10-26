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

    // A jump on the last frame before release is dropped
    in = touch(&t, true, 101, 20);
    CHECK(in.ty == 205);
    in = touch(&t, false, 0, 0);
    CHECK(in.t_up && in.ty == 205);
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

int main(void) {
    ui_gesture_set_clock(fake_clock);
    test_touch_edges();
    test_spike_filter();
    test_latch();
    test_repeat();
    test_tap();
    TEST_DONE();
}
