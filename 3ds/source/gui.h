#ifndef GAMESYNC_GUI_H
#define GAMESYNC_GUI_H

// Small drawing kit on top of citro2d: frame handling, the GameSync dark
// palette, rounded panels, system-font text (measure / fit / wrap), pills,
// button glyphs, progress bars and the shared header / footer chrome.
//
// The design language is shared with the DS client: deep slate background,
// lighter cards, teal accent, green / amber / blue / red status colours.

#include "common.h"
#include "gui_motion.h"
#include <citro2d.h>

#define GUI_TOP 0
#define GUI_BOTTOM 1

#define GUI_TOP_W 400
#define GUI_BOT_W 320
#define GUI_H 240

#define GUI_HEADER_H 26
#define GUI_FOOTER_H 22
#define GUI_FOOTER_Y (GUI_H - GUI_FOOTER_H)

// Bottom-screen layout of the list screens (Saves, Catalog, History,
// Settings): header 0-26, a list of 8 rows x 22 px at 26-202, then a 38 px
// action bar of 30 px buttons. The header and the bar are opaque and drawn
// after the list, so they hide the rows sliding under them.
#define GUI_LIST_Y GUI_HEADER_H
#define GUI_LIST_ROWS 8
#define GUI_ROW_H 22
#define GUI_BAR_Y 202
#define GUI_BAR_H (GUI_H - GUI_BAR_Y)

// Buttons (gui_btn)
#define GUI_BTN_H 30
#define GUI_BTN_R 8
#define GUI_BTN_GAP 8
#define GUI_BTN_MIN_W 64

// Text scales. The system font has one weight, so colour carries the rest
// of the hierarchy: titles and values in HEX_TEXT, labels, captions and
// hints in HEX_DIM.
#define GUI_S_TITLE 0.62f  // game name, screen title
#define GUI_S_BODY 0.50f   // values, rows, dialog body
#define GUI_S_SMALL 0.43f  // labels, captions, hints
#define GUI_S_TINY 0.40f   // pills and tags only

// Palette (0xRRGGBB)
#define HEX_BG 0x0F1720
#define HEX_BG2 0x1B2633
#define HEX_PANEL 0x243244
#define HEX_PANEL_HI 0x2C3D52
#define HEX_LINE 0x33465C
#define HEX_TEXT 0xE6EDF3
#define HEX_DIM 0x8DA2B5
#define HEX_MUTED 0x6E8396   // disabled / placeholder text only
#define HEX_ACCENT 0x2EC4B6
#define HEX_ACCENT2 0x3DDBD9
#define HEX_OK 0x3FB950
#define HEX_WARN 0xF0B429
#define HEX_INFO 0x58A6FF
#define HEX_ERR 0xF85149
#define HEX_CART 0x39C5F7
#define HEX_NDS 0xC678DD
#define HEX_RA 0xE5B143
#define HEX_INK 0x0F1720   // dark text on accent fills

static inline u32 gui_rgb(u32 hex) {
    return C2D_Color32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 0xFF);
}
static inline u32 gui_rgba(u32 hex, u8 a) {
    return C2D_Color32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, a);
}
// Opaque blend of two 0xRRGGBB colours, t = 0 -> a, 1 -> b
u32 gui_mix(u32 a, u32 b, float t);
// The same blend as 0xRRGGBB (to blend again)
u32 gui_mix_hex(u32 a, u32 b, float t);

typedef enum {
    GUI_LEFT = 0,
    GUI_CENTER,
    GUI_RIGHT,
} GuiAlign;

// ---------------------------------------------------------------------------
// Lifetime and frames
// ---------------------------------------------------------------------------

// gfxInitDefault + citro3d/citro2d + both screen targets + system font
void gui_init(void);
void gui_exit(void);

// Start a frame. vsync = true paces to 60 fps (interactive loops); false
// draws right away (progress updates in the middle of a transfer). Also
// samples the monotonic clock: see gui_now_ms() / gui_dt().
void gui_begin(bool vsync);

#ifdef GUI_PERF
// Frames in which a list coast ran faster than its release velocity
// (drawn in red by the overlay; must stay 0)
extern u32 gui_perf_coast_overspeed;
#endif
// Select and clear a screen (GUI_TOP / GUI_BOTTOM), draws the background
void gui_screen(int screen);
void gui_end(void);

