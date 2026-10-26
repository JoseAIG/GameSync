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

// View filtering: filtered[] maps visible indices -> titles[] indices
static int view_mode = VIEW_ALL;
static int filtered[MAX_TITLES];
static int filtered_count = 0;

// Last compare with the server (A), shown on the top screen for that title
static SaveDetails last_details;
static int last_details_idx = -1;

static SavesView view;
static UiListAnim saves_anim;   // the save list's presentation state
static UiBar saves_bar = UI_BAR_INIT;
static UiTap saves_chip_tap = UI_TAP_INIT;
static bool saves_chip_pressed;
static u32 saves_held;          // keys held, while the Saves loop itself draws

#define LIST_VISIBLE SAVES_ROWS

static void set_status(UiTone tone, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_status(UiTone tone, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status, sizeof(status), fmt, ap);
    va_end(ap);
    status_tone = tone;
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

// Rebuild the filtered index list based on current view_mode
static void rebuild_filter(void) {
    filtered_count = 0;
    for (int i = 0; i < title_count; i++) {
        bool include = false;
        switch (view_mode) {
            case VIEW_3DS: include = !titles[i].is_nds; break;
            case VIEW_NDS: include = titles[i].is_nds; break;
            default:       include = true; break;
        }
        if (include)
            filtered[filtered_count++] = i;
    }
    // Reset selection (and whatever the list was doing with the old rows)
    selected = 0;
    scroll_offset = 0;
    ui_list_reset(&saves_anim);
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
        titles[i].marked = false;
}

static void scan_titles(void) {
    ui_busy("Loading", "Scanning titles...");
    title_count = titles_scan(titles, MAX_TITLES, config.nds_dir);

    // Fetch game names from server
    if (title_count > 0) {
        ui_busy("Loading", "Fetching game names...");
        titles_fetch_names(&config, titles, title_count);
        qsort(titles, title_count, sizeof(TitleInfo), title_compare);
    }

    last_details_idx = -1;
    rebuild_filter();
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
    int idx = sel_title_idx();
    view.details = (idx >= 0 && idx == last_details_idx) ? &last_details : NULL;
    view.title_count = title_count;
    view.bar = &saves_bar;
    view.held = saves_held;
    view.chip_pressed = saves_chip_pressed;
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

static bool compare_title(int idx, const char *what);

static void do_history(void) {
    int idx = sel_title_idx();
    if (idx < 0) return;
    ui_busy("History", "Loading saved versions...");

    HistoryVersion versions[MAX_HISTORY_VERSIONS];
    int count = sync_get_history(&config, titles[idx].title_id_hex, versions, MAX_HISTORY_VERSIONS);

    if (count < 0) {
        set_status(UI_TONE_ERR, "Failed to load history");
    } else if (count == 0) {
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
        if (vi < 0 || !compare_title(idx, "Restore")) {
            ui_message(UI_TONE_ERR, "Restore", "Couldn't read this console's save.\n" UI_DIM "Nothing was changed.");
            free(selected_ts);
            return;
        }
        bool had_save = last_details.local_exists && last_details.local_file_count > 0;
        if (!ui_confirm_restore(&titles[idx], &last_details, &versions[vi], vi, count, config.local_backups)) {
            set_status(UI_TONE_ACCENT, "Restore cancelled");
            free(selected_ts);
            return;
        }
        op_title = "Restoring";
        sync_progress("Downloading history version...");
        SyncResult res = sync_download_history(&config, &titles[idx], selected_ts, sync_progress);
        if (res == SYNC_OK) {
            set_status(UI_TONE_OK, "Restored %.40s%s", titles[idx].name,
                       had_save && config.local_backups > 0 ? " \xC2\xB7 previous save backed up to SD" : "");
            titles[idx].in_conflict = false;
            titles[idx].sync_state = TSTATE_DOWNLOADED;
        } else {
            set_status(UI_TONE_ERR, "Restore failed: %s", sync_result_str(res));
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
    for (int i = 0; i < title_count; i++) {
        if (!titles[i].marked) continue;
        char msg[160];
        snprintf(msg, sizeof(msg), "Uploading %d/%d: %s", ok_count + fail_count + 1, marked,
                 titles[i].title_id_hex);
        sync_progress(msg);
        SyncResult res = sync_title(&config, &titles[i], NULL);
        if (res == SYNC_OK) {
            ok_count++;
            titles[i].in_conflict = false;
            titles[i].sync_state = TSTATE_UPLOADED;
        } else {
            fail_count++;
            titles[i].sync_state = TSTATE_FAILED;
        }
    }
    clear_marks();
    last_details_idx = -1;
    set_status(fail_count ? UI_TONE_WARN : UI_TONE_OK, "Batch upload: %d OK, %d failed", ok_count, fail_count);
}

// Fetch the server's view of title `idx` into last_details; false on failure
static bool compare_title(int idx, const char *what) {
    TitleInfo *t = &titles[idx];
    UiProgress pr = { 0 };
    pr.title = what;
    pr.name = t->name;
    pr.status = "Comparing with the server...";
    pr.frac = -1;
    ui_progress(&pr);

    if (!sync_get_save_details(&config, t, &last_details)) {
        last_details_idx = -1;
        set_status(UI_TONE_ERR, "Failed to load save details");
        return false;
    }
    last_details_idx = idx;
    switch (sync_decide(&last_details)) {
        case SYNC_ACTION_UPLOAD:   t->sync_state = TSTATE_NEEDS_UPLOAD; break;
        case SYNC_ACTION_DOWNLOAD: t->sync_state = TSTATE_NEEDS_DOWNLOAD; break;
        case SYNC_ACTION_CONFLICT: t->sync_state = TSTATE_CONFLICT; break;
        // Couldn't ask the server: what we knew about this title still stands
        case SYNC_ACTION_SERVER_ERROR: break;
        default:                   t->sync_state = TSTATE_SYNCED; break;
    }
    return true;
}

// The server couldn't be queried (no answer, or an HTTP error): say so
// instead of offering an upload or download based on "no server save"
static void server_query_failed(const char *what) {
    char body[256];
    if (last_details.server_status)
        snprintf(body, sizeof(body),
                 "The server answered with HTTP %lu.\n\n" UI_DIM "Nothing was changed. Check the server and retry.",
                 (unsigned long)last_details.server_status);
    else
        snprintf(body, sizeof(body),
                 "The server didn't answer.\n" UI_DIM "%s\n\n" UI_DIM "Nothing was changed. Check Wi-Fi and the server, then retry.",
                 last_details.server_error_msg[0] ? last_details.server_error_msg : "No response");
    ui_message(UI_TONE_ERR, what, body);
    set_status(UI_TONE_ERR, "Server unreachable, nothing changed");
}

static void upload_title(TitleInfo *t) {
    op_title = "Uploading";
    SyncResult res = sync_title(&config, t, sync_progress);
    if (res == SYNC_OK) {
        set_status(UI_TONE_OK, "Uploaded: %.40s", t->name);
        t->in_conflict = false;
        t->sync_state = TSTATE_UPLOADED;
    } else {
        set_status(UI_TONE_ERR, "Upload failed: %s", sync_result_str(res));
        t->sync_state = TSTATE_FAILED;
    }
    last_details_idx = -1;
}

static void download_title(TitleInfo *t) {
    op_title = "Downloading";
    SyncResult res = sync_download_title(&config, t, sync_progress);
    if (res == SYNC_OK) {
        set_status(UI_TONE_OK, "Downloaded: %.40s", t->name);
        t->in_conflict = false;
        t->sync_state = TSTATE_DOWNLOADED;
    } else {
        set_status(UI_TONE_ERR, "Download failed: %s", sync_result_str(res));
        t->sync_state = TSTATE_FAILED;
    }
    last_details_idx = -1;
}

static void toggle_mark(int idx) {
    titles[idx].marked = !titles[idx].marked;
    int mc = count_marked();
    if (mc > 0)
        set_status(UI_TONE_ACCENT, "%d title%s marked: A uploads them", mc, mc == 1 ? "" : "s");
    else
        set_status(UI_TONE_ACCENT, "Marks cleared");
}

// Y: what can be done with the selected save, as a list
static void do_save_menu(void) {
    int idx = sel_title_idx();
    if (idx < 0) return;
    TitleInfo *t = &titles[idx];
    enum { M_COMPARE, M_UPLOAD, M_DOWNLOAD, M_HISTORY, M_MARK, M_UNMARK_ALL };
    const char *items[6];
    int ids[6], n = 0;
    char unmark_all[32];
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
    int pick = ui_choose(UI_TONE_ACCENT, t->name, items, n);
    if (pick < 0) return;

    switch (ids[pick]) {
        case M_COMPARE:
            if (compare_title(idx, "Save details")) ui_show_save_details(t, &last_details);
            break;
        case M_UPLOAD:
            if (!compare_title(idx, "Upload")) break;
            if (last_details.server_error) {
                server_query_failed("Upload");
            } else if (!last_details.local_exists) {
                ui_message(UI_TONE_INFO, "Upload", "This console has no save for this game yet.");
            } else if (ui_confirm_sync(t, &last_details, true)) {
                upload_title(t);
            } else {
                set_status(UI_TONE_ACCENT, "Upload cancelled");
            }
            break;
        case M_DOWNLOAD:
            if (!compare_title(idx, "Download")) break;
            if (last_details.server_error) {
                server_query_failed("Download");
            } else if (!last_details.server_exists) {
                ui_message(UI_TONE_INFO, "Download", "The server has no save for this game yet.");
            } else if (ui_confirm_sync(t, &last_details, false)) {
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
    if (!compare_title(idx, "Smart Sync")) return;
    SyncAction suggested = sync_decide(&last_details);

    SyncAction chosen = ui_confirm_smart_sync(t, &last_details, suggested);
    if (suggested == SYNC_ACTION_SERVER_ERROR) {
        // Never upload or download on a server state we couldn't read
        set_status(UI_TONE_ERR, "Server unreachable, nothing changed");
    } else if (chosen == SYNC_ACTION_UPLOAD) {
        upload_title(t);
    } else if (chosen == SYNC_ACTION_DOWNLOAD) {
        download_title(t);
    } else if (suggested == SYNC_ACTION_UP_TO_DATE) {
        set_status(UI_TONE_OK, "Up to date: %.40s", t->name);
    } else {
        set_status(UI_TONE_ACCENT, "Sync cancelled");
    }
}

static void do_sync_all(void) {
    // Clear all conflict flags before sync
    for (int i = 0; i < title_count; i++)
        titles[i].in_conflict = false;

    op_title = "Sync all";
    SyncSummary summary;
    bool ok = sync_all(&config, titles, title_count, sync_progress, &summary);
    last_details_idx = -1;
    if (!ok) {
        if (network_last_error()[0])
            set_status(UI_TONE_ERR, "Sync failed: %.80s", network_last_error());
        else
            set_status(UI_TONE_ERR, "Sync failed! Check the server.");
        return;
    }
    for (int i = 0; i < title_count && i < MAX_TITLES; i++)
        titles[i].sync_state = summary.title_state[i];

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
                titles[i].marked = true;

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

        set_status(UI_TONE_WARN, "Up %d  Down %d  OK %d  Conflict %d  Failed %d",
                   summary.uploaded, summary.downloaded, summary.up_to_date,
                   summary.conflicts, summary.failed);
    } else {
        set_status(summary.failed ? UI_TONE_WARN : UI_TONE_OK, "Up %d  Down %d  OK %d  Failed %d",
                   summary.uploaded, summary.downloaded, summary.up_to_date, summary.failed);
    }
}

// Settings > Check for updates. Returns true if the app should exit
// (update installed, not relaunched).
static bool do_update(void) {
    // update_check=0: never offer the release the server publishes (it would
    // replace this build with a different one)
    if (!config.update_check) {
        ui_message(UI_TONE_INFO, "Updates disabled",
                   "Update checks are turned off.\n\n"
                   UI_DIM "Set update_check=1 in config.txt to enable them.");
        return false;
    }

    ui_busy("Update", "Checking for updates...");

    UpdateInfo update_info;
    if (!update_check(&config, &update_info)) {
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

// B: cancel the batch selection
static void saves_do_b(void) {
    if (count_marked() <= 0) return;
    clear_marks();
    set_status(UI_TONE_ACCENT, "Marks cleared");
}

// SELECT (next view) or a tap on a segment: show All / 3DS / NDS
static void saves_set_view(int mode) {
    view_mode = mode;
    rebuild_filter();
    const char *names[] = {"All", "3DS", "NDS"};
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
        UiBarItem items[SAVES_BAR_COUNT];
        int nitems = ui_saves_bar(&view, items);
        int hit = ui_bar_input(&saves_bar, &in, items, nitems);

        if (kDown & KEY_SELECT) saves_set_view((view_mode + 1) % 3);
        if (hit == SAVES_BAR_Y || (kDown & KEY_Y)) saves_do_y();
        if (hit == SAVES_BAR_A || (kDown & KEY_A)) saves_do_a();
        if (hit == SAVES_BAR_X || (kDown & KEY_X)) saves_do_x();
        if (chip_hit || (kDown & KEY_B)) saves_do_b();

        saves_held = in.held;
        gui_begin(true);
        gui_screen(GUI_TOP);
        saves_top(NULL);
        gui_screen(GUI_BOTTOM);
        saves_bottom(NULL);
        gui_end();
        saves_held = 0;
        saves_chip_pressed = false;
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

    char config_error[512];
    if (!config_load(&config, config_error, sizeof(config_error))) {
        char msg[768];
        snprintf(msg, sizeof(msg),
            "%s\n\n"
            UI_DIM "Expected file at:\n"
            UI_HI "%s\n\n"
            UI_DIM "With contents:\n"
            UI_HI "server_url=http://<pc-ip>:8000\n"
            UI_HI "api_key=<your-key>",
            config_error, CONFIG_PATH);
        wait_for_start(UI_TONE_ERR, "Config error", msg);
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

    // Initialize network
    if (!network_init()) {
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
    }

    // Cleanup
    catalog_exit();
    network_exit();
    card_spi_exit();
    psExit();
    fsExit();
    amExit();
    ui_exit();
    return 0;
}
