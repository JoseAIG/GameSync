// Folder picker (FBI-style): browse the SD card's folders one level at a
// time and choose one, with a live preview of what the folder holds.
//
// Everything here runs on the main thread. Listing the folder the user
// opened is the direct result of the press and may block (a spinner card
// shows after 150 ms); counting what a folder holds never blocks a frame:
// it is a resumable DIR* cursor (folder_scan.c) given ~4 ms per frame.

#include "ui.h"
#include "folder_scan.h"

#include <dirent.h>
#include <math.h>
#include <strings.h>

#define PICK_ROWS GUI_LIST_ROWS
#define PICK_DEPTH 16
#define PICK_REST_MS 150          // the highlight rests this long before its count starts
#define PICK_BUDGET_MS 4          // counting time per frame
#define PICK_BUSY_MS 150          // a listing this slow shows a spinner card
#define PICK_SUBFOLDERS 64        // "Looking in subfolders": at most this many...
#define PICK_SUBFOLDER_MS 2000    // ...for at most this long
#define PICK_PUSH_PX 24.0f        // a new level slides in from this far

static const UiListGeom pick_geom = { 0, GUI_LIST_Y, GUI_BOT_W, PICK_ROWS, GUI_ROW_H, false, true };

enum { PICK_BAR_USE = 0, PICK_BAR_OPEN, PICK_BAR_TYPE, PICK_BAR_COUNT };
enum { PICK_HEAD_BACK = 0, PICK_HEAD_CANCEL, PICK_HEAD_COUNT };

typedef struct {
    int selected, scroll;
    char name[FOLDER_NAME_MAX];   // the folder opened from this level
} PickLevel;

typedef struct {
    const UiFolderRules *rules;
    FolderRules fr;
    char path[FOLDER_PATH_MAX];
    FolderList list, spare;       // the level shown; the next one is read into spare
    int selected, scroll;
    UiListAnim anim;
    PickLevel stack[PICK_DEPTH];
    int depth;
    GuiTransition trans;
    // What the current folder (what X picks) and the highlighted one hold
    bool cur_known;
    FolderCount cur_count;
    int child_for;                // index the child count is for, -1 = none
    bool child_known;
    FolderCount child_count;
    u64 child_since;              // when the highlight arrived on child_for
    // Input state for drawing
    UiBar bar;
    UiTap head_tap;
    int head_held;                // 1 + PICK_HEAD_* under a finger, 0 none
    u32 held;
    // A slow listing in progress
    const char *busy_msg;
    u64 busy_start, busy_drawn;
} Picker;

// 6 KB each: kept out of the stack
static FolderCounter cur_counter, child_counter;
static FolderCache count_cache;

static void child_path(const Picker *p, int index, char *out, size_t size) {
    folder_path_join(p->path, p->list.names[index], out, size);
}

