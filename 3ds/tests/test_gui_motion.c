// Host test for gui_motion.c (springs, dt clamp, reduced motion)
#include "gui_motion.h"
#include "test_util.h"
#include <math.h>

#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

static void run(GuiSpring *s, float target, float response, float dt, float seconds) {
    for (float t = 0; t < seconds; t += dt) gui_spring_always(s, target, response, dt);
}

static void test_critically_damped(void) {
    GuiSpring s = { 0, 0 };
    // Settles on the target and rests exactly there
    run(&s, 1, GUI_RESP_SELECT, 1.0f / 60, 0.6f);
    CHECK(s.x == 1.0f && s.v == 0.0f);
    CHECK(gui_spring_resting(&s, 1));

    // From rest it never overshoots, at any frame time
    float dts[] = { 1.0f / 120, 1.0f / 60, 1.0f / 20, 0.25f, 1.0f };
    for (unsigned k = 0; k < sizeof(dts) / sizeof(dts[0]); k++) {
        GuiSpring a = { 0, 0 };
        float prev = 0;
        bool monotonic = true, finite = true;
        for (int i = 0; i < 200; i++) {
            gui_spring_always(&a, 10, GUI_RESP_SELECT, dts[k]);
            if (a.x < prev - 1e-4f || a.x > 10 + 1e-4f) monotonic = false;
            if (!isfinite(a.x) || !isfinite(a.v)) finite = false;
            prev = a.x;
        }
        CHECK(monotonic);
        CHECK(finite);
        CHECK(a.x == 10.0f);
    }

    // "Response 0.16" reaches 90% in about 0.1 s (v055's snappiness)
    GuiSpring b = { 0, 0 };
    run(&b, 1, GUI_RESP_SELECT, 0.1f / 12, 0.1f - 1e-4f);
    CHECK(b.x > 0.86f && b.x < 0.93f);
}

static void test_exact_in_dt(void) {
    // The closed form doesn't depend on how a span is cut into frames
    GuiSpring a = { 3, -5 }, b = { 3, -5 };
    for (int i = 0; i < 6; i++) gui_spring_always(&a, 0, GUI_RESP_PUSH, 1.0f / 120);
    gui_spring_always(&b, 0, GUI_RESP_PUSH, 6.0f / 120);
    CHECK(NEAR(a.x, b.x, 1e-3f));
    CHECK(NEAR(a.v, b.v, 1e-2f));
}

static void test_retarget_keeps_velocity(void) {
    GuiSpring s = { 0, 0 };
    run(&s, 5, GUI_RESP_SELECT, 1.0f / 60, 0.05f);
    float x = s.x, v = s.v;
    CHECK(v > 0);
    // A new target doesn't jump the value or drop the velocity: over a
    // millisecond it moves by about v * 1 ms
    gui_spring_always(&s, 6, GUI_RESP_SELECT, 1.0f / 1000);
    CHECK(NEAR(s.x, x + v / 1000, 0.01f));
    CHECK(s.v > 0.9f * v);
}

static void test_bouncy(void) {
    gui_set_reduce_motion(false);
    // Released 10 rows past an edge: damping 0.8 swings past it (~0.6%
    // with 1/120 s semi-implicit Euler steps; the exact solution is 1.5%)...
    GuiSpring s = { 10, 0 };
    float low = 10;
    for (int i = 0; i < 240; i++) {
        gui_spring_bouncy(&s, 0, GUI_RESP_EDGE, 0.8f, 1.0f / 60);
        if (s.x < low) low = s.x;
    }
    CHECK(low < -0.03f);
    CHECK(low > -0.2f);
    CHECK(s.x == 0.0f);       // ...and settles
    // Critically damped from rest never crosses
    GuiSpring c = { 10, 0 };
    low = 10;
    for (int i = 0; i < 240; i++) {
        gui_spring_always(&c, 0, GUI_RESP_EDGE, 1.0f / 60);
        if (c.x < low) low = c.x;
    }
    CHECK(low >= 0.0f);

    // Reduced motion: no bounce, but it still moves (not a snap)
    gui_set_reduce_motion(true);
    GuiSpring r = { 10, 0 };
    gui_spring_bouncy(&r, 0, GUI_RESP_EDGE, 0.8f, 1.0f / 60);
    CHECK(r.x > 0 && r.x < 10);
    low = 10;
    for (int i = 0; i < 240; i++) {
        gui_spring_bouncy(&r, 0, GUI_RESP_EDGE, 0.8f, 1.0f / 60);
        if (r.x < low) low = r.x;
    }
    CHECK(low >= 0.0f);
    gui_set_reduce_motion(false);
}

