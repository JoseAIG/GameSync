#ifndef UI_H
#define UI_H

// Screens and dialogs of the 3DS client, drawn with citro2d (see gui.h).
//
// Every screen is redrawn each frame. Blocking work (network, AM installs)
// draws a frame through ui_progress()/ui_busy() whenever it has news; modal
// dialogs draw on the bottom screen over a dimmed "backdrop", which is
// whatever screen is current (the save list, the catalog...).

#include "common.h"
#include "gui.h"
#include "sync.h"
#include "ui_format.h"
#include "ui_gesture.h"

void ui_init(void);
void ui_exit(void);

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// One snapshot per frame: aptMainLoop() + hidScanInput(), the keys, the
// accelerated repeat and the filtered touch (see ui_gesture.h). False when
// the app is closing. Every interactive loop reads its input through this.
bool ui_input_scan(UiInput *in);
// Ignore the finger on the screen (if any) until it lifts. Every modal
// calls it when it opens and before it returns.
void ui_input_latch_touch(void);

// What the formatting helpers need to know about this console (set once
// the config is loaded)
void ui_set_local_console_id(const char *console_id);
void ui_set_utc_offset(int utc_offset_minutes);

// A server timestamp (ISO 8601 or Unix seconds) as "Today 21:14",
// "Yesterday 21:14" or "Oct 12 21:14" in the console's local time, see
// ui_format_when_at()
void ui_format_when(const char *iso, char *out, int out_size);
// "This console" or "Another device · XXXX"
void ui_device_label(const char *console_id, char *out, int out_size);

// ---------------------------------------------------------------------------
// Backdrop
// ---------------------------------------------------------------------------

typedef void (*UiDrawFn)(void *ctx);

typedef struct {
    UiDrawFn top, bottom;
    void *ctx;
} UiBackdrop;

// Set what's drawn behind dialogs / progress; returns the previous one
UiBackdrop ui_set_backdrop(UiDrawFn top, UiDrawFn bottom, void *ctx);
void ui_restore_backdrop(UiBackdrop previous);
void ui_draw_backdrop_top(void);
void ui_draw_backdrop_bottom(void);

// ---------------------------------------------------------------------------
// Dialogs and progress
// ---------------------------------------------------------------------------

typedef enum {
    UI_TONE_ACCENT = 0,
    UI_TONE_OK,
    UI_TONE_WARN,
    UI_TONE_INFO,
    UI_TONE_ERR,
} UiTone;

u32 ui_tone_hex(UiTone tone);

// Body markup: a line starting with UI_DIM is drawn in the secondary colour,
// one starting with UI_HI in the dialog's tone colour.
#define UI_DIM "\x01"
#define UI_HI "\x02"

typedef struct {
    u32 keys;             // KEY_* mask that picks this button
    const char *button;   // glyph, see gui_button()
    const char *label;
} UiButton;

// Modal card on the bottom screen. Returns the key that closed it (one of the
// buttons' keys), 0 if the app is closing. Every button is also a touch
// target (commits on touch-up; the button with A, else the last, is the
// filled one). With count == 0 it is a message with one "A Continue":
// A/B/X/Y/START/SELECT/L/R close it, and so does a touch-up anywhere
// (returns KEY_TOUCH).
u32 ui_dialog(UiTone tone, const char *title, const char *body, const UiButton *buttons, int count);

typedef struct {
    // > 0: the first `row1` buttons share a first row, all filled (equal
    // choices, neither looks like the default); the rest fill a second row
    int row1;
    bool x_icon;   // an error card: a cross before the title
} UiDialogOpts;
u32 ui_dialog_ex(UiTone tone, const char *title, const char *body, const UiButton *buttons, int count,
                 const UiDialogOpts *opts);