static int max_scroll(const Picker *p) {
    return p->list.count > PICK_ROWS ? p->list.count - PICK_ROWS : 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void folder_icon(float x, float cy, u32 color) {
    gui_rrect(x, cy - 5, 6, 3, 1, color);
    gui_rrect(x, cy - 3, 14, 9, 2, color);
}

static void pick_row(void *ctx, int index, float x, float y, float w, float h, float hl, bool pressed) {
    (void)pressed;
    const Picker *p = ctx;
    folder_icon(x + 12, y + h / 2, gui_mix(HEX_DIM, HEX_INK, hl));
    gui_text_mid(x + 34, y, h, GUI_S_BODY, gui_mix(HEX_TEXT, HEX_INK, hl), GUI_LEFT, w - 34 - 26,
                 p->list.names[index]);
    gui_text_mid(x + w - 14, y, h, GUI_S_BODY, gui_mix(HEX_MUTED, HEX_INK, hl), GUI_RIGHT, 0, ">");
}

static const char *type_label(void) {
    static char label[16];
    snprintf(label, sizeof(label), "Type%s", gui_ellipsis());
    return label;
}

static int pick_bar_items(const Picker *p, UiBarItem *items) {
    items[PICK_BAR_USE] = (UiBarItem){ "X", "Use this folder", GUI_BTN_PRIMARY, 0, KEY_X, 0, false };
    items[PICK_BAR_OPEN] = (UiBarItem){ "A", "Open", p->list.count > 0 ? GUI_BTN_TONAL : GUI_BTN_DISABLED, 0,
                                        KEY_A, 0, false };
    items[PICK_BAR_TYPE] = (UiBarItem){ "Y", type_label(), GUI_BTN_TONAL, 0, KEY_Y, 0, false };
    return PICK_BAR_COUNT;
}

#define CANCEL_W 62
#define CANCEL_H 20

// Header touch targets: the back chevron + breadcrumb (no area at the
// root), the Cancel chip; *chip = the chip as drawn
static void pick_header_targets(const Picker *p, GuiRect *touch, GuiRect *chip) {
    *chip = gui_rect_make(GUI_BOT_W - 6 - CANCEL_W, (GUI_HEADER_H - CANCEL_H) / 2.0f, CANCEL_W, CANCEL_H);
    float back_w = chip->x - 6;
    touch[PICK_HEAD_BACK] = folder_path_is_root(p->path) ? gui_rect_make(0, 0, 0, 0)
                                                          : gui_rect_make(0, 0, back_w, GUI_HEADER_H);
    touch[PICK_HEAD_CANCEL] = gui_rect_make(chip->x - 3, 0, GUI_BOT_W - chip->x + 3, GUI_HEADER_H);
}

static void pick_bottom(void *ctx) {
    Picker *p = ctx;
    // The level's content slides; the header and the action bar stay put
    gui_transition_begin(&p->trans);
    if (p->list.count > 0) {
        ui_list_draw(&p->anim, &pick_geom, p->list.count, p->selected, p->scroll, pick_row, p);
    } else {
        float cy = GUI_LIST_Y + 60;
        folder_icon(GUI_BOT_W / 2.0f - 7, cy - 18, gui_rgb(HEX_MUTED));
        gui_text(GUI_BOT_W / 2.0f, cy, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_CENTER, "No folders in here");
        gui_text(GUI_BOT_W / 2.0f, cy + gui_line_h(GUI_S_BODY) + 2, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER,
                 "X uses this folder, B goes up");
    }
    gui_transition_end();

    gui_header_bar_ex(p->list.count == 0 || ui_list_at_top(&p->anim));
    GuiRect touch[PICK_HEAD_COUNT], chip;
    pick_header_targets(p, touch, &chip);
    bool root = folder_path_is_root(p->path);
    float x = 8;
    if (!root) {
        if (p->head_held == 1 + PICK_HEAD_BACK)
            gui_rrect(2, 3, touch[PICK_HEAD_BACK].w - 4, GUI_HEADER_H - 6, 6, gui_rgb(HEX_PANEL_HI));
        gui_icon_chevron_left(12, GUI_HEADER_H / 2.0f, 8, gui_rgb(HEX_ACCENT2));
        x = 22;
    }
    float bw = chip.x - 6 - x;
    char crumb[FOLDER_PATH_MAX + 8];
    if (p->list.truncated) {
        // Two lines: where, and that the list is cut
        gui_fit_left(p->path, GUI_S_SMALL, bw, crumb, sizeof(crumb));
        gui_text(x, 1, GUI_S_SMALL, gui_rgb(HEX_TEXT), GUI_LEFT, crumb);
        gui_textf(x, 13, GUI_S_TINY, gui_rgb(HEX_WARN), GUI_LEFT, "Showing the first %d folders", FOLDER_SCAN_CAP);
    } else {
        gui_fit_left(p->path, GUI_S_BODY, bw, crumb, sizeof(crumb));
        gui_text_mid(x, 0, GUI_HEADER_H, GUI_S_BODY, gui_rgb(HEX_TEXT), GUI_LEFT, 0, crumb);
    }
    gui_btn(chip, NULL, "Cancel", GUI_BTN_TONAL, 0, p->head_held == 1 + PICK_HEAD_CANCEL);

    UiBarItem items[PICK_BAR_COUNT];
    int n = pick_bar_items(p, items);
    ui_bar_draw(&p->bar, items, n, p->held);
}

// A line with a spinner while something is being counted
static void counting_line(float x, float y, float scale, const char *prefix) {
    float lh = gui_line_h(scale);
    float w = prefix && prefix[0] ? gui_text(x, y, scale, gui_rgb(HEX_DIM), GUI_LEFT, prefix) + 6 : 0;
    gui_spinner(x + w + 5, y + lh / 2 + 1, 5, gui_rgb(HEX_ACCENT));
    gui_textf(x + w + 16, y, scale, gui_rgb(HEX_DIM), GUI_LEFT, "Counting%s", gui_ellipsis());
}

static void pick_top(void *ctx) {
    const Picker *p = ctx;
    gui_header(p->rules->title);
    gui_panel(8, 32, GUI_TOP_W - 16, 150);
    float x = 20, w = GUI_TOP_W - 40;
    gui_text(x, 40, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, "This folder");
    char path[FOLDER_PATH_MAX + 8];
    gui_fit_left(p->path, GUI_S_TITLE, w, path, sizeof(path));
    gui_text(x, 40 + gui_line_h(GUI_S_SMALL), GUI_S_TITLE, gui_rgb(HEX_TEXT), GUI_LEFT, path);

    // What X would choose
    float y = 40 + gui_line_h(GUI_S_SMALL) + gui_line_h(GUI_S_TITLE) + 6;
    if (!p->cur_known) {
        counting_line(x, y, GUI_S_BODY, NULL);
    } else {
        char line[128];
        folder_count_summary(&p->cur_count, &p->fr, p->rules->item_label, line, sizeof(line));
        bool good = p->cur_count.items > 0;
        u32 hex = good ? HEX_OK : HEX_WARN;
        gui_circle(x + 4, y + gui_line_h(GUI_S_BODY) / 2 + 1, 4, gui_rgb(hex));
        gui_text_fit(x + 14, y, GUI_S_BODY, gui_rgb(hex), GUI_LEFT, w - 14, line);
        if (p->cur_count.capped)
            gui_textf(x + 14, y + gui_line_h(GUI_S_BODY), GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT,
                      "Counted the first %d files", FOLDER_SCAN_CAP);
    }

    // The highlighted folder, once the highlight rests on it
    y = 132;
    gui_rect(x, y - 8, w, 1, gui_rgb(HEX_LINE));
    if (p->list.count == 0) {
        gui_text(x, y, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, "No folders inside this one.");
    } else if (p->selected >= 0 && p->selected < p->list.count) {
        char name[48], prefix[80];
        gui_fit(p->list.names[p->selected], GUI_S_SMALL, 150, name, sizeof(name));
        snprintf(prefix, sizeof(prefix), "Inside %s:", name);
        if (p->child_for == p->selected && p->child_known) {
            char brief[96], line[200];
            folder_count_brief(&p->child_count, p->rules->item_label, brief, sizeof(brief));
            snprintf(line, sizeof(line), "%s %s", prefix, brief);
            gui_text_fit(x, y, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, w, line);
        } else {
            counting_line(x, y, GUI_S_SMALL, prefix);
        }
    }
    gui_text_fit(x, 156, GUI_S_SMALL, gui_rgb(HEX_MUTED), GUI_LEFT, w,
                 "X uses the folder shown above; A opens the highlighted one.");

    static const GuiHint hints[] = {
        { "A", "Open" }, { "B", "Up" }, { "X", "Use" }, { "Y", "Type path" }, { "LR", "Page" },
    };
    gui_footer(hints, 5);
}

static void pick_draw(Picker *p) {
    gui_begin(true);
    gui_screen(GUI_TOP);
    pick_top(p);
    gui_screen(GUI_BOTTOM);
    pick_bottom(p);
    gui_end();
}

// ---------------------------------------------------------------------------
// Counting (a few ms a frame)
// ---------------------------------------------------------------------------

static void counts_reset(Picker *p) {
    folder_count_stop(&cur_counter);
    folder_count_stop(&child_counter);
    p->cur_known = false;
    p->child_for = -1;
    p->child_known = false;
}

static void counts_step(Picker *p) {
    u64 now = gui_clock_ms(), deadline = now + PICK_BUDGET_MS;
    // The current folder first: it is what X would pick
    if (!p->cur_known) {
        if (!folder_count_busy(&cur_counter)) {
            if (folder_cache_get(&count_cache, p->path, &p->cur_count)) p->cur_known = true;
            else folder_count_start(&cur_counter, &p->fr, p->path);
        }
        if (!p->cur_known && folder_count_step(&cur_counter, gui_clock_ms, deadline)) {
            p->cur_count = cur_counter.result;
            p->cur_known = true;
            folder_cache_put(&count_cache, p->path, &p->cur_count);
        }
    }
    // The highlighted folder: a new highlight abandons the last one's count
    if (p->list.count == 0) return;
    if (p->selected != p->child_for) {
        folder_count_stop(&child_counter);
        p->child_for = p->selected;
        p->child_known = false;
        p->child_since = now;
    }
    if (p->child_known || p->child_for < 0 || p->child_for >= p->list.count) return;
    char path[FOLDER_PATH_MAX];
    child_path(p, p->child_for, path, sizeof(path));
    if (folder_cache_get(&count_cache, path, &p->child_count)) {
        p->child_known = true;
        return;
    }
    if (now - p->child_since < PICK_REST_MS || gui_clock_ms() >= deadline) return;
    if (!folder_count_busy(&child_counter)) folder_count_start(&child_counter, &p->fr, path);
    if (folder_count_step(&child_counter, gui_clock_ms, deadline)) {
        p->child_count = child_counter.result;
        p->child_known = true;
        folder_cache_put(&count_cache, path, &p->child_count);
    }
}

// The current folder's count, now (X needs the answer)
static const FolderCount *current_count(Picker *p) {
    if (!p->cur_known) {
        if (!folder_count_busy(&cur_counter)) folder_count_start(&cur_counter, &p->fr, p->path);
        folder_count_step(&cur_counter, NULL, 0);
        p->cur_count = cur_counter.result;
        p->cur_known = true;
        folder_cache_put(&count_cache, p->path, &p->cur_count);
    }
    return &p->cur_count;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

static void load_tick(void *ctx) {
    Picker *p = ctx;
    u64 now = gui_clock_ms();
    if (now - p->busy_start < PICK_BUSY_MS || now - p->busy_drawn < 250) return;
    p->busy_drawn = now;
    ui_busy(p->rules->title, p->busy_msg);
}

// Read `path`'s folders into the spare list and swap it in. False (the
// level shown is kept) if the folder can't be opened.
static bool pick_load(Picker *p, const char *path) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Opening %.40s%s", folder_path_name(path), gui_ellipsis());
    p->busy_msg = msg;
    p->busy_start = gui_clock_ms();
    p->busy_drawn = 0;
    bool ok = folder_list_read(path, &p->spare, load_tick, p);
    p->busy_msg = NULL;
    if (!ok) return false;
    FolderList t = p->list;
    p->list = p->spare;
    p->spare = t;
    return true;
}

static void pick_show(Picker *p, const char *path, int selected, int scroll, float from_px) {
    snprintf(p->path, sizeof(p->path), "%s", path);
    p->selected = p->list.count > 0 ? selected : 0;
    p->scroll = scroll;
    if (p->scroll > max_scroll(p)) p->scroll = max_scroll(p);
    if (p->scroll < 0) p->scroll = 0;
    ui_list_reset(&p->anim);
    if (from_px != 0) gui_transition_start(&p->trans, from_px);
    counts_reset(p);
}

static void cant_open(const char *path) {
    char body[FOLDER_PATH_MAX + 64];
    snprintf(body, sizeof(body), "%s\n" UI_DIM "The SD card couldn't list this folder.", path);
    ui_message(UI_TONE_WARN, "Can't open this folder", body);
}

static void pick_enter(Picker *p, int index) {
    if (index < 0 || index >= p->list.count) return;
    char path[FOLDER_PATH_MAX];
    child_path(p, index, path, sizeof(path));
    PickLevel level = { p->selected, p->scroll, "" };
    snprintf(level.name, sizeof(level.name), "%s", p->list.names[index]);
    if (!pick_load(p, path)) {
        cant_open(path);
        return;
    }
    // A deeper level than the stack holds forgets the oldest (going up
    // then finds the folder by name instead)
    if (p->depth == PICK_DEPTH) {
        memmove(&p->stack[0], &p->stack[1], sizeof(p->stack[0]) * (PICK_DEPTH - 1));
        p->depth--;
    }
    p->stack[p->depth++] = level;
    pick_show(p, path, 0, 0, PICK_PUSH_PX);
}

// Up one level, the highlight back on the folder we came out of. False at
// the root (nothing above).
static bool pick_up(Picker *p) {
    char parent[FOLDER_PATH_MAX], name[FOLDER_NAME_MAX];
    if (!folder_path_parent(p->path, parent, sizeof(parent))) return false;
    snprintf(name, sizeof(name), "%s", folder_path_name(p->path));
    if (!pick_load(p, parent)) {
        cant_open(parent);
        return true;
    }
    int sel = -1, scroll = 0;
    if (p->depth > 0) {
        const PickLevel *l = &p->stack[--p->depth];
        if (l->selected < p->list.count && strcasecmp(p->list.names[l->selected], name) == 0) {
            sel = l->selected;
            scroll = l->scroll;
        }
    }
    if (sel < 0) {
        // Above where the picker opened (or a typed path): find it by name
        p->depth = 0;
        sel = folder_list_find(&p->list, name);
        if (sel < 0) sel = 0;
        scroll = sel >= PICK_ROWS ? sel - PICK_ROWS / 2 : 0;
    }
    pick_show(p, parent, sel, scroll, -PICK_PUSH_PX);
    return true;
}

// Go straight to a folder (a typed path)
static bool pick_go(Picker *p, const char *path) {
    if (!pick_load(p, path)) return false;
    p->depth = 0;
    pick_show(p, path, 0, 0, PICK_PUSH_PX);
    return true;
}

// ---------------------------------------------------------------------------
// Choosing
// ---------------------------------------------------------------------------

typedef enum { PICK_STAY = 0, PICK_CHOSEN, PICK_CANCEL } PickOutcome;

// X / START on a folder with nothing in it: look one level down for the
// folder with the most items, then say so (B Back, X Use anyway, A Open it)
static PickOutcome pick_empty_choice(Picker *p) {
    int best = -1;
    FolderCount best_count = { 0 };
    if (p->list.count > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Looking in subfolders%s", gui_ellipsis());
        ui_busy(p->rules->title, msg);
        folder_count_stop(&child_counter);
        p->child_for = -1;
        u64 deadline = gui_clock_ms() + PICK_SUBFOLDER_MS;
        for (int i = 0; i < p->list.count && i < PICK_SUBFOLDERS; i++) {
            if (gui_clock_ms() >= deadline) break;
            char path[FOLDER_PATH_MAX];
            child_path(p, i, path, sizeof(path));
            FolderCount c;
            if (!folder_cache_get(&count_cache, path, &c)) {
                folder_count_start(&child_counter, &p->fr, path);
                if (!folder_count_step(&child_counter, gui_clock_ms, deadline)) {
                    folder_count_stop(&child_counter);
                    break;   // out of time: what was found so far
                }
                c = child_counter.result;
                folder_cache_put(&count_cache, path, &c);
            }
            if (c.items > best_count.items) {
                best = i;
                best_count = c;
            }
        }
    }

    char title[64], body[FOLDER_PATH_MAX + 160], name[40], open[48];
    snprintf(title, sizeof(title), "No %s here", p->rules->item_label);
    if (best >= 0) {
        char found[24];
        bool cut = ui_utf8_prefix(p->list.names[best], 12, name, sizeof(name));
        if (cut) strncat(name, gui_ellipsis(), sizeof(name) - strlen(name) - 1);
        snprintf(found, sizeof(found), "%d", best_count.items);
        snprintf(body, sizeof(body), "%s\n" UI_HI "Found %s in %s/.", p->path, found, name);
        snprintf(open, sizeof(open), "Open %s/", name);
        UiButton b[3] = { { KEY_B, "B", "Back" }, { KEY_X, "X", "Use anyway" }, { KEY_A, "A", open } };
        u32 k = ui_dialog(UI_TONE_WARN, title, body, b, 3);
        if (k & KEY_X) return PICK_CHOSEN;
        if (k & KEY_A) pick_enter(p, best);
        return PICK_STAY;
    }
    snprintf(body, sizeof(body), "%s\n" UI_DIM "Nothing in its subfolders either.", p->path);
    UiButton b[2] = { { KEY_B, "B", "Back" }, { KEY_X, "X", "Use anyway" } };
    return (ui_dialog(UI_TONE_WARN, title, body, b, 2) & KEY_X) ? PICK_CHOSEN : PICK_STAY;
}

static PickOutcome pick_choose(Picker *p) {
    if (current_count(p)->items > 0) return PICK_CHOSEN;
    return pick_empty_choice(p);
}

// Y: type a path. A folder that exists is opened (X still confirms it,
// with its preview); one that doesn't can be used anyway.
static PickOutcome pick_type(Picker *p, char *chosen, size_t chosen_size) {
    char text[FOLDER_PATH_MAX];
    snprintf(text, sizeof(text), "%s", p->path);
    UiTextField f = { p->rules->title,
                      p->rules->allow_none ? "A folder on the SD card (empty = none)" : "A folder on the SD card",
                      "Go", false, false, p->rules->allow_none };
    if (!ui_edit_text_ex(&f, text, sizeof(text))) return PICK_STAY;
    if (!text[0]) {
        chosen[0] = '\0';   // allow_none: no folder at all
        return PICK_CHOSEN;
    }
    char path[FOLDER_PATH_MAX];
    folder_path_normalize(text, path, sizeof(path));
    if (pick_go(p, path)) return PICK_STAY;
    char body[FOLDER_PATH_MAX + 64];
    snprintf(body, sizeof(body), "%s\n" UI_DIM "There's no folder at this path on the SD card.", path);
    UiButton b[2] = { { KEY_B, "B", "Back" }, { KEY_X, "X", "Use anyway" } };
    if (ui_dialog(UI_TONE_WARN, "Folder not found", body, b, 2) & KEY_X) {
        snprintf(chosen, chosen_size, "%s", path);
        return PICK_CHOSEN;
    }
    return PICK_STAY;
}

// ---------------------------------------------------------------------------
// The screen
// ---------------------------------------------------------------------------

// Where to open: `start` if it can be listed, else its nearest parent that
// can, else the root
static bool pick_open_start(Picker *p, const char *start) {
    char path[FOLDER_PATH_MAX];
    folder_path_normalize(start && start[0] ? start : "sdmc:/", path, sizeof(path));
    for (;;) {
        if (pick_load(p, path)) {
            pick_show(p, path, 0, 0, 0);
            return true;
        }
        char parent[FOLDER_PATH_MAX];
        if (!folder_path_parent(path, parent, sizeof(parent)) || strcmp(parent, path) == 0) break;
        snprintf(path, sizeof(path), "%s", parent);
    }
    return false;
}

bool ui_pick_folder_ex(const UiFolderRules *rules, const char *start, char *out, int out_size,
                       FolderCount *count_out) {
    if (count_out) memset(count_out, 0, sizeof(*count_out));
    static Picker p;   // ~10 KB of state, plus the two listings on the heap
    memset(&p, 0, sizeof(p));
    // Counts are kept for this visit only: ROMs may have arrived since
    // (the Catalog installs DS games into this folder)
    memset(&count_cache, 0, sizeof(count_cache));
    p.rules = rules;
    p.fr = (FolderRules){ rules->exts, rules->save_ext, rules->save_subdir };
    p.list.cap = p.spare.cap = FOLDER_SCAN_CAP;
    p.list.names = malloc(sizeof(*p.list.names) * FOLDER_SCAN_CAP);
    p.spare.names = malloc(sizeof(*p.spare.names) * FOLDER_SCAN_CAP);
    p.bar = (UiBar)UI_BAR_INIT;
    p.head_tap = (UiTap)UI_TAP_INIT;
    p.child_for = -1;
    if (!p.list.names || !p.spare.names) {
        free(p.list.names);
        free(p.spare.names);
        ui_message(UI_TONE_ERR, rules->title, "Not enough memory to list folders.");
        return false;
    }

    UiBackdrop prev = ui_set_backdrop(pick_top, pick_bottom, &p);
    bool chosen = false;
    char result[FOLDER_PATH_MAX] = "";
    if (!pick_open_start(&p, start)) {
        ui_message(UI_TONE_ERR, rules->title, "The SD card can't be read.");
        goto done;
    }

    hidScanInput();
    ui_input_latch_touch();
    UiInput in;
    while (ui_input_scan(&in)) {
        u32 down = in.down;
        int count = p.list.count;
        UiListEvent ev = ui_list_input(&p.anim, &pick_geom, count, &p.selected, &p.scroll, &in,
                                       in.rep & UI_LIST_KEYS);
        if (ev.kind != UI_LIST_REATTACHED && (in.rep & UI_LIST_KEYS) && count > 0) {
            ui_list_keys(&p.anim, in.rep, count, PICK_ROWS, &p.selected);
            if (p.selected < p.scroll) p.scroll = p.selected;
            if (p.selected >= p.scroll + PICK_ROWS) p.scroll = p.selected - PICK_ROWS + 1;
        }
        GuiRect head[PICK_HEAD_COUNT], chip;
        pick_header_targets(&p, head, &chip);
        int head_pressed = -1;
        int head_hit = ui_tap_buttons(&p.head_tap, &in, head, PICK_HEAD_COUNT, &head_pressed);
        UiBarItem items[PICK_BAR_COUNT];
        int n = pick_bar_items(&p, items);
        int hit = ui_bar_input(&p.bar, &in, items, n);

        PickOutcome out_now = PICK_STAY;
        if (head_hit == PICK_HEAD_CANCEL) {
            out_now = PICK_CANCEL;
        } else if (head_hit == PICK_HEAD_BACK) {
            pick_up(&p);
        } else if (down & KEY_B) {
            // Up from any level; only a fresh B at the root cancels (down,
            // never repeat: holding B can't run past the root)
            if (!pick_up(&p)) out_now = PICK_CANCEL;
        } else if (hit == PICK_BAR_USE || (down & (KEY_X | KEY_START))) {
            if (count > 0) ui_list_reveal(&p.anim, count, p.selected, &p.scroll);
            out_now = pick_choose(&p);
            if (out_now == PICK_CHOSEN) snprintf(result, sizeof(result), "%s", p.path);
        } else if (count > 0 && (hit == PICK_BAR_OPEN || ev.kind == UI_LIST_TAP_SELECTED || (down & KEY_A))) {
            ui_list_reveal(&p.anim, count, p.selected, &p.scroll);
            pick_enter(&p, p.selected);
        } else if (hit == PICK_BAR_TYPE || (down & KEY_Y)) {
            if (count > 0) ui_list_reveal(&p.anim, count, p.selected, &p.scroll);
            out_now = pick_type(&p, result, sizeof(result));
        }
        if (out_now == PICK_CANCEL) break;
        if (out_now == PICK_CHOSEN) {
            chosen = true;
            break;
        }

        counts_step(&p);
        p.held = in.held;
        p.head_held = head_pressed + 1;
        pick_draw(&p);
        p.held = 0;
        p.head_held = 0;
    }

done:
    if (chosen) {
        snprintf(out, (size_t)out_size, "%s", result);
        // What the caller reports: the count of the chosen folder, if known
        if (count_out && result[0] && strcmp(result, p.path) == 0 && p.cur_known) *count_out = p.cur_count;
    }
    folder_count_stop(&cur_counter);
    folder_count_stop(&child_counter);
    free(p.list.names);
    free(p.spare.names);
    ui_input_latch_touch();
    ui_restore_backdrop(prev);
    return chosen;
}

bool ui_pick_folder(const UiFolderRules *rules, const char *start, char *out, int out_size) {
    return ui_pick_folder_ex(rules, start, out, out_size, NULL);
}
