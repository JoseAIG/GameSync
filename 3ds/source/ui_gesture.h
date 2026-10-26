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
    bool t_cancel;               // with t_up: the touch ended on an unconfirmed jump
                                 // (a flick that left on its first fast frame, or a
                                 // release glitch): never a tap
    float tx, ty;                // current point; on t_up the LAST HELD sample
    float tx0, ty0;              // point at touch-down
    float moved;                 // max distance from (tx0, ty0) during this touch
    u64 t_ms;                    // monotonic ms at scan
} UiInput;

// Touch tracker: one finger, raw samples in, filtered edges out.
//
// Spike rejection (not a speed limit): a held sample more than 40 px from
// both the last accepted one and where the finger's last step would carry
// it is held back a frame. The next sample confirms it (a real fast flick)
// if it lies within 16 px of it or carries on the same way (further along
// the jump, inside a ~27 degree cone); both are then accepted in order.
// A sample that turns back was a resistive-panel glitch: the jump is
// dropped. Once a flick is confirmed, its speed is the prediction, so
// constant fast motion passes with no further delay. A jump on the last
// frame before release is dropped and the release is flagged t_cancel.
// Ordinary samples pass with no delay.
//
// Latch: after ui_touch_latch() a finger already on the screen is ignored
// until it lifts, so the touch that opened or closed a dialog never taps or
// drags what is underneath.
typedef struct {
    bool down;          // a touch is being tracked
    bool latched;       // ignore the screen until it is released
    float ax, ay;       // last accepted sample
    float sx, sy;       // last accepted step (accepted minus the one before)
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

// ---------------------------------------------------------------------------
// List controller: keys, taps, drag, momentum and rubber band
// ---------------------------------------------------------------------------
//
// The selection and the scroll stay plain ints owned by the caller (they
// are what hit-testing of data windows and v055's key logic work on); the
// springs below are what is drawn, in rows. Per frame the caller:
//
//   ev = ui_list_input(&anim, &geom, count, &selected, &scroll, &in, in.rep & UI_LIST_KEYS);
//   if (ev.kind != UI_LIST_REATTACHED) { its v055 key handling (ui_list_keys, update_scroll) }
//   ... acts on ev (UI_LIST_TAP / UI_LIST_TAP_SELECTED) ...
//   ui_list_draw(&anim, &geom, count, selected, scroll, row_fn, ctx);   // steps the motion
//
// Touch: a touch-down presses the row under it; past 8 px of vertical
// travel it becomes a drag that tracks the finger 1:1 (with a rubber band
// past the ends) and the tap is cancelled. On release the scroll coasts
// from the release velocity and lands exactly on the row the momentum
// projects to (Apple's scroll deceleration, rate 0.998), or bounces softly
// off an end the projection crossed. A touch while it coasts stops it.
// A drag leaves the selection where it was, possibly off screen
// ("detached"): the next UP/DOWN/page key then selects the nearest visible
// row instead of moving (UI_LIST_REATTACHED), and ui_list_reveal() brings
// the selection back into view when A/Y act on it.

#define UI_LIST_KEYS (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)
#define UI_LIST_DRAG_PX 8.0f         // drag / tap disambiguation
#define UI_LIST_SAMPLES 6
#define UI_LIST_VELOCITY_MS 60       // release velocity from the last 60 ms
#define UI_LIST_RUBBER 0.55f
#define UI_DECEL_RATE 0.998f         // per ms, Apple's normal scroll deceleration

typedef struct {
    float x, y, w;        // top-left of the list, width
    int rows;             // visible rows (a page)
    float row_h;
    bool clip;            // inside a card: clip the rows to the list rect
    bool edges;           // under a header / above an action bar: scroll-edge shadows
} UiListGeom;

typedef enum {
    UI_LIST_NONE = 0,
    UI_LIST_TAP,            // a row was tapped (and is now selected)
    UI_LIST_TAP_SELECTED,   // the row that was already selected was tapped
    UI_LIST_REATTACHED,     // a move key selected the nearest visible row: skip the move
} UiListEventKind;

typedef struct {
    UiListEventKind kind;
    int index;   // the row tapped
    float px;    // screen x of the tap (e.g. to test a mark box)
} UiListEvent;

typedef enum {
    UI_COAST_NONE = 0,
    UI_COAST_EXP,      // decelerating onto a whole row inside the list
    UI_COAST_SETTLE,   // spring onto the nearest row (tiny flick / stopped)
    UI_COAST_DECAY,    // decelerating toward an end the projection crossed
    UI_COAST_BOUNCE,   // past that end: damping 0.8 spring back
    UI_COAST_RETURN,   // released while pulled past an end: damping 1 back
} UiCoastMode;

typedef struct {
    GuiSpring sel;            // highlight position, rows
    GuiSpring scroll;         // viewport top, rows; past an end it is the raw
                              // finger position, drawn through the rubber band
    int count;
    bool init;
    // Gesture
    bool tracking, dragging, coasting, detached;
    float grab_y, grab_scroll;
    struct { float y; u64 ms; } samples[UI_LIST_SAMPLES];
    int nsamples;
    float coast_target, coast_lambda;
    bool coast_to_edge;
    int pressed_row;          // valid while tracking && !dragging, -1 none
    GuiSpring nudge;          // px added to the highlight: the end-of-list nudge
    u64 wrap_ms;              // last wrap (rows cross-fade), 0 = none
    u64 flash_ms;             // last wrap or clamp (scrollbar flash), 0 = none
    bool snap;                // a wrap: jump to the new selection on the next step
    u8 coast_mode;            // UiCoastMode
    bool no_tap;              // this touch stopped a coast: no tap on release
    bool froze;               // this touch stopped a coast at a fractional row
    int sel_at_down;
    bool wb_pending;          // a coast ended: *scroll = wb at the next input
    int wb;
    bool range_on;            // optional landing range (catalog live window)
    int range_lo, range_hi;
    float release_v;          // |velocity| at release, rows/s
    u32 coast_overspeed;      // frames a coast ran faster than its release (should stay 0)
    int rows;                 // geometry of the last input / draw
    float row_h;
    u32 frame;                // last stepped frame (ui.c)
} UiListAnim;

// Report an edge the caller's key handler hit (only it knows a key was
// pressed): a wrap jumps and cross-fades the rows in; a press that can't
// move further nudges the highlight 6 px that way. Both flash the thumb.
typedef enum {
    UI_EDGE_WRAP = 0,
    UI_EDGE_CLAMP_TOP,
    UI_EDGE_CLAMP_BOTTOM,
} UiListEdge;
void ui_list_edge(UiListAnim *a, UiListEdge edge);

// The rows changed under the viewport (a new filter): fade them in from
// 30% over GUI_FADE_MS, like a wrap, without moving anything
void ui_list_crossfade(UiListAnim *a);

// v055's list keys on `*selected`: UP / DOWN move one row and wrap, LEFT /
// RIGHT move `page` rows and clamp, reporting edges to ui_list_edge().
void ui_list_keys(UiListAnim *a, u32 rep, int count, int page, int *selected);

UiListEvent ui_list_input(UiListAnim *a, const UiListGeom *g, int count, int *selected, int *scroll,
                          const UiInput *in, u32 keys_moved);
// Advance the motion by dt seconds (ui_list_draw does this once a frame)
void ui_list_step(UiListAnim *a, int rows, float row_h, int count, int selected, int scroll, float dt);

// The viewport top as drawn (rows, through the rubber band past the ends)
float ui_list_view(const UiListAnim *a);
// floor(view), clamped to >= 0: the first row on screen, for data windows
int ui_list_first_visible(const UiListAnim *a);
// No finger on it and no coast running
bool ui_list_settled(const UiListAnim *a);
// Row drawn pressed (a finger is on it, not dragging), or -1
int ui_list_pressed_row(const UiListAnim *a);
// Start over (new data): the next input/draw takes the caller's ints as is
void ui_list_reset(UiListAnim *a);
// A/Y act on the selection: stop any coast, write the viewport back and
// scroll `*scroll` (GUI_RESP_SELECT, or a snap past a page) to show it
void ui_list_reveal(UiListAnim *a, int count, int selected, int *scroll);
// Limit where a coast may land (rows [lo, hi]); for a partly loaded list
void ui_list_set_range(UiListAnim *a, int lo, int hi);
void ui_list_clear_range(UiListAnim *a);

#endif // UI_GESTURE_H