// While a dialog runs: the keys of the button a finger is pressing, 0
// otherwise. The backdrop reads it to preview the choice before it is made
// (a key commits on its press frame, so only touch can preview).
u32 ui_dialog_pressed(void);
// Something failed: what happened (`reason`), whether anything changed
// (`safety`, dim) and what to do next. ERR tone with a cross in the title;
// B Close, plus A Try again when `retry`. True when A (try again) was picked.
bool ui_error_card(const char *title, const char *reason, const char *safety, const char *next, bool retry);
// Copy `s` cut to one line of dialog body text (for lists of names)
void ui_body_line(const char *s, char *out, int out_size);
// Message closed by any button
void ui_message(UiTone tone, const char *title, const char *body);
// A = yes, B = no
bool ui_confirm(UiTone tone, const char *title, const char *body, const char *yes_label);

typedef struct {
    const char *title;    // card title
    const char *name;     // what's being worked on (bold line)
    const char *status;   // line under the name
    float frac;           // 0..1; < 0 = indeterminate; > 1 = no bar
    const char *left;     // under the bar, left ("12 MB / 64 MB")
    const char *right;    // under the bar, right ("18%")
    const char *info1;    // extra lines (speed, time)
    const char *info2;
    bool cancel_hint;     // a "B Cancel" pill (see ui_progress_cancel_poll)
    UiTone tone;
} UiProgress;

// Draw one frame: backdrop + progress card. Doesn't wait for vsync.
void ui_progress(const UiProgress *p);
// For work that polls between progress frames: reads the input and returns
// true once B is held or a finger is on the card's Cancel pill (or the app
// is closing). It stays true for the rest of that operation, whose next
// frames then say "Cancelling..." with the pill pressed. Downloads poll a
// few times a second and can't see a release reliably, so touching the
// pill commits right away, like holding B.
bool ui_progress_cancel_poll(void);
// Indeterminate progress card with a message
void ui_busy(const char *title, const char *message);

// Pick one of `count` items from a list card (A picks, B cancels, D-pad /
// touch moves). Returns the index, or -1 if cancelled.
int ui_choose(UiTone tone, const char *title, const char *const *items, int count);

// ---------------------------------------------------------------------------
// Top-level tabs (L / R cycle them, wrapping)
// ---------------------------------------------------------------------------

#define UI_TAB_SAVES    0
#define UI_TAB_CATALOG  1
#define UI_TAB_SETTINGS 2
#define UI_TAB_COUNT    3

typedef enum {
    UI_NAV_STAY = 0,
    UI_NAV_PREV,    // L: previous tab
    UI_NAV_NEXT,    // R: next tab
    UI_NAV_EXIT,    // START, confirmed: leave the app
} UiNav;

// Top screen header of a top-level screen, showing the tab strip
void ui_tab_header(int active);
// The keys every top-level screen shares: L / R switch tabs, START asks
// to exit (over the current backdrop). UI_NAV_STAY for anything else.
UiNav ui_tab_nav(u32 down);

// ---------------------------------------------------------------------------
// Scrolling list with an animated selection bar
// ---------------------------------------------------------------------------

// The list controller (UiListAnim, ui_list_input, ui_list_keys,
// ui_list_edge, ui_list_reveal...) lives in ui_gesture.h.

// Draws one row. `hl` is how much of the row the gliding highlight bar
// covers (1 = fully under it, 0 = clear of it): pick every colour that
// differs on the bar as gui_mix(normal, on_bar, hl), never by comparing the
// index with the selection, or the new row's text turns dark on the
// background for a frame or two while the bar is still on its way.
// `pressed`: a finger is down on this row (the list already drew the
// pressed fill behind it).
typedef void (*UiRowFn)(void *ctx, int index, float x, float y, float w, float h,
                        float hl, bool pressed);

// Step the list's motion (once a frame) and draw rows floor(view) to
// view + rows + 1, the highlight bar, the pressed row and the scrollbar.
// `scroll` is the caller's int scroll (the target while the keys drive the
// viewport). No mask: an opaque header / action bar drawn afterwards hides
// the rows sliding under them; geom->clip clips to the list rect instead.
void ui_list_draw(UiListAnim *a, const UiListGeom *g, int count, int selected, int scroll,
                  UiRowFn row, void *ctx);
// The list's viewport is at the top (the header keeps its divider line)
bool ui_list_at_top(const UiListAnim *a);

// ---------------------------------------------------------------------------
// Action bar (bottom of the list screens, y 202-240)
// ---------------------------------------------------------------------------

