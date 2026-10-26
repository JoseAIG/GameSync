#include "common.h"
#include "card_spi.h"
#include "catalog.h"
#include "config.h"
#include "network.h"
#include "sync.h"
#include "title.h"
#include "ui.h"
#include "update.h"
#include <ctype.h>
#include <stdarg.h>

static AppConfig config;
static TitleInfo titles[MAX_TITLES];
static int title_count = 0;
static int selected = 0;
static int scroll_offset = 0;
static char status[MAX_URL_LEN + 64];
static UiTone status_tone = UI_TONE_ACCENT;
static u32 status_serial = 0;

// View filtering: filtered[] maps visible indices -> titles[] indices
static int view_mode = VIEW_ALL;
static int filtered[MAX_TITLES];
static int filtered_count = 0;

// Search by name, combined with the view ("" = none). Kept across tab
// visits and rescans.
static char search[64];
static int view_total = 0;   // titles in the view before the search

// Last compare with the server (A), shown on the top screen for that title
static SaveDetails last_details;
static int last_details_idx = -1;
// When this session last synced that title (gui_clock_ms(), 0 = it came
// from a compare): the top screen says "just now" from it, never from the
// console's settable clock
static u64 synced_ms = 0;

static SavesView view;
static UiListAnim saves_anim;   // the save list's presentation state
static UiBar saves_bar = UI_BAR_INIT;
static UiTap saves_chip_tap = UI_TAP_INIT;
static bool saves_chip_pressed;
static UiTap saves_seg_tap = UI_TAP_INIT;
static int saves_seg_held;      // 1 + segment under a finger, 0 = none
static u32 saves_held;          // keys held, while the Saves loop itself draws
static UiTap saves_search_tap = UI_TAP_INIT;
static int saves_search_held;   // 1 query / magnifier, 2 its x, 0 none
static UiTap saves_empty_tap = UI_TAP_INIT;
static int saves_empty_held;    // 1 + empty-result button under a finger

#define LIST_VISIBLE SAVES_ROWS

static void set_status(UiTone tone, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_status(UiTone tone, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status, sizeof(status), fmt, ap);
    va_end(ap);
    status_tone = tone;
    status_serial++;   // the toast enters (and flashes) once per new message
}

// Every sync_state change goes through here: the stamp is what the row's
// state ring animates from, so the ring only ever fires on its cause
static void set_title_state(TitleInfo *t, u8 state) {
    t->sync_state = state;
    t->state_ms = gui_clock_ms();
    if (!t->state_ms) t->state_ms = 1;
}

static void set_title_marked(TitleInfo *t, bool marked) {
    if (t->marked == marked) return;
    t->marked = marked;
    t->mark_ms = gui_clock_ms();
    if (!t->mark_ms) t->mark_ms = 1;
}

static int title_compare(const void *a, const void *b) {
    const TitleInfo *ta = (const TitleInfo *)a;
    const TitleInfo *tb = (const TitleInfo *)b;

    // Sort 3DS games before DS games
    if (ta->is_nds != tb->is_nds) {
        return ta->is_nds ? 1 : -1;  // 3DS (is_nds=false) comes first
    }

    // Within same type, sort alphabetically
    return strcasecmp(ta->name, tb->name);
}

// Rebuild the filtered index list from view_mode and the search. The title
// at titles[keep_idx] stays selected if it is still visible, at the same
// row on screen (`keep_off` rows below the top); otherwise the first row is.
static void rebuild_filter_keeping(int keep_idx, int keep_off) {
    filtered_count = 0;
    view_total = 0;
    for (int i = 0; i < title_count; i++) {
        bool include = false;
        switch (view_mode) {
            case VIEW_3DS: include = !titles[i].is_nds; break;
            case VIEW_NDS: include = titles[i].is_nds; break;
            default:       include = true; break;
        }
        if (!include) continue;
        view_total++;
        if (ui_name_matches(titles[i].name, search))
            filtered[filtered_count++] = i;
    }
    int pos = -1;
    for (int i = 0; i < filtered_count && keep_idx >= 0; i++)
        if (filtered[i] == keep_idx) pos = i;
    if (pos >= 0) {
        // A drag may have left the selection off screen: keep it in view
        if (keep_off < 0) keep_off = 0;
        if (keep_off > LIST_VISIBLE - 1) keep_off = LIST_VISIBLE - 1;
        int max_scroll = filtered_count > LIST_VISIBLE ? filtered_count - LIST_VISIBLE : 0;
        int sc = pos - keep_off;
        if (sc > max_scroll) sc = max_scroll;
        if (sc < 0) sc = 0;
        selected = pos;
        scroll_offset = sc;
    } else {
        selected = 0;
        scroll_offset = 0;
    }
    // Whatever the list was doing with the old rows ends; the new ones fade in
    ui_list_reset(&saves_anim);
    ui_list_crossfade(&saves_anim);
}

static int sel_title_idx(void);

static void rebuild_filter(void) {
    rebuild_filter_keeping(sel_title_idx(), selected - scroll_offset);
}

// Get the actual title index for the current selection
static int sel_title_idx(void) {
    if (selected >= 0 && selected < filtered_count)
        return filtered[selected];
    return -1;
}

// Count how many titles are marked (across ALL titles, not just filtered)
static int count_marked(void) {
    int count = 0;
    for (int i = 0; i < title_count; i++)
        if (titles[i].marked) count++;
    return count;
}

// Clear all marks
static void clear_marks(void) {
    for (int i = 0; i < title_count; i++)
        set_title_marked(&titles[i], false);
}

// Game names from the server, on the worker (a copy of the titles goes
// with it; the names come back once it is done). Cancelled, or no memory
// for the copy: the titles keep their product codes.
typedef struct {
    AppConfig config;
    int count;
    TitleInfo titles[MAX_TITLES];
} NamesJob;

static bool names_job(void *ctx) {
    NamesJob *j = ctx;
    titles_fetch_names(&j->config, j->titles, j->count);
    return true;
}

static void fetch_names(void) {
    NamesJob *j = ui_job_ctx(sizeof(NamesJob));
    if (!j) return;
    j->config = config;
    j->count = title_count;
    memcpy(j->titles, titles, (size_t)title_count * sizeof(TitleInfo));
    UiProgress p = { 0 };
    p.title = "Loading";
    p.status = "Fetching game names...";
    p.frac = -1;
    bool cancelled = false;
    if (ui_run_job(names_job, j, &p, true, &cancelled) && !cancelled)
        for (int i = 0; i < title_count; i++) memcpy(titles[i].name, j->titles[i].name, sizeof(titles[i].name));
    ui_job_release(j);
}

static void scan_titles(void) {
    // The selected title, to find it again in the new (re-sorted) list
    char keep_id[17] = "";
    FS_MediaType keep_media = 0;
    bool keep_nds = false;
    int keep_off = selected - scroll_offset;
    int idx = sel_title_idx();
    if (idx >= 0) {
        snprintf(keep_id, sizeof(keep_id), "%s", titles[idx].title_id_hex);
        keep_media = titles[idx].media_type;
        keep_nds = titles[idx].is_nds;
    }

    ui_job_settle();   // a cancelled job may still be reading a save
    ui_busy("Loading", "Scanning titles...");
    title_count = titles_scan(titles, MAX_TITLES, config.nds_dir);

    // Fetch game names from server
    if (title_count > 0) {
        fetch_names();
        qsort(titles, title_count, sizeof(TitleInfo), title_compare);
    }

    int keep = -1;
    for (int i = 0; i < title_count && keep_id[0] && keep < 0; i++)
        if (strcmp(titles[i].title_id_hex, keep_id) == 0 && titles[i].media_type == keep_media &&
            titles[i].is_nds == keep_nds)
            keep = i;
    last_details_idx = -1;
    rebuild_filter_keeping(keep, keep_off);
}

