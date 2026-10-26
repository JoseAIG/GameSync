#ifndef GUI_MOTION_H
#define GUI_MOTION_H

// Springs and motion tokens for the GUI. Plain C with no citro2d or libctru
// dependency, so the maths is host-tested (tests/test_gui_motion.c); gui.h
// includes this header, so callers just include gui.h.
//
// A spring is described the way Apple's UIKit/SwiftUI springs are: a
// `response` (seconds; roughly how quickly the value gets there) and a
// damping ratio (1 = critically damped, no overshoot). Every spring keeps
// its velocity, so retargeting one mid-flight continues smoothly from the
// value on screen instead of restarting.

#include <stdbool.h>

typedef struct {
    float x;   // presentation value
    float v;   // velocity, units/s
} GuiSpring;

// Motion tokens (response in seconds; damping 1 unless stated)
#define GUI_RESP_SELECT   0.16f   // selection highlight, scroll following the keys
#define GUI_RESP_SETTLE   0.30f   // scroll snapping to a row after a drag/flick
#define GUI_RESP_EDGE     0.35f   // rubber-band spring-back (damping 0.8 only after a flick)
#define GUI_RESP_PILL     0.25f   // tab/segment pill, switch knob
#define GUI_RESP_MODAL    0.25f   // modal card rise/scale
#define GUI_RESP_SCRIM    0.20f   // modal scrim alpha
#define GUI_RESP_PUSH     0.25f   // push/pop of a level, tab content entry
#define GUI_RESP_NUDGE    0.25f   // "nothing more here" nudge at list ends
#define GUI_RESP_PROGRESS 0.30f   // shown progress-bar fraction

#define GUI_FADE_MS 120           // reduced-motion fades, wrap cross-fade

// Critically damped (damping 1), exact closed form: stable at any dt.
// Under reduced motion it snaps to the target.
void gui_spring(GuiSpring *s, float target, float response, float dt);
// The same, ignoring reduced motion (direct manipulation, progress)
void gui_spring_always(GuiSpring *s, float target, float response, float dt);
// Under-damped (damping < 1), integrated in ~1/120 s substeps. Only after a
// flick that earned the bounce. Under reduced motion it is critically damped.
void gui_spring_bouncy(GuiSpring *s, float target, float response, float damping, float dt);
static inline void gui_spring_set(GuiSpring *s, float x) { s->x = x; s->v = 0; }
// |x - target| < 0.01 and |v| < 0.05
bool gui_spring_resting(const GuiSpring *s, float target);

void gui_set_reduce_motion(bool on);
bool gui_reduce_motion(void);

// A frame time clamped to [1/120, 1/20] s, so a long stall (a blocking
// network call between frames) can't make anything jump
float gui_dt_clamp(float seconds);

// Rubber band: how far content follows a drag `overshoot` past a bound,
// for a viewport of `dimension` (same units). Follows at `constant` (Apple
// uses 0.55) near the bound, resists more the further it goes and never
// reaches `dimension`.
float gui_rubberband(float overshoot, float dimension, float constant);

// Timestamp-driven effects (no state: they are pure functions of the time
// since their cause, so they can only fire on that cause)

// How far an effect of `duration_ms` that started at `since_ms` has run at
// `now_ms`: 0 at the start, 1 when done (and when `since_ms` is 0 = never,
// or in the future)
float gui_effect_t(unsigned long long since_ms, unsigned long long now_ms, unsigned duration_ms);

// Two fades toward the same colour, `outer` (already applied) then `inner`
// (0 = none, 1 = all the way), as one: 1 - (1 - outer)(1 - inner), in [0, 1]
float gui_fade_compose(float outer, float inner);

// Scale of a mark's check popping in, `t` seconds after the toggle: the
// analytic critically damped spring from 0.6 to 1 with response 0.15 s,
// s(t) = 1 - 0.4 (1 + wt) e^(-wt), w = 2 pi / 0.15
#define GUI_CHECK_RESPONSE 0.15f
float gui_check_scale(float t);

#define GUI_RING_MS 300           // state-change ring around a row's badge
#define GUI_TOAST_FLASH_MS 400    // a new toast's background flash
#define GUI_TOAST_STALE_MS 4000   // after this a toast reads as old news

// Screen rectangle (also used as a touch target)
typedef struct {
    float x, y, w, h;
} GuiRect;

static inline GuiRect gui_rect_make(float x, float y, float w, float h) {
    GuiRect r = { x, y, w, h };
    return r;
}
// `r` grown by `d` px on every side
static inline GuiRect gui_rect_grow(GuiRect r, float d) {
    GuiRect g = { r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d };
    return g;
}
static inline bool gui_rect_contains(GuiRect r, float x, float y) {
    return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// A view transform with one scale for both axes: p' = s * p + (tx, ty).
// citro2d's C2D_ViewTranslate/C2D_ViewScale build the same matrix on the
// GPU; this copy is what clipping rects (scissor, in screen pixels) and
// tests use.
typedef struct {
    float s, tx, ty;
} GuiXf;

#define GUI_XF_IDENTITY ((GuiXf){ 1.0f, 0.0f, 0.0f })

// Scale by `s` around (px, py), then move by (dx, dy)
GuiXf gui_xf_pivot(float s, float px, float py, float dx, float dy);
// `outer` applied after `inner`
GuiXf gui_xf_compose(GuiXf outer, GuiXf inner);
GuiRect gui_xf_rect(GuiXf xf, GuiRect r);
bool gui_xf_is_identity(GuiXf xf);

// A modal card entering: `t` is its presentation value (0 = just opened,
// 1 = at rest). It rises from GUI_MODAL_RISE_PX below and grows from
// GUI_MODAL_SCALE0 around the pivot (the card's centre, or the row it was
// opened from), so at t = 1 it is the identity.
#define GUI_MODAL_RISE_PX 8.0f
#define GUI_MODAL_SCALE0 0.96f
GuiXf gui_modal_xf(float t, float pivot_x, float pivot_y);

// The modal scrim's opacity (0..1 of its full 0xB4) on screen
#define GUI_SCRIM_ALPHA 0xB4

#endif // GUI_MOTION_H