// Width of the selected screen, frame counter. The counter must not drive
// animation: frames are irregular (progress cards draw a few times a second)
float gui_w(void);
u32 gui_ticks(void);

// Time. One monotonic clock, from the system tick counter (never osGetTime(),
// which is the user-settable RTC):
// seconds since the previous gui_begin(), clamped to [1/120, 1/20]
float gui_dt(void);
// ms since gui_init(), sampled at the current gui_begin()
u64 gui_now_ms(void);
// the same clock, read now (for code that runs between frames)
u64 gui_clock_ms(void);

// Content offset + fade toward the background, e.g. a tab or a folder level
// entering from one side. start() at the change, then wrap the content's
// drawing in begin()/end() every frame (begin() steps once per frame, so it
// may wrap content on both screens). Under reduced motion the offset is 0
// and a GUI_FADE_MS fade runs instead.
typedef struct {
    GuiSpring off;    // px
    float from;       // px the content started from
    u64 start_ms;
    u32 frame;        // gui_ticks() of the last step
} GuiTransition;
void gui_transition_start(GuiTransition *t, float from_px);
void gui_transition_begin(GuiTransition *t);
void gui_transition_end(void);

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

void gui_rect(float x, float y, float w, float h, u32 color);
void gui_vgrad(float x, float y, float w, float h, u32 top, u32 bottom);
// Rounded rectangle; colour must be opaque (corners overlap)
void gui_rrect(float x, float y, float w, float h, float r, u32 color);
void gui_circle(float cx, float cy, float r, u32 color);
// Small filled arrows / marks, centred on (cx, cy), `s` = size in px
void gui_icon_up(float cx, float cy, float s, u32 color);
void gui_icon_down(float cx, float cy, float s, u32 color);
void gui_icon_check(float cx, float cy, float s, u32 color);
// A cross (close / clear), a "<" chevron and a magnifier, drawn as shapes
// (the system font isn't guaranteed to have these symbols)
void gui_icon_x(float cx, float cy, float s, u32 color);
void gui_icon_chevron_left(float cx, float cy, float s, u32 color);
void gui_icon_search(float cx, float cy, float s, u32 color);

// Clip what's drawn next to a rectangle of the current screen, until
// gui_clip_off(). Flushes what was drawn before.
void gui_clip(float x, float y, float w, float h);
void gui_clip_off(void);

// ---------------------------------------------------------------------------
// Text (system font, UTF-8)
// ---------------------------------------------------------------------------

float gui_line_h(float scale);
float gui_text_w(float scale, const char *s);
// Draws at (x, y) = top of the line box. Returns the drawn width.
float gui_text(float x, float y, float scale, u32 color, GuiAlign align, const char *s);
float gui_textf(float x, float y, float scale, u32 color, GuiAlign align, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));
// The ellipsis in use: U+2026 when the system font has it, else "..."
const char *gui_ellipsis(void);
// Copy `s` into `out`, cut to `max_w` with an ellipsis if needed
void gui_fit(const char *s, float scale, float max_w, char *out, size_t size);
// Cut to `max_w` with an ellipsis
float gui_text_fit(float x, float y, float scale, u32 color, GuiAlign align, float max_w, const char *s);
// Vertically centred in a box of height h
float gui_text_mid(float x, float y, float h, float scale, u32 color, GuiAlign align, float max_w, const char *s);

#define GUI_WRAP_LINE 160
// Greedy word wrap of `s` ('\n' forces a break) into at most `max_lines`
// lines of `max_w`; the last line gets an ellipsis if text remains.
// Returns the number of lines.
int gui_wrap(const char *s, float scale, float max_w, char lines[][GUI_WRAP_LINE], int max_lines);
// Wrap and draw; returns the number of lines drawn
int gui_text_wrap(float x, float y, float scale, u32 color, float max_w, int max_lines, const char *s);

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

float gui_pill_w(float h, float scale, const char *label);
// Rounded label of height h; returns its width
float gui_pill(float x, float y, float h, float scale, u32 bg, u32 fg, const char *label);
// Outlined variant (for low-emphasis tags)
float gui_pill_outline(float x, float y, float h, float scale, u32 color, u32 fill, const char *label);

