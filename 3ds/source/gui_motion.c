// Spring response/damping parameterisation and the rubber-band formula from
// Apple's "Designing Fluid Interfaces" (WWDC 2018). Written from the talk's
// description; no code copied.
#include "gui_motion.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool reduce_motion;

void gui_set_reduce_motion(bool on) { reduce_motion = on; }
bool gui_reduce_motion(void) { return reduce_motion; }

float gui_dt_clamp(float seconds) {
    if (!(seconds >= 1.0f / 120.0f)) return 1.0f / 120.0f;  // also catches NaN
    if (seconds > 1.0f / 20.0f) return 1.0f / 20.0f;
    return seconds;
}

bool gui_spring_resting(const GuiSpring *s, float target) {
    return fabsf(s->x - target) < 0.01f && fabsf(s->v) < 0.05f;
}

// x(t) = target + (x0 + (v0 + w x0) t) e^(-w t), w = 2 pi / response
void gui_spring_always(GuiSpring *s, float target, float response, float dt) {
    if (response <= 0) {
        gui_spring_set(s, target);
        return;
    }
    float w = 2.0f * (float)M_PI / response;
    float x0 = s->x - target;
    float e = expf(-w * dt);
    float t = (s->v + w * x0) * dt;
    s->x = target + (x0 + t) * e;
    s->v = (s->v - w * t) * e;
    if (gui_spring_resting(s, target)) gui_spring_set(s, target);
}

void gui_spring(GuiSpring *s, float target, float response, float dt) {
    if (reduce_motion) {
        gui_spring_set(s, target);
        return;
    }
    gui_spring_always(s, target, response, dt);
}

void gui_spring_bouncy(GuiSpring *s, float target, float response, float damping, float dt) {
    if (reduce_motion || damping >= 1.0f || response <= 0) {
        gui_spring_always(s, target, response, dt);
        return;
    }
    float w = 2.0f * (float)M_PI / response;
    int n = (int)(dt * 120.0f + 0.5f);
    if (n < 1) n = 1;
    float h = dt / n;
    for (int i = 0; i < n; i++) {
        // Semi-implicit Euler: velocity first, then position with the new one
        float a = -2.0f * damping * w * s->v - w * w * (s->x - target);
        s->v += a * h;
        s->x += s->v * h;
    }
    if (gui_spring_resting(s, target)) gui_spring_set(s, target);
}

// (o * d * c) / (d + c * |o|): slope c near the bound, then ever less,
// approaching d
float gui_rubberband(float overshoot, float dimension, float constant) {
    if (dimension <= 0) return 0;
    return (overshoot * dimension * constant) / (dimension + constant * fabsf(overshoot));
}

float gui_fade_compose(float outer, float inner) {
    if (!(outer > 0)) outer = 0;
    if (!(inner > 0)) inner = 0;
    if (outer >= 1 || inner >= 1) return 1;
    // = 1 - (1 - outer)(1 - inner), but exact when either is 0
    float f = outer + inner - outer * inner;
    return f > 1 ? 1 : f;
}

float gui_effect_t(unsigned long long since_ms, unsigned long long now_ms, unsigned duration_ms) {
    if (!since_ms || duration_ms == 0) return 1.0f;
    if (now_ms < since_ms) return 0.0f;
    unsigned long long age = now_ms - since_ms;
    if (age >= duration_ms) return 1.0f;
    return (float)age / (float)duration_ms;
}

float gui_check_scale(float t) {
    if (!(t > 0)) return 0.6f;
    float wt = 2.0f * (float)M_PI / GUI_CHECK_RESPONSE * t;
    float s = 1.0f - 0.4f * (1.0f + wt) * expf(-wt);
    return s > 1.0f ? 1.0f : s;
}