typedef struct {
    const char *glyph;     // key glyph, NULL for a touch-only button
    const char *label;
    GuiBtnStyle style;
    u32 tone;              // 0 = HEX_ACCENT
    u32 keys;              // the key that does the same (drawn pressed while held)
    float width;           // 0 = the width rule (see ui_bar_layout)
    bool on;               // a toggle that is on: fill tinted with the tone
} UiBarItem;

#define UI_BAR_MAX 4

typedef struct {
    UiTap tap;
    int pressed;           // item a finger is on, -1
} UiBar;

#define UI_BAR_INIT { UI_TAP_INIT, -1 }

// Left to right from x 8, 8 px apart, at y 206 (30 px). The first item is
// max(120, glyph + label + 24) wide (or its `width`); the rest split what
// is left, at least 64 each. The touch rects span the bar's full height
// and half of each gap. Returns the right edge of the last button.
float ui_bar_layout(const UiBarItem *items, int n, GuiRect *draw, GuiRect *touch);
// Touch: the item committed this frame (touch-up on it) or -1. DISABLED
// items never commit.
int ui_bar_input(UiBar *bar, const UiInput *in, const UiBarItem *items, int n);
// `held`: keys held now (pass 0 when drawn behind a dialog)
void ui_bar_draw(const UiBar *bar, const UiBarItem *items, int n, u32 held);

// ---------------------------------------------------------------------------
// Save list (main screen)
// ---------------------------------------------------------------------------

#define VIEW_ALL  0
#define VIEW_3DS  1
#define VIEW_NDS  2

#define SAVES_ROWS GUI_LIST_ROWS
#define SAVES_ROW_H GUI_ROW_H
#define SAVES_LIST_Y GUI_LIST_Y
#define SAVES_MARK_W 26   // a tap left of this on a row toggles its mark

// The save list's geometry (ui_list_input / ui_list_draw)
#define SAVES_GEOM { 0, SAVES_LIST_Y, GUI_BOT_W, SAVES_ROWS, SAVES_ROW_H, false, true }

// Action bar items of the Saves screen, in this order
enum { SAVES_BAR_A = 0, SAVES_BAR_X, SAVES_BAR_Y, SAVES_BAR_COUNT };

typedef struct {
    const TitleInfo *titles;   // all titles
    const int *filtered;       // visible -> titles[] index
    int count;                 // visible count
    int selected, scroll;
    UiListAnim *anim;          // the list's presentation state (owned by the caller)
    int view_mode;
    int marked;
    const char *status;
    UiTone status_tone;
    u32 status_serial;         // bumped with every new status: the toast enters again
    const SaveDetails *details;  // last compare of the selected title, or NULL
    u64 synced_ms;             // with details: gui_clock_ms() of a sync this session, 0 = none
    int title_count;           // all titles (X works when the view is empty)
    // Live input state for drawing (zero when drawn behind a dialog)
    const UiBar *bar;
    u32 held;
    bool chip_pressed;         // finger on the "N marked" chip
    int seg_held;              // 1 + All/3DS/NDS segment under a finger, 0 = none
    // Search by name
    const char *search;        // the active search, "" = none
    int view_total;            // titles in the view before the search
    int search_held;           // finger on the search chip: 1 its query / magnifier, 2 its x, 0 none
    int empty_held;            // finger on an empty-result button: 1 + index, 0 none
} SavesView;

void ui_draw_saves_top(const SavesView *v);
void ui_draw_saves_bottom(const SavesView *v);
// The Saves action bar: A Sync / Resolve / Upload N marked, X Sync all,
// Y Details (SAVES_BAR_*). Returns the count.
int ui_saves_bar(const SavesView *v, UiBarItem *items);
// Touch target of the header's "N marked" chip (B = unmark all); w = 0
// when nothing is marked
GuiRect ui_saves_marked_chip(const SavesView *v);
// Touch targets of the header's All / 3DS / NDS segments (VIEW_*); returns 3
int ui_saves_segments(GuiRect *touch);
// Touch targets of the header's search chip: [0] the magnifier (no search)
// or the query (re-opens the keyboard), [1] the x that clears it. Returns
// the count (1 without a search, 2 with one).
int ui_saves_search_targets(const SavesView *v, GuiRect *touch);
// Buttons shown when a search matches nothing: [0] B Clear search,
// [1] ZR Edit search. Returns 2, or 0 when they aren't shown.
int ui_saves_empty_buttons(const SavesView *v, GuiRect *draw, GuiRect *touch);
// A New 3DS (ZR, the C-stick): only then are ZR hints shown
bool ui_is_new3ds(void);