// Button glyph: "A" "B" "X" "Y" "L" "R" "ZR" "START" "SELECT" "DPAD" "UD"
// "LR". (x, y) is the left edge and the vertical centre. Returns the width.
float gui_button(float x, float cy, const char *button);
float gui_button_w(const char *button);

// Pill button: the key glyph (may be NULL) then the label, centred.
// PRIMARY is filled with the tone (0 = HEX_ACCENT), TONAL is a neutral
// raised fill, DISABLED is flat with muted text. `pressed` (a finger on it,
// or its key held) darkens the fill and insets it by 1 px. `r` is the drawn
// rect; touch targets are usually larger.
typedef enum {
    GUI_BTN_PRIMARY = 0,
    GUI_BTN_TONAL,
    GUI_BTN_DISABLED,
} GuiBtnStyle;
void gui_btn(GuiRect r, const char *glyph, const char *label, GuiBtnStyle style, u32 tone_hex, bool pressed);
// The same; `on` = a TONAL toggle that is on: fill tinted toward the tone
void gui_btn_ex(GuiRect r, const char *glyph, const char *label, GuiBtnStyle style, u32 tone_hex,
                bool pressed, bool on);
// Width of the glyph + label inside a button (without padding)
float gui_btn_content_w(const char *glyph, const char *label);

// Progress bar; frac < 0 draws an animated indeterminate bar
void gui_bar(float x, float y, float w, float h, float frac, u32 color);
void gui_spinner(float cx, float cy, float r, u32 color);
void gui_scrollbar(float x, float y, float h, int first, int visible, int total, float smooth_first);
// The same with the thumb colour given (0xRRGGBB)
void gui_scrollbar_ex(float x, float y, float h, int visible, int total, float smooth_first, u32 thumb_hex);

// Rounded card with a title strip. tone = title accent colour (0xRRGGBB)
void gui_card(float x, float y, float w, float h, const char *title, u32 tone);
// Plain panel (no title)
void gui_panel(float x, float y, float w, float h);
// Translucent overlay over the whole screen (behind a modal card)
void gui_dim(void);

// Header bar: "GameSync" + section title on the left. On the top screen it
// also shows the version, WiFi strength and the server status dot.
void gui_header(const char *section);
// Header bar background only (for custom headers, e.g. tabs)
void gui_header_bar(void);
// The same; `line` = draw the 1 px divider under it (left out while list
// content is scrolled under the header, see gui_scroll_edges)
void gui_header_bar_ex(bool line);
// Scroll-edge shadows of a list: 6 px fading down from `top_y` when rows
// are scrolled under the header (`above`), 6 px fading up to `bottom_y`
// when more rows continue under the action bar (`below`)
void gui_scroll_edges(float x, float w, float top_y, float bottom_y, bool above, bool below);
// Header of a top-level screen: logo, then the L / R shoulder glyphs around
// the segmented tab strip (the active tab highlighted); WiFi, server dot and
// version on the right of the top screen.
void gui_header_tabs(const char *const *labels, int count, int active);
// WiFi bars + server dot ending at x_right; returns the left edge
float gui_status_icons(float x_right, float cy);

typedef struct {
    const char *button;   // glyph name, see gui_button
    const char *label;
} GuiHint;

// Footer bar with button hints, laid out left to right
void gui_footer(const GuiHint *hints, int count);

// Segmented tabs. The pill's x and width spring toward the active segment
// (GUI_RESP_PILL), so a change glides from wherever the pill is, and each
// label's colour follows how much of the pill covers it. `anim` may be
// NULL (no motion); keep one per strip, zeroed to start.
#define GUI_TABS_MAX 6
typedef struct {
    GuiSpring x, w;     // pill, px from the strip's left edge
    int count;
    bool init;
    u32 frame;          // last stepped frame
    int held;           // 1 + index of the segment under a finger, 0 = none
} GuiTabsAnim;
// Segment rects of a strip at (x, y), height h, without drawing; returns
// the strip's right edge
float gui_tabs_layout(float x, float y, float h, const char *const *labels, int count, GuiRect *rects);
// Draws the strip; `rects` (may be NULL) gets each segment's rect.
// Returns the right edge.
float gui_tabs(GuiTabsAnim *anim, float x, float y, float h, const char *const *labels, int count, int active,
               GuiRect *rects);

#endif // GAMESYNC_GUI_H