static void test_reduce_motion(void) {
    gui_set_reduce_motion(true);
    CHECK(gui_reduce_motion());
    GuiSpring s = { 0, 3 };
    gui_spring(&s, 4, GUI_RESP_SELECT, 1.0f / 60);
    CHECK(s.x == 4.0f && s.v == 0.0f);   // positions snap
    GuiSpring p = { 0, 0 };
    gui_spring_always(&p, 1, GUI_RESP_PROGRESS, 1.0f / 60);
    CHECK(p.x > 0 && p.x < 1);           // the "always" variant keeps moving
    gui_set_reduce_motion(false);
    GuiSpring q = { 0, 0 };
    gui_spring(&q, 4, GUI_RESP_SELECT, 1.0f / 60);
    CHECK(q.x > 0 && q.x < 4);
}

static void test_dt_clamp(void) {
    CHECK(gui_dt_clamp(0) == 1.0f / 120);
    CHECK(gui_dt_clamp(-1) == 1.0f / 120);
    CHECK(gui_dt_clamp(NAN) == 1.0f / 120);
    CHECK(gui_dt_clamp(1.0f / 60) == 1.0f / 60);
    CHECK(gui_dt_clamp(2.5f) == 1.0f / 20);
}

static void test_effects(void) {
    // Linear in time, done at the duration, never before its cause
    CHECK(gui_effect_t(1000, 1000, 300) == 0.0f);
    CHECK(NEAR(gui_effect_t(1000, 1150, 300), 0.5f, 1e-6f));
    CHECK(gui_effect_t(1000, 1300, 300) == 1.0f);
    CHECK(gui_effect_t(1000, 99999, 300) == 1.0f);
    CHECK(gui_effect_t(0, 1150, 300) == 1.0f);      // never fired
    CHECK(gui_effect_t(2000, 1150, 300) == 0.0f);   // stamped after this frame began

    // Check scale: 0.6 at the toggle, rising without overshoot to 1
    CHECK(NEAR(gui_check_scale(0), 0.6f, 1e-6f));
    CHECK(NEAR(gui_check_scale(-1), 0.6f, 1e-6f));
    float prev = 0.6f;
    bool monotonic = true;
    for (int i = 1; i <= 60; i++) {
        float s = gui_check_scale(i / 120.0f);
        if (s < prev || s > 1.0f) monotonic = false;
        prev = s;
    }
    CHECK(monotonic);
    CHECK(gui_check_scale(0.15f) > 0.99f);
    CHECK(gui_check_scale(5.0f) == 1.0f);
    // The closed form: w = 2 pi / 0.15, at t = 1/w s = 1 - 0.8/e
    float t1 = GUI_CHECK_RESPONSE / (2.0f * 3.14159265f);
    CHECK(NEAR(gui_check_scale(t1), 1.0f - 0.8f / 2.7182818f, 1e-4f));
}

// Nested fades toward the background compose (a toast inside a fading
// transition stays faded), and an inner 0 changes nothing
static void test_fade_compose(void) {
    CHECK(gui_fade_compose(0, 0) == 0);
    CHECK(gui_fade_compose(0.6f, 0) == 0.6f);
    CHECK(gui_fade_compose(0, 0.4f) == 0.4f);
    CHECK(NEAR(gui_fade_compose(0.6f, 0.4f), 0.76f, 1e-5f));
    CHECK(gui_fade_compose(0.6f, 0.4f) >= 0.6f);
    CHECK(gui_fade_compose(1, 0.3f) == 1);
    CHECK(gui_fade_compose(2, -1) == 1);
    CHECK(gui_fade_compose(-1, 0.5f) == 0.5f);
}

int main(void) {
    test_effects();
    test_fade_compose();
    test_critically_damped();
    test_exact_in_dt();
    test_retarget_keeps_velocity();
    test_bouncy();
    test_reduce_motion();
    test_dt_clamp();
    TEST_DONE();
}
