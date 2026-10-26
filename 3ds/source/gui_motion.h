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

#endif // GUI_MOTION_H