// ---------------------------------------------------------------------------
// Save dialogs
// ---------------------------------------------------------------------------

// Save details (local vs server); returns when B (or A) is pressed
void ui_show_save_details(const TitleInfo *title, const SaveDetails *details);

// Upload / download confirmation with save details. A = true, B = false.
// `local_backups`: the config's value (what the card says about backups).
bool ui_confirm_sync(const TitleInfo *title, const SaveDetails *details, bool is_upload, int local_backups);

// Smart sync dialog: shows the comparison and the suggested action.
// Returns SYNC_ACTION_UPLOAD / SYNC_ACTION_DOWNLOAD to perform, or
// SYNC_ACTION_UP_TO_DATE for "nothing to do / cancelled" (an up-to-date
// save opens no dialog at all). For a conflict A keeps this console's copy
// (uploads; the server keeps the old copy in its history), X keeps the
// server's (downloads), B cancels; the two are equal buttons mirroring the
// cards, and pressing one previews which copy stays. When the server
// couldn't be queried (SYNC_ACTION_SERVER_ERROR) it only explains that and
// returns SYNC_ACTION_SERVER_ERROR: never an upload or download.
SyncAction ui_confirm_smart_sync(const TitleInfo *title, const SaveDetails *details, SyncAction suggested,
                                 int local_backups);

// History versions; returns the chosen timestamp (caller frees) or NULL
char *ui_show_history(const TitleInfo *title, HistoryVersion *versions, int version_count);

// Restore confirmation: this console's save now (details, from a compare)
// beside history version `index` of `count`. True only for A.
bool ui_confirm_restore(const TitleInfo *title, const SaveDetails *details,
                        const HistoryVersion *version, int index, int count, int local_backups);

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

typedef enum {
    SETTINGS_NAV_PREV = 0,      // L
    SETTINGS_NAV_NEXT,          // R
    SETTINGS_EXIT,              // START, confirmed
    SETTINGS_RESCAN,            // "Rescan titles"
    SETTINGS_REFRESH_CATALOG,   // "Refresh catalog"
    SETTINGS_UPDATE,            // "Check for updates"
} SettingsResult;

// Runs a SETTINGS_RESCAN / _REFRESH_CATALOG / _UPDATE entry (over the
// settings screen). Returns true if the app must close (update installed).
typedef bool (*SettingsActionFn)(SettingsResult action);

// Settings tab. Edited values are written to the config file as soon as
// they are confirmed. Returns SETTINGS_NAV_PREV / _NEXT / _EXIT when the
// user leaves the tab.
SettingsResult ui_settings_tab(AppConfig *config, SettingsActionFn run_action);

// ---------------------------------------------------------------------------
// Text entry
// ---------------------------------------------------------------------------

typedef struct {
    const char *title;      // what is being edited (D-pad editor header)
    const char *hint;       // shown while the field is empty
    const char *ok_label;   // the confirm button, NULL = "OK"
    bool password;          // characters hidden a moment after typing
    bool url;               // only "http://..." / "https://..." is accepted
    bool allow_empty;       // an empty text can be confirmed
} UiTextField;

// Edit `buffer` (max_len bytes with the NUL): the system keyboard, or a
// D-pad editor if the keyboard applet can't be started. Text the field
// refuses (ui_text_field_error) keeps the keyboard open with the reason.
// Returns true if confirmed; `buffer` is untouched otherwise.
bool ui_edit_text_ex(const UiTextField *f, char *buffer, int max_len);
// The same with OK, any text, empty allowed
bool ui_edit_text(const char *title, const char *hint, char *buffer, int max_len);

#endif // UI_H