// Clamp scroll so the selected item is always visible
static void update_scroll(void) {
    if (selected < scroll_offset)
        scroll_offset = selected;
    if (selected >= scroll_offset + LIST_VISIBLE)
        scroll_offset = selected - LIST_VISIBLE + 1;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void refresh_view(void) {
    view.titles = titles;
    view.filtered = filtered;
    view.count = filtered_count;
    view.selected = selected;
    view.scroll = scroll_offset;
    view.anim = &saves_anim;
    view.view_mode = view_mode;
    view.marked = count_marked();
    view.status = status;
    view.status_tone = status_tone;
    view.status_serial = status_serial;
    int idx = sel_title_idx();
    view.details = (idx >= 0 && idx == last_details_idx) ? &last_details : NULL;
    view.synced_ms = view.details ? synced_ms : 0;
    view.title_count = title_count;
    view.bar = &saves_bar;
    view.held = saves_held;
    view.chip_pressed = saves_chip_pressed;
    view.seg_held = saves_seg_held;
    view.search = search;
    view.view_total = view_total;
    view.search_held = saves_search_held;
    view.empty_held = saves_empty_held;
}

static void saves_top(void *ctx) {
    (void)ctx;
    refresh_view();
    ui_draw_saves_top(&view);
}

static void saves_bottom(void *ctx) {
    (void)ctx;
    refresh_view();
    ui_draw_saves_bottom(&view);
}

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

static const char *op_title = "Syncing";

// Name for a 16-hex title id inside a progress message, or NULL
static const char *name_for_id(const char *hex) {
    for (int i = 0; i < title_count; i++)
        if (strncmp(titles[i].title_id_hex, hex, 16) == 0) return titles[i].name;
    return NULL;
}

// Progress callback for sync operations. Messages look like
// "Uploading 3/10: 0004000000055D00": show the phase, a bar from "n/m",
// and the game's name instead of its id.
static void sync_progress(const char *message) {
    char phase[96];
    snprintf(phase, sizeof(phase), "%s", message);
    const char *name = NULL;
    char *colon = strstr(phase, ": ");
    if (colon) {
        const char *id = colon + 2;
        if (strlen(id) >= 16 && isxdigit((unsigned char)id[0])) name = name_for_id(id);
        if (name) *colon = '\0';
    }

    float frac = -1;
    for (const char *p = phase; *p; p++) {
        int n, m;
        if (isdigit((unsigned char)*p) && sscanf(p, "%d/%d", &n, &m) == 2 && m > 0) {
            frac = (float)n / (float)m;
            break;
        }
    }

    UiProgress pr = { 0 };
    pr.title = op_title;
    pr.name = name ? name : phase;
    pr.status = name ? phase : NULL;
    pr.frac = frac;
    if (!pr.status) {
        // No name: put the message as the status line under a spinner
        pr.name = NULL;
        pr.status = phase;
        pr.frac = frac >= 0 ? frac : -1;
    }
    ui_progress(&pr);
}

// Update progress callback
static const char *update_phase = "Downloading update";
static void update_progress_cb(int pct) {
    static int last_pct = -1;
    static u64 last_ms = 0;
    u64 now = osGetTime();
    if (pct != last_pct && (pct >= 100 || pct == 0 || now - last_ms >= 50)) {
        char right[8];
        snprintf(right, sizeof(right), "%d%%", pct);
        UiProgress pr = { 0 };
        pr.title = "Update";
        pr.name = update_phase;
        pr.status = "Please wait, don't power off.";
        pr.frac = pct / 100.0f;
        pr.right = right;
        ui_progress(&pr);
        last_pct = pct;
        last_ms = now;
    }
    // Reset for next use when complete
    if (pct >= 100) last_pct = -1;
}

static void wait_for_start(UiTone tone, const char *title, const char *body) {
    static const UiButton b[] = { { KEY_START, "START", "Exit" } };
    ui_dialog(tone, title, body, b, 1);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

// A compare: done (last_details holds it), this console's save couldn't
// be read (out of memory), or the user cancelled it (nothing changed)
typedef enum { CMP_OK = 0, CMP_UNREADABLE, CMP_CANCELLED } CompareResult;
static CompareResult compare_title(int idx, const char *what);
static bool compare_ready(int idx, const char *what);

// What a failed upload / download means for the save, by SyncResult. No
// default: a code added to sync.h later must get its own (true) safety
// statement, and -Wswitch says so.
typedef struct {
    const char *safety, *next;
    bool retry;
    bool unchanged;   // `safety` says the console's save is as it was
} SyncFailText;

static SyncFailText sync_fail_text(SyncResult r, bool upload) {
    SyncFailText f = { "Nothing was changed on the console or the server.", "Try again.", true, true };
    bool backup = config.local_backups > 0;
    switch (r) {
        case SYNC_OK:
            break;
        case SYNC_ERR_NETWORK:
            f.next = "Check WiFi, or Settings > Server URL.";
            break;
        case SYNC_ERR_SERVER:
            f.next = "Check the server, then try again.";
            break;
        case SYNC_ERR_BUNDLE:
            break;
        case SYNC_ERR_TOO_LARGE:
        case SYNC_ERR_TOO_MANY_FILES:
            f.next = "This save is too big for GameSync to sync.";
            f.retry = false;
            break;
        case SYNC_ERR_INTEGRITY:
            f.safety = "The download was damaged, so nothing was written. Your save is unchanged.";
            break;
        case SYNC_ERR_BACKUP:
            f.safety = "Your current save couldn't be backed up, so nothing was written.";
            f.next = "Free some SD space, or turn Local backups off.";
            break;
        case SYNC_ERR_RESTORED:
            f.safety = "Writing failed; your previous save was put back.";
            break;
        case SYNC_ERR_RESTORE_FAILED:
            // A previous save existed (it was being put back); with backups
            // on it was copied to the SD before anything was written
            f.safety = backup ? "Writing failed and the previous save couldn't be put back. "
                                "A copy is in the local backup."
                              : "Writing failed and the previous save couldn't be put back. The save may be damaged.";
            f.next = backup ? "Copy it back from " BACKUP_DIR " with a save manager such as Checkpoint."
                            : "Check the save with a save manager such as Checkpoint.";
            f.retry = false;
            f.unchanged = false;
            break;
        case SYNC_ERR_ARCHIVE:
            // The fork only returns a plain ARCHIVE error on a download when
            // there was no previous save to protect
            f.safety = upload ? "Couldn't read this console's save. Nothing was changed."
                              : "Couldn't write the save. There was no previous save on this console.";
            f.unchanged = upload;
            break;
    }
    return f;
}

// Remember why `t` failed (shown on the top screen for its row)
static void set_title_failed(TitleInfo *t, const char *why) {
    set_title_state(t, TSTATE_FAILED);
    ui_utf8_copy(why && why[0] ? why : "Sync failed", t->last_error, sizeof(t->last_error));
}

// The card for a failed transfer of `t`; true = try again
static bool transfer_failed_card(const TitleInfo *t, SyncResult res, bool upload, const char *verb) {
    char title[96];
    snprintf(title, sizeof(title), "Couldn't %s %.60s", verb, t->name);
    SyncFailText f = sync_fail_text(res, upload);
    return ui_error_card(title, sync_result_str(res), f.safety, f.next, f.retry);
}

// The History list, on the worker
typedef struct {
    AppConfig config;
    char title_id[17];
    HistoryVersion versions[MAX_HISTORY_VERSIONS];
    int count;
} HistoryJob;

static bool history_job(void *ctx) {
    HistoryJob *j = ctx;
    j->count = sync_get_history(&j->config, j->title_id, j->versions, MAX_HISTORY_VERSIONS);
    return j->count >= 0;
}

// -1 failed, -2 cancelled, else the number of versions (in `versions`)
static int load_history(int idx, HistoryVersion *versions) {
    HistoryJob *j = ui_job_ctx(sizeof(HistoryJob));
    if (!j) {
        ui_busy("History", "Loading saved versions...");
        return sync_get_history(&config, titles[idx].title_id_hex, versions, MAX_HISTORY_VERSIONS);
    }
    j->config = config;
    memcpy(j->title_id, titles[idx].title_id_hex, sizeof(j->title_id));
    UiProgress p = { 0 };
    p.title = "History";
    p.name = titles[idx].name;
    p.status = "Loading saved versions...";
    p.frac = -1;
    bool cancelled = false;
    ui_run_job(history_job, j, &p, true, &cancelled);
    int count = cancelled ? -2 : j->count;
    if (count > 0) memcpy(versions, j->versions, (size_t)count * sizeof(HistoryVersion));
    ui_job_release(j);
    return count;
}

// A transfer of one title on the worker (upload, download or a History
// version). Not cancellable: once it has begun it runs to the end, and the
// main thread leaves `titles` alone meanwhile.
typedef enum { XFER_UPLOAD, XFER_DOWNLOAD, XFER_HISTORY } XferKind;
typedef struct {
    XferKind kind;
    const TitleInfo *title;
    const char *timestamp;   // XFER_HISTORY
    SyncResult res;
} XferJob;

static bool xfer_job(void *ctx) {
    XferJob *j = ctx;
    switch (j->kind) {
        case XFER_UPLOAD:   j->res = sync_title(&config, j->title, sync_progress); break;
        case XFER_DOWNLOAD: j->res = sync_download_title(&config, j->title, sync_progress); break;
        case XFER_HISTORY:  j->res = sync_download_history(&config, j->title, j->timestamp, sync_progress); break;
    }
    return j->res == SYNC_OK;
}

// `first` = the card shown until the transfer reports its own progress
static SyncResult run_xfer(XferKind kind, const TitleInfo *t, const char *timestamp, const char *first) {
    XferJob j = { kind, t, timestamp, SYNC_ERR_NETWORK };
    UiProgress p = { 0 };
    p.title = op_title;
    p.name = t->name;
    p.status = first;
    p.frac = -1;
    ui_run_job(xfer_job, &j, &p, false, NULL);
    return j.res;
}

static void do_history(void) {
    int idx = sel_title_idx();
    if (idx < 0) return;
    HistoryVersion versions[MAX_HISTORY_VERSIONS];
    int count;
    for (;;) {
        count = load_history(idx, versions);
        if (count == -2) {
            set_status(UI_TONE_ACCENT, "History cancelled, nothing changed");
            return;
        }
        if (count >= 0) break;
        if (!ui_error_card("Couldn't load History",
                           network_last_error()[0] ? network_last_error() : "The server didn't send the list.",
                           "Nothing was changed.", "Check WiFi, or Settings > Server URL.", true)) {
            set_status(UI_TONE_ERR, "History not loaded, nothing changed");
            return;
        }
    }

    if (count == 0) {
        set_status(UI_TONE_INFO, "No history available for %.40s", titles[idx].name);
    } else {
        char *selected_ts = ui_show_history(&titles[idx], versions, count);
        if (!selected_ts) {
            set_status(UI_TONE_ACCENT, "History closed");
            return;
        }
        int vi = -1;
        for (int i = 0; i < count && vi < 0; i++)
            if (strcmp(versions[i].timestamp, selected_ts) == 0) vi = i;
        // Overwriting the console's save: show what's there now and ask first
        bool read = vi >= 0;
        CompareResult cr = CMP_UNREADABLE;
        while (read && (cr = compare_title(idx, "Restore")) == CMP_UNREADABLE) {
            read = ui_error_card("Couldn't read this console's save", "Not enough memory to read it.",
                                 "Nothing was changed.", "Try again.", true);
        }
        if (!read || cr == CMP_CANCELLED) {
            if (cr == CMP_CANCELLED) set_status(UI_TONE_ACCENT, "Restore cancelled, nothing changed");
            else set_status(UI_TONE_ERR, "Restore not started, nothing changed");
            free(selected_ts);
            return;
        }
        bool had_save = last_details.local_exists && last_details.local_file_count > 0;
        if (!ui_confirm_restore(&titles[idx], &last_details, &versions[vi], vi, count, config.local_backups)) {
            set_status(UI_TONE_ACCENT, "Restore cancelled");
            free(selected_ts);
            return;
        }
        for (;;) {
            op_title = "Restoring";
            SyncResult res = run_xfer(XFER_HISTORY, &titles[idx], selected_ts, "Downloading history version...");
            if (res == SYNC_OK) {
                set_status(UI_TONE_OK, "Restored %.40s%s", titles[idx].name,
                           had_save && config.local_backups > 0 ? " \xC2\xB7 previous save backed up to SD" : "");
                titles[idx].in_conflict = false;
                set_title_state(&titles[idx], TSTATE_DOWNLOADED);
                break;
            }
            set_status(UI_TONE_ERR, "Restore failed: %s", sync_result_str(res));
            char title[96];
            snprintf(title, sizeof(title), "Couldn't restore %.60s", titles[idx].name);
            SyncFailText f = sync_fail_text(res, false);
            if (!ui_error_card(title, sync_result_str(res), f.safety, f.next, f.retry)) break;
        }
        last_details_idx = -1;
        free(selected_ts);
    }
}

static bool is_conflict(const TitleInfo *t) {
    return t->in_conflict || t->sync_state == TSTATE_CONFLICT;
}

// Append up to 3 names of titles[] passing `pick` as UI_HI lines (one line
// each), then "...and N more" for the rest of `total`
static int append_names(char *body, int size, int pos, bool (*pick)(const TitleInfo *), int total,
                        const char *style) {
    int listed = 0;
    for (int i = 0; i < title_count && listed < 3 && pos < size; i++) {
        if (!pick(&titles[i])) continue;
        char name[GUI_WRAP_LINE];
        ui_body_line(titles[i].name, name, sizeof(name));
        pos += snprintf(body + pos, size - pos, "%s%s\n", style, name);
        listed++;
    }
    if (total > listed && pos < size)
        pos += snprintf(body + pos, size - pos, "%s%sand %d more\n", style, gui_ellipsis(), total - listed);
    return pos < size ? pos : size - 1;
}

static bool is_marked(const TitleInfo *t) { return t->marked; }
static bool is_marked_conflict(const TitleInfo *t) { return t->marked && is_conflict(t); }

// How each entry of titles[] failed in the batch upload / Sync All being
// reported (result SYNC_OK: it didn't). Only this run's failures: a title
// still FAILED from an earlier single transfer isn't part of it.
typedef struct {
    u8 result;     // SyncResult
    bool upload;
} RunFailure;
static RunFailure run_failures[MAX_TITLES];

static bool run_failed(int i) {
    return i >= 0 && i < MAX_TITLES && run_failures[i].result != SYNC_OK;
}

static SyncFailText run_fail_text(int i) {
    return sync_fail_text((SyncResult)run_failures[i].result, run_failures[i].upload);
}

// Listing order: 0 = may be damaged, 1 = changed (nothing lost), 2 = unchanged
static int run_fail_rank(int i) {
    if (run_failures[i].result == SYNC_ERR_RESTORE_FAILED) return 0;
    return run_fail_text(i).unchanged ? 2 : 1;
}

// The card after a run with `total` failures: which ones and why, and
// whether the saves were changed (from each one's SyncResult, never
// assumed). `counts` ends the card when nothing changed. Saves that may
// have been changed are listed first so the cap never hides them.
static void run_failures_card(const char *title, int total, const char *counts) {
    bool damaged = false, changed = false;
    for (int i = 0; i < title_count && i < MAX_TITLES; i++) {
        if (!run_failed(i)) continue;
        int rank = run_fail_rank(i);
        if (rank == 0) damaged = true;
        if (rank < 2) changed = true;
    }
    // The card holds 10 body lines: a damaged save's statement and what to
    // do come first, so a long list can never cut them off
    SyncFailText worst = sync_fail_text(SYNC_ERR_RESTORE_FAILED, false);
    char body[768];
    int pos = 0;
    if (damaged) pos += snprintf(body, sizeof(body), "%s\n%s\n", worst.safety, worst.next);
    int cap = damaged ? 2 : 3, listed = 0;
    for (int rank = 0; rank < 3; rank++) {
        for (int i = 0; i < title_count && i < MAX_TITLES && listed < cap && pos < (int)sizeof(body); i++) {
            if (!run_failed(i) || run_fail_rank(i) != rank) continue;
            char name[GUI_WRAP_LINE], why[GUI_WRAP_LINE];
            ui_body_line(titles[i].name, name, sizeof(name));
            ui_body_line(titles[i].last_error[0] ? titles[i].last_error : "Sync failed", why, sizeof(why));
            pos += snprintf(body + pos, sizeof(body) - pos, UI_HI "%s\n" UI_DIM "%s\n", name, why);
            listed++;
        }
    }
    if (total > listed && pos < (int)sizeof(body))
        pos += snprintf(body + pos, sizeof(body) - pos, UI_HI "%sand %d more\n", gui_ellipsis(), total - listed);
    if (pos < (int)sizeof(body) && !damaged) {
        if (changed)
            snprintf(body + pos, sizeof(body) - pos,
                     UI_DIM "Nothing was lost: where a save couldn't be written, this console had none before.");
        else
            snprintf(body + pos, sizeof(body) - pos, UI_DIM "Nothing changed for these. %s", counts);
    }
    ui_dialog_ex(UI_TONE_ERR, title, body, (const UiButton[]){ { KEY_A | KEY_B, "A", "OK" } }, 1,
                 &(UiDialogOpts){ 0, true });
}

// Every marked save uploaded on the worker, one after the other; the main
// thread applies the results afterwards (it alone changes `titles`)
typedef struct {
    int marked;
    u8 res[MAX_TITLES];   // SyncResult per entry of titles[] (marked ones)
} BatchJob;

static bool batch_job(void *ctx) {
    BatchJob *j = ctx;
    int n = 0;
    for (int i = 0; i < title_count; i++) {
        if (!titles[i].marked) continue;
        char msg[160];
        snprintf(msg, sizeof(msg), "Uploading %d/%d: %s", ++n, j->marked, titles[i].title_id_hex);
        sync_progress(msg);
        j->res[i] = (u8)sync_title(&config, &titles[i], NULL);
    }
    return true;
}

static void do_batch_upload(int marked) {
    // Say plainly when an upload replaces a server copy that changed too
    int conflicts = 0;
    for (int i = 0; i < title_count; i++)
        if (is_marked_conflict(&titles[i])) conflicts++;

    char body[640], title[64], yes[24];
    int pos;
    UiTone tone;
    snprintf(yes, sizeof(yes), "Upload %d", marked);
    if (conflicts > 0) {
        tone = UI_TONE_ERR;
        snprintf(title, sizeof(title), "Replace %d server cop%s?", conflicts, conflicts == 1 ? "y" : "ies");
        pos = append_names(body, sizeof(body), 0, is_marked_conflict, conflicts, UI_HI);
        pos += snprintf(body + pos, sizeof(body) - pos,
                        "These changed on the server too. Uploading replaces the server's copies; "
                        "the server keeps them in History.");
        if (marked > conflicts && pos < (int)sizeof(body))
            snprintf(body + pos, sizeof(body) - pos, "\n" UI_DIM "%d other marked save%s upload%s too.",
                     marked - conflicts, marked - conflicts == 1 ? "" : "s", marked - conflicts == 1 ? "s" : "");
    } else {
        tone = UI_TONE_WARN;
        snprintf(title, sizeof(title), "Upload %d marked save%s?", marked, marked == 1 ? "" : "s");
        pos = append_names(body, sizeof(body), 0, is_marked, marked, UI_HI);
        snprintf(body + pos, sizeof(body) - pos,
                 UI_DIM "The server keeps its current copies in History.");
    }
    if (!ui_confirm(tone, title, body, yes)) {
        set_status(UI_TONE_ACCENT, "Batch upload cancelled");
        return;
    }
    int ok_count = 0, fail_count = 0;
    op_title = "Uploading marked saves";
    memset(run_failures, 0, sizeof(run_failures));
    static BatchJob batch;   // a result per title: off the stack
    batch.marked = marked;
    char first[48];
    snprintf(first, sizeof(first), "Uploading 1/%d", marked);
    UiProgress p = { 0 };
    p.title = op_title;
    p.status = first;
    p.frac = 0;
    ui_run_job(batch_job, &batch, &p, false, NULL);
    for (int i = 0; i < title_count; i++) {
        if (!titles[i].marked) continue;
        SyncResult res = (SyncResult)batch.res[i];
        if (res == SYNC_OK) {
            ok_count++;
            titles[i].in_conflict = false;
            set_title_state(&titles[i], TSTATE_UPLOADED);
        } else {
            fail_count++;
            set_title_failed(&titles[i], sync_result_str(res));
            if (i < MAX_TITLES) run_failures[i] = (RunFailure){ (u8)res, true };
        }
    }
    clear_marks();
    last_details_idx = -1;
    if (fail_count > 0) {
        // Which ones, and why (the rest of the batch went through)
        char counts[32], ftitle[64];
        snprintf(counts, sizeof(counts), "%d uploaded.", ok_count);
        snprintf(ftitle, sizeof(ftitle), "%d upload%s failed", fail_count, fail_count == 1 ? "" : "s");
        run_failures_card(ftitle, fail_count, counts);
    }
    set_status(fail_count ? UI_TONE_WARN : UI_TONE_OK, "Batch upload: %d uploaded, %d failed", ok_count, fail_count);
}

// The compare on the worker: reads this console's save, hashes it and asks
// the server, all on copies (it may outlive a cancel)
typedef struct {
    AppConfig config;
    TitleInfo title;
    SaveDetails details;
} CompareJob;

static bool compare_job(void *ctx) {
    CompareJob *j = ctx;
    return sync_get_save_details(&j->config, &j->title, &j->details);
}

// Fetch the server's view of title `idx` into last_details. The spinner
// keeps turning while the server thinks; after a while the card offers
// Cancel, which leaves everything as it was.
static CompareResult compare_title(int idx, const char *what) {
    TitleInfo *t = &titles[idx];
    UiProgress pr = { 0 };
    pr.title = what;
    pr.name = t->name;
    pr.status = "Comparing with the server...";
    pr.frac = -1;

    bool ok, cancelled = false;
    CompareJob *j = ui_job_ctx(sizeof(CompareJob));
    if (j) {
        j->config = config;
        j->title = *t;
        ok = ui_run_job(compare_job, j, &pr, true, &cancelled);
        if (cancelled) return CMP_CANCELLED;
        if (ok) last_details = j->details;
        ui_job_release(j);
    } else {
        ui_progress(&pr);
        ok = sync_get_save_details(&config, t, &last_details);
    }

    synced_ms = 0;
    if (!ok) {
        last_details_idx = -1;
        return CMP_UNREADABLE;
    }
    last_details_idx = idx;
    switch (sync_decide(&last_details)) {
        case SYNC_ACTION_UPLOAD:   set_title_state(t, TSTATE_NEEDS_UPLOAD); break;
        case SYNC_ACTION_DOWNLOAD: set_title_state(t, TSTATE_NEEDS_DOWNLOAD); break;
        case SYNC_ACTION_CONFLICT: set_title_state(t, TSTATE_CONFLICT); break;
        // Couldn't ask the server: what we knew about this title still
        // stands (never TSTATE_FAILED: the save itself is fine)
        case SYNC_ACTION_SERVER_ERROR: break;
        default:                   set_title_state(t, TSTATE_SYNCED); break;
    }
    return CMP_OK;
}

// Compare, and when that can't be done say what happened, that nothing
// changed and what to do; A tries again. True once last_details holds a
// real answer from the server (never a "no server save" made up from a
// failed query).
static bool compare_ready(int idx, const char *what) {
    for (;;) {
        CompareResult cr = compare_title(idx, what);
        if (cr == CMP_CANCELLED) {
            set_status(UI_TONE_ACCENT, "Compare cancelled, nothing changed");
            return false;
        }
        if (cr == CMP_UNREADABLE) {
            if (ui_error_card("Couldn't compare", "Not enough memory to read this console's save.",
                              "Nothing was changed.", "Try again.", true))
                continue;
            set_status(UI_TONE_ERR, "Couldn't compare, nothing changed");
            return false;
        }
        if (!last_details.server_error) return true;
        char reason[160];
        if (last_details.server_status)
            snprintf(reason, sizeof(reason), "The server answered with HTTP %lu.",
                     (unsigned long)last_details.server_status);
        else
            snprintf(reason, sizeof(reason), "%s",
                     last_details.server_error_msg[0] ? last_details.server_error_msg : "The server didn't answer.");
        if (ui_error_card("Server unreachable", reason, "Nothing was changed.",
                          "Check WiFi, or Settings > Server URL.", true))
            continue;
        set_status(UI_TONE_ERR, "Server unreachable, nothing changed");
        return false;
    }
}

// After a successful transfer the copies match: if the top screen was
// showing this title's compare, update it to what was sent / received
// instead of falling back to "Press A". The server's timestamp of an
// upload isn't known until the next compare (and the console clock may be
// wrong), so the time shown is synced_ms.
_Static_assert(sizeof(((SaveDetails *)0)->server_hash) == sizeof(((SaveDetails *)0)->local_hash) &&
               sizeof(((SaveDetails *)0)->last_synced_hash) == sizeof(((SaveDetails *)0)->server_hash) &&
               sizeof(((SaveDetails *)0)->server_console_id) == sizeof(((AppConfig *)0)->console_id),
               "details_after_sync copies these fields whole");

static void details_after_sync(const TitleInfo *t, bool uploaded) {
    int idx = (int)(t - titles);
    if (idx != last_details_idx) {
        last_details_idx = -1;
        return;
    }
    SaveDetails *d = &last_details;
    if (uploaded) {
        d->server_exists = true;
        d->server_file_count = d->local_file_count;
        d->server_size = d->local_size;
        memcpy(d->server_hash, d->local_hash, sizeof(d->server_hash));
        memcpy(d->server_console_id, config.console_id, sizeof(d->server_console_id));
        d->server_last_sync[0] = '\0';
    } else {
        d->local_exists = true;
        d->local_file_count = d->server_file_count;
        d->local_size = d->server_size;
        memcpy(d->local_hash, d->server_hash, sizeof(d->local_hash));
    }
    d->server_error = false;
    d->server_status = 0;
    d->server_error_msg[0] = '\0';
    d->is_synced = true;
    d->has_last_synced = true;
    memcpy(d->last_synced_hash, d->server_hash, sizeof(d->last_synced_hash));
    synced_ms = gui_clock_ms();
    if (!synced_ms) synced_ms = 1;
}

static void upload_title(TitleInfo *t) {
    for (;;) {
        op_title = "Uploading";
        SyncResult res = run_xfer(XFER_UPLOAD, t, NULL, "Reading the save...");
        if (res == SYNC_OK) {
            set_status(UI_TONE_OK, "Uploaded: %.40s", t->name);
            t->in_conflict = false;
            set_title_state(t, TSTATE_UPLOADED);
            details_after_sync(t, true);
            return;
        }
        set_status(UI_TONE_ERR, "Upload failed: %s", sync_result_str(res));
        set_title_failed(t, sync_result_str(res));
        // The compare stays on the top screen for a retry (it succeeds
        // into "Matches the server"); closing the card drops it
        if (!transfer_failed_card(t, res, true, "upload")) {
            last_details_idx = -1;
            return;
        }
    }
}

static void download_title(TitleInfo *t) {
    for (;;) {
        op_title = "Downloading";
        SyncResult res = run_xfer(XFER_DOWNLOAD, t, NULL, "Asking the server...");
        if (res == SYNC_OK) {
            set_status(UI_TONE_OK, "Downloaded: %.40s", t->name);
            t->in_conflict = false;
            set_title_state(t, TSTATE_DOWNLOADED);
            details_after_sync(t, false);
            return;
        }
        set_status(UI_TONE_ERR, "Download failed: %s", sync_result_str(res));
        set_title_failed(t, sync_result_str(res));
        // The compare stays on the top screen for a retry (it succeeds
        // into "Matches the server"); closing the card drops it
        if (!transfer_failed_card(t, res, false, "download")) {
            last_details_idx = -1;
            return;
        }
    }
}

static void toggle_mark(int idx) {
    set_title_marked(&titles[idx], !titles[idx].marked);
    int mc = count_marked();
    if (mc > 0)
        set_status(UI_TONE_ACCENT, "%d title%s marked: A uploads them", mc, mc == 1 ? "" : "s");
    else
        set_status(UI_TONE_ACCENT, "Marks cleared");
}

// Y: what can be done with the selected save, as a list
static void saves_edit_search(void);

static void do_save_menu(void) {
    int idx = sel_title_idx();
    if (idx < 0) return;
    TitleInfo *t = &titles[idx];
    enum { M_COMPARE, M_UPLOAD, M_DOWNLOAD, M_HISTORY, M_MARK, M_UNMARK_ALL, M_SEARCH };
    const char *items[7];
    int ids[7], n = 0;
    char unmark_all[32], search_item[48];
    items[n] = "Compare with the server"; ids[n++] = M_COMPARE;
    items[n] = "Upload to the server"; ids[n++] = M_UPLOAD;
    items[n] = "Download from the server"; ids[n++] = M_DOWNLOAD;
    items[n] = "History: restore an older version"; ids[n++] = M_HISTORY;
    items[n] = t->marked ? "Unmark" : "Mark for batch upload"; ids[n++] = M_MARK;
    int marked = count_marked();
    if (marked > 0) {
        snprintf(unmark_all, sizeof(unmark_all), "Unmark all (%d)", marked);
        items[n] = unmark_all;
        ids[n++] = M_UNMARK_ALL;
    }
    // Last, so v055's items keep their places
    snprintf(search_item, sizeof(search_item), "Search saves by name%s", gui_ellipsis());
    items[n] = search_item; ids[n++] = M_SEARCH;
    // The menu grows out of the row it is about (where it is on screen now;
    // saves_do_y has just started bringing it into view)
    float row_y = SAVES_LIST_Y + (selected - ui_list_view(&saves_anim) + 0.5f) * SAVES_ROW_H;
    if (row_y < SAVES_LIST_Y) row_y = SAVES_LIST_Y;
    if (row_y > GUI_BAR_Y) row_y = GUI_BAR_Y;
    ui_set_modal_origin(row_y);
    int pick = ui_choose(UI_TONE_ACCENT, t->name, items, n);
    if (pick < 0) return;

    switch (ids[pick]) {
        case M_COMPARE:
            if (compare_ready(idx, "Save details")) ui_show_save_details(t, &last_details);
            break;
        case M_UPLOAD:
            if (!compare_ready(idx, "Upload")) break;
            if (!last_details.local_exists) {
                ui_message(UI_TONE_INFO, "Upload", "This console has no save for this game yet.");
            } else if (ui_confirm_sync(t, &last_details, true, config.local_backups)) {
                upload_title(t);
            } else {
                set_status(UI_TONE_ACCENT, "Upload cancelled");
            }
            break;
        case M_DOWNLOAD:
            if (!compare_ready(idx, "Download")) break;
            if (!last_details.server_exists) {
                ui_message(UI_TONE_INFO, "Download", "The server has no save for this game yet.");
            } else if (ui_confirm_sync(t, &last_details, false, config.local_backups)) {
                download_title(t);
            } else {
                set_status(UI_TONE_ACCENT, "Download cancelled");
            }
            break;
        case M_HISTORY:
            do_history();
            break;
        case M_MARK:
            toggle_mark(idx);
            break;
        case M_SEARCH:
            saves_edit_search();
            break;
        default:
            clear_marks();
            set_status(UI_TONE_ACCENT, "Marks cleared");
            break;
    }
}

static void do_smart_sync(void) {
    int idx = sel_title_idx();
    if (idx < 0) return;
    TitleInfo *t = &titles[idx];
    // A server we couldn't ask is explained there (with Try again); it
    // never becomes an upload or download
    if (!compare_ready(idx, "Smart Sync")) return;
    SyncAction suggested = sync_decide(&last_details);
    if (suggested == SYNC_ACTION_SERVER_ERROR) return;   // compare_ready() already refused it

    if (suggested == SYNC_ACTION_UP_TO_DATE) {
        // Nothing to decide: say so without a modal
        set_status(UI_TONE_OK, "Up to date: %.40s matches the server", t->name);
        return;
    }
    SyncAction chosen = ui_confirm_smart_sync(t, &last_details, suggested, config.local_backups);
    if (chosen == SYNC_ACTION_UPLOAD) {
        upload_title(t);
    } else if (chosen == SYNC_ACTION_DOWNLOAD) {
        download_title(t);
    } else {
        set_status(UI_TONE_ACCENT, "Sync cancelled");
    }
}

static bool sync_all_job(void *ctx) {
    return sync_all(&config, titles, title_count, sync_progress, (SyncSummary *)ctx);
}

static void do_sync_all(void) {
    static char title_errors[MAX_TITLES][TITLE_ERROR_LEN];
    static SyncSummary summary;   // ~1 KB: off the main thread's stack
    for (;;) {
        // Clear all conflict flags before sync
        for (int i = 0; i < title_count; i++)
            titles[i].in_conflict = false;

        op_title = "Sync all";
        memset(title_errors, 0, sizeof(title_errors));
        summary.title_error = title_errors;
        // On the worker; not cancellable (it uploads and downloads as it goes)
        UiProgress p = { 0 };
        p.title = op_title;
        p.status = "Comparing every save with the server...";
        p.frac = -1;
        bool ok = ui_run_job(sync_all_job, &summary, &p, false, NULL);
        last_details_idx = -1;
        if (ok) break;
        // sync_all() only gives up before it uploads or downloads anything
        if (!ui_error_card("Sync failed",
                           network_last_error()[0] ? network_last_error()
                                                   : "The server didn't accept the sync request.",
                           "Nothing was changed.", "Check WiFi, or Settings > Server URL.", true)) {
            set_status(UI_TONE_ERR, "Sync failed, nothing changed");
            return;
        }
    }
    memset(run_failures, 0, sizeof(run_failures));
    for (int i = 0; i < title_count && i < MAX_TITLES; i++) {
        set_title_state(&titles[i], summary.title_state[i]);
        if (summary.title_state[i] != TSTATE_FAILED) continue;
        set_title_failed(&titles[i], title_errors[i]);
        // A failure sync_all() didn't classify can't be called harmless
        SyncResult r = summary.title_result[i] != SYNC_OK ? (SyncResult)summary.title_result[i]
                                                           : SYNC_ERR_RESTORE_FAILED;
        run_failures[i] = (RunFailure){ (u8)r, summary.title_upload[i] };
    }

    // Mark conflicting titles in our list
    for (int i = 0; i < summary.conflicts && i < MAX_CONFLICT_DISPLAY; i++) {
        for (int j = 0; j < title_count; j++) {
            if (strcmp(titles[j].title_id_hex, summary.conflict_titles[i]) == 0) {
                titles[j].in_conflict = true;
                break;
            }
        }
    }

    if (summary.conflicts > 0) {
        // Auto-mark conflicting titles for batch resolve
        for (int i = 0; i < title_count; i++)
            if (is_conflict(&titles[i]))
                set_title_marked(&titles[i], true);

        // The instruction first, so it can never be cut off by a long list
        char body[640];
        int pos = snprintf(body, sizeof(body),
                           UI_HI "Marked for upload: A uploads them all (this console wins). "
                           "To choose per game, press B to unmark, then A on each one.\n");
        pos = append_names(body, sizeof(body), pos, is_conflict, summary.conflicts, "");
        snprintf(body + pos, sizeof(body) - pos,
                 UI_DIM "Up %d \xC2\xB7 Down %d \xC2\xB7 OK %d \xC2\xB7 Failed %d",
                 summary.uploaded, summary.downloaded, summary.up_to_date, summary.failed);
        char title[64];
        snprintf(title, sizeof(title), "%d conflict%s", summary.conflicts, summary.conflicts == 1 ? "" : "s");
        ui_message(UI_TONE_WARN, title, body);
    }
    if (summary.failed > 0) {
        // Which saves failed and why; the rest of the sync went through
        char counts[96], title[64];
        snprintf(counts, sizeof(counts), "%d uploaded \xC2\xB7 %d downloaded \xC2\xB7 %d up to date",
                 summary.uploaded, summary.downloaded, summary.up_to_date);
        snprintf(title, sizeof(title), "Sync all: %d failed", summary.failed);
        run_failures_card(title, summary.failed, counts);
    }

    if (summary.conflicts > 0 || summary.failed > 0) {
        set_status(UI_TONE_WARN, "Synced: %d uploaded, %d downloaded, %d up to date, %d conflict%s, %d failed",
                   summary.uploaded, summary.downloaded, summary.up_to_date, summary.conflicts,
                   summary.conflicts == 1 ? "" : "s", summary.failed);
    } else if (summary.uploaded + summary.downloaded + summary.up_to_date == 0) {
        set_status(UI_TONE_OK, "Synced: nothing on this console to sync");
    } else {
        set_status(UI_TONE_OK, "Synced: %d uploaded, %d downloaded, %d already up to date",
                   summary.uploaded, summary.downloaded, summary.up_to_date);
    }
}

// The update check on the worker
typedef struct {
    AppConfig config;
    UpdateInfo info;
} UpdateJob;

static bool update_job(void *ctx) {
    UpdateJob *j = ctx;
    return update_check(&j->config, &j->info);
}

// Settings > Check for updates. Returns true if the app should exit
// (update installed, not relaunched).
static bool do_update(void) {
    // update_check=0: never offer the release the server publishes (it would
    // replace this build with a different one)
    if (!config.update_check) {
        ui_message(UI_TONE_INFO, "Updates disabled",
                   "Update checks are turned off.\n\n"
                   UI_DIM "Turn on Settings > Allow update checks to use this.");
        return false;
    }

    UpdateJob *j = ui_job_ctx(sizeof(UpdateJob));
    UpdateInfo update_info;
    bool checked, cancelled = false;
    if (j) {
        j->config = config;
        UiProgress p = { 0 };
        p.title = "Update";
        p.status = "Checking for updates...";
        p.frac = -1;
        checked = ui_run_job(update_job, j, &p, true, &cancelled);
        if (cancelled) return false;   // nothing changed
        update_info = j->info;
        ui_job_release(j);
    } else {
        ui_busy("Update", "Checking for updates...");
        checked = update_check(&config, &update_info);
    }
    if (!checked) {
        ui_message(UI_TONE_ERR, "Update", "Update check failed.\n\n" UI_DIM "Check WiFi and the server URL.");
        return false;
    }
    if (!update_info.available) {
        char body[96];
        snprintf(body, sizeof(body), "You have the latest version (%s).", APP_VERSION);
        ui_message(UI_TONE_OK, "Up to date", body);
        return false;
    }

    char body[256];
    snprintf(body, sizeof(body),
             "Current  %s\n" UI_HI "Latest    %s\n" UI_DIM "Size       %lu KB\n"
             UI_DIM "SHA-256  %s\n\n"
             "Download and install it now?",
             APP_VERSION, update_info.latest_version, (unsigned long)(update_info.file_size / 1024),
             update_info.sha256[0] ? "will be verified" : "not published (size check only)");
    if (!ui_confirm(UI_TONE_ACCENT, "Update available", body, "Install"))
        return false;

    update_phase = "Downloading update";
    update_progress_cb(0);
    char download_error[128] = {0};
    if (!update_download(&config, &update_info, update_progress_cb,
                         download_error, sizeof(download_error))) {
        char msg[192];
        snprintf(msg, sizeof(msg), "Update download failed.\n\n" UI_DIM "%s",
                 download_error[0] ? download_error : "Unknown error");
        ui_message(UI_TONE_ERR, "Update", msg);
        return false;
    }
    update_phase = "Installing update";
    update_progress_cb(0);
    char install_error[128] = {0};
    if (!update_install(update_progress_cb, install_error, sizeof(install_error))) {
        ui_message(UI_TONE_ERR, "Install failed", install_error);
        return false;
    }

    UiProgress pr = { 0 };
    pr.title = "Update installed";
    pr.tone = UI_TONE_OK;
    pr.name = "GameSync " APP_VERSION " -> new version";
    pr.status = "Restarting the application...";
    pr.frac = 1;
    pr.right = "100%";
    ui_progress(&pr);
    svcSleepThread(1500000000LL);
    update_relaunch();

    wait_for_start(UI_TONE_OK, "Update installed",
                   "Please restart the application to use the new version.");
    return true;
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

static bool saves_need_rescan = false;

// What each Saves key does; a tap on its button calls the same function

// A: Smart Sync the selected save, or upload every marked one
static void saves_do_a(void) {
    if (filtered_count <= 0) return;
    ui_list_reveal(&saves_anim, filtered_count, selected, &scroll_offset);
    int marked = count_marked();
    if (marked > 0)
        do_batch_upload(marked);
    else
        do_smart_sync();
}

// X: Sync All
static void saves_do_x(void) {
    if (title_count <= 0) return;
    int keep = selected, keep_scroll = scroll_offset;
    do_sync_all();
    // The list itself didn't change: stay where we were
    if (keep < filtered_count) {
        selected = keep;
        scroll_offset = keep_scroll;
    }
}

// Y: details and actions for the selected save
static void saves_do_y(void) {
    if (filtered_count <= 0) return;
    ui_list_reveal(&saves_anim, filtered_count, selected, &scroll_offset);
    do_save_menu();
}

// "Search: zel · 3 of 41"
static void search_status(void) {
    if (search[0])
        set_status(UI_TONE_ACCENT, "Search: %.40s \xC2\xB7 %d of %d", search, filtered_count, view_total);
    else
        set_status(UI_TONE_ACCENT, "Showing all %d save%s", filtered_count, filtered_count == 1 ? "" : "s");
}

// ZR, the header's search chip, or Y > Search: edit the search (pre-filled)
static void saves_edit_search(void) {
    char text[sizeof(search)];
    snprintf(text, sizeof(text), "%s", search);
    UiTextField f = { "Search saves", "Search your saves (empty = all)", "Search", false, false, true };
    if (!ui_edit_text_ex(&f, text, sizeof(text))) return;
    // Spaces around the query would only make it miss
    char *p = text;
    while (*p == ' ') p++;
    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == ' ') p[--len] = '\0';
    snprintf(search, sizeof(search), "%s", p);
    rebuild_filter();
    search_status();
}

// The search chip's x, B (with nothing marked) or the empty result's button
static void saves_clear_search(void) {
    if (!search[0]) return;
    search[0] = '\0';
    rebuild_filter();
    search_status();
}

// B: cancel the batch selection; with nothing marked, clear the search
static void saves_do_b(void) {
    if (count_marked() > 0) {
        clear_marks();
        set_status(UI_TONE_ACCENT, "Marks cleared");
    } else {
        saves_clear_search();
    }
}

// SELECT (next view) or a tap on a segment: show All / 3DS / NDS
static void saves_set_view(int mode) {
    view_mode = mode;
    rebuild_filter();
    const char *names[] = {"All", "3DS", "NDS"};
    if (search[0])
        set_status(UI_TONE_ACCENT, "View: %s \xC2\xB7 search %.30s: %d of %d", names[view_mode], search,
                   filtered_count, view_total);
    else
        set_status(UI_TONE_ACCENT, "View: %s (%d title%s)", names[view_mode], filtered_count,
                   filtered_count == 1 ? "" : "s");
}

// Saves tab: runs until the user switches tab or exits
static UiNav saves_tab(void) {
    ui_set_backdrop(saves_top, saves_bottom, NULL);
    if (saves_need_rescan) {
        saves_need_rescan = false;
        scan_titles();
        set_status(UI_TONE_OK, "Rescanned. %d title%s found.", title_count, title_count == 1 ? "" : "s");
    }

    hidScanInput();
    UiInput in;
    while (ui_input_scan(&in)) {
        u32 kDown = in.down;

        UiNav nav = ui_tab_nav(kDown);
        if (nav != UI_NAV_STAY) return nav;

        // The list: taps, drags and flicks; UP / DOWN one row (wrapping),
        // LEFT / RIGHT a page (clamped)
        UiListGeom geom = SAVES_GEOM;
        UiListEvent ev = ui_list_input(&saves_anim, &geom, filtered_count, &selected, &scroll_offset, &in,
                                       in.rep & UI_LIST_KEYS);
        if (ev.kind != UI_LIST_REATTACHED && (in.rep & UI_LIST_KEYS) && filtered_count > 0) {
            ui_list_keys(&saves_anim, in.rep, filtered_count, LIST_VISIBLE, &selected);
            update_scroll();
        }
        // A tap selects the row; on its mark box it also toggles the mark
        // (on release, so a drag never toggles anything). A tap on the
        // selected row does nothing more: Smart Sync talks to the server,
        // and the A button is its touch path.
        if ((ev.kind == UI_LIST_TAP || ev.kind == UI_LIST_TAP_SELECTED) && ev.px < SAVES_MARK_W)
            toggle_mark(filtered[ev.index]);

        refresh_view();
        GuiRect chip = ui_saves_marked_chip(&view);
        int pressed_chip = -1;
        bool chip_hit = ui_tap_buttons(&saves_chip_tap, &in, &chip, 1, &pressed_chip) == 0;
        saves_chip_pressed = pressed_chip == 0;
        GuiRect seg[3];
        int nseg = ui_saves_segments(seg), seg_pressed = -1;
        int seg_hit = ui_tap_buttons(&saves_seg_tap, &in, seg, nseg, &seg_pressed);
        saves_seg_held = seg_pressed + 1;
        GuiRect srch[2];
        int nsrch = ui_saves_search_targets(&view, srch), srch_pressed = -1;
        int srch_hit = ui_tap_buttons(&saves_search_tap, &in, srch, nsrch, &srch_pressed);
        saves_search_held = srch_pressed + 1;
        GuiRect edraw[2], etouch[2];
        int nempty = ui_saves_empty_buttons(&view, edraw, etouch), empty_pressed = -1;
        int empty_hit = ui_tap_buttons(&saves_empty_tap, &in, etouch, nempty, &empty_pressed);
        saves_empty_held = empty_pressed + 1;
        UiBarItem items[SAVES_BAR_COUNT];
        int nitems = ui_saves_bar(&view, items);
        int hit = ui_bar_input(&saves_bar, &in, items, nitems);

        // SELECT cycles All -> 3DS -> NDS; a tap on a segment picks it
        if (kDown & KEY_SELECT) saves_set_view((view_mode + 1) % 3);
        else if (seg_hit >= 0 && seg_hit != view_mode) saves_set_view(seg_hit);
        if (hit == SAVES_BAR_Y || (kDown & KEY_Y)) saves_do_y();
        if (hit == SAVES_BAR_A || (kDown & KEY_A)) saves_do_a();
        if (hit == SAVES_BAR_X || (kDown & KEY_X)) saves_do_x();
        if (chip_hit) {
            clear_marks();
            set_status(UI_TONE_ACCENT, "Marks cleared");
        } else if (kDown & KEY_B) {
            saves_do_b();
        }
        // Search: ZR (New 3DS), the header chip (its x clears), or the
        // buttons under an empty result
        if ((kDown & KEY_ZR) || srch_hit == 0 || empty_hit == 1) saves_edit_search();
        else if (srch_hit == 1 || empty_hit == 0) saves_clear_search();

        saves_held = in.held;
        gui_begin(true);
        gui_screen(GUI_TOP);
        saves_top(NULL);
        gui_screen(GUI_BOTTOM);
        saves_bottom(NULL);
        gui_end();
        saves_held = 0;
        saves_chip_pressed = false;
        saves_seg_held = 0;
        saves_search_held = 0;
        saves_empty_held = 0;
    }
    return UI_NAV_EXIT;
}

// Settings entries that run something (over the settings screen)
static bool settings_action(SettingsResult action) {
    switch (action) {
        case SETTINGS_RESCAN: {
            scan_titles();
            saves_need_rescan = false;
            char body[64];
            snprintf(body, sizeof(body), "%d title%s with save data found.", title_count,
                     title_count == 1 ? "" : "s");
            set_status(UI_TONE_OK, "Rescanned. %s", body);
            ui_message(UI_TONE_OK, "Rescan titles", body);
            return false;
        }
        case SETTINGS_REFRESH_CATALOG: {
            char report[512];
            catalog_refresh(&config, report, sizeof(report));
            ui_message(UI_TONE_ACCENT, "Refresh catalog", report);
            return false;
        }
        case SETTINGS_UPDATE:
            return do_update();
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// First-run setup
// ---------------------------------------------------------------------------

// Probe the server and the key just typed. Returns true to go on (it
// answered and took the key, or "Continue anyway"), false to edit again.
static bool setup_check(AppConfig *cfg, bool net_ok) {
    char title[64], body[MAX_URL_LEN + 160];
    if (net_ok) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Checking the server%s", gui_ellipsis());
        ui_busy("Set up", msg);
        bool answered = network_select_server(cfg);
        u32 key = answered ? titles_probe_key(cfg) : 0;
        if (answered && key != 401) return true;
        if (answered) {
            snprintf(title, sizeof(title), "Key rejected");
            snprintf(body, sizeof(body), "The server at %s refused this API key (401).\n"
                     UI_DIM "It is saved anyway; you can fix it later in Settings.", config_active_url(cfg));
        } else {
            snprintf(title, sizeof(title), "No answer from the server");
            snprintf(body, sizeof(body), "%s\n" UI_DIM "%s\n" UI_DIM "It is saved anyway; check WiFi and the address.",
                     config_active_url(cfg), network_last_error()[0] ? network_last_error() : "No reply");
        }
    } else {
        snprintf(title, sizeof(title), "Can't check the server");
        snprintf(body, sizeof(body), "The network service didn't start, so the address wasn't tried.\n"
                 UI_DIM "It is saved anyway.");
    }
    UiButton b[2] = { { KEY_B, "B", "Edit" }, { KEY_A, "A", "Continue anyway" } };
    return (ui_dialog(UI_TONE_WARN, title, body, b, 2) & KEY_A) != 0;
}

// No usable config.txt: ask for the server address and the API key on the
// console, write the file and load it. True once config_load succeeds;
// false to go back to the config error (B in the first field).
static bool first_run_setup(AppConfig *cfg, bool net_ok) {
    for (;;) {
        UiTextField url = { "Server URL", "http://192.168.1.100:8000", "Next", false, true, false };
        if (!ui_edit_text_ex(&url, cfg->server_url, MAX_URL_LEN)) return false;
        UiTextField key = { "API key", "The server's SYNC_API_KEY", "Save", true, false, false };
        if (!ui_edit_text_ex(&key, cfg->api_key, MAX_API_KEY_LEN)) continue;   // back to the address

        // config_save creates sdmc:/3ds/3dssync/ when it is missing
        if (!config_save(cfg)) {
            if (ui_error_card("Couldn't save the settings", "The SD card didn't accept " CONFIG_PATH ".",
                              "Nothing was written.", "Check that the SD card isn't locked or full.", true))
                continue;
            return false;
        }
        if (!setup_check(cfg, net_ok)) continue;

        char error[512];
        AppConfig loaded;
        if (config_load(&loaded, error, sizeof(error))) {
            *cfg = loaded;
            return true;
        }
        ui_message(UI_TONE_ERR, "Config error", error);
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    // Initialize services
    ui_init();
    amInit();
    fsInit();
    psInit();  // For random number generation (console ID)
    card_spi_init();  // For NDS cartridge SPI save access

    ui_busy("Starting", "Loading config...");

    // The network service first: first-run setup checks the server with it
    bool net_ok = network_init();

    char config_error[512];
    bool config_ok = config_load(&config, config_error, sizeof(config_error));
    while (!config_ok) {
        // No usable config.txt: set it up here (A), or exit (START)
        char msg[768];
        snprintf(msg, sizeof(msg),
            "%s\n\n"
            "Set up the server address and API key here (A), or write\n"
            UI_HI "%s\n"
            UI_DIM "on a PC (server_url=..., api_key=...).",
            config_error, CONFIG_PATH);
        static const UiButton b[] = { { KEY_START, "START", "Exit" }, { KEY_A, "A", "Set up now" } };
        if (!(ui_dialog(UI_TONE_ERR, "Config error", msg, b, 2) & KEY_A)) break;
        config_ok = first_run_setup(&config, net_ok);
    }
    if (!config_ok) {
        if (net_ok) network_exit();
        card_spi_exit();
        psExit();
        fsExit();
        amExit();
        ui_exit();
        return 0;
    }
    gui_set_reduce_motion(config.reduce_motion);
    ui_set_local_console_id(config.console_id);
    ui_set_utc_offset(config.utc_offset_minutes);

    if (!net_ok) {
        wait_for_start(UI_TONE_ERR, "Network unavailable",
                       "Failed to start the network service.\n\nMake sure WiFi is enabled.");
        card_spi_exit();
        psExit();
        fsExit();
        amExit();
        ui_exit();
        return 0;
    }

    // With a second address configured, find out which one answers before
    // the first request (the title scan fetches game names from the server)
    if (config.server_url_alt[0]) {
        ui_busy("Starting", "Looking for the server...");
        network_select_server(&config);
    }

    // Initial title scan
    scan_titles();
    set_status(UI_TONE_ACCENT, "Server: %.200s", config_active_url(&config));
    ui_set_backdrop(saves_top, saves_bottom, NULL);
    hidSetRepeatParameters(20, 4);

    // Top-level tabs: L / R cycle Saves -> Catalog -> Settings, wrapping
    int tab = UI_TAB_SAVES;
    for (;;) {
        UiNav nav;
        if (tab == UI_TAB_SAVES) {
            nav = saves_tab();
        } else if (tab == UI_TAB_CATALOG) {
            bool saves_changed = false;
            nav = catalog_tab(&config, &saves_changed);
            if (saves_changed) saves_need_rescan = true;
        } else {
            AppConfig before = config;
            SettingsResult r = ui_settings_tab(&config, settings_action);
            if (strcmp(config_active_url(&before), config_active_url(&config)) != 0 ||
                strcmp(before.api_key, config.api_key) != 0)
                catalog_reset();  // another server: load its catalog on the next visit
            if (strcmp(before.nds_dir, config.nds_dir) != 0) {
                saves_need_rescan = true;
                catalog_reset();  // the installed DS games live elsewhere now
            }
            nav = r == SETTINGS_NAV_PREV ? UI_NAV_PREV : r == SETTINGS_NAV_NEXT ? UI_NAV_NEXT : UI_NAV_EXIT;
        }
        if (nav == UI_NAV_EXIT) break;
        tab = (tab + (nav == UI_NAV_NEXT ? 1 : UI_TAB_COUNT - 1)) % UI_TAB_COUNT;
        // The new tab's content comes in from the side of the button pressed
        // (wrapping included: R from Settings enters Saves from the right)
        ui_tab_transition(nav == UI_NAV_NEXT ? +16 : -16);
    }

    // Cleanup: no worker may outlive the network service
    ui_job_settle();
    catalog_exit();
    network_exit();
    card_spi_exit();
    psExit();
    fsExit();
    amExit();
    ui_exit();
    return 0;
}
