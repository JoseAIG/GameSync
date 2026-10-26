#include "ui.h"
#include <math.h>
#include "config.h"
#include "sync.h"
#include "network.h"

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

static void splash_top(void *ctx);
static void splash_bottom(void *ctx);

static UiBackdrop backdrop = { splash_top, splash_bottom, NULL };

static bool new3ds;

void ui_init(void) {
    gui_init();
    ui_gesture_set_clock(gui_clock_ms);
    APT_CheckNew3DS(&new3ds);
}

bool ui_is_new3ds(void) {
    return new3ds;
}

void ui_exit(void) {
    gui_exit();
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

u32 ui_tone_hex(UiTone tone) {
    switch (tone) {
        case UI_TONE_OK:   return HEX_OK;
        case UI_TONE_WARN: return HEX_WARN;
        case UI_TONE_INFO: return HEX_INFO;
        case UI_TONE_ERR:  return HEX_ERR;
        default:           return HEX_ACCENT;
    }
}

static void format_size(u32 bytes, char *out, int out_size) {
    ui_format_size_str(bytes, out, out_size);
}

// ISO 8601 "YYYY-MM-DDTHH:MM:SS..." -> "YYYY-MM-DD HH:MM"
static void format_date(const char *iso, char *out, int out_size) {
    ui_format_date_str(iso, out, out_size);
}

static char local_console_id[17];
static int utc_offset_minutes;

void ui_set_local_console_id(const char *console_id) {
    snprintf(local_console_id, sizeof(local_console_id), "%s", console_id ? console_id : "");
}

void ui_set_utc_offset(int minutes) {
    utc_offset_minutes = minutes;
}

void ui_format_when(const char *iso, char *out, int out_size) {
    // osGetTime(): ms since 1900-01-01 on the console's (local, settable)
    // clock. Only used for the date words; ui_format_when_at() distrusts it
    // when it is implausible.
    int64_t now_local = (int64_t)(osGetTime() / 1000ULL) - 2208988800LL;
    ui_format_when_at(iso, utc_offset_minutes, now_local, out, out_size);
}

void ui_device_label(const char *console_id, char *out, int out_size) {
    ui_device_label_for(console_id, local_console_id, out, out_size);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static UiTouchTrack touch_track;
static UiRepeat key_repeat;

// ZL / ZR (New 3DS) need no setup here: libctru's hidInit() starts ir:rst
// itself on a New 3DS (hidShouldUseIrrst) and hidScanInput() merges its
// keys into hidKeysDown()/hidKeysHeld().
bool ui_input_scan(UiInput *in) {
    memset(in, 0, sizeof(*in));
    if (!aptMainLoop()) return false;
    hidScanInput();
    in->down = hidKeysDown();
    in->held = hidKeysHeld();
    in->up = hidKeysUp();
    in->t_ms = gui_clock_ms();
    in->rep = ui_repeat_accel(&key_repeat, in->held, hidKeysDownRepeat(), in->t_ms);
    // hidTouchRead() reads (0, 0) on the release frame: only sample while held
    bool raw = (in->held & KEY_TOUCH) != 0;
    touchPosition tp = { 0, 0 };
    if (raw) hidTouchRead(&tp);
    ui_touch_update(&touch_track, raw, tp.px, tp.py, in);
    return true;
}

void ui_input_latch_touch(void) {
    // hidKeysHeld() is the last scan: callers drain with hidScanInput() first
    ui_touch_latch(&touch_track, (hidKeysHeld() & KEY_TOUCH) != 0);
}

// ---------------------------------------------------------------------------
// Backdrop
// ---------------------------------------------------------------------------

static void splash_top(void *ctx) {
    (void)ctx;
    gui_header(NULL);
    float cx = GUI_TOP_W / 2.0f, cy = 112;
    gui_rrect(cx - 26, cy - 44, 52, 52, 14, gui_rgb(HEX_ACCENT));
    gui_rrect(cx - 12, cy - 30, 24, 24, 6, gui_rgb(HEX_BG2));
    gui_text(cx, cy + 16, 0.9f, gui_rgb(HEX_TEXT), GUI_CENTER, "GameSync");
    gui_text(cx, cy + 46, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER, "Save sync for 3DS & DS  \xC2\xB7  v" APP_VERSION);
}

static void splash_bottom(void *ctx) {
    (void)ctx;
    gui_header_bar();
}

UiBackdrop ui_set_backdrop(UiDrawFn top, UiDrawFn bottom, void *ctx) {
    UiBackdrop prev = backdrop;
    backdrop.top = top;
    backdrop.bottom = bottom;
    backdrop.ctx = ctx;
    return prev;
}

void ui_restore_backdrop(UiBackdrop previous) {
    backdrop = previous;
}

void ui_draw_backdrop_top(void) {
    if (backdrop.top) backdrop.top(backdrop.ctx);
}

void ui_draw_backdrop_bottom(void) {
    if (backdrop.bottom) backdrop.bottom(backdrop.ctx);
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

#define DLG_X 10
#define DLG_W (GUI_BOT_W - 2 * DLG_X)
#define DLG_MAX_LINES 10

typedef struct {
    char text[DLG_MAX_LINES][GUI_WRAP_LINE];
    u8 style[DLG_MAX_LINES];  // 0 normal, 1 dim, 2 tone
    int count;
} DialogLines;

static void layout_body(const char *body, float width, DialogLines *out) {
    out->count = 0;
    if (!body) return;
    const char *p = body;
    while (*p && out->count < DLG_MAX_LINES) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char para[512];
        if (len > sizeof(para) - 1) len = sizeof(para) - 1;
        memcpy(para, p, len);
        para[len] = '\0';
        u8 style = 0;
        char *text = para;
        if (*text == '\x01') { style = 1; text++; }
        else if (*text == '\x02') { style = 2; text++; }
        if (!*text) {
            // Blank line: half a line of space, kept as an empty entry
            out->text[out->count][0] = '\0';
            out->style[out->count++] = 0;
        } else {
            char lines[DLG_MAX_LINES][GUI_WRAP_LINE];
            int n = gui_wrap(text, GUI_S_BODY, width, lines, DLG_MAX_LINES - out->count);
            for (int i = 0; i < n; i++) {
                memcpy(out->text[out->count], lines[i], GUI_WRAP_LINE);
                out->style[out->count++] = style;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

#define DLG_MAX_BUTTONS 4
#define DLG_BTN_AREA 40   // the card's bottom: 30 px buttons, 10 px margin below

static float body_height(const DialogLines *lines) {
    float lh = gui_line_h(GUI_S_BODY), h = 0;
    for (int i = 0; i < lines->count; i++) h += lines->text[i][0] ? lh : lh / 2;
    return h;
}

// Buttons split the card's inner width equally, 8 px apart. The touch
// rects reach 5 px above and below each pill and halfway into the gaps,
// so the row has no dead strips.
static void card_buttons(float card_y, float card_h, int n, GuiRect *draw, GuiRect *touch) {
    float by = card_y + card_h - 10 - GUI_BTN_H;
    float avail = DLG_W - 24;
    float bw = (avail - GUI_BTN_GAP * (n - 1)) / (n > 0 ? n : 1);
    for (int i = 0; i < n; i++) {
        float x = DLG_X + 12 + i * (bw + GUI_BTN_GAP);
        draw[i] = gui_rect_make(x, by, bw, GUI_BTN_H);
        touch[i] = gui_rect_make(x - GUI_BTN_GAP / 2.0f, by - 5, bw + GUI_BTN_GAP, GUI_BTN_H + 10);
    }
}

// The button that confirms (has A) is the filled one; with none, the last
static int primary_button(const UiButton *b, int n) {
    for (int i = 0; i < n; i++)
        if (b[i].keys & KEY_A) return i;
    return n - 1;
}

typedef struct {
    float y, h;
    GuiRect draw[DLG_MAX_BUTTONS], touch[DLG_MAX_BUTTONS];
    int n, primary;
} DialogGeom;

static void dialog_geom(const DialogLines *lines, const UiButton *b, int n, DialogGeom *g) {
    g->h = 24 + 10 + body_height(lines) + 10 + DLG_BTN_AREA;
    if (g->h > GUI_H - 12) g->h = GUI_H - 12;
    g->y = (GUI_H - g->h) / 2;
    g->n = n;
    g->primary = primary_button(b, n);
    card_buttons(g->y, g->h, n, g->draw, g->touch);
}

// `pressed`: index a finger is on, or -1; `held`: keys held down
static void draw_dialog(UiTone tone, const char *title, const DialogLines *lines,
                        const UiButton *buttons, const DialogGeom *g, int pressed, u32 held) {
    float lh = gui_line_h(GUI_S_BODY);
    u32 hex = ui_tone_hex(tone);

    gui_dim();
    gui_card(DLG_X, g->y, DLG_W, g->h, title, hex);

    float ty = g->y + 24 + 9;
    for (int i = 0; i < lines->count; i++) {
        if (!lines->text[i][0]) {
            ty += lh / 2;
            continue;
        }
        // Too long for the screen: stop above the buttons
        if (ty + lh > g->y + g->h - DLG_BTN_AREA - 2) break;
        u32 color = lines->style[i] == 1 ? HEX_DIM : (lines->style[i] == 2 ? hex : HEX_TEXT);
        gui_text(DLG_X + 12, ty, GUI_S_BODY, gui_rgb(color), GUI_LEFT, lines->text[i]);
        ty += lh;
    }

    for (int i = 0; i < g->n; i++) {
        bool down = pressed == i || (held & buttons[i].keys) != 0;
        gui_btn(g->draw[i], buttons[i].button, buttons[i].label,
                i == g->primary ? GUI_BTN_PRIMARY : GUI_BTN_TONAL, hex, down);
    }
}

static bool progress_cancelling;

u32 ui_dialog(UiTone tone, const char *title, const char *body, const UiButton *buttons, int count) {
    static const UiButton any = { 0, "A", "Continue" };
    bool any_key = (count <= 0);
    if (any_key) {
        buttons = &any;
        count = 1;
    }
    if (count > DLG_MAX_BUTTONS) count = DLG_MAX_BUTTONS;
    DialogLines lines;
    layout_body(body, DLG_W - 24, &lines);
    DialogGeom g;
    dialog_geom(&lines, buttons, count, &g);
    progress_cancelling = false;   // whatever was cancelling has been answered

    // Ignore keys still held from whatever opened the dialog, and the
    // finger that opened it
    hidScanInput();
    ui_input_latch_touch();
    UiTap tap = UI_TAP_INIT;
    u32 armed = 0, result = 0;   // keys pressed since the dialog opened
    UiInput in;
    while (ui_input_scan(&in)) {
        int pressed = -1;
        armed = (armed | in.down) & in.held;
        if (any_key) {
            u32 keys = in.down & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_START | KEY_SELECT | KEY_L | KEY_R);
            if (keys) { result = keys; break; }
            // A tap anywhere closes a message, on release
            if (in.t_up) { result = KEY_TOUCH; break; }
            if (in.t_held) pressed = 0;
        } else {
            int hit = ui_tap_buttons(&tap, &in, g.touch, count, &pressed);
            if (hit >= 0) { result = buttons[hit].keys; break; }
            for (int i = 0; i < count && !result; i++)
                if (in.down & buttons[i].keys) result = in.down & buttons[i].keys;
            if (result) break;
        }

        gui_begin(true);
        gui_screen(GUI_TOP);
        ui_draw_backdrop_top();
        gui_screen(GUI_BOTTOM);
        ui_draw_backdrop_bottom();
        draw_dialog(tone, title, &lines, buttons, &g, pressed, any_key ? 0 : (in.held & armed));
        gui_end();
    }
    ui_input_latch_touch();
    return result;
}

void ui_body_line(const char *s, char *out, int out_size) {
    gui_fit(s, GUI_S_BODY, DLG_W - 24, out, (size_t)out_size);
}

void ui_message(UiTone tone, const char *title, const char *body) {
    ui_dialog(tone, title, body, NULL, 0);
}

bool ui_confirm(UiTone tone, const char *title, const char *body, const char *yes_label) {
    UiButton b[2] = {
        { KEY_B, "B", "Cancel" },
        { KEY_A, "A", yes_label ? yes_label : "OK" },
    };
    return (ui_dialog(tone, title, body, b, 2) & KEY_A) != 0;
}

// The bar's shown fraction glides toward the reported one (progress frames
// come at irregular times, a few per second) but never runs ahead of it and
// snaps to full at the end. A new operation (title, name or an
// indeterminate phase) starts from its own value, not the last one's.
static float smooth_progress(const UiProgress *p) {
    static GuiSpring shown;
    static char last_title[48], last_name[96];
    static u64 last_ms;
    const char *title = p->title ? p->title : "";
    const char *name = p->name ? p->name : "";
    u64 now = gui_clock_ms();
    float frac = p->frac;

    bool changed = strncmp(title, last_title, sizeof(last_title) - 1) != 0 ||
                   strncmp(name, last_name, sizeof(last_name) - 1) != 0;
    if (changed || frac < 0) {
        snprintf(last_title, sizeof(last_title), "%s", title);
        snprintf(last_name, sizeof(last_name), "%s", name);
        gui_spring_set(&shown, frac > 0 ? (frac < 1 ? frac : 1) : 0);
        last_ms = now;
        return frac;
    }
    if (frac > 1) return frac;   // no bar
    if (frac >= 1) {
        gui_spring_set(&shown, 1);
        last_ms = now;
        return 1;
    }
    float dt = now > last_ms ? (float)(now - last_ms) / 1000.0f : 0;
    if (dt > 0.25f) dt = 0.25f;
    last_ms = now;
    gui_spring_always(&shown, frac, GUI_RESP_PROGRESS, dt);
    if (shown.x > frac) gui_spring_set(&shown, frac);
    if (shown.x < 0) gui_spring_set(&shown, 0);
    return shown.x;
}

static GuiRect progress_cancel_rect;   // touch rect of the last card's Cancel pill
static bool progress_cancel_shown;

void ui_progress(const UiProgress *p) {
    if (!p->cancel_hint) progress_cancelling = false;   // another operation
    bool cancelling = p->cancel_hint && progress_cancelling;
    float bar_frac = smooth_progress(p);
    gui_begin(false);
    gui_screen(GUI_TOP);
    ui_draw_backdrop_top();
    gui_screen(GUI_BOTTOM);
    ui_draw_backdrop_bottom();
    gui_dim();

    u32 hex = ui_tone_hex(p->tone);
    float lh_small = gui_line_h(GUI_S_SMALL);
    float h = 24 + 12;
    if (p->name) h += gui_line_h(GUI_S_BODY) + 2;
    if (p->status) h += lh_small + 4;
    if (p->frac <= 1) h += 14 + lh_small + 6;
    if (p->info1) h += lh_small;
    if (p->info2) h += lh_small;
    if (p->cancel_hint) h += DLG_BTN_AREA;
    h += 6;
    float y = (GUI_H - h) / 2;
    gui_card(DLG_X, y, DLG_W, h, p->title ? p->title : "Working", hex);

    float x = DLG_X + 12, w = DLG_W - 24, ty = y + 24 + 10;
    if (p->name) {
        gui_text_fit(x, ty, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, w, p->name);
        ty += gui_line_h(GUI_S_BODY) + 2;
    }
    if (p->status) {
        char cancel_status[32];
        const char *status = p->status;
        if (cancelling) {
            snprintf(cancel_status, sizeof(cancel_status), "Cancelling%s", gui_ellipsis());
            status = cancel_status;
        }
        if (p->frac < 0) {
            gui_spinner(x + 5, ty + lh_small / 2 + 1, 5, gui_rgb(cancelling ? HEX_MUTED : hex));
            gui_text_fit(x + 16, ty, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, w - 16, status);
        } else {
            gui_text_fit(x, ty, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, w, status);
        }
        ty += lh_small + 4;
    }
    if (p->frac <= 1) {
        gui_bar(x, ty, w, 10, bar_frac, gui_rgb(cancelling ? HEX_MUTED : hex));
        ty += 14;
        if (p->left) gui_text(x, ty, GUI_S_SMALL, gui_rgb(HEX_TEXT), GUI_LEFT, p->left);
        if (p->right) gui_text(x + w, ty, GUI_S_SMALL, gui_rgb(hex), GUI_RIGHT, p->right);
        ty += lh_small + 6;
    }
    if (p->info1) {
        gui_text_fit(x, ty, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, w, p->info1);
        ty += lh_small;
    }
    if (p->info2) {
        gui_text_fit(x, ty, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, w, p->info2);
        ty += lh_small;
    }
    progress_cancel_shown = p->cancel_hint;
    if (p->cancel_hint) {
        GuiRect draw;
        card_buttons(y, h, 1, &draw, &progress_cancel_rect);
        gui_btn(draw, "B", "Cancel", GUI_BTN_TONAL, hex, cancelling);
    }
    gui_end();
}

bool ui_progress_cancel_poll(void) {
    if (progress_cancelling) return true;
    UiInput in;
    if (!ui_input_scan(&in)) {
        progress_cancelling = true;   // the app is closing
        return true;
    }
    if ((in.held & KEY_B) ||
        (progress_cancel_shown && in.t_held && gui_rect_contains(progress_cancel_rect, in.tx, in.ty)))
        progress_cancelling = true;
    return progress_cancelling;
}

void ui_busy(const char *title, const char *message) {
    UiProgress p = { 0 };
    p.title = title;
    p.status = message;
    p.frac = -1;
    ui_progress(&p);
}

// ---------------------------------------------------------------------------
// Choice list
// ---------------------------------------------------------------------------

#define CHOICE_ROWS 7
#define CHOICE_ROW_H 22


typedef struct {
    const char *const *items;
} ChoiceView;

static void choice_row(void *ctx, int index, float x, float y, float w, float h, float hl, bool pressed) {
    (void)pressed;
    const ChoiceView *c = ctx;
    gui_text_mid(x + 13, y, h, GUI_S_BODY, gui_mix(HEX_TEXT, HEX_INK, hl), GUI_LEFT, w - 26, c->items[index]);
}

static const UiButton choice_buttons[2] = { { KEY_B, "B", "Cancel" }, { KEY_A, "A", "Select" } };

typedef struct {
    int rows;
    float y, h;
    UiListGeom list;
    GuiRect draw[2], touch[2];
} ChoiceGeom;

static void choice_geom(int count, ChoiceGeom *g) {
    g->rows = count < CHOICE_ROWS ? count : CHOICE_ROWS;
    g->h = 24 + 4 + g->rows * CHOICE_ROW_H + 2 + DLG_BTN_AREA;
    g->y = (GUI_H - g->h) / 2;
    UiListGeom list = { DLG_X + 3, g->y + 24 + 4, DLG_W - 6, g->rows, CHOICE_ROW_H, true, false };
    g->list = list;
    card_buttons(g->y, g->h, 2, g->draw, g->touch);
}

static void draw_choice(UiTone tone, const char *title, const char *const *items, int count,
                        int selected, int scroll, UiListAnim *anim, const ChoiceGeom *g,
                        int pressed, u32 held) {
    u32 hex = ui_tone_hex(tone);
    gui_dim();
    gui_card(DLG_X, g->y, DLG_W, g->h, title, hex);
    ChoiceView c = { items };
    // Clipped to the card's list area, so rows can glide and be dragged
    ui_list_draw(anim, &g->list, count, selected, scroll, choice_row, &c);
    for (int i = 0; i < 2; i++)
        gui_btn(g->draw[i], choice_buttons[i].button, choice_buttons[i].label,
                i == 1 ? GUI_BTN_PRIMARY : GUI_BTN_TONAL, hex,
                pressed == i || (held & choice_buttons[i].keys));
}

int ui_choose(UiTone tone, const char *title, const char *const *items, int count) {
    if (count <= 0) return -1;
    int selected = 0, scroll = 0, result = -1;
    ChoiceGeom g;
    choice_geom(count, &g);
    UiListAnim anim = { 0 };
    UiTap tap = UI_TAP_INIT;
    u32 armed = 0;
    hidScanInput();
    ui_input_latch_touch();
    UiInput in;
    while (ui_input_scan(&in)) {
        armed = (armed | in.down) & in.held;
        UiListEvent ev = ui_list_input(&anim, &g.list, count, &selected, &scroll, &in, in.rep & UI_LIST_KEYS);
        if (ev.kind != UI_LIST_REATTACHED && (in.rep & UI_LIST_KEYS)) {
            ui_list_keys(&anim, in.rep, count, g.rows, &selected);
            if (selected < scroll) scroll = selected;
            if (selected >= scroll + g.rows) scroll = selected - g.rows + 1;
        }
        // Any tap on a row picks it, on release
        if (ev.kind == UI_LIST_TAP || ev.kind == UI_LIST_TAP_SELECTED) {
            result = ev.index;
            break;
        }
        int pressed = -1;
        int hit = ui_tap_buttons(&tap, &in, g.touch, 2, &pressed);
        if (hit == 0 || (in.down & KEY_B)) break;
        if (hit == 1 || (in.down & KEY_A)) { result = selected; break; }

        gui_begin(true);
        gui_screen(GUI_TOP);
        ui_draw_backdrop_top();
        gui_screen(GUI_BOTTOM);
        ui_draw_backdrop_bottom();
        draw_choice(tone, title, items, count, selected, scroll, &anim, &g, pressed, in.held & armed);
        gui_end();
    }
    ui_input_latch_touch();
    return result;
}

// ---------------------------------------------------------------------------
// Top-level tabs
// ---------------------------------------------------------------------------

static const char *const tab_labels[UI_TAB_COUNT] = { "Saves", "Catalog", "Settings" };

void ui_tab_header(int active) {
    gui_header_tabs(tab_labels, UI_TAB_COUNT, active);
}

UiNav ui_tab_nav(u32 down) {
    if (down & KEY_L) return UI_NAV_PREV;
    if (down & KEY_R) return UI_NAV_NEXT;
    if (down & KEY_START) {
        UiButton b[2] = { { KEY_B, "B", "Cancel" }, { KEY_A | KEY_START, "A", "Exit" } };
        if (ui_dialog(UI_TONE_ACCENT, "Exit GameSync?", "Close the app? Press START again or A to exit.", b, 2) &
            (KEY_A | KEY_START))
            return UI_NAV_EXIT;
    }
    return UI_NAV_STAY;
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

// 1 at `since_ms`, falling to 0 over GUI_FADE_MS (0 when `since_ms` is 0)
static float edge_fade(u64 since_ms) {
    u64 now = gui_now_ms();
    if (!since_ms || now < since_ms || now - since_ms >= GUI_FADE_MS) return 0;
    return 1.0f - (float)(now - since_ms) / GUI_FADE_MS;
}

bool ui_list_at_top(const UiListAnim *a) {
    return !a->init || ui_list_view(a) <= 0.01f;
}

void ui_list_draw(UiListAnim *a, const UiListGeom *g, int count, int selected, int scroll,
                  UiRowFn row, void *ctx) {
    // Once a frame, also when drawn behind a dialog or a progress card
    if (!a->init || a->frame != gui_ticks()) {
        a->frame = gui_ticks();
        ui_list_step(a, g->rows, g->row_h, count, selected, scroll, gui_dt());
    }
#ifdef GUI_PERF
    // A coast running faster than its release velocity must never happen:
    // the overlay shows the count in red if it does
    if (a->coast_overspeed > gui_perf_coast_overspeed) gui_perf_coast_overspeed = a->coast_overspeed;
#endif
    float view = ui_list_view(a);
    float list_h = g->rows * g->row_h;
    if (g->clip) gui_clip(g->x, g->y, g->w, list_h);

    // After a wrap the rows fade in from 30% (toward the background)
    float wrap = edge_fade(a->wrap_ms);
    if (wrap > 0) C2D_Fade(gui_rgba(HEX_BG, (u8)(0.7f * wrap * 255)));

    bool bar = count > g->rows;
    float rw = bar ? g->w - 7 : g->w;
    int pressed = ui_list_pressed_row(a);
    bool pressed_on_bar = pressed >= 0 && fabsf((float)pressed - a->sel.x) < 0.5f && selected >= 0;
    // The row under a finger: a soft fill (on the bar the bar darkens instead)
    if (pressed >= 0 && !pressed_on_bar)
        gui_rrect(g->x + 3, g->y + (pressed - view) * g->row_h + 1, rw - 6, g->row_h - 2, 5,
                  gui_mix(HEX_BG, HEX_ACCENT, 0.25f));
    if (count > 0 && selected >= 0) {
        float sy = g->y + (a->sel.x - view) * g->row_h + a->nudge.x;
        gui_rrect(g->x + 3, sy + 1, rw - 6, g->row_h - 2, 5,
                  pressed_on_bar ? gui_mix(HEX_ACCENT, 0x000000, 0.20f) : gui_rgb(HEX_ACCENT));
    }
    int first = (int)floorf(view);
    for (int i = first; i <= first + g->rows + 1 && i < count; i++) {
        if (i < 0) continue;
        float ry = g->y + (i - view) * g->row_h;
        float hl = 1.0f - fabsf((float)i - a->sel.x);
        if (hl < 0 || selected < 0) hl = 0;
        row(ctx, i, g->x, ry, rw, g->row_h, hl, i == pressed);
    }
    if (wrap > 0) C2D_Fade(0);
    if (g->clip) gui_clip_off();

    // Content under the header / the action bar: a soft shadow instead of
    // a hard line (the header draws its line only while the list is at
    // the top, see ui_list_at_top)
    if (g->edges)
        gui_scroll_edges(g->x, g->w, g->y, g->y + list_h, view > 0.01f, view + g->rows < count - 0.01f);

    // The thumb flashes bright at an edge and settles back to dim
    if (bar)
        gui_scrollbar_ex(g->x + g->w - 6, g->y + 3, list_h - 6, g->rows, count, view,
                         gui_mix_hex(HEX_DIM, HEX_TEXT, edge_fade(a->flash_ms)));
}

// ---------------------------------------------------------------------------
// Action bar
// ---------------------------------------------------------------------------

float ui_bar_layout(const UiBarItem *items, int n, GuiRect *draw, GuiRect *touch) {
    if (n > UI_BAR_MAX) n = UI_BAR_MAX;
    const float margin = 8, gap = GUI_BTN_GAP, by = GUI_BAR_Y + (GUI_BAR_H - GUI_BTN_H) / 2;
    float inner = GUI_BOT_W - 2 * margin;
    float w0 = items[0].width > 0 ? items[0].width : gui_btn_content_w(items[0].glyph, items[0].label) + 24;
    if (items[0].width <= 0 && w0 < 120) w0 = 120;
    float rest = n > 1 ? (inner - w0 - gap * (n - 1)) / (n - 1) : 0;
    if (n > 1 && rest < GUI_BTN_MIN_W) rest = GUI_BTN_MIN_W;
    float x = margin;
    for (int i = 0; i < n; i++) {
        float w = i == 0 ? w0 : (items[i].width > 0 ? items[i].width : rest);
        draw[i] = gui_rect_make(x, by, w, GUI_BTN_H);
        float tx0 = i == 0 ? 0 : x - gap / 2, tx1 = i == n - 1 ? GUI_BOT_W : x + w + gap / 2;
        touch[i] = gui_rect_make(tx0, GUI_BAR_Y, tx1 - tx0, GUI_BAR_H);
        x += w + gap;
    }
    return x - gap;
}

int ui_bar_input(UiBar *bar, const UiInput *in, const UiBarItem *items, int n) {
    GuiRect draw[UI_BAR_MAX], touch[UI_BAR_MAX];
    if (n > UI_BAR_MAX) n = UI_BAR_MAX;
    ui_bar_layout(items, n, draw, touch);
    for (int i = 0; i < n; i++)
        if (items[i].style == GUI_BTN_DISABLED) touch[i].w = 0;
    return ui_tap_buttons(&bar->tap, in, touch, n, &bar->pressed);
}

void ui_bar_draw(const UiBar *bar, const UiBarItem *items, int n, u32 held) {
    GuiRect draw[UI_BAR_MAX], touch[UI_BAR_MAX];
    if (n > UI_BAR_MAX) n = UI_BAR_MAX;
    gui_rect(0, GUI_BAR_Y, GUI_BOT_W, GUI_BAR_H, gui_rgb(HEX_BG2));
    if (n <= 0) return;
    ui_bar_layout(items, n, draw, touch);
    for (int i = 0; i < n; i++) {
        bool pressed = (bar && bar->pressed == i) || (items[i].keys & held);
        gui_btn_ex(draw[i], items[i].glyph, items[i].label, items[i].style, items[i].tone, pressed, items[i].on);
    }
}

// ---------------------------------------------------------------------------
// Save list
// ---------------------------------------------------------------------------

static const char *state_label(const TitleInfo *t, u32 *hex) {
    TitleSyncState s = (TitleSyncState)t->sync_state;
    if (t->in_conflict) s = TSTATE_CONFLICT;
    switch (s) {
        case TSTATE_SYNCED:         *hex = HEX_OK;   return "Up to date";
        case TSTATE_UPLOADED:       *hex = HEX_OK;   return "Uploaded";
        case TSTATE_DOWNLOADED:     *hex = HEX_OK;   return "Downloaded";
        case TSTATE_NEEDS_UPLOAD:   *hex = HEX_WARN; return "Needs upload";
        case TSTATE_NEEDS_DOWNLOAD: *hex = HEX_INFO; return "Needs download";
        case TSTATE_CONFLICT:       *hex = HEX_ERR;  return "Conflict";
        case TSTATE_FAILED:         *hex = HEX_ERR;  return "Sync failed";
        case TSTATE_SKIPPED:        *hex = HEX_CART; return "Manual sync";
        default:                    *hex = 0;        return NULL;
    }
}

static bool is_cart(const TitleInfo *t) {
    return t->media_type == MEDIATYPE_GAME_CARD;
}

static const char *system_tag(const TitleInfo *t, u32 *hex) {
    if (is_cart(t)) {
        *hex = HEX_CART;
        return "Cart";
    }
    if (t->is_nds) {
        *hex = HEX_NDS;
        return "NDS";
    }
    *hex = HEX_DIM;
    return "3DS";
}

static void kv(float x, float y, float label_w, float w, const char *label, const char *value, u32 color) {
    gui_text(x, y, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, label);
    gui_text_fit(x + label_w, y, GUI_S_SMALL, gui_rgb(color), GUI_LEFT, w - label_w, value);
}

static void draw_status_toast(const char *status, UiTone tone) {
    if (!status || !status[0]) return;
    float y = 190, h = 22;
    u32 hex = ui_tone_hex(tone);
    gui_rrect(8, y, GUI_TOP_W - 16, h, 6, gui_rgb(HEX_BG2));
    gui_rrect(8, y, 4, h, 2, gui_rgb(hex));
    gui_text_mid(20, y, h, GUI_S_SMALL, gui_rgb(HEX_TEXT), GUI_LEFT, GUI_TOP_W - 40, status);
}

// "zel" in quotes, cut to 10 characters
static void search_label(const char *search, char *out, size_t size) {
    char q[64];
    bool cut = ui_utf8_prefix(search, 10, q, sizeof(q));
    snprintf(out, size, "\"%s%s\"", q, cut ? gui_ellipsis() : "");
}

void ui_draw_saves_top(const SavesView *v) {
    ui_tab_header(UI_TAB_SAVES);
    bool searching = v->search && v->search[0];
    GuiHint hints[5];
    int nh = 0;
    hints[nh++] = (GuiHint){ "SELECT", "All/3DS/NDS" };
    hints[nh++] = (GuiHint){ "LR", "Page" };
    if (new3ds) hints[nh++] = (GuiHint){ "ZR", "Search" };
    // B clears the search only while nothing is marked (else it unmarks)
    if (searching && v->marked <= 0) hints[nh++] = (GuiHint){ "B", "Clear search" };
    hints[nh++] = (GuiHint){ "START", "Exit" };

    if (v->count == 0 || v->selected < 0 || v->selected >= v->count) {
        gui_panel(8, 34, GUI_TOP_W - 16, 148);
        if (searching && v->view_total > 0) {
            char q[80], line[112];
            search_label(v->search, q, sizeof(q));
            snprintf(line, sizeof(line), "No saves match %s", q);
            gui_text_fit(GUI_TOP_W / 2.0f, 74, GUI_S_TITLE, gui_rgb(HEX_TEXT), GUI_CENTER, GUI_TOP_W - 40, line);
            gui_text(GUI_TOP_W / 2.0f, 102, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER,
                     v->marked > 0 ? "Clear the search with the button on the touch screen."
                                   : "B clears the search.");
            gui_text(GUI_TOP_W / 2.0f, 120, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER,
                     new3ds ? "ZR or the search chip changes it." : "The search chip changes it.");
        } else {
            gui_text(GUI_TOP_W / 2.0f, 74, GUI_S_TITLE, gui_rgb(HEX_TEXT), GUI_CENTER, "No saves here");
            gui_text(GUI_TOP_W / 2.0f, 102, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER,
                     v->view_mode == VIEW_ALL ? "No titles with save data were found."
                                              : "Nothing in this view. Press SELECT to switch.");
            gui_text(GUI_TOP_W / 2.0f, 120, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER,
                     "L / R switch to the game catalog and the settings.");
        }
        draw_status_toast(v->status, v->status_tone);
        gui_footer(hints, nh);
        return;
    }

    const TitleInfo *t = &v->titles[v->filtered[v->selected]];
    float px = 8, py = 32, pw = GUI_TOP_W - 16, ph = 152;
    gui_panel(px, py, pw, ph);

    // Name
    float x = px + 12;
    int lines = gui_text_wrap(x, py + 8, GUI_S_TITLE, gui_rgb(HEX_TEXT), pw - 24, 2, t->name);
    float y = py + 10 + lines * gui_line_h(GUI_S_TITLE);

    // Pills: system, marked, sync state
    u32 hex;
    const char *tag = system_tag(t, &hex);
    float pxx = x;
    pxx += gui_pill(pxx, y, 15, GUI_S_TINY, gui_mix(HEX_PANEL, hex, 0.3f), gui_rgb(hex), tag) + 5;
    if (t->marked) pxx += gui_pill(pxx, y, 15, GUI_S_TINY, gui_rgb(HEX_ACCENT), gui_rgb(HEX_INK), "Marked") + 5;
    const char *state = state_label(t, &hex);
    if (state) {
        float sw = gui_pill_w(15, GUI_S_TINY, state) + 10;
        gui_rrect(pxx, y, sw, 15, 7.5f, gui_mix(HEX_PANEL, hex, 0.25f));
        gui_circle(pxx + 8, y + 7.5f, 3, gui_rgb(hex));
        gui_text_mid(pxx + 14, y, 15, GUI_S_TINY, gui_rgb(hex), GUI_LEFT, 0, state);
        pxx += sw + 5;
    }
    y += 22;

    // Left column: the title. Right column: the last compare.
    float colw = (pw - 36) / 2, lh = gui_line_h(GUI_S_SMALL) + 1;
    float lx = x, rx = x + colw + 12;
    gui_rect(rx - 7, y, 1, ph - (y - py) - 10, gui_rgb(HEX_LINE));

    kv(lx, y, 54, colw, "Title ID", t->title_id_hex, HEX_TEXT);
    kv(lx, y + lh, 54, colw, "Product", t->product_code[0] ? t->product_code : "-", HEX_TEXT);
    const char *media = t->is_nds ? (is_cart(t) ? "DS game card" : "SD card (.sav)")
                                  : (is_cart(t) ? "Game card" : "SD card");
    kv(lx, y + 2 * lh, 54, colw, "Storage", media, HEX_TEXT);
    if (t->is_nds && !is_cart(t) && t->sav_path[0]) {
        const char *base = strrchr(t->sav_path, '/');
        kv(lx, y + 3 * lh, 54, colw, "Save file", base ? base + 1 : t->sav_path, HEX_DIM);
    } else if (is_cart(t)) {
        gui_text_fit(lx, y + 3 * lh, GUI_S_SMALL, gui_rgb(HEX_CART), GUI_LEFT, colw,
                     "Cartridge: not part of Sync All");
    }

    const SaveDetails *d = v->details;
    if (d) {
        char size[24], line[96];
        if (d->local_exists) {
            format_size(d->local_size, size, sizeof(size));
            snprintf(line, sizeof(line), "%s \xC2\xB7 %.8s", size, d->local_hash);
        } else {
            snprintf(line, sizeof(line), "No save");
        }
        kv(rx, y, 50, colw, "Console", line, HEX_TEXT);
        if (d->server_error) {
            if (d->server_status)
                snprintf(line, sizeof(line), "Error (HTTP %lu)", (unsigned long)d->server_status);
            else
                snprintf(line, sizeof(line), "No response");
        } else if (d->server_exists) {
            format_size(d->server_size, size, sizeof(size));
            snprintf(line, sizeof(line), "%s \xC2\xB7 %.8s", size, d->server_hash);
        } else {
            snprintf(line, sizeof(line), "Not uploaded");
        }
        kv(rx, y + lh, 50, colw, "Server", line, d->server_error ? HEX_ERR : HEX_TEXT);
        char date[32];
        // Synced this session: say how long ago (monotonic), not a date
        if (v->synced_ms) ui_format_ago(gui_clock_ms() - v->synced_ms, date, sizeof(date));
        else format_date(d->server_last_sync, date, sizeof(date));
        kv(rx, y + 2 * lh, 50, colw, "Synced", d->server_exists ? date : "-", HEX_DIM);
        kv(rx, y + 3 * lh, 50, colw, "Last ok", d->has_last_synced ? d->last_synced_hash : "never", HEX_DIM);
    } else {
        gui_text(rx, y, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, "Save");
        float bx = rx;
        bx += gui_text(bx, y + lh, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, "Press ") ;
        bx += gui_button(bx, y + lh + lh / 2 - 0.5f, "A") + 3;
        gui_text(bx, y + lh, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, "to compare");
        gui_text(rx, y + 2 * lh, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, "with the server copy.");
    }

    draw_status_toast(v->status, v->status_tone);
    gui_footer(hints, nh);
}

static void saves_row(void *ctx, int index, float x, float y, float w, float h, float hl, bool pressed) {
    (void)pressed;
    const SavesView *v = ctx;
    const TitleInfo *t = &v->titles[v->filtered[index]];
    float cy = y + h / 2;

    // Mark box
    if (t->marked) {
        gui_rrect(x + 9, cy - 5.5f, 11, 11, 3, gui_mix(HEX_ACCENT, HEX_INK, hl));
        gui_icon_check(x + 14.5f, cy, 8, gui_mix(HEX_INK, HEX_ACCENT, hl));
    } else {
        gui_rrect(x + 9, cy - 5.5f, 11, 11, 3, gui_mix(HEX_LINE, 0x1E8F86, hl));
        gui_rrect(x + 10.5f, cy - 4, 8, 8, 2, gui_mix(HEX_BG, HEX_ACCENT, hl));
    }

    // System tag, fixed width
    u32 hex;
    const char *tag = system_tag(t, &hex);
    gui_rrect(x + 26, cy - 6.5f, 30, 13, 6.5f, gui_mix(gui_mix_hex(HEX_BG, hex, 0.25f), HEX_INK, hl));
    gui_text_mid(x + 41, cy - 6.5f, 13, GUI_S_TINY, gui_rgb(hex), GUI_CENTER, 0, tag);

    // Name
    bool conflict = t->in_conflict || t->sync_state == TSTATE_CONFLICT;
    gui_text_mid(x + 62, y, h, GUI_S_BODY, gui_mix(conflict ? HEX_ERR : HEX_TEXT, HEX_INK, hl), GUI_LEFT,
                 w - 62 - 18, t->name);

    // State dot, with an ink ring that fades in under the bar
    if (state_label(t, &hex)) {
        if (hl > 0) gui_circle(x + w - 11, cy, 5, gui_rgba(HEX_INK, (u8)(hl * 255)));
        gui_circle(x + w - 11, cy, 3.5f, gui_rgb(hex));
    }
}

// Right side of the Saves header, right to left: the search chip (a
// magnifier, or the active query as a pill with an x), the "B N marked x"
// chip when something is marked, then the count ("41 saves", "3 of 41").
// When space runs out the count goes first, then the query gets shorter.
#define CHIP_H 18
#define SEARCH_CHIP_W 28
#define SEARCH_X_W 22

typedef struct {
    char count[32];
    bool show_count;
    float count_right;
    char marked[24];
    GuiRect chip;          // "N marked" (draw rect), w 0 = none
    GuiRect search;        // magnifier chip or query pill (draw rect)
    char query[96];        // the pill's text ("" = no search)
} SavesHeader;

static const char *const saves_tabs[] = { "All", "3DS", "NDS" };

static float saves_tabs_x(void) {
    return 6 + gui_button_w("SELECT") + 4;
}

static void saves_header_layout(const SavesView *v, SavesHeader *h) {
    bool searching = v->search && v->search[0];
    float cy0 = (GUI_HEADER_H - CHIP_H) / 2.0f;
    float right = GUI_BOT_W - 6;
    float left = gui_tabs_layout(saves_tabs_x(), 4, 18, saves_tabs, 3, NULL) + 6;

    if (searching) snprintf(h->count, sizeof(h->count), "%d of %d", v->count, v->view_total);
    else snprintf(h->count, sizeof(h->count), "%d save%s", v->count, v->count == 1 ? "" : "s");
    float count_w = gui_text_w(GUI_S_SMALL, h->count);

    h->marked[0] = '\0';
    float chip_w = 0;
    if (v->marked > 0) {
        snprintf(h->marked, sizeof(h->marked), "%d marked", v->marked);
        chip_w = 5 + gui_button_w("B") + 4 + gui_text_w(GUI_S_TINY, h->marked) + 5 + 8 + 6;
    }

    float search_w = SEARCH_CHIP_W;
    h->query[0] = '\0';
    if (searching) {
        search_label(v->search, h->query, sizeof(h->query));
        search_w = 8 + gui_text_w(GUI_S_TINY, h->query) + SEARCH_X_W;
    }
    float gap = 6;
    float need = search_w + (chip_w > 0 ? chip_w + gap : 0) + count_w + gap;
    h->show_count = need <= right - left;
    if (!h->show_count) need -= count_w + gap;
    if (searching && need > right - left) {
        // Shorten the query to what is left (keeping at least a few characters)
        float qw = gui_text_w(GUI_S_TINY, h->query) - (need - (right - left));
        if (qw < 24) qw = 24;
        char fitted[96];
        gui_fit(h->query, GUI_S_TINY, qw, fitted, sizeof(fitted));
        snprintf(h->query, sizeof(h->query), "%s", fitted);
        search_w = 8 + gui_text_w(GUI_S_TINY, h->query) + SEARCH_X_W;
    }

    h->search = gui_rect_make(right - search_w, cy0, search_w, CHIP_H);
    float x = h->search.x - gap;
    h->chip = gui_rect_make(0, 0, 0, 0);
    if (chip_w > 0) {
        h->chip = gui_rect_make(x - chip_w, cy0, chip_w, CHIP_H);
        x = h->chip.x - gap;
    }
    h->count_right = x;
}

GuiRect ui_saves_marked_chip(const SavesView *v) {
    SavesHeader h;
    saves_header_layout(v, &h);
    if (h.chip.w <= 0) return h.chip;
    // The whole header height, and a little to the sides
    return gui_rect_make(h.chip.x - 3, 0, h.chip.w + 6, GUI_HEADER_H);
}

int ui_saves_search_targets(const SavesView *v, GuiRect *touch) {
    SavesHeader h;
    saves_header_layout(v, &h);
    GuiRect s = h.search;
    if (!h.query[0]) {
        touch[0] = gui_rect_make(s.x - 3, 0, GUI_BOT_W - (s.x - 3), GUI_HEADER_H);
        return 1;
    }
    float xs = s.x + s.w - SEARCH_X_W;
    touch[0] = gui_rect_make(s.x - 3, 0, xs - (s.x - 3), GUI_HEADER_H);
    touch[1] = gui_rect_make(xs, 0, GUI_BOT_W - xs, GUI_HEADER_H);
    return 2;
}

// Under the "No saves match" message
int ui_saves_empty_buttons(const SavesView *v, GuiRect *draw, GuiRect *touch) {
    if (v->count > 0 || !v->search || !v->search[0]) return 0;
    float w = 130, y = 116;
    for (int i = 0; i < 2; i++) {
        float x = i == 0 ? GUI_BOT_W / 2.0f - GUI_BTN_GAP / 2.0f - w : GUI_BOT_W / 2.0f + GUI_BTN_GAP / 2.0f;
        draw[i] = gui_rect_make(x, y, w, GUI_BTN_H);
        touch[i] = gui_rect_make(x - GUI_BTN_GAP / 2.0f, y - 5, w + GUI_BTN_GAP, GUI_BTN_H + 10);
    }
    return 2;
}

static bool title_in_conflict(const TitleInfo *t) {
    return t->in_conflict || t->sync_state == TSTATE_CONFLICT;
}

int ui_saves_bar(const SavesView *v, UiBarItem *items) {
    static char upload[32];   // the items point into it until the next call
    bool any = v->count > 0 && v->selected >= 0 && v->selected < v->count;
    const char *a = "Sync";
    if (v->marked > 0) {
        snprintf(upload, sizeof(upload), "Upload %d marked", v->marked);
        a = upload;
    } else if (any && title_in_conflict(&v->titles[v->filtered[v->selected]])) {
        a = "Resolve";
    }
    items[SAVES_BAR_A] = (UiBarItem){ "A", a, any ? GUI_BTN_PRIMARY : GUI_BTN_DISABLED, 0, KEY_A, 0, false };
    items[SAVES_BAR_X] = (UiBarItem){ "X", "Sync all", v->title_count > 0 ? GUI_BTN_TONAL : GUI_BTN_DISABLED,
                                      0, KEY_X, 0, false };
    items[SAVES_BAR_Y] = (UiBarItem){ "Y", "Details", any ? GUI_BTN_TONAL : GUI_BTN_DISABLED, 0, KEY_Y, 0, false };
    return SAVES_BAR_COUNT;
}

int ui_saves_segments(GuiRect *touch) {
    gui_tabs_layout(saves_tabs_x(), 4, 18, saves_tabs, 3, touch);
    // The whole header height
    for (int i = 0; i < 3; i++) touch[i] = gui_rect_make(touch[i].x, 0, touch[i].w, GUI_HEADER_H);
    return 3;
}

// The search chip: a magnifier, or the active query with an x to clear it
static void draw_search_chip(const SavesHeader *h, int held) {
    GuiRect s = h->search;
    float cy = s.y + CHIP_H / 2.0f;
    if (!h->query[0]) {
        GuiRect r = held == 1 ? gui_rect_grow(s, -1) : s;
        gui_rrect(r.x, r.y, r.w, r.h, r.h / 2, gui_rgb(HEX_LINE));
        gui_rrect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, (r.h - 2) / 2,
                  held == 1 ? gui_mix(HEX_PANEL_HI, HEX_ACCENT, 0.35f) : gui_rgb(HEX_PANEL_HI));
        gui_icon_search(s.x + s.w / 2, cy, 12, gui_rgb(HEX_TEXT));
        return;
    }
    u32 fill = gui_rgb(HEX_ACCENT), dark = gui_mix(HEX_ACCENT, 0x000000, 0.20f);
    GuiRect r = held ? gui_rect_grow(s, -1) : s;
    gui_rrect(r.x, r.y, r.w, r.h, r.h / 2, fill);
    // The pressed part darkens: the query (edit) or the x (clear)
    float xs = s.x + s.w - SEARCH_X_W;
    if (held == 1) {
        gui_rrect(r.x, r.y, xs - r.x + r.h / 2, r.h, r.h / 2, dark);
        gui_rect(xs, r.y, 1, r.h, fill);
    } else if (held == 2) {
        gui_rrect(xs - r.h / 2, r.y, r.x + r.w - xs + r.h / 2, r.h, r.h / 2, dark);
        gui_rect(xs - r.h / 2, r.y, r.h / 2, r.h, fill);
    }
    gui_text_mid(s.x + 8, s.y, CHIP_H, GUI_S_TINY, gui_rgb(HEX_INK), GUI_LEFT, 0, h->query);
    gui_rect(xs, s.y + 4, 1, CHIP_H - 8, gui_mix(HEX_ACCENT, HEX_INK, 0.35f));
    gui_icon_x(xs + SEARCH_X_W / 2.0f - 1, cy, 9, gui_rgb(HEX_INK));
}

void ui_draw_saves_bottom(const SavesView *v) {
    UiListGeom g = SAVES_GEOM;
    GuiRect edraw[2], etouch[2];
    int nempty = ui_saves_empty_buttons(v, edraw, etouch);
    if (v->count > 0) {
        ui_list_draw(v->anim, &g, v->count, v->selected, v->scroll, saves_row, (void *)v);
    } else if (nempty > 0) {
        char q[80], line[112];
        search_label(v->search, q, sizeof(q));
        snprintf(line, sizeof(line), "No saves match %s", q);
        gui_text_fit(GUI_BOT_W / 2.0f, 86, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_CENTER, GUI_BOT_W - 24, line);
        // Both edit and clear work by touch, so an Old 3DS (no ZR) is
        // never stuck on an empty result
        gui_btn(edraw[0], "B", "Clear search", GUI_BTN_TONAL, 0, v->empty_held == 1 || ((v->held & KEY_B) && v->marked <= 0));
        gui_btn(edraw[1], new3ds ? "ZR" : "^search", "Edit search", GUI_BTN_TONAL, 0,
                v->empty_held == 2 || (v->held & KEY_ZR));
    } else {
        gui_text(GUI_BOT_W / 2.0f, 104, GUI_S_BODY, gui_rgb(HEX_DIM), GUI_CENTER, "No saves to show");
    }

    gui_header_bar_ex(v->count == 0 || ui_list_at_top(v->anim));
    // SELECT cycles the view; a tap on a segment picks it
    static GuiTabsAnim tabs_anim;
    gui_button(6, GUI_HEADER_H / 2.0f, "SELECT");
    tabs_anim.held = v->seg_held;
    gui_tabs(&tabs_anim, saves_tabs_x(), 4, 18, saves_tabs, 3, v->view_mode, NULL);
    SavesHeader h;
    saves_header_layout(v, &h);
    if (h.show_count)
        gui_text_mid(h.count_right, 0, GUI_HEADER_H, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_RIGHT, 0, h.count);
    if (h.chip.w > 0) {
        // "B 3 marked x": a tap on it (or B) unmarks everything
        GuiRect chip = h.chip;
        bool down = v->chip_pressed || (v->held & KEY_B);
        GuiRect r = down ? gui_rect_grow(chip, -1) : chip;
        gui_rrect(r.x, r.y, r.w, r.h, r.h / 2,
                  down ? gui_mix(HEX_ACCENT, 0x000000, 0.20f) : gui_rgb(HEX_ACCENT));
        float x = chip.x + 5, cy = chip.y + CHIP_H / 2.0f;
        x += gui_button(x, cy, "B") + 4;
        x += gui_text_mid(x, chip.y, CHIP_H, GUI_S_TINY, gui_rgb(HEX_INK), GUI_LEFT, 0, h.marked) + 5;
        gui_icon_x(x + 4, cy, 8, gui_rgb(HEX_INK));
    }
    draw_search_chip(&h, v->search_held);

    UiBarItem items[SAVES_BAR_COUNT];
    int n = ui_saves_bar(v, items);
    ui_bar_draw(v->bar, items, n, v->held);
}

// ---------------------------------------------------------------------------
// Save compare (details / smart sync / confirm)
// ---------------------------------------------------------------------------

typedef struct {
    const TitleInfo *title;
    const SaveDetails *details;
    SyncAction verdict;
    const char *section;
} CompareView;

static void verdict_text(const SaveDetails *d, SyncAction a, u32 *hex, const char **head, const char **sub) {
    switch (a) {
        case SYNC_ACTION_UPLOAD:
            *hex = HEX_WARN;
            *head = "Upload";
            *sub = !d->server_exists ? "Not on the server yet"
                 : d->has_last_synced ? "Only this console changed since the last sync"
                 : "This console's copy goes to the server";
            break;
        case SYNC_ACTION_DOWNLOAD:
            *hex = HEX_INFO;
            *head = "Download";
            *sub = !d->local_exists ? "No save on this console yet"
                 : d->has_last_synced ? "Only the server changed since the last sync"
                 : "The server's copy replaces this console's";
            break;
        case SYNC_ACTION_CONFLICT:
            *hex = HEX_ERR;
            *head = "Conflict";
            *sub = d->has_last_synced ? "Both this console and the server changed"
                                      : "No sync history: pick which copy wins";
            break;
        case SYNC_ACTION_SERVER_ERROR:
            *hex = HEX_ERR;
            *head = d->server_status ? "Server error" : "Server unreachable";
            *sub = d->server_status ? "The server answered with an error: nothing to compare"
                                    : "The server didn't answer: nothing to compare";
            break;
        default:
            *hex = HEX_OK;
            *head = "Up to date";
            *sub = (!d->local_exists && !d->server_exists) ? "No save anywhere yet"
                                                           : "This console and the server match";
            break;
    }
}

// `error` (with an optional `error_detail` line) replaces the contents when
// the copy couldn't be read at all. `hash` NULL leaves the hash line out;
// `extra_label` NULL shows `extra` on its own.
static void save_column(float x, float y, float w, const char *heading, bool exists,
                        int files, u32 size, const char *hash, const char *extra_label,
                        const char *extra, u32 tone, const char *error, const char *error_detail) {
    gui_rrect(x, y, w, 76, 6, gui_rgb(HEX_BG2));
    gui_rrect(x, y, w, 3, 1.5f, gui_rgb(error ? HEX_ERR : tone));
    gui_text(x + 8, y + 6, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, heading);
    float lh = gui_line_h(GUI_S_SMALL);
    if (error) {
        gui_text_fit(x + 8, y + 8 + lh, GUI_S_BODY, gui_rgb(HEX_ERR), GUI_LEFT, w - 16, error);
        if (error_detail && error_detail[0])
            gui_text_wrap(x + 8, y + 9 + 2 * lh + 2, GUI_S_SMALL, gui_rgb(HEX_DIM), w - 16, 2, error_detail);
        return;
    }
    if (!exists) {
        gui_text(x + 8, y + 8 + lh, GUI_S_BODY, gui_rgb(HEX_MUTED), GUI_LEFT, "No save");
        return;
    }
    char size_str[24], line[48];
    format_size(size, size_str, sizeof(size_str));
    snprintf(line, sizeof(line), "%s  \xC2\xB7  %d file%s", size_str, files, files == 1 ? "" : "s");
    gui_text_fit(x + 8, y + 6 + lh, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, w - 16, line);
    float ly = y + 9 + 2 * lh;
    if (hash) {
        snprintf(line, sizeof(line), "%.16s", hash);
        gui_text_fit(x + 8, ly, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, w - 16, line);
        ly += lh;
    }
    if (extra && extra[0]) {
        char e[64];
        if (extra_label) snprintf(e, sizeof(e), "%s %s", extra_label, extra);
        else snprintf(e, sizeof(e), "%s", extra);
        gui_text_fit(x + 8, ly, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, w - 16, e);
    }
}

static void compare_top(void *ctx) {
    const CompareView *c = ctx;
    const SaveDetails *d = c->details;
    gui_header(c->section);

    gui_text_fit(12, 32, GUI_S_TITLE, gui_rgb(HEX_TEXT), GUI_LEFT, GUI_TOP_W - 24, c->title->name);
    gui_textf(12, 32 + gui_line_h(GUI_S_TITLE), GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT,
              "%s%s", c->title->title_id_hex,
              c->title->media_type == MEDIATYPE_GAME_CARD ? "  \xC2\xB7  game card" : "");

    float y = 70, w = (GUI_TOP_W - 16 - 10) / 2.0f;
    char date[32];
    format_date(d->server_last_sync, date, sizeof(date));
    char server_err[40] = "";
    if (d->server_error) {
        if (d->server_status)
            snprintf(server_err, sizeof(server_err), "Server error (HTTP %lu)", (unsigned long)d->server_status);
        else
            snprintf(server_err, sizeof(server_err), "No response");
    }
    save_column(8, y, w, "This console", d->local_exists, d->local_file_count, d->local_size,
                d->local_hash, NULL, NULL, HEX_WARN, NULL, NULL);
    save_column(8 + w + 10, y, w, "Server", d->server_exists, d->server_file_count, d->server_size,
                d->server_hash, "Synced", d->server_exists ? date : NULL, HEX_INFO,
                d->server_error ? server_err : NULL, d->server_error_msg);

    // Last synced hash
    y += 82;
    gui_text(10, y, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, "Last synced");
    gui_text_fit(84, y, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, GUI_TOP_W - 94,
                 d->has_last_synced ? d->last_synced_hash : "never synced from this console");

    // Verdict banner
    u32 hex;
    const char *head, *sub;
    verdict_text(d, c->verdict, &hex, &head, &sub);
    float by = 176, bh = 34;
    gui_rrect(8, by, GUI_TOP_W - 16, bh, 7, gui_mix(HEX_BG2, hex, 0.18f));
    gui_rrect(8, by, 4, bh, 2, gui_rgb(hex));
    float icx = 28, icy = by + bh / 2;
    gui_circle(icx, icy, 10, gui_rgb(hex));
    switch (c->verdict) {
        case SYNC_ACTION_UPLOAD:   gui_icon_up(icx, icy, 11, gui_rgb(HEX_INK)); break;
        case SYNC_ACTION_DOWNLOAD: gui_icon_down(icx, icy, 11, gui_rgb(HEX_INK)); break;
        case SYNC_ACTION_CONFLICT:
        case SYNC_ACTION_SERVER_ERROR: gui_text_mid(icx, by, bh, 0.6f, gui_rgb(HEX_INK), GUI_CENTER, 0, "!"); break;
        default:                   gui_icon_check(icx, icy, 11, gui_rgb(HEX_INK)); break;
    }
    gui_text(46, by + 3, GUI_S_BODY, gui_rgb(hex), GUI_LEFT, head);
    gui_text_fit(46, by + 3 + gui_line_h(GUI_S_BODY) - 1, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT,
                 GUI_TOP_W - 60, sub);

    // Thin footer so the top screen looks finished
    gui_rect(0, GUI_FOOTER_Y + 8, GUI_TOP_W, GUI_FOOTER_H - 8, gui_rgb(HEX_BG2));
}

static void compare_bottom(void *ctx) {
    (void)ctx;
    gui_header_bar();
}

static u32 run_compare(const TitleInfo *title, const SaveDetails *details, SyncAction verdict,
                       const char *section, UiTone tone, const char *card_title, const char *body,
                       const UiButton *buttons, int count) {
    CompareView view = { title, details, verdict, section };
    UiBackdrop prev = ui_set_backdrop(compare_top, compare_bottom, &view);
    u32 key = ui_dialog(tone, card_title, body, buttons, count);
    ui_restore_backdrop(prev);
    return key;
}

void ui_show_save_details(const TitleInfo *title, const SaveDetails *details) {
    static const UiButton b[] = { { KEY_B | KEY_A, "B", "Close" } };
    run_compare(title, details, sync_decide(details), "Save details", UI_TONE_ACCENT,
                "Save details", UI_DIM "Local copy on the left, server copy on the right.", b, 1);
}

bool ui_confirm_sync(const TitleInfo *title, const SaveDetails *details, bool is_upload) {
    UiButton b[] = { { KEY_B, "B", "Cancel" }, { KEY_A, "A", is_upload ? "Upload" : "Download" } };
    u32 key = run_compare(title, details, is_upload ? SYNC_ACTION_UPLOAD : SYNC_ACTION_DOWNLOAD,
                          is_upload ? "Upload" : "Download", is_upload ? UI_TONE_WARN : UI_TONE_INFO,
                          is_upload ? "Upload to the server?" : "Download from the server?",
                          is_upload ? "This console's save replaces the server copy."
                                    : "The server's save replaces the one on this console.",
                          b, 2);
    return (key & KEY_A) != 0;
}

SyncAction ui_confirm_smart_sync(const TitleInfo *title, const SaveDetails *details, SyncAction suggested) {
    if (suggested == SYNC_ACTION_SERVER_ERROR) {
        // Couldn't read the server's copy: offer nothing that would change a save
        static const UiButton b[] = { { KEY_A | KEY_B, "A", "OK" } };
        run_compare(title, details, suggested, "Smart Sync", UI_TONE_ERR, "Can't compare with the server",
                    "Nothing was changed.\n"
                    UI_DIM "Check Wi-Fi and the server, then try again.",
                    b, 1);
        return SYNC_ACTION_SERVER_ERROR;
    }
    if (suggested == SYNC_ACTION_CONFLICT) {
        static const UiButton b[] = {
            { KEY_B, "B", "Cancel" }, { KEY_X, "X", "Download" }, { KEY_A, "A", "Upload" },
        };
        u32 key = run_compare(title, details, suggested, "Smart Sync", UI_TONE_ERR,
                              "Both copies changed",
                              "Pick the copy to keep:\n"
                              UI_DIM "A uploads this console's save (the server\n"
                              UI_DIM "keeps its old copy in the history).\n"
                              UI_DIM "X downloads the server's save to this console.",
                              b, 3);
        if (key & KEY_A) return SYNC_ACTION_UPLOAD;
        if (key & KEY_X) return SYNC_ACTION_DOWNLOAD;
        return SYNC_ACTION_UP_TO_DATE;
    }
    if (suggested == SYNC_ACTION_UP_TO_DATE) {
        static const UiButton b[] = { { KEY_A | KEY_B, "A", "OK" } };
        run_compare(title, details, suggested, "Smart Sync", UI_TONE_OK, "Already in sync",
                    "Nothing to do: this console and the server have the same save.", b, 1);
        return SYNC_ACTION_UP_TO_DATE;
    }
    bool up = (suggested == SYNC_ACTION_UPLOAD);
    UiButton b[] = { { KEY_B, "B", "Cancel" }, { KEY_A, "A", up ? "Upload" : "Download" } };
    u32 key = run_compare(title, details, suggested, "Smart Sync", up ? UI_TONE_WARN : UI_TONE_INFO,
                          up ? "Upload this save?" : "Download this save?",
                          up ? "This console has the newer save. Send it to the server?"
                             : "The server has the newer save. Copy it to this console?",
                          b, 2);
    return (key & KEY_A) ? suggested : SYNC_ACTION_UP_TO_DATE;
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

#define HIST_ROWS GUI_LIST_ROWS
#define HIST_ROW_H GUI_ROW_H

static const UiListGeom hist_geom = { 0, GUI_LIST_Y, GUI_BOT_W, HIST_ROWS, HIST_ROW_H, false, true };
static const UiBarItem hist_bar_items[] = {
    { "A", "Restore", GUI_BTN_PRIMARY, 0, KEY_A, 0, false },
    { "B", "Back", GUI_BTN_TONAL, 0, KEY_B, 0, false },
};

typedef struct {
    const TitleInfo *title;
    const HistoryVersion *versions;
    int count, selected, scroll;
    UiListAnim anim;   // per visit: the bar never glides in from another game
    UiBar bar;
    u32 held;          // keys held (0 behind a dialog)
} HistoryView;

static void history_row(void *ctx, int index, float x, float y, float w, float h, float hl, bool pressed) {
    (void)pressed;
    const HistoryView *v = ctx;
    const HistoryVersion *hv = &v->versions[index];
    char date[32], size[24];
    ui_format_when(hv->timestamp, date, sizeof(date));
    format_size(hv->size, size, sizeof(size));
    u32 text = gui_mix(HEX_TEXT, HEX_INK, hl), dim = gui_mix(HEX_DIM, HEX_INK, hl);
    float dw = gui_text_mid(x + 10, y, h, GUI_S_BODY, text, GUI_LEFT, 0, date);
    float tag_x = x + 10 + dw + 8 > x + 134 ? x + 10 + dw + 8 : x + 134;
    if (index == 0) gui_text_mid(tag_x, y, h, GUI_S_TINY, gui_mix(HEX_ACCENT2, HEX_INK, hl), GUI_LEFT, 0, "Newest");
    gui_text_mid(x + w - 70, y, h, GUI_S_SMALL, dim, GUI_RIGHT, 0, size);
    char files[16];
    snprintf(files, sizeof(files), "%d file%s", hv->file_count, hv->file_count == 1 ? "" : "s");
    gui_text_mid(x + w - 10, y, h, GUI_S_SMALL, dim, GUI_RIGHT, 0, files);
}

static void history_top(void *ctx) {
    const HistoryView *v = ctx;
    gui_header("History");
    gui_panel(8, 32, GUI_TOP_W - 16, 152);
    gui_text_wrap(20, 40, GUI_S_TITLE, gui_rgb(HEX_TEXT), GUI_TOP_W - 40, 2, v->title->name);
    const HistoryVersion *hv = &v->versions[v->selected];
    char date[32], size[24], files[16], pos[24];
    ui_format_when(hv->timestamp, date, sizeof(date));
    format_size(hv->size, size, sizeof(size));
    snprintf(files, sizeof(files), "%d", hv->file_count);
    snprintf(pos, sizeof(pos), "%d of %d", v->selected + 1, v->count);
    float y = 92, lh = gui_line_h(GUI_S_BODY) + 2;
    kv(20, y, 70, 300, "Version", pos, HEX_TEXT);
    kv(20, y + lh, 70, 300, "Saved", date, HEX_TEXT);
    kv(20, y + 2 * lh, 70, 300, "Size", size, HEX_TEXT);
    kv(20, y + 3 * lh, 70, 300, "Files", files, HEX_TEXT);
    static const GuiHint hints[] = { { "LR", "Page" } };
    gui_footer(hints, 1);
}

static void history_bottom(void *ctx) {
    HistoryView *v = ctx;
    ui_list_draw(&v->anim, &hist_geom, v->count, v->selected, v->scroll, history_row, v);
    gui_header_bar_ex(ui_list_at_top(&v->anim));
    gui_text_mid(10, 0, GUI_HEADER_H, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, 0, "Saved versions");
    char n[16];
    snprintf(n, sizeof(n), "%d", v->count);
    gui_text_mid(GUI_BOT_W - 10, 0, GUI_HEADER_H, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_RIGHT, 0, n);
    ui_bar_draw(&v->bar, hist_bar_items, 2, v->held);
}

char *ui_show_history(const TitleInfo *title, HistoryVersion *versions, int version_count) {
    if (version_count <= 0) {
        ui_message(UI_TONE_INFO, "History", "No previous versions of this save on the server.");
        return NULL;
    }
    HistoryView v;
    memset(&v, 0, sizeof(v));
    v.title = title;
    v.versions = versions;
    v.count = version_count;
    v.bar = (UiBar)UI_BAR_INIT;
    UiBackdrop prev = ui_set_backdrop(history_top, history_bottom, &v);
    char *result = NULL;
    hidScanInput();
    ui_input_latch_touch();
    UiInput in;
    while (ui_input_scan(&in)) {
        u32 down = in.down;
        UiListEvent ev = ui_list_input(&v.anim, &hist_geom, v.count, &v.selected, &v.scroll, &in,
                                       in.rep & UI_LIST_KEYS);
        if (ev.kind != UI_LIST_REATTACHED && (in.rep & UI_LIST_KEYS)) {
            ui_list_keys(&v.anim, in.rep, v.count, HIST_ROWS, &v.selected);
            if (v.selected < v.scroll) v.scroll = v.selected;
            if (v.selected >= v.scroll + HIST_ROWS) v.scroll = v.selected - HIST_ROWS + 1;
        }
        int hit = ui_bar_input(&v.bar, &in, hist_bar_items, 2);
        if (hit == 1 || (down & KEY_B)) break;
        // A, the Restore button, or a tap on the selected row: the caller
        // asks before anything is written
        if (hit == 0 || ev.kind == UI_LIST_TAP_SELECTED || (down & KEY_A)) {
            ui_list_reveal(&v.anim, v.count, v.selected, &v.scroll);
            result = (char *)malloc(32);
            if (result) {
                strncpy(result, versions[v.selected].timestamp, 31);
                result[31] = '\0';
            }
            break;
        }

        v.held = in.held;
        gui_begin(true);
        gui_screen(GUI_TOP);
        history_top(&v);
        gui_screen(GUI_BOTTOM);
        history_bottom(&v);
        gui_end();
        v.held = 0;
    }
    ui_input_latch_touch();
    ui_restore_backdrop(prev);
    return result;
}

// Restore confirmation: the save this replaces beside the chosen version
typedef struct {
    const TitleInfo *title;
    const SaveDetails *details;
    const HistoryVersion *version;
    int index, count;
} RestoreView;

static void restore_top(void *ctx) {
    const RestoreView *r = ctx;
    const SaveDetails *d = r->details;
    gui_header("Restore");

    gui_text_fit(12, 32, GUI_S_TITLE, gui_rgb(HEX_TEXT), GUI_LEFT, GUI_TOP_W - 24, r->title->name);
    gui_textf(12, 32 + gui_line_h(GUI_S_TITLE), GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT,
              "%s%s", r->title->title_id_hex,
              r->title->media_type == MEDIATYPE_GAME_CARD ? "  \xC2\xB7  game card" : "");

    float y = 70, w = (GUI_TOP_W - 16 - 10) / 2.0f;
    // Left: what is on the console now, and whether it changed since the
    // last sync (i.e. whether restoring loses progress the server never saw)
    const char *state = !d->has_last_synced ? "No sync record"
                      : strcmp(d->local_hash, d->last_synced_hash) != 0 ? "Changed since last sync"
                      : "Same as last sync";
    bool unreadable = d->local_exists && d->local_file_count == 0 && strncmp(d->local_hash, "ERROR", 5) == 0;
    save_column(8, y, w, "Now on this console", d->local_exists, d->local_file_count, d->local_size,
                NULL, NULL, state, HEX_LINE, unreadable ? "Can't read the save" : NULL,
                unreadable ? d->local_hash : NULL);

    // Right: the version that replaces it
    char pos[32], when[40];
    snprintf(pos, sizeof(pos), "Version %d of %d", r->index + 1, r->count);
    ui_format_when(r->version->timestamp, when, sizeof(when));
    save_column(8 + w + 10, y, w, pos, true, r->version->file_count, r->version->size,
                NULL, NULL, when, HEX_INFO, NULL, NULL);

    gui_text_fit(10, y + 84, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, GUI_TOP_W - 20,
                 "The server keeps every version: restoring one doesn't remove any.");

    // Thin footer so the top screen looks finished
    gui_rect(0, GUI_FOOTER_Y + 8, GUI_TOP_W, GUI_FOOTER_H - 8, gui_rgb(HEX_BG2));
}

bool ui_confirm_restore(const TitleInfo *title, const SaveDetails *details,
                        const HistoryVersion *version, int index, int count, int local_backups) {
    RestoreView view = { title, details, version, index, count };
    char body[256];
    bool has_save = details->local_exists && details->local_file_count > 0;
    if (!has_save)
        snprintf(body, sizeof(body), "Writes this version to this console.\n"
                 UI_DIM "There's no save on this console now.");
    else if (local_backups == 1)
        snprintf(body, sizeof(body), "Replaces the save on this console.\n"
                 "Your current save is copied to the SD first (keeps 1 copy).");
    else if (local_backups > 1)
        snprintf(body, sizeof(body), "Replaces the save on this console.\n"
                 "Your current save is copied to the SD first (keeps the last %d).", local_backups);
    else
        snprintf(body, sizeof(body), "Replaces the save on this console.\n"
                 UI_HI "Your current save is not backed up (Local backups is off).");
    UiBackdrop prev = ui_set_backdrop(restore_top, compare_bottom, &view);
    bool ok = ui_confirm(UI_TONE_WARN, "Restore this version?", body, "Restore");
    ui_restore_backdrop(prev);
    return ok;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

#define CFG_ITEMS 7
#define CFG_ROW_H GUI_ROW_H
#define CFG_ACTIONS 4   // first entry that is an action, not a value

static const UiListGeom cfg_geom = { 0, GUI_LIST_Y, GUI_BOT_W, GUI_LIST_ROWS, CFG_ROW_H, false, true };

enum {
    CFG_SERVER_URL = 0, CFG_SERVER_URL_ALT, CFG_API_KEY, CFG_NDS_DIR,
    CFG_RESCAN, CFG_REFRESH_CATALOG, CFG_UPDATE,
};

static const char *const cfg_labels[CFG_ITEMS] = {
    "Server URL", "Alternate URL", "API key", "NDS ROM folder",
    "Rescan titles", "Refresh catalog", "Check for updates",
};

static const char *const cfg_help[CFG_ITEMS] = {
    "Address of your GameSync server, e.g. http://192.168.1.100:8000",
    "Optional second address of the same server, tried when the first doesn't answer. Empty = off.",
    "The server's SYNC_API_KEY. Sent with every request.",
    "Where your DS ROMs and .sav files live (TWiLight Menu++ / nds-bootstrap).",
    "Look for installed titles and DS saves again.",
    "Ask the server to rescan its ROM folder, then download the whole game catalog again.",
    "Download and install the latest GameSync CIA from your server.",
};

typedef struct {
    const AppConfig *cfg;
    int selected;
    UiBar bar;
    u32 held;   // keys held (0 behind a dialog)
} ConfigView;

// What A does on a row, for the action bar
static const char *cfg_verb(int index) {
    return index < CFG_ACTIONS ? "Edit" : "Run";
}

static void cfg_bar_item(int index, UiBarItem *item) {
    *item = (UiBarItem){ "A", cfg_verb(index), GUI_BTN_PRIMARY, 0, KEY_A, 140, false };
}

static void mask_key(const char *key, char *out, size_t size) {
    size_t len = strlen(key);
    if (len == 0) snprintf(out, size, "(not set)");
    else if (len > 4) snprintf(out, size, "%.4s\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2", key);
    else snprintf(out, size, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
}

static void config_top(void *ctx) {
    const ConfigView *v = ctx;
    ui_tab_header(UI_TAB_SETTINGS);
    gui_panel(8, 32, GUI_TOP_W - 16, 118);
    float lh = gui_line_h(GUI_S_BODY) + 1, y = 40;
    char key[48];
    mask_key(v->cfg->api_key, key, sizeof(key));
    // Mark which address requests go to when an alternate one is set
    bool alt = v->cfg->server_url_alt[0] != '\0';
    char server[MAX_URL_LEN + 16], server_alt[MAX_URL_LEN + 16];
    snprintf(server, sizeof(server), "%s%s", v->cfg->server_url[0] ? v->cfg->server_url : "(not set)",
             alt && !v->cfg->use_alt_url ? "  (in use)" : "");
    snprintf(server_alt, sizeof(server_alt), "%s%s", alt ? v->cfg->server_url_alt : "(not set)",
             alt && v->cfg->use_alt_url ? "  (in use)" : "");
    kv(20, y, 96, GUI_TOP_W - 40, "Server", server, HEX_TEXT);
    kv(20, y + lh, 96, GUI_TOP_W - 40, "Alternate", server_alt, alt ? HEX_TEXT : HEX_DIM);
    kv(20, y + 2 * lh, 96, GUI_TOP_W - 40, "API key", key, HEX_TEXT);
    kv(20, y + 3 * lh, 96, GUI_TOP_W - 40, "NDS ROMs", v->cfg->nds_dir[0] ? v->cfg->nds_dir : "(not set)", HEX_TEXT);
    kv(20, y + 4 * lh, 96, GUI_TOP_W - 40, "Console ID", v->cfg->console_id, HEX_DIM);
    kv(20, y + 5 * lh, 96, GUI_TOP_W - 40, "Config file", CONFIG_PATH, HEX_DIM);

    // Help for the highlighted entry
    gui_rrect(8, 158, GUI_TOP_W - 16, 52, 7, gui_rgb(HEX_BG2));
    gui_rrect(8, 158, 4, 52, 2, gui_rgb(HEX_ACCENT));
    gui_text(20, 163, GUI_S_SMALL, gui_rgb(HEX_ACCENT2), GUI_LEFT, cfg_labels[v->selected]);
    gui_text_wrap(20, 163 + gui_line_h(GUI_S_SMALL), GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_TOP_W - 40, 2,
                  cfg_help[v->selected]);
    static const GuiHint hints[] = { { "UD", "Move" }, { "START", "Exit" } };
    gui_footer(hints, 2);
}

static void config_row(void *ctx, int index, float x, float y, float w, float h, float hl, bool pressed) {
    (void)pressed;
    const ConfigView *v = ctx;
    if (index == CFG_ACTIONS) gui_rect(x + 10, y, w - 20, 1, gui_rgb(HEX_LINE));
    gui_text_mid(x + 12, y, h, GUI_S_BODY, gui_mix(HEX_TEXT, HEX_INK, hl), GUI_LEFT, 0, cfg_labels[index]);
    char value[64] = "";
    if (index == CFG_SERVER_URL) snprintf(value, sizeof(value), "%.63s", v->cfg->server_url);
    else if (index == CFG_SERVER_URL_ALT)
        snprintf(value, sizeof(value), "%.63s", v->cfg->server_url_alt[0] ? v->cfg->server_url_alt : "(not set)");
    else if (index == CFG_API_KEY) mask_key(v->cfg->api_key, value, sizeof(value));
    else if (index == CFG_NDS_DIR)
        snprintf(value, sizeof(value), "%.63s", v->cfg->nds_dir[0] ? v->cfg->nds_dir : "(not set)");
    else if (index == CFG_UPDATE && !v->cfg->update_check) snprintf(value, sizeof(value), "Off");
    if (value[0])
        gui_text_mid(x + w - 12, y, h, GUI_S_SMALL, gui_mix(HEX_DIM, HEX_INK, hl), GUI_RIGHT, 150, value);
    else
        gui_text_mid(x + w - 14, y, h, GUI_S_BODY, gui_mix(HEX_MUTED, HEX_INK, hl), GUI_RIGHT, 0, ">");
}

// Kept across visits to the tab, like the selection
static UiListAnim config_anim;

static void config_bottom(void *ctx) {
    ConfigView *v = ctx;
    ui_list_draw(&config_anim, &cfg_geom, CFG_ITEMS, v->selected, 0, config_row, v);
    gui_header_bar_ex(ui_list_at_top(&config_anim));
    gui_text_mid(10, 0, GUI_HEADER_H, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, 0, "Settings");
    UiBarItem item;
    cfg_bar_item(v->selected, &item);
    float right = ui_bar_layout(&item, 1, &(GuiRect){ 0, 0, 0, 0 }, &(GuiRect){ 0, 0, 0, 0 });
    ui_bar_draw(&v->bar, &item, 1, v->held);
    // The rest of the bar: what LEFT / RIGHT do here
    float x = right + 14, cy = GUI_BAR_Y + GUI_BAR_H / 2.0f;
    x += gui_button(x, cy, "LR") + 4;
    gui_text_mid(x, GUI_BAR_Y, GUI_BAR_H, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, 0, "First / last");
}

static void save_settings(const AppConfig *config) {
    if (!config_save(config))
        ui_message(UI_TONE_ERR, "Settings", "Couldn't write the settings to the SD card:\n" UI_DIM CONFIG_PATH);
}

// After a server address changed: pick the primary again, or probe both
// when an alternate is set (as at startup)
static void select_server(AppConfig *config) {
    config->use_alt_url = false;
    if (!config->server_url_alt[0]) return;
    ui_busy("Settings", "Looking for the server...");
    network_select_server(config);
}

// A on a row (or a tap on the selected row). True if the app must close.
static bool settings_activate(AppConfig *config, int selected, SettingsActionFn run_action) {
    SettingsResult action = SETTINGS_EXIT;
    switch (selected) {
        case CFG_SERVER_URL: {
            UiTextField f = { "Server URL", "http://192.168.1.100:8000", "OK", false, true, false };
            if (ui_edit_text_ex(&f, config->server_url, MAX_URL_LEN)) {
                save_settings(config);
                select_server(config);
            }
            break;
        }
        case CFG_SERVER_URL_ALT: {
            // Empty turns the alternate address off
            UiTextField f = { "Alternate URL", "Second address of the server (empty = off)", "OK", false, true, true };
            if (ui_edit_text_ex(&f, config->server_url_alt, MAX_URL_LEN)) {
                save_settings(config);
                select_server(config);
            }
            break;
        }
        case CFG_API_KEY: {
            UiTextField f = { "API key", "The server's SYNC_API_KEY", "OK", true, false, false };
            if (ui_edit_text_ex(&f, config->api_key, MAX_API_KEY_LEN)) save_settings(config);
            break;
        }
        case CFG_NDS_DIR: {
            // Empty = no DS ROM folder (the scan skips DS games)
            UiTextField f = { "NDS ROM folder", "sdmc:/roms/nds", "OK", false, false, true };
            if (ui_edit_text_ex(&f, config->nds_dir, MAX_PATH_LEN)) save_settings(config);
            break;
        }
        case CFG_RESCAN: action = SETTINGS_RESCAN; break;
        case CFG_REFRESH_CATALOG: action = SETTINGS_REFRESH_CATALOG; break;
        default: action = SETTINGS_UPDATE; break;
    }
    return action != SETTINGS_EXIT && run_action && run_action(action);
}

SettingsResult ui_settings_tab(AppConfig *config, SettingsActionFn run_action) {
    static int selected = 0;   // kept while the user visits other tabs
    ConfigView v = { config, selected, UI_BAR_INIT, 0 };
    UiBackdrop prev = ui_set_backdrop(config_top, config_bottom, &v);
    SettingsResult result = SETTINGS_EXIT;  // the app is closing
    int scroll = 0;   // the whole list fits: only ever dragged and rubber-banded

    hidScanInput();
    UiInput in;
    while (ui_input_scan(&in)) {
        u32 down = in.down;
        UiNav nav = ui_tab_nav(down);
        if (nav == UI_NAV_PREV) { result = SETTINGS_NAV_PREV; break; }
        if (nav == UI_NAV_NEXT) { result = SETTINGS_NAV_NEXT; break; }
        if (nav == UI_NAV_EXIT) { result = SETTINGS_EXIT; break; }

        // UP / DOWN wrap; LEFT / RIGHT are first / last (absolute moves, so
        // they never need reattaching)
        UiListEvent ev = ui_list_input(&config_anim, &cfg_geom, CFG_ITEMS, &v.selected, &scroll, &in,
                                       in.rep & (KEY_UP | KEY_DOWN));
        if (ev.kind != UI_LIST_REATTACHED) ui_list_keys(&config_anim, in.rep, CFG_ITEMS, CFG_ITEMS, &v.selected);

        UiBarItem item;
        cfg_bar_item(v.selected, &item);
        int hit = ui_bar_input(&v.bar, &in, &item, 1);
        if (hit == 0 || ev.kind == UI_LIST_TAP_SELECTED || (down & KEY_A)) {
            if (settings_activate(config, v.selected, run_action)) {
                result = SETTINGS_EXIT;
                break;
            }
        }

        v.held = in.held;
        gui_begin(true);
        gui_screen(GUI_TOP);
        config_top(&v);
        gui_screen(GUI_BOTTOM);
        config_bottom(&v);
        gui_end();
        v.held = 0;
    }
    selected = v.selected;
    ui_restore_backdrop(prev);
    return result;
}

// ---------------------------------------------------------------------------
// Text entry
// ---------------------------------------------------------------------------

// The longest field: a ROM path or a URL, whichever is longer
#define UI_TEXT_MAX ((MAX_PATH_LEN > MAX_URL_LEN ? MAX_PATH_LEN : MAX_URL_LEN) + 1)

// Characters of the D-pad editor (a space last, for searches)
static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789.:/-_ABCDEFGHIJKLMNOPQRSTUVWXYZ@?=&#%+! ";
static const int charset_len = sizeof(charset) - 1;

static int charset_index(char c) {
    for (int i = 0; i < charset_len; i++)
        if (charset[i] == c) return i;
    return 0;
}

typedef struct {
    const char *title, *hint;
    const char *text;
    int cursor, len;
    bool password;
} EditView;

#define BULLET "\xE2\x80\xA2"

// `n` bytes of `s`, or that many bullets for a password
static void edit_span(const EditView *e, const char *s, int n, char *out, size_t size) {
    if (!e->password) {
        snprintf(out, size, "%.*s", n, s);
        return;
    }
    size_t pos = 0;
    out[0] = '\0';
    for (int i = 0; i < n && pos + 4 <= size; i++) {
        memcpy(out + pos, BULLET, 3);
        pos += 3;
        out[pos] = '\0';
    }
}

static void edit_top(void *ctx) {
    const EditView *e = ctx;
    gui_header(e->title);
    gui_panel(8, 40, GUI_TOP_W - 16, 90);
    gui_text(20, 48, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, e->hint);

    // Text box with a window around the cursor. A password shows bullets,
    // except the character under the cursor (the one being chosen).
    float bx = 20, by = 72, bw = GUI_TOP_W - 40, bh = 30;
    gui_rrect(bx, by, bw, bh, 6, gui_rgb(HEX_BG));
    gui_rrect(bx, by + bh - 2, bw, 2, 1, gui_rgb(HEX_ACCENT));
    int start = 0;
    char before[UI_TEXT_MAX * 3];
    for (;;) {
        edit_span(e, e->text + start, e->cursor - start, before, sizeof(before));
        if (gui_text_w(GUI_S_BODY, before) < bw - 30 || start >= e->cursor) break;
        start++;
    }
    float x = bx + 8;
    float ty = by + (bh - gui_line_h(GUI_S_BODY)) / 2 + 1;
    x += gui_text(x, ty, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, before);
    char cur[2] = { e->cursor < e->len ? e->text[e->cursor] : ' ', 0 };
    float cw = gui_text_w(GUI_S_BODY, cur);
    if (cw < 6) cw = 6;
    bool blink = (gui_now_ms() / 333) % 2 == 0;
    gui_rrect(x, by + 6, cw + 2, bh - 12, 2, gui_rgb(blink ? HEX_ACCENT : HEX_PANEL_HI));
    gui_text(x + 1, ty, GUI_S_BODY, gui_rgb(HEX_INK), GUI_LEFT, cur);
    if (e->cursor < e->len) {
        char after[UI_TEXT_MAX * 3];
        edit_span(e, e->text + e->cursor + 1, e->len - e->cursor - 1, after, sizeof(after));
        gui_text_fit(x + cw + 3, ty, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, bx + bw - (x + cw + 3) - 6, after);
    }
    gui_text(20, 140, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT,
             "The system keyboard is unavailable: edit with the D-pad.");
    gui_footer(NULL, 0);
}

static void edit_bottom(void *ctx) {
    (void)ctx;
    gui_header_bar();
    gui_text_mid(10, 0, GUI_HEADER_H, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, 0, "D-pad editor");
    static const struct { const char *button, *label; } rows[] = {
        { "LR", "Move the cursor" }, { "UD", "Change the character" }, { "Y", "Insert a character" },
        { "X", "Delete before the cursor" }, { "A", "Confirm" }, { "B", "Cancel" },
    };
    for (int i = 0; i < 6; i++) {
        float y = 40 + i * 26;
        gui_rrect(10, y, GUI_BOT_W - 20, 22, 5, gui_rgb(HEX_PANEL));
        gui_button(20, y + 11, rows[i].button);
        gui_text_mid(44, y, 22, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, 0, rows[i].label);
    }
}

static bool dpad_editor(const UiTextField *f, char *buffer, int max_len) {
    char temp[UI_TEXT_MAX];
    if (max_len > (int)sizeof(temp)) max_len = sizeof(temp);
    ui_utf8_copy(buffer, temp, max_len);
    EditView e = { f->title, f->hint ? f->hint : "", temp, (int)strlen(temp), (int)strlen(temp), f->password };
    UiBackdrop prev = ui_set_backdrop(edit_top, edit_bottom, &e);
    bool confirmed = false;

    hidScanInput();
    ui_input_latch_touch();
    UiInput in;
    while (ui_input_scan(&in)) {
        u32 down = in.down, rep = in.rep;
        if (rep & KEY_LEFT && e.cursor > 0) e.cursor--;
        if (rep & KEY_RIGHT && e.cursor < e.len) e.cursor++;
        if (rep & (KEY_UP | KEY_DOWN)) {
            int dir = (rep & KEY_UP) ? 1 : -1;
            if (e.cursor < e.len) {
                temp[e.cursor] = charset[(charset_index(temp[e.cursor]) + dir + charset_len) % charset_len];
            } else if (e.len < max_len - 1) {
                temp[e.len++] = 'a';
                temp[e.len] = '\0';
            }
        }
        if ((down & KEY_Y) && e.len < max_len - 1) {
            memmove(&temp[e.cursor + 1], &temp[e.cursor], e.len - e.cursor + 1);
            temp[e.cursor++] = 'a';
            e.len++;
        }
        if ((rep & KEY_X) && e.cursor > 0) {
            memmove(&temp[e.cursor - 1], &temp[e.cursor], e.len - e.cursor + 1);
            e.cursor--;
            e.len--;
        }
        if (down & KEY_A) {
            // The same check as the keyboard's: say why, and keep editing
            const char *err = ui_text_field_error(temp, f->url, f->allow_empty);
            if (!err) {
                confirmed = true;
                break;
            }
            ui_message(UI_TONE_WARN, f->title, err);
            hidScanInput();
            continue;
        }
        if (down & KEY_B) break;

        gui_begin(true);
        gui_screen(GUI_TOP);
        edit_top(&e);
        gui_screen(GUI_BOTTOM);
        edit_bottom(&e);
        gui_end();
    }
    ui_input_latch_touch();
    ui_restore_backdrop(prev);
    if (confirmed) snprintf(buffer, max_len, "%s", temp);
    return confirmed;
}

// The keyboard applet calls this on OK: a URL field keeps the keyboard open
// with the reason until the text starts with http:// or https://
static SwkbdCallbackResult url_filter(void *user, const char **message, const char *text, size_t len) {
    (void)len;
    const UiTextField *f = user;
    const char *err = ui_text_field_error(text, true, f->allow_empty);
    if (!err) return SWKBD_CALLBACK_OK;
    *message = err;
    return SWKBD_CALLBACK_CONTINUE;
}

bool ui_edit_text_ex(const UiTextField *f, char *buffer, int max_len) {
    char text[UI_TEXT_MAX * 4];   // UTF-8: up to 4 bytes a character
    if (max_len > UI_TEXT_MAX) max_len = UI_TEXT_MAX;
    if (max_len < 2) return false;
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, max_len - 1);
    swkbdSetHintText(&kb, f->hint ? f->hint : "");
    swkbdSetInitialText(&kb, buffer);
    swkbdSetValidation(&kb, f->allow_empty ? SWKBD_ANYTHING : SWKBD_NOTEMPTY_NOTBLANK,
                       f->url ? SWKBD_FILTER_CALLBACK : 0, 0);
    if (f->url) swkbdSetFilterCallback(&kb, url_filter, (void *)f);
    if (f->password) swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
    swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, f->ok_label ? f->ok_label : "OK", true);
    text[0] = '\0';
    SwkbdButton button = swkbdInputText(&kb, text, sizeof(text));
    // Whatever the applet did, its last touch must not reach the screen
    hidScanInput();
    ui_input_latch_touch();
    if (button == SWKBD_BUTTON_RIGHT) {
        ui_utf8_copy(text, buffer, max_len);
        return true;
    }
    if (button == SWKBD_BUTTON_LEFT) return false;
    // No keyboard result: HOME / power pressed, or the applet couldn't run
    SwkbdResult r = swkbdGetResult(&kb);
    if (r == SWKBD_HOMEPRESSED || r == SWKBD_RESETPRESSED || r == SWKBD_POWERPRESSED) return false;
    return dpad_editor(f, buffer, max_len);
}

bool ui_edit_text(const char *title, const char *hint, char *buffer, int max_len) {
    UiTextField f = { title, hint, "OK", false, false, true };
    return ui_edit_text_ex(&f, buffer, max_len);
}
