// Game catalog tab for the 3DS client. The browsing UX (paging window, RA
// badges and filter, search, progress with speed/ETA, hold-B cancel) is
// ported from the DS client's ds/source/catalog.c.
//
// The list is kept on the SD card between runs (sdmc:/3ds/3dssync/cache/),
// one file per system with the server's fingerprint for it, the same
// strategy as the MiSTer client (mister/gamesync/catalogcache.py): on open
// only the systems whose fingerprint moved are fetched again, a server that
// can't be reached leaves the cached copy usable, and the search and the
// RetroAchievements filter run locally over the cached rows. A server
// without GET /roms/fingerprints (404) is browsed live like before: paged
// from the server, which also does the search and the RA filter.
//
// 3DS games install as a CIA streamed straight into AM (the same path the
// self-updater uses), chunk by chunk from the HTTP body, so a ROM is never
// held in RAM or staged on the SD. The server converts .3ds/.cci carts with
// ?extract=cia (operator-configured, see server/README.md); a .cia on the
// server is installed as-is. DS/DSi games are written to the SD as .nds.
//
// Layout: the list is on the bottom screen, the selected game's details on
// the top screen; dialogs and install progress are cards over the list.
#include "catalog.h"
#include "catalog_data.h"
#include "network.h"
#include "ui.h"
#include <stdarg.h>
#include <strings.h>
#include <sys/stat.h>

#define CAT_WINDOW 200       // entries held around the visible rows
#define CAT_ROWS GUI_LIST_ROWS   // list rows on the bottom screen
#define CAT_ROW_H GUI_ROW_H
#define CAT_LIST_Y GUI_LIST_Y
#define CAT_SCAN_DEPTH 4     // ROM folder levels searched for installed games
#define CAT_FETCH_PAGE 500   // rows per request when filling the cache
#define CAT_FETCH_MAX_PAGES 200
#define CAT_MAX_SYSTEMS 3

#define CIA_MAP_PATH "sdmc:/3ds/3dssync/catalog_cia.txt"
#define CACHE_DIR "sdmc:/3ds/3dssync/cache"
#define ROMS_3DS_DIR "sdmc:/roms/3ds"
#define ROMS_DSI_DIR "sdmc:/roms/dsi"
#define ROMS_NDS_DIR "sdmc:/roms/nds"

#define WAIT_CONVERT_S (45 * 60)   // a CIA conversion of a big cart is slow
#define WAIT_PLAIN_S 60
#define RESCAN_TIMEOUT_S 300       // GET /roms/scan answers when the scan is done

static const char *const cat_systems[] = { "3DS", "NDS", "DSI", NULL };
static const UiListGeom cat_geom = { 0, CAT_LIST_Y, GUI_BOT_W, CAT_ROWS, CAT_ROW_H, false, true };

// Action bar items, in this order
enum { CAT_BAR_A = 0, CAT_BAR_Y, CAT_BAR_X, CAT_BAR_COUNT };
static const char *const nds_exts[] = { ".nds", ".dsi", NULL };
static const char *const cart_exts[] = { ".3ds", ".cci", NULL };

typedef struct {
    const AppConfig *config;

    bool ready;              // the system list is known (cached or live)
    bool cached;             // rows come from the SD cache, filtered here
    bool offline;            // the server didn't answer: cached copy shown
    char notice[96];         // banner: offline / stale copy

    char systems[CAT_MAX_SYSTEMS][8];
    int nsystems, sys;

    CatList lists[CAT_MAX_SYSTEMS];   // cached rows, loaded on first visit
    bool list_loaded[CAT_MAX_SYSTEMS];
    int *filtered;                    // rows of lists[sys] passing the filters
    int filtered_count;

    bool ra_only;
    char search[48];

    CatEntry *win;           // loaded window of the filtered list
    int win_off, win_count;
    int total;               // filtered count, -1 before the first page
    bool filter_ignored;     // server didn't apply has_ra (too old)
    char error[96];

    int selected, scroll;
    int direction;           // of the last move: where to load ahead
    // The last fetch ensure_window() made (viewport top, offset, total)
    // and the window it left: while nothing of that changed, the request
    // isn't repeated every settled frame when its window still fails the
    // test (a server returning fewer rows than its total claims)
    int ew_first, ew_off, ew_total, ew_win_off, ew_win_count;
    bool loading;            // a page request is in flight (drawn as a spinner)
    UiListAnim anim;
    UiBar bar;
    u32 held;                // keys held while the tab's own loop draws (else 0)
    GuiTabsAnim tabs;        // the systems strip
    UiTap seg_tap;

    bool installed_loaded;
    char rom_dir[MAX_PATH_LEN];  // where files of this system go
    CatNameSet files;            // ROM files already in rom_dir
    u64 *titles;                 // installed SD titles (sorted)
    u32 title_count;
    CatCiaMap cia_map;           // rom_id -> title id of catalog installs

    bool nds_installed;          // a DS game landed in the save scan folder
} Catalog;

// Kept for the whole session, so switching tabs doesn't reload anything
static Catalog *g_cat;

// What one load did, for the "Refresh catalog" report
typedef struct {
    int fetched;                 // systems downloaded again
    int unchanged;               // systems kept from the cache
    int failed;                  // systems that couldn't be fetched
    int rows[CAT_MAX_SYSTEMS];   // rows per system, -1 unknown
} LoadSummary;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void format_time(unsigned seconds, char *out, size_t size) {
    if (seconds > 99 * 3600 + 3599) seconds = 99 * 3600 + 3599;
    if (seconds >= 3600)
        snprintf(out, size, "%u:%02u:%02u", seconds / 3600, (seconds / 60) % 60, seconds % 60);
    else
        snprintf(out, size, "%u:%02u", seconds / 60, seconds % 60);
}

static void format_speed(u32 bytes_per_s, char *out, size_t size) {
    if (bytes_per_s >= 1024 * 1024)
        snprintf(out, size, "%.1f MB/s", bytes_per_s / (1024.0 * 1024.0));
    else
        snprintf(out, size, "%lu KB/s", (unsigned long)(bytes_per_s / 1024));
}

static void mkdir_parents(const char *path) {
    char buf[MAX_PATH_LEN];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = strchr(buf, '/'); p; p = strchr(p + 1, '/')) {
        if (p > buf && p[-1] == ':') continue;  // "sdmc:/"
        *p = '\0';
        mkdir(buf, 0777);
        *p = '/';
    }
    mkdir(buf, 0777);
}

static u64 sd_free_bytes(void) {
    FS_ArchiveResource res;
    if (R_FAILED(FSUSER_GetArchiveResource(&res, SYSTEM_MEDIATYPE_SD))) return 0;
    return (u64)res.freeClusters * res.clusterSize;
}

static const char *entry_name(const CatEntry *e) {
    return e->name[0] ? e->name : e->filename;
}

static void cache_path(const char *system, char *out, size_t size) {
    snprintf(out, size, CACHE_DIR "/catalog_%s.txt", system);
}

static void draw_frame(Catalog *cat);

// ---------------------------------------------------------------------------
// Server list
// ---------------------------------------------------------------------------

// GET with one retry when the server didn't answer at all
static u8 *cat_get_cfg(const AppConfig *config, const char *path, u32 *size, u32 *status) {
    *status = 0;
    u8 *body = network_get(config, path, size, status);
    if (!body && *status == 0) {
        svcSleepThread(1000000000LL);
        body = network_get(config, path, size, status);
    }
    // No real HTTP status (seen as -1 when nothing answered): no answer
    if (*status < 100 || *status > 599) {
        free(body);
        body = NULL;
        *status = 0;
    }
    return body;
}

static u8 *cat_get(const Catalog *cat, const char *path, u32 *size, u32 *status) {
    return cat_get_cfg(cat->config, path, size, status);
}

static void describe_failure(u32 status, char *out, size_t size) {
    if (status == 0 && network_last_error()[0])
        snprintf(out, size, "No response from server: %s", network_last_error());
    else if (status == 0)
        snprintf(out, size, "No response from server");
    else if (status == 401 || status == 403)
        snprintf(out, size, "API key rejected (HTTP %lu)", (unsigned long)status);
    else
        snprintf(out, size, "Server error (HTTP %lu)", (unsigned long)status);
}

// Live mode (no fingerprints on the server): the systems it has
static bool load_systems(Catalog *cat) {
    u32 size = 0, status = 0;
    u8 *body = cat_get(cat, "/roms/systems", &size, &status);
    bool ok = false;
    if (body && status == 200) {
        int counts[CAT_MAX_SYSTEMS];
        int n = cat_parse_systems((const char *)body, size, cat_systems,
                                  cat->systems, counts, CAT_MAX_SYSTEMS);
        if (n < 0) {
            snprintf(cat->error, sizeof(cat->error), "Bad reply from server");
        } else {
            cat->nsystems = n;
            ok = true;
        }
    } else {
        describe_failure(body ? status : 0, cat->error, sizeof(cat->error));
    }
    free(body);
    return ok;
}

// Download every row of `system` into its cache file
static bool fetch_system(Catalog *cat, const char *system, const char *fingerprint, int *rows) {
    CatEntry *page = malloc(CAT_FETCH_PAGE * sizeof(CatEntry));
    if (!page) {
        snprintf(cat->error, sizeof(cat->error), "Out of memory");
        return false;
    }
    char path[MAX_PATH_LEN];
    cache_path(system, path, sizeof(path));
    CatCacheWriter w;
    // A fingerprint the cache can't hold gets "-", which never matches:
    // the system is then fetched every time rather than trusted wrongly
    if (!cat_cache_begin(&w, path, fingerprint[0] ? fingerprint : "-")) {
        free(page);
        snprintf(cat->error, sizeof(cat->error), "Can't write the catalog cache on the SD card");
        return false;
    }

    int offset = 0, total = -1;
    bool ok = false;
    for (int pages = 0; pages < CAT_FETCH_MAX_PAGES; pages++) {
        char status_line[64], name[48];
        snprintf(name, sizeof(name), "%s game list", system);
        if (total > 0) snprintf(status_line, sizeof(status_line), "%d / %d games", offset, total);
        else snprintf(status_line, sizeof(status_line), "Downloading...");
        UiProgress p = { 0 };
        p.title = "Game Catalog";
        p.name = name;
        p.status = status_line;
        p.frac = total > 0 ? (float)offset / (float)total : -1;
        ui_progress(&p);

        char url[96];
        snprintf(url, sizeof(url), "/roms?system=%s&limit=%d&offset=%d", system, CAT_FETCH_PAGE, offset);
        u32 size = 0, status = 0;
        u8 *body = cat_get(cat, url, &size, &status);
        if (!body || status != 200) {
            describe_failure(body ? status : 0, cat->error, sizeof(cat->error));
            free(body);
            break;
        }
        CatPageInfo info;
        int n = cat_parse_page((const char *)body, size, page, CAT_FETCH_PAGE, &info);
        free(body);
        if (n < 0) {
            snprintf(cat->error, sizeof(cat->error), "Bad catalog reply");
            break;
        }
        for (int i = 0; i < n; i++) cat_cache_add(&w, &page[i]);
        offset += n;
        total = info.total;
        if (!info.has_more || n == 0) {
            ok = true;
            break;
        }
    }
    free(page);
    if (!ok) {
        cat_cache_abort(&w);
        return false;
    }
    if (!cat_cache_end(&w)) {
        snprintf(cat->error, sizeof(cat->error), "Can't write the catalog cache on the SD card");
        return false;
    }
    if (rows) *rows = offset;
    return true;
}

// Drop the loaded rows and the list position (not the installed sets)
static void unload_lists(Catalog *cat) {
    for (int i = 0; i < CAT_MAX_SYSTEMS; i++) {
        cat_list_free(&cat->lists[i]);
        cat->list_loaded[i] = false;
    }
    free(cat->filtered);
    cat->filtered = NULL;
    cat->filtered_count = 0;
    cat->ready = cat->cached = cat->offline = false;
    cat->notice[0] = cat->error[0] = '\0';
    cat->nsystems = cat->sys = 0;
    cat->total = -1;
    cat->win_off = cat->win_count = 0;
    cat->ew_first = cat->ew_off = cat->ew_total = -1;
    cat->selected = cat->scroll = 0;
    ui_list_reset(&cat->anim);
}

// Systems that have a complete cache file, for offline use
static int cached_systems(Catalog *cat) {
    int n = 0;
    for (int i = 0; cat_systems[i] && n < CAT_MAX_SYSTEMS; i++) {
        char path[MAX_PATH_LEN], fp[CAT_FP_LEN];
        cache_path(cat_systems[i], path, sizeof(path));
        if (cat_cache_fingerprint(path, fp, sizeof(fp))) snprintf(cat->systems[n++], 8, "%s", cat_systems[i]);
    }
    return n;
}

// Find out what the server has and bring the cache up to date. False with
// cat->error set if there is nothing to show.
static bool load_catalog(Catalog *cat, LoadSummary *sum) {
    LoadSummary dummy;
    if (!sum) sum = &dummy;
    memset(sum, 0, sizeof(*sum));
    for (int i = 0; i < CAT_MAX_SYSTEMS; i++) sum->rows[i] = -1;
    unload_lists(cat);

    ui_job_settle();   // a cancelled job may still be talking to the server
    ui_busy("Game Catalog", "Contacting the server...");
    u32 size = 0, status = 0;
    u8 *body = cat_get(cat, "/roms/fingerprints", &size, &status);
    char fps[CAT_MAX_SYSTEMS][CAT_FP_LEN];
    int counts[CAT_MAX_SYSTEMS];
    int n = -1;
    if (!body) status = 0;
    if (body && status == 200)
        n = cat_parse_fingerprints((const char *)body, size, cat_systems, cat->systems, fps, counts,
                                   CAT_MAX_SYSTEMS);
    free(body);

    if (n >= 0) {
        // Cached mode
        mkdir_parents(CACHE_DIR);
        cat->cached = true;
        cat->nsystems = n;
        // Systems the server no longer has
        for (int i = 0; cat_systems[i]; i++) {
            bool listed = false;
            for (int j = 0; j < n; j++) listed |= strcasecmp(cat->systems[j], cat_systems[i]) == 0;
            if (!listed) {
                char path[MAX_PATH_LEN];
                cache_path(cat_systems[i], path, sizeof(path));
                remove(path);
            }
        }
        int kept = 0;
        char failed_error[96] = "";
        for (int i = 0; i < n; i++) {
            char path[MAX_PATH_LEN], have[CAT_FP_LEN];
            cache_path(cat->systems[i], path, sizeof(path));
            bool have_file = cat_cache_fingerprint(path, have, sizeof(have));
            if (have_file && fps[i][0] && strcmp(have, fps[i]) == 0) {
                sum->unchanged++;
            } else if (fetch_system(cat, cat->systems[i], fps[i], &sum->rows[i])) {
                sum->fetched++;
            } else {
                sum->failed++;
                snprintf(failed_error, sizeof(failed_error), "%s", cat->error);
                if (!have_file) continue;  // nothing to show for this system
                snprintf(cat->notice, sizeof(cat->notice), "%s: couldn't update, showing the cached list",
                         cat->systems[i]);
            }
            if (kept != i) {
                memcpy(cat->systems[kept], cat->systems[i], sizeof(cat->systems[i]));
                sum->rows[kept] = sum->rows[i];
                sum->rows[i] = -1;
            }
            kept++;
        }
        cat->nsystems = kept;
        cat->error[0] = '\0';
        if (kept == 0 && n > 0) {
            snprintf(cat->error, sizeof(cat->error), "%s", failed_error);
            return false;
        }
        cat->ready = true;
        return true;
    }

    if (status == 404 || status == 405) {
        // A server without fingerprints: browse it live, cache nothing
        if (!load_systems(cat)) return false;
        cat->ready = true;
        return true;
    }

    // No answer, or an error: the cached copy beats an empty tab
    describe_failure(status, cat->error, sizeof(cat->error));
    if (status == 200) snprintf(cat->error, sizeof(cat->error), "Bad reply from server");
    int cached = cached_systems(cat);
    if (cached == 0) return false;
    snprintf(cat->notice, sizeof(cat->notice), "%.60s: showing the cached catalog", cat->error);
    cat->error[0] = '\0';
    cat->nsystems = cached;
    cat->cached = cat->offline = cat->ready = true;
    return true;
}

// Cached mode: rows of the current system matching the filters
static bool apply_filter(Catalog *cat) {
    int s = cat->sys;
    if (!cat->list_loaded[s]) {
        char path[MAX_PATH_LEN];
        cache_path(cat->systems[s], path, sizeof(path));
        ui_busy("Game Catalog", "Reading the cached list...");
        if (!cat_list_load(&cat->lists[s], path)) {
            // Out of memory, or a damaged file: drop it so A fetches it again
            if (!cat->offline) remove(path);
            snprintf(cat->error, sizeof(cat->error), "Couldn't read the cached %s list: A fetches it again",
                     cat->systems[s]);
            return false;
        }
        cat->list_loaded[s] = true;
    }
    free(cat->filtered);
    cat->filtered = malloc((size_t)(cat->lists[s].count > 0 ? cat->lists[s].count : 1) * sizeof(int));
    if (!cat->filtered) {
        cat->filtered_count = 0;
        snprintf(cat->error, sizeof(cat->error), "Out of memory");
        return false;
    }
    cat->filtered_count = cat_list_filter(&cat->lists[s], cat->search, cat->ra_only, cat->filtered);
    return true;
}

// A live page, fetched on the worker (see ui_run_job): everything it needs
// is copied in, everything it finds is copied out by the main thread
typedef struct {
    AppConfig config;
    char path[320];
    CatEntry win[CAT_WINDOW];
    int n;                   // entries parsed, -1 bad reply
    CatPageInfo info;
    bool ok;                 // a 200 that parsed
    char error[96];
} PageJob;

static bool page_job(void *ctx) {
    PageJob *j = ctx;
    u32 size = 0, status = 0;
    u8 *body = cat_get_cfg(&j->config, j->path, &size, &status);
    j->error[0] = '\0';
    if (body && status == 200) {
        j->n = cat_parse_page((const char *)body, size, j->win, CAT_WINDOW, &j->info);
        if (j->n < 0) snprintf(j->error, sizeof(j->error), "Bad catalog reply");
        else j->ok = true;
    } else {
        describe_failure(body ? status : 0, j->error, sizeof(j->error));
    }
    free(body);
    return j->ok;
}

// Load the page starting at `offset` into the window
static void fetch_window(Catalog *cat, int offset) {
    if (cat->cached) {
        const CatList *list = &cat->lists[cat->sys];
        int n = 0;
        for (int i = offset; i < cat->filtered_count && n < CAT_WINDOW; i++, n++)
            cat_row_to_entry(&list->rows[cat->filtered[i]], &cat->win[n]);
        cat->win_off = offset;
        cat->win_count = n;
        cat->total = cat->filtered_count;
        cat->filter_ignored = false;
        return;
    }

    char search[160] = "";
    if (cat->search[0]) {
        char enc[144];
        cat_url_encode(cat->search, enc, sizeof(enc));
        snprintf(search, sizeof(search), "&search=%s", enc);
    }
    char path[320];
    snprintf(path, sizeof(path), "/roms?system=%s&limit=%d&offset=%d%s%s",
             cat->systems[cat->sys], CAT_WINDOW, offset,
             cat->ra_only ? "&has_ra=true" : "", search);

    PageJob *j = ui_job_ctx(sizeof(PageJob));
    if (!j) {
        snprintf(cat->error, sizeof(cat->error), "Out of memory");
        cat->win_count = 0;
        return;
    }
    j->config = *cat->config;
    snprintf(j->path, sizeof(j->path), "%s", path);
    // The list stays live underneath, showing its "Loading..." rows and the
    // header spinner; a card only appears if the server is slow
    UiProgress p = { 0 };
    p.title = "Game Catalog";
    p.status = "Loading the list...";
    p.frac = -1;
    bool cancelled = false;
    cat->loading = true;
    ui_run_job_ex(page_job, j, &p, UI_JOB_CANCELLABLE | UI_JOB_QUIET, &cancelled);
    cat->loading = false;
    if (cancelled) {
        // Nothing changed; A asks again
        snprintf(cat->error, sizeof(cat->error), "Loading cancelled");
        cat->win_count = 0;
        return;
    }
    snprintf(cat->error, sizeof(cat->error), "%s", j->error);
    if (j->ok) {
        memcpy(cat->win, j->win, (size_t)j->n * sizeof(CatEntry));
        cat->win_off = offset;
        cat->win_count = j->n;
        cat->total = j->info.total;
        cat->filter_ignored = cat->ra_only && j->info.has_ra != 1;
    } else {
        cat->win_count = 0;
    }
    ui_job_release(j);
}

// Entry `index` of the filtered list, or NULL while it isn't loaded. In
// cached mode every row is in memory: one outside the window (a selection a
// drag left behind) is converted on the fly, valid until the next call.
static const CatEntry *entry_at(const Catalog *cat, int index) {
    if (index >= cat->win_off && index < cat->win_off + cat->win_count) return &cat->win[index - cat->win_off];
    if (cat->cached && cat->filtered && index >= 0 && index < cat->filtered_count &&
        cat->list_loaded[cat->sys]) {
        static CatEntry scratch;
        cat_row_to_entry(&cat->lists[cat->sys].rows[cat->filtered[index]], &scratch);
        return &scratch;
    }
    return NULL;
}

// After a drag or a flick settled at row `first`: load the rows on screen,
// and the next stretch when the viewport is within a page of the loaded
// window's end, so the next flick can carry on (a coast never lands outside
// the window in live mode). The selection doesn't pull the viewport here.
static void ensure_window(Catalog *cat, int first) {
    if (cat->total <= 0 || cat->error[0]) return;
    if (first < 0) first = 0;
    int end = cat->win_off + cat->win_count;
    bool near_end = end < cat->total && first + 2 * CAT_ROWS > end;
    bool near_start = cat->win_off > 0 && first - CAT_ROWS < cat->win_off;
    if (cat_window_covers(cat->win_off, cat->win_count, first, CAT_ROWS, cat->total) && !near_end && !near_start)
        return;
    int off = cat_window_start(first, CAT_ROWS, CAT_WINDOW, cat->total, cat->direction);
    if (first == cat->ew_first && off == cat->ew_off && cat->total == cat->ew_total &&
        cat->win_off == cat->ew_win_off && cat->win_count == cat->ew_win_count)
        return;
    fetch_window(cat, off);
    cat->ew_first = first;
    cat->ew_off = off;
    cat->ew_total = cat->total;
    cat->ew_win_off = cat->win_off;
    cat->ew_win_count = cat->win_count;
}

// Make sure the visible rows are loaded, scrolling to keep the selection
// in view (after a key move)
static void ensure_loaded(Catalog *cat) {
    if (cat->total < 0) {
        fetch_window(cat, 0);
        if (cat->total < 0) return;
    }
    for (int tries = 0; tries < 2; tries++) {
        if (cat->selected >= cat->total) cat->selected = cat->total > 0 ? cat->total - 1 : 0;
        if (cat->scroll > cat->selected) cat->scroll = cat->selected;
        if (cat->selected >= cat->scroll + CAT_ROWS) cat->scroll = cat->selected - CAT_ROWS + 1;
        if (cat->scroll < 0) cat->scroll = 0;
        if (cat->error[0] || cat_window_covers(cat->win_off, cat->win_count, cat->scroll, CAT_ROWS, cat->total))
            return;
        // The total may shrink on the refetch (server rescan): clamp again
        fetch_window(cat, cat_window_start(cat->scroll, CAT_ROWS, CAT_WINDOW, cat->total, cat->direction));
    }
}

// New filter/system: start over from the top
static void reset_list(Catalog *cat) {
    cat->total = -1;
    cat->win_off = cat->win_count = 0;
    cat->ew_first = cat->ew_off = cat->ew_total = -1;
    cat->selected = cat->scroll = 0;
    cat->error[0] = '\0';
    ui_list_reset(&cat->anim);
    if (cat->cached && !apply_filter(cat)) return;
    ensure_loaded(cat);
}

// ---------------------------------------------------------------------------
// What's installed
// ---------------------------------------------------------------------------

static int u64_compare(const void *a, const void *b) {
    u64 x = *(const u64 *)a, y = *(const u64 *)b;
    return (x > y) - (x < y);
}

static void load_installed_titles(Catalog *cat) {
    free(cat->titles);
    cat->titles = NULL;
    cat->title_count = 0;
    u32 count = 0;
    if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count)) || count == 0) return;
    cat->titles = malloc(count * sizeof(u64));
    if (!cat->titles) return;
    u32 read = 0;
    if (R_FAILED(AM_GetTitleList(&read, MEDIATYPE_SD, count, cat->titles))) read = 0;
    cat->title_count = read;
    qsort(cat->titles, read, sizeof(u64), u64_compare);
}

static bool title_installed(const Catalog *cat, u64 tid) {
    if (!tid || !cat->title_count) return false;
    return bsearch(&tid, cat->titles, cat->title_count, sizeof(u64), u64_compare) != NULL;
}

static bool system_is_3ds(const Catalog *cat) {
    return strcasecmp(cat->systems[cat->sys], "3DS") == 0;
}

static void set_rom_dir(Catalog *cat) {
    const char *sys = cat->systems[cat->sys];
    if (strcasecmp(sys, "3DS") == 0)
        snprintf(cat->rom_dir, sizeof(cat->rom_dir), "%s", ROMS_3DS_DIR);
    else if (strcasecmp(sys, "DSI") == 0)
        snprintf(cat->rom_dir, sizeof(cat->rom_dir), "%s", ROMS_DSI_DIR);
    else if (cat->config->nds_dir[0])
        snprintf(cat->rom_dir, sizeof(cat->rom_dir), "%s", cat->config->nds_dir);
    else
        snprintf(cat->rom_dir, sizeof(cat->rom_dir), "%s", ROMS_NDS_DIR);
    // No trailing slash
    size_t len = strlen(cat->rom_dir);
    while (len > 1 && cat->rom_dir[len - 1] == '/') cat->rom_dir[--len] = '\0';
}

static void scan_installed(Catalog *cat) {
    set_rom_dir(cat);
    cat_names_free(&cat->files);
    cat_names_scan(&cat->files, cat->rom_dir, CAT_SCAN_DEPTH,
                   system_is_3ds(cat) ? cart_exts : nds_exts);
}

// Title id this entry is installed as, 0 if not installed / unknown
static u64 installed_tid(const Catalog *cat, const CatEntry *e) {
    u64 tid = cat_cia_map_get(&cat->cia_map, e->rom_id);
    if (title_installed(cat, tid)) return tid;
    if (cat_parse_tid(e->title_id, &tid) && title_installed(cat, tid)) return tid;
    return 0;
}

static bool file_on_sd(const Catalog *cat, const CatEntry *e) {
    char target[CAT_FILE_LEN];
    cat_target_name(e->filename, system_is_3ds(cat) ? ".3ds" : ".nds", target, sizeof(target));
    return cat_names_contains(&cat->files, target);
}

static bool is_installed(const Catalog *cat, const CatEntry *e) {
    if (system_is_3ds(cat) && installed_tid(cat, e)) return true;
    return file_on_sd(cat, e);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void ra_tag(const CatEntry *e, char *out, size_t size) {
    if (!cat_entry_has_ra(e)) out[0] = '\0';
    else if (e->ra_title_only) snprintf(out, size, "RA?");
    else if (e->ra_achievements > 999) snprintf(out, size, "RA 999+");
    else snprintf(out, size, "RA %d", e->ra_achievements);
}

static const char *plan_label(const CatPlan *plan) {
    switch (plan->kind) {
        case CAT_PLAN_CIA:         return "CIA to the HOME Menu";
        case CAT_PLAN_CIA_CONVERT: return "CIA (the server converts it)";
        case CAT_PLAN_3DS_FILE:    return ".3ds file (no CIA conversion)";
        case CAT_PLAN_NDS:         return ".nds file";
        case CAT_PLAN_NDS_EXTRACT: return ".nds (the server unzips it)";
        default:                   return "Can't be installed here";
    }
}

static void kv(float x, float y, float label_w, float w, const char *label, const char *value, u32 color) {
    gui_text(x, y, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_LEFT, label);
    gui_text_fit(x + label_w, y, GUI_S_SMALL, gui_rgb(color), GUI_LEFT, w - label_w, value);
}

// RA badge: gold pill with the count, outlined when matched by name only
// `hl`: how much of it the list's highlight bar covers (0 = none)
static float ra_badge(float x_right, float y, float h, const CatEntry *e, float hl) {
    char tag[16];
    ra_tag(e, tag, sizeof(tag));
    if (!tag[0]) return 0;
    float w = gui_pill_w(h, GUI_S_TINY, tag);
    if (e->ra_title_only)
        gui_pill_outline(x_right - w, y, h, GUI_S_TINY, gui_mix(HEX_RA, HEX_INK, hl),
                         gui_mix(HEX_BG, HEX_ACCENT, hl), tag);
    else
        gui_pill(x_right - w, y, h, GUI_S_TINY, gui_mix(HEX_RA, HEX_INK, hl),
                 gui_mix(HEX_INK, HEX_RA, hl), tag);
    return w;
}

static void draw_top(void *ctx) {
    Catalog *cat = ctx;
    ui_tab_header(UI_TAB_CATALOG);
    GuiHint hints[5];
    int nhints = 0;
    if (cat->nsystems > 1) hints[nhints++] = (GuiHint){ "SELECT", "System" };
    hints[nhints++] = (GuiHint){ "X", "RA only" };
    hints[nhints++] = (GuiHint){ "Y", "Search" };
    hints[nhints++] = (GuiHint){ "LR", "Page" };
    hints[nhints++] = (GuiHint){ "START", "Exit" };
    ui_tab_content_begin();

    float px = 8, py = 32, pw = GUI_TOP_W - 16;
    float banner_y = 190;
    const char *banner = NULL;
    u32 banner_hex = HEX_ERR, banner_text = HEX_ERR;
    if (cat->error[0]) {
        banner = cat->error;
    } else if (cat->filter_ignored) {
        banner = "This server can't filter by RetroAchievements: update it";
    } else if (cat->notice[0]) {
        banner = cat->notice;
        banner_hex = HEX_WARN;
        banner_text = HEX_TEXT;
    } else if (cat->search[0]) {
        banner_hex = HEX_ACCENT;
    }

    const CatEntry *e = (cat->ready && cat->total > 0) ? entry_at(cat, cat->selected) : NULL;
    gui_panel(px, py, pw, 152);
    if (!e) {
        const char *msg = !cat->ready ? (cat->error[0] ? "Couldn't load the catalog" : "Loading the catalog...")
                        : cat->nsystems == 0 ? "The server has no 3DS or DS games"
                        : cat->loading || cat->total < 0 ? "Loading the catalog..."
                        : cat->total == 0 ? (cat->search[0] ? "No games match the search"
                                             : cat->ra_only ? "No games with achievements"
                                             : "No games for this system")
                        : "Loading...";
        gui_text(GUI_TOP_W / 2.0f, 92, GUI_S_TITLE, gui_rgb(HEX_DIM), GUI_CENTER, msg);
        if (!cat->ready && cat->error[0])
            gui_text(GUI_TOP_W / 2.0f, 120, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER,
                     "A: try again   (check WiFi and the server URL in Settings)");
        else if (cat->ready && cat->total == 0 && cat->search[0])
            gui_text(GUI_TOP_W / 2.0f, 120, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER,
                     "Y: new search   B: clear the search");
        else if (cat->ready && cat->total == 0 && cat->ra_only)
            gui_text(GUI_TOP_W / 2.0f, 120, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_CENTER, "X: show all games");
    } else {
        float x = px + 12, w = pw - 24;
        int lines = gui_text_wrap(x, py + 8, GUI_S_TITLE, gui_rgb(HEX_TEXT), w, 2, entry_name(e));
        float y = py + 10 + lines * gui_line_h(GUI_S_TITLE);

        // Pills: system, size, RA, installed
        char size[16];
        cat_format_size(e->size, size, sizeof(size));
        const char *ext = strrchr(e->filename, '.');
        bool zipped = ext && strcasecmp(ext, ".zip") == 0;
        float pxx = x;
        u32 sys_hex = system_is_3ds(cat) ? HEX_ACCENT2 : HEX_NDS;
        pxx += gui_pill(pxx, y, 15, GUI_S_TINY, gui_mix(HEX_PANEL, sys_hex, 0.3f), gui_rgb(sys_hex),
                        cat->systems[cat->sys]) + 5;
        char size_label[32];
        snprintf(size_label, sizeof(size_label), "%s%s", size, zipped ? " zip" : "");
        pxx += gui_pill(pxx, y, 15, GUI_S_TINY, gui_rgb(HEX_BG2), gui_rgb(HEX_DIM), size_label) + 5;
        if (cat_entry_has_ra(e)) {
            char tag[16];
            ra_tag(e, tag, sizeof(tag));
            pxx += ra_badge(pxx + gui_pill_w(15, GUI_S_TINY, tag), y, 15, e, 0) + 5;
        }
        bool inst = is_installed(cat, e);
        if (inst) {
            float iw = gui_pill_w(15, GUI_S_TINY, "Installed") + 12;
            gui_rrect(pxx, y, iw, 15, 7.5f, gui_mix(HEX_PANEL, HEX_OK, 0.25f));
            gui_icon_check(pxx + 8, y + 7.5f, 7, gui_rgb(HEX_OK));
            gui_text_mid(pxx + 15, y, 15, GUI_S_TINY, gui_rgb(HEX_OK), GUI_LEFT, 0, "Installed");
        }
        y += 22;

        float lh = gui_line_h(GUI_S_SMALL) + 1;
        CatPlan plan = cat_plan_install(cat->systems[cat->sys], e);
        kv(x, y, 66, w, "File", e->filename, HEX_DIM);
        kv(x, y + lh, 66, w, "Installs as", plan_label(&plan), plan.kind == CAT_PLAN_NONE ? HEX_ERR : HEX_TEXT);
        if (system_is_3ds(cat)) kv(x, y + 2 * lh, 66, w, "Goes to", "HOME Menu (title on the SD)", HEX_TEXT);
        else kv(x, y + 2 * lh, 66, w, "Goes to", cat->rom_dir, HEX_TEXT);

        char ra[64];
        u32 ra_hex = HEX_DIM;
        if (cat_entry_has_ra(e) && e->ra_title_only) {
            snprintf(ra, sizeof(ra), "%d achievements? (matched by name)", e->ra_achievements);
            ra_hex = HEX_RA;
        } else if (cat_entry_has_ra(e)) {
            snprintf(ra, sizeof(ra), "%d achievements", e->ra_achievements);
            ra_hex = HEX_RA;
        } else if (e->ra_game_id) {
            snprintf(ra, sizeof(ra), "Known, no achievements yet");
        } else {
            snprintf(ra, sizeof(ra), "None");
        }
        kv(x, y + 3 * lh, 66, w, "RetroAch.", ra, ra_hex);

        u64 tid = system_is_3ds(cat) ? installed_tid(cat, e) : 0;
        char inst_line[48];
        if (tid) snprintf(inst_line, sizeof(inst_line), "Yes, %016llX", (unsigned long long)tid);
        else snprintf(inst_line, sizeof(inst_line), "%s", file_on_sd(cat, e) ? "On the SD card" : "No");
        kv(x, y + 4 * lh, 66, w, "Installed", inst_line, (tid || inst) ? HEX_OK : HEX_DIM);
    }

    // Banner: error, offline / stale notice, or the active search
    if (banner || cat->search[0]) {
        char text[128];
        if (banner) snprintf(text, sizeof(text), "%s", banner);
        else snprintf(text, sizeof(text), "Search: \"%s\"   (B clears)", cat->search);
        gui_rrect(8, banner_y, GUI_TOP_W - 16, 22, 6, gui_rgb(HEX_BG2));
        gui_rrect(8, banner_y, 4, 22, 2, gui_rgb(banner_hex));
        gui_text_mid(20, banner_y, 22, GUI_S_SMALL, gui_rgb(banner ? banner_text : HEX_TEXT), GUI_LEFT,
                     GUI_TOP_W - 40, text);
    }
    ui_tab_content_end();
    gui_footer(hints, nhints);
}

static void cat_row(void *ctx, int index, float x, float y, float w, float h, float hl, bool pressed) {
    (void)pressed;
    Catalog *cat = ctx;
    const CatEntry *e = entry_at(cat, index);
    float cy = y + h / 2;
    if (!e) {
        gui_text_mid(x + 26, y, h, GUI_S_BODY, gui_mix(HEX_MUTED, HEX_INK, hl), GUI_LEFT, 0,
                     "Loading...");
        return;
    }
    bool inst = is_installed(cat, e);
    if (inst) {
        gui_circle(x + 14, cy, 6, gui_mix(HEX_OK, HEX_INK, hl));
        gui_icon_check(x + 14, cy, 7, gui_mix(HEX_INK, HEX_ACCENT, hl));
    } else {
        gui_circle(x + 14, cy, 2, gui_mix(HEX_LINE, HEX_INK, hl));
    }
    float badge = 0;
    if (cat_entry_has_ra(e)) badge = ra_badge(x + w - 8, cy - 6.5f, 13, e, hl) + 6;
    gui_text_mid(x + 26, y, h, GUI_S_BODY, gui_mix(inst ? HEX_OK : HEX_TEXT, HEX_INK, hl), GUI_LEFT,
                 w - 26 - 8 - badge, entry_name(e));
}

// The action bar: A Install (Try again after an error), Y Search, X RA only
static int cat_bar(const Catalog *cat, UiBarItem *items) {
    bool usable = cat->ready && cat->nsystems > 0;
    bool retry = !usable || cat->error[0];
    bool has_entry = usable && cat->total > 0;   // A loads the selection if it isn't yet
    items[CAT_BAR_A] = (UiBarItem){ "A", retry ? "Try again" : "Install",
                                    (retry || has_entry) ? GUI_BTN_PRIMARY : GUI_BTN_DISABLED, 0, KEY_A, 0, false };
    items[CAT_BAR_Y] = (UiBarItem){ "Y", "Search", usable ? GUI_BTN_TONAL : GUI_BTN_DISABLED, 0, KEY_Y, 0, false };
    items[CAT_BAR_X] = (UiBarItem){ "X", "RA only", usable ? GUI_BTN_TONAL : GUI_BTN_DISABLED, HEX_RA, KEY_X, 0,
                                    cat->ra_only };
    return CAT_BAR_COUNT;
}

// Left edge of the systems strip (after the SELECT glyph when it cycles)
static float cat_tabs_x(const Catalog *cat) {
    return 6 + (cat->nsystems > 1 ? gui_button_w("SELECT") + 4 : 0);
}

// Touch targets of the systems strip (the whole header height)
static int cat_segments(const Catalog *cat, GuiRect *touch) {
    const char *labels[CAT_MAX_SYSTEMS];
    int n = cat->nsystems < CAT_MAX_SYSTEMS ? cat->nsystems : CAT_MAX_SYSTEMS;
    for (int i = 0; i < n; i++) labels[i] = cat->systems[i];
    gui_tabs_layout(cat_tabs_x(cat), 4, 18, labels, n, touch);
    for (int i = 0; i < n; i++) touch[i] = gui_rect_make(touch[i].x, 0, touch[i].w, GUI_HEADER_H);
    return n;
}

static void draw_bottom(void *ctx) {
    Catalog *cat = ctx;
    bool has_rows = cat->ready && cat->total > 0;
    ui_tab_content_begin();
    if (has_rows) {
        ui_list_draw(&cat->anim, &cat_geom, cat->total, cat->selected, cat->scroll, cat_row, cat);
    } else {
        const char *msg = cat->error[0] && cat->win_count == 0 ? "A: try again"
                        : cat->total == 0 || cat->nsystems == 0 ? "Nothing to show" : "";
        if (!cat->error[0] && (cat->total < 0 || cat->loading) && cat->nsystems > 0) {
            gui_spinner(GUI_BOT_W / 2.0f, 104, 10, gui_rgb(HEX_ACCENT));
        } else {
            gui_text(GUI_BOT_W / 2.0f, 100, GUI_S_BODY, gui_rgb(HEX_DIM), GUI_CENTER, msg);
        }
    }
    ui_tab_content_end();

    gui_header_bar_ex(!has_rows || ui_list_at_top(&cat->anim));
    const char *labels[CAT_MAX_SYSTEMS];
    for (int i = 0; i < cat->nsystems && i < CAT_MAX_SYSTEMS; i++) labels[i] = cat->systems[i];
    // SELECT cycles the systems: the glyph sits before the segmented
    // control; a tap on a segment picks it
    float x = cat_tabs_x(cat);
    if (cat->nsystems > 1) gui_button(6, GUI_HEADER_H / 2.0f, "SELECT");
    if (cat->nsystems > 0) x = gui_tabs(&cat->tabs, x, 4, 18, labels, cat->nsystems, cat->sys, NULL) + 6;
    if (cat->ra_only) x += gui_pill(x, 5.5f, 15, GUI_S_TINY, gui_rgb(HEX_RA), gui_rgb(HEX_INK), "RA only") + 4;
    if (cat->search[0]) x += gui_pill(x, 5.5f, 15, GUI_S_TINY, gui_rgb(HEX_ACCENT), gui_rgb(HEX_INK), "Search") + 4;
    if (cat->offline) x += gui_pill(x, 5.5f, 15, GUI_S_TINY, gui_rgb(HEX_WARN), gui_rgb(HEX_INK), "Offline") + 4;
    if (cat->loading) {
        gui_spinner(GUI_BOT_W - 14, GUI_HEADER_H / 2.0f, 6, gui_rgb(HEX_ACCENT));
    } else if (has_rows) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%d/%d", cat->selected + 1, cat->total);
        // Only if it doesn't run into the tabs and pills
        if (x + gui_text_w(GUI_S_SMALL, pos) < GUI_BOT_W - 8)
            gui_text_mid(GUI_BOT_W - 8, 0, GUI_HEADER_H, GUI_S_SMALL, gui_rgb(HEX_DIM), GUI_RIGHT, 0, pos);
    }

    UiBarItem items[CAT_BAR_COUNT];
    int n = cat_bar(cat, items);
    ui_bar_draw(&cat->bar, items, n, cat->held);
}

static void draw_frame(Catalog *cat) {
    gui_begin(!cat->loading);
    gui_screen(GUI_TOP);
    draw_top(cat);
    gui_screen(GUI_BOTTOM);
    draw_bottom(cat);
    gui_end();
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

typedef struct {
    Catalog *cat;
    const char *name;
    bool cia;                // stream into AM rather than a file
    FILE *f;
    Handle handle;           // AM CIA install handle
    bool am_started;
    Result am_result;
    u64 offset;              // bytes written into the CIA handle

    u8 *probe;               // start of the CIA, for its title id
    u32 probe_len;
    int probe_state;         // 0 need more, 1 found, -1 not a CIA
    u64 tid;

    u64 replaced;            // size of the file being replaced
    u64 start_ms, last_draw_ms, sample_ms;
    u64 sample_bytes;
    u32 speed;               // bytes/s over the last ~second
    bool checked;
    bool no_space, bad_data;
} Download;

static u32 bytes_per_second(u64 bytes, u64 ms) {
    if (ms == 0) return 0;
    return (u32)(bytes * 1000 / ms);
}

static void draw_progress(Download *dl, u64 done, u64 total) {
    u64 now = osGetTime();
    unsigned elapsed = (unsigned)((now - dl->start_ms) / 1000);
    u32 avg = bytes_per_second(done, now - dl->start_ms);
    char a[16], b[16], left[40], right[8] = "", speed[24], avg_s[24], info1[80], info2[64], t1[16], t2[16];

    cat_format_size(done, a, sizeof(a));
    if (total) {
        cat_format_size(total, b, sizeof(b));
        snprintf(left, sizeof(left), "%s / %s", a, b);
        snprintf(right, sizeof(right), "%lu%%", (unsigned long)(done * 100 / total));
    } else {
        snprintf(left, sizeof(left), "%s", a);
    }
    format_speed(dl->speed ? dl->speed : avg, speed, sizeof(speed));
    format_speed(avg, avg_s, sizeof(avg_s));
    snprintf(info1, sizeof(info1), "Speed %s   (average %s)", speed, avg_s);
    format_time(elapsed, t1, sizeof(t1));
    if (total && avg > 0 && done < total) {
        format_time((unsigned)((total - done) / avg), t2, sizeof(t2));
        snprintf(info2, sizeof(info2), "Elapsed %s   Remaining %s", t1, t2);
    } else {
        snprintf(info2, sizeof(info2), "Elapsed %s", t1);
    }

    UiProgress p = { 0 };
    p.title = dl->cia ? "Installing CIA" : "Downloading";
    p.name = dl->name;
    p.status = dl->cia ? "Streaming into the system" : "Writing to the SD card";
    p.frac = total ? (float)((double)done / (double)total) : -1;
    p.left = left;
    p.right = total ? right : NULL;
    p.info1 = info1;
    p.info2 = info2;
    p.cancel_hint = true;
    ui_progress(&p);
}

// Read the input; true if the user wants out (B held or the Cancel pill
// touched, or the app closing)
static bool cancel_requested(void) {
    return ui_progress_cancel_poll();
}

static bool download_wait(u32 seconds, void *user) {
    Download *dl = user;
    char t[16], status[64];
    format_time(seconds, t, sizeof(t));
    if (dl->cia)
        snprintf(status, sizeof(status), "The server is converting it to CIA...  %s", t);
    else
        snprintf(status, sizeof(status), "Waiting for the server...  %s", t);
    UiProgress p = { 0 };
    p.title = dl->cia ? "Installing CIA" : "Downloading";
    p.name = dl->name;
    p.status = status;
    p.frac = -1;
    p.info1 = (dl->cia && seconds >= 3) ? "Big games take a few minutes to convert." : NULL;
    p.cancel_hint = true;
    ui_progress(&p);
    return cancel_requested();
}

static int download_sink(const u8 *data, u32 size, u64 done, u64 total, void *user) {
    Download *dl = user;

    if (!dl->checked) {
        dl->checked = true;
        dl->start_ms = dl->last_draw_ms = dl->sample_ms = osGetTime();
        // An old server may ignore ?extract=nds and send the zip itself
        if (!dl->cia && size >= 4 && memcmp(data, "PK\x03\x04", 4) == 0) {
            dl->bad_data = true;
            return -1;
        }
        u64 free_bytes = sd_free_bytes();
        // 0 free is more likely a failed query than a full card; a really
        // full card still fails on the first write
        if (total && free_bytes > 0 && free_bytes + dl->replaced < total + 1024 * 1024) {
            dl->no_space = true;
            return -1;
        }
    }

    if (dl->cia) {
        // The title id sits in the TMD, a few KB in
        if (dl->probe_state == 0 && dl->probe) {
            u32 take = CAT_CIA_PROBE_MAX - dl->probe_len;
            if (take > size) take = size;
            memcpy(dl->probe + dl->probe_len, data, take);
            dl->probe_len += take;
            dl->probe_state = cat_cia_title_id(dl->probe, dl->probe_len, &dl->tid);
            if (dl->probe_state == 0 && dl->probe_len >= CAT_CIA_PROBE_MAX) dl->probe_state = -1;
            if (dl->probe_state < 0) {
                dl->bad_data = true;
                return -1;
            }
        }
        if (!dl->am_started) {
            dl->am_result = AM_StartCiaInstall(MEDIATYPE_SD, &dl->handle);
            if (R_FAILED(dl->am_result)) return -1;
            dl->am_started = true;
        }
        u32 written = 0;
        dl->am_result = FSFILE_Write(dl->handle, &written, dl->offset, data, size, 0);
        if (R_FAILED(dl->am_result) || written != size) return -1;
        dl->offset += size;
    } else {
        if (fwrite(data, 1, size, dl->f) != size) return -1;
    }

    u64 now = osGetTime();
    if (now - dl->sample_ms >= 1000) {
        dl->speed = bytes_per_second(done - dl->sample_bytes, now - dl->sample_ms);
        dl->sample_ms = now;
        dl->sample_bytes = done;
    }
    // A GUI frame costs a few ms: redraw a few times a second only
    if (now - dl->last_draw_ms >= 250 || (total && done >= total)) {
        dl->last_draw_ms = now;
        draw_progress(dl, done, total);
    }

    return cancel_requested() ? 1 : 0;
}

typedef enum { MODE_CIA, MODE_FILE } InstallMode;

static int confirm_install(Catalog *cat, const CatEntry *e, const CatPlan *plan,
                           const char *target, bool exists) {
    char body[640], size[16];
    cat_format_size(e->size, size, sizeof(size));
    bool cia = (plan->kind == CAT_PLAN_CIA || plan->kind == CAT_PLAN_CIA_CONVERT);
    int pos = snprintf(body, sizeof(body), UI_HI "%s\n" UI_DIM "%s on the server  \xC2\xB7  %s\n",
                       entry_name(e), size, plan_label(plan));
    if (cia) {
        pos += snprintf(body + pos, sizeof(body) - pos,
                        "Goes straight into the system: nothing is kept on the SD card.\n");
        if (plan->kind == CAT_PLAN_CIA_CONVERT)
            pos += snprintf(body + pos, sizeof(body) - pos,
                            UI_DIM "The server converts it first: big games can take minutes to start.\n");
        if (exists)
            pos += snprintf(body + pos, sizeof(body) - pos, "\nAlready installed: install it again?\n");
    } else {
        pos += snprintf(body + pos, sizeof(body) - pos, "To %s/%s\n", cat->rom_dir, target);
        if (plan->kind == CAT_PLAN_3DS_FILE)
            pos += snprintf(body + pos, sizeof(body) - pos,
                            UI_DIM "The server can't make a CIA: install the file with GodMode9 (Build CIA).\n");
        if (exists)
            pos += snprintf(body + pos, sizeof(body) - pos, "\nAlready on the SD: replace it?\n");
    }
    (void)pos;

    UiButton buttons[3];
    int n = 0;
    buttons[n++] = (UiButton){ KEY_B, "B", "Cancel" };
    if (cia && plan->file_fallback) buttons[n++] = (UiButton){ KEY_X, "X", "Save .3ds" };
    buttons[n++] = (UiButton){ KEY_A, "A", cia ? "Install" : "Download" };

    u32 key = ui_dialog(exists ? UI_TONE_WARN : UI_TONE_ACCENT, cia ? "Install game" : "Download game",
                        body, buttons, n);
    if (key & KEY_A) return cia ? MODE_CIA : MODE_FILE;
    if (key & KEY_X) return MODE_FILE;
    return -1;
}

// First lines of the server's error body, appended to `out`
static void append_error_body(const NetDlInfo *info, char *out, size_t size) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%s", info->error);
    char *p = msg;
    for (int lines = 0; *p && lines < 4; lines++) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        size_t len = strlen(out);
        snprintf(out + len, size - len, UI_DIM "%.120s\n", p);
        if (!nl) break;
        p = nl + 1;
    }
}

// Run one install; returns true if the user asked to retry as a file
static bool run_install(Catalog *cat, const CatEntry *e, const CatPlan *plan, InstallMode mode) {
    ui_job_settle();   // a cancelled job may still be talking to the server
    bool cia = (mode == MODE_CIA);
    char target[CAT_FILE_LEN];
    const char *ext = ".nds";
    if (system_is_3ds(cat)) {
        const char *dot = strrchr(e->filename, '.');
        ext = (dot && strcasecmp(dot, ".cci") == 0) ? ".cci" : ".3ds";
    } else if (strcasecmp(cat->systems[cat->sys], "DSI") == 0) {
        const char *dot = strrchr(e->filename, '.');
        if (dot && strcasecmp(dot, ".dsi") == 0) ext = ".dsi";
    }
    cat_target_name(e->filename, ext, target, sizeof(target));
    char path[MAX_PATH_LEN + CAT_FILE_LEN], part[MAX_PATH_LEN + CAT_FILE_LEN + 8];
    snprintf(path, sizeof(path), "%s/%s", cat->rom_dir, target);
    snprintf(part, sizeof(part), "%s.part", path);

    // A file save of a 3DS entry wants the raw cart, whatever the plan said
    const char *query = cia ? plan->query : (plan->kind == CAT_PLAN_NDS_EXTRACT ? plan->query : "");

    struct stat st;
    bool exists = !cia && stat(path, &st) == 0;

    UiProgress connecting = { 0 };
    connecting.title = cia ? "Installing CIA" : "Downloading";
    connecting.name = entry_name(e);
    connecting.status = "Connecting...";
    connecting.frac = -1;
    connecting.cancel_hint = true;
    ui_progress(&connecting);

    FILE *f = NULL;
    if (!cia) {
        mkdir_parents(cat->rom_dir);
        remove(part);
        f = fopen(part, "wb");
        if (!f) {
            char body[400];
            snprintf(body, sizeof(body), "Can't create the file on the SD card:\n" UI_DIM "%.300s", part);
            ui_message(UI_TONE_ERR, "Download failed", body);
            return false;
        }
        setvbuf(f, NULL, _IOFBF, 64 * 1024);
    }

    char id[CAT_ID_LEN * 3];
    cat_url_encode(e->rom_id, id, sizeof(id));
    char url_path[CAT_ID_LEN * 3 + 64];
    snprintf(url_path, sizeof(url_path), "/roms/%s%s", id, query);

    Download dl;
    memset(&dl, 0, sizeof(dl));
    dl.cat = cat;
    dl.name = entry_name(e);
    dl.cia = cia;
    dl.f = f;
    dl.replaced = exists ? (u64)st.st_size : 0;
    if (cia) dl.probe = malloc(CAT_CIA_PROBE_MAX);
    dl.start_ms = osGetTime();
    if (cia && !dl.probe) {
        ui_message(UI_TONE_ERR, "Install failed", "Out of memory.");
        return false;
    }

    bool sleep_allowed = aptIsSleepAllowed();
    aptSetSleepAllowed(false);
    NetDlInfo info;
    u32 max_wait = (cia && plan->kind == CAT_PLAN_CIA_CONVERT) ? WAIT_CONVERT_S : WAIT_PLAIN_S;
    NetDlResult rc = network_download(cat->config, url_path, max_wait,
                                      download_wait, download_sink, &dl, &info);
    aptSetSleepAllowed(sleep_allowed);
    u64 ms = osGetTime() - dl.start_ms;

    if (cia && rc == NET_DL_OK) {
        if (!dl.am_started) {
            rc = NET_DL_SHORT;  // empty body
        } else {
            UiProgress p = { 0 };
            p.title = "Installing CIA";
            p.name = entry_name(e);
            p.status = "Finishing the install...";
            p.frac = -1;
            ui_progress(&p);
            dl.am_result = AM_FinishCiaInstall(dl.handle);
            dl.am_started = false;
            if (R_FAILED(dl.am_result)) rc = NET_DL_SINK;
        }
    }
    if (dl.am_started) {
        AM_CancelCIAInstall(dl.handle);
        dl.am_started = false;
    }
    free(dl.probe);
    if (f) {
        bool closed_ok = (fclose(f) == 0);
        if (rc == NET_DL_OK && !closed_ok) rc = NET_DL_SINK;
    }

    if (rc != NET_DL_OK) {
        if (!cia) remove(part);
        char body[640] = "";
        char a[16], b[16];
        const char *title = cia ? "Install failed" : "Download failed";
        bool offer_file = false;
        UiTone tone = UI_TONE_ERR;
        switch (rc) {
            case NET_DL_CANCELLED:
                title = "Cancelled";
                tone = UI_TONE_ACCENT;
                snprintf(body, sizeof(body), "The %s was cancelled.", cia ? "install" : "download");
                break;
            case NET_DL_CONNECT:
                if (network_last_error()[0])
                    snprintf(body, sizeof(body), "No response from the server.\n" UI_DIM "%s",
                             network_last_error());
                else
                    snprintf(body, sizeof(body), "No response from the server.");
                break;
            case NET_DL_TIMEOUT:
                snprintf(body, sizeof(body), "The server took too long to answer.");
                break;
            case NET_DL_STATUS:
                offer_file = cia && plan->file_fallback && info.status == 503;
                snprintf(body, sizeof(body), "Server error (HTTP %lu)\n%s", (unsigned long)info.status,
                         offer_file ? "Save the .3ds file to the SD instead, to install with GodMode9?\n\n" : "");
                append_error_body(&info, body, sizeof(body));
                break;
            case NET_DL_SINK:
                if (dl.bad_data && cia)
                    snprintf(body, sizeof(body), "The server didn't send a CIA.");
                else if (dl.bad_data)
                    snprintf(body, sizeof(body), "The server sent a zip, not a ROM:\nupdate the server.");
                else if (dl.no_space)
                    snprintf(body, sizeof(body), "Not enough free space on the SD card.");
                else if (cia)
                    snprintf(body, sizeof(body), "The system refused the CIA.\n" UI_DIM "AM error %08lX",
                             (unsigned long)dl.am_result);
                else
                    snprintf(body, sizeof(body), "Writing to the SD card failed (card full?).");
                break;
            case NET_DL_SHORT:
                cat_format_size(info.received, a, sizeof(a));
                cat_format_size(info.total, b, sizeof(b));
                snprintf(body, sizeof(body), "Connection lost at %s of %s.", a, b);
                break;
            default:
                snprintf(body, sizeof(body), "Download failed.");
                break;
        }
        if (offer_file) {
            static const UiButton buttons[] = { { KEY_B, "B", "Back" }, { KEY_A, "A", "Save .3ds" } };
            return (ui_dialog(UI_TONE_WARN, "Can't convert to CIA", body, buttons, 2) & KEY_A) != 0;
        }
        ui_message(tone, title, body);
        return false;
    }

    if (!cia) {
        if (exists) remove(path);
        if (rename(part, path) != 0) {
            remove(part);
            ui_message(UI_TONE_ERR, "Download failed", "Couldn't rename the finished download.");
            return false;
        }
        cat_names_add(&cat->files, target);
        cat_names_sort(&cat->files);
        // Its save shows up in the save list after a rescan
        if (!system_is_3ds(cat) && cat->config->nds_dir[0]) cat->nds_installed = true;
    } else if (dl.probe_state == 1) {
        cat_cia_map_set(&cat->cia_map, e->rom_id, dl.tid);
        cat_cia_map_save(&cat->cia_map, CIA_MAP_PATH);
        load_installed_titles(cat);
    }

    char size[16], secs[16], avg[24], body[400];
    cat_format_size(info.received, size, sizeof(size));
    format_time((unsigned)(ms / 1000), secs, sizeof(secs));
    format_speed(bytes_per_second(info.received, ms), avg, sizeof(avg));
    const char *next = cia ? "Find it on the HOME Menu."
                     : system_is_3ds(cat) ? "In GodMode9, open the file and pick \"Build CIA from file\" to install it."
                     : "Start it from TWiLight Menu++.";
    snprintf(body, sizeof(body), UI_HI "%s\n" UI_DIM "%s in %s  \xC2\xB7  %s average\n\n%s",
             entry_name(e), size, secs, avg, next);
    ui_message(UI_TONE_OK, cia ? "Installed" : "Saved", body);
    return false;
}

static void install(Catalog *cat, const CatEntry *entry) {
    CatEntry e = *entry;  // the window may be refetched meanwhile
    CatPlan plan = cat_plan_install(cat->systems[cat->sys], &e);

    if (plan.kind == CAT_PLAN_NONE) {
        char body[480];
        snprintf(body, sizeof(body), UI_HI "%s\n\n%s", e.filename, plan.reason);
        ui_message(UI_TONE_ERR, "Can't install this game", body);
        return;
    }

    bool cia = (plan.kind == CAT_PLAN_CIA || plan.kind == CAT_PLAN_CIA_CONVERT);
    char target[CAT_FILE_LEN];
    cat_target_name(e.filename, system_is_3ds(cat) ? ".3ds" : ".nds", target, sizeof(target));
    bool exists = cia ? installed_tid(cat, &e) != 0 : file_on_sd(cat, &e);

    int mode = confirm_install(cat, &e, &plan, target, exists);
    if (mode < 0) return;
    if (run_install(cat, &e, &plan, (InstallMode)mode))
        run_install(cat, &e, &plan, MODE_FILE);  // CIA conversion unavailable
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

static void move_to(Catalog *cat, int index, bool wrap) {
    if (cat->total <= 0) return;
    if (wrap) {
        int wrapped = (index % cat->total + cat->total) % cat->total;
        if (wrapped != index)
            ui_list_edge(&cat->anim, cat->total > 1 ? UI_EDGE_WRAP
                                     : index < 0 ? UI_EDGE_CLAMP_TOP : UI_EDGE_CLAMP_BOTTOM);
        index = wrapped;
    } else {
        // A page press with nowhere left to go
        if (index < 0 && cat->selected == 0) ui_list_edge(&cat->anim, UI_EDGE_CLAMP_TOP);
        if (index >= cat->total && cat->selected == cat->total - 1) ui_list_edge(&cat->anim, UI_EDGE_CLAMP_BOTTOM);
        if (index < 0) index = 0;
        if (index >= cat->total) index = cat->total - 1;
    }
    cat->direction = (index > cat->selected) - (index < cat->selected);
    cat->selected = index;
    ensure_loaded(cat);
}

// The system keyboard (or the D-pad editor when it can't start)
static bool edit_search(char *search, size_t size) {
    UiTextField f = { "Search the catalog", "Search game names (empty = all)", "Search", false, false, true };
    return ui_edit_text_ex(&f, search, (int)size);
}

static Catalog *get_catalog(const AppConfig *config) {
    if (!g_cat) {
        Catalog *cat = calloc(1, sizeof(Catalog));
        CatEntry *win = cat ? malloc(CAT_WINDOW * sizeof(CatEntry)) : NULL;
        if (!win) {
            free(cat);
            return NULL;
        }
        cat->win = win;
        cat->total = -1;
        cat->bar = (UiBar)UI_BAR_INIT;
        cat->seg_tap = (UiTap)UI_TAP_INIT;
        cat->anim.pressed_row = -1;
        g_cat = cat;
    }
    g_cat->config = config;
    return g_cat;
}

// Load (or reload) the catalog and the installed state, then the list
static void open_catalog(Catalog *cat, LoadSummary *sum) {
    char current[8] = "";
    if (cat->nsystems > 0) snprintf(current, sizeof(current), "%s", cat->systems[cat->sys]);
    if (load_catalog(cat, sum) && cat->nsystems > 0) {
        // Stay on the system that was showing
        for (int i = 0; i < cat->nsystems; i++)
            if (strcmp(cat->systems[i], current) == 0) cat->sys = i;
        if (!cat->installed_loaded) {
            ui_busy("Game Catalog", "Looking for installed games...");
            cat_cia_map_load(&cat->cia_map, CIA_MAP_PATH);
            load_installed_titles(cat);
            cat->installed_loaded = true;
        }
        scan_installed(cat);
        reset_list(cat);
    }
}

// What each key does; a tap on its button calls the same function

// A: install the selected game, or try again after an error
static void cat_do_a(Catalog *cat) {
    if (!cat->ready || cat->nsystems == 0) {
        open_catalog(cat, NULL);
        return;
    }
    if (cat->error[0]) {
        // Retry: a broken cache file was removed, so reopening fetches
        // that system again; live mode refetches the page
        cat->error[0] = '\0';
        if (cat->cached) open_catalog(cat, NULL);
        else if (cat->total < 0) reset_list(cat);
        else fetch_window(cat, cat_window_start(cat->scroll, CAT_ROWS, CAT_WINDOW, cat->total, cat->direction));
        return;
    }
    if (cat->total <= 0) return;
    // A drag may have left the selection off screen: show it (and load it)
    ui_list_reveal(&cat->anim, cat->total, cat->selected, &cat->scroll);
    ensure_loaded(cat);
    const CatEntry *e = entry_at(cat, cat->selected);
    if (e) install(cat, e);
}

// X: only games with RetroAchievements / all games
static void cat_toggle_ra(Catalog *cat) {
    cat->ra_only = !cat->ra_only;
    reset_list(cat);
}

// Y: search
static void cat_search(Catalog *cat) {
    if (edit_search(cat->search, sizeof(cat->search))) reset_list(cat);
}

// B: clear the search
static void cat_clear_search(Catalog *cat) {
    if (!cat->search[0]) return;
    cat->search[0] = '\0';
    reset_list(cat);
}

// SELECT (next system): show system `index`
static void cat_set_system(Catalog *cat, int index) {
    if (cat->nsystems <= 1 || index == cat->sys) return;
    cat->sys = index;
    scan_installed(cat);
    reset_list(cat);
}

// Rows on screen after a drag or flick: cached rows are copied in as soon
// as they show; live pages are only fetched once the list has settled
// (fetch_window() blocks on the network)
static void follow_viewport(Catalog *cat) {
    if (!cat->ready || cat->total <= 0 || cat->error[0]) return;
    if (cat->anim.scroll.v > 0.5f) cat->direction = 1;
    else if (cat->anim.scroll.v < -0.5f) cat->direction = -1;
    int first = ui_list_first_visible(&cat->anim);
    if (cat->cached) {
        if (!cat_window_covers(cat->win_off, cat->win_count, first, CAT_ROWS + 1, cat->total))
            fetch_window(cat, cat_window_start(first, CAT_ROWS, CAT_WINDOW, cat->total, cat->direction));
    } else if (ui_list_settled(&cat->anim)) {
        ensure_window(cat, first);
    }
}

UiNav catalog_tab(const AppConfig *config, bool *saves_changed) {
    if (saves_changed) *saves_changed = false;
    Catalog *cat = get_catalog(config);
    if (!cat) {
        ui_message(UI_TONE_ERR, "Game Catalog", "Out of memory.");
        return UI_NAV_NEXT;
    }

    UiBackdrop prev = ui_set_backdrop(draw_top, draw_bottom, cat);
    if (!cat->ready) open_catalog(cat, NULL);
    hidSetRepeatParameters(20, 4);

    UiNav nav = UI_NAV_EXIT;  // the app is closing unless a key says otherwise
    hidScanInput();
    UiInput in;
    while (ui_input_scan(&in)) {
        u32 down = in.down;
        u32 rep = in.rep;

        UiNav n = ui_tab_nav(down);
        if (n != UI_NAV_STAY) {
            nav = n;
            break;
        }

        UiBarItem items[CAT_BAR_COUNT];
        int nitems = cat_bar(cat, items);
        int hit = ui_bar_input(&cat->bar, &in, items, nitems);
        GuiRect seg[CAT_MAX_SYSTEMS];
        int nseg = cat->ready ? cat_segments(cat, seg) : 0, seg_pressed = -1;
        int seg_hit = ui_tap_buttons(&cat->seg_tap, &in, seg, nseg, &seg_pressed);
        cat->tabs.held = seg_pressed + 1;

        if (!cat->ready || cat->nsystems == 0) {
            if (hit == CAT_BAR_A || (down & KEY_A)) cat_do_a(cat);
        } else {
            // Live mode: a coast lands inside the loaded window
            if (!cat->cached && cat->win_count > 0)
                ui_list_set_range(&cat->anim, cat->win_off, cat->win_off + cat->win_count - CAT_ROWS);
            else
                ui_list_clear_range(&cat->anim);
            int total = cat->total > 0 ? cat->total : 0;
            UiListEvent ev = ui_list_input(&cat->anim, &cat_geom, total, &cat->selected, &cat->scroll, &in,
                                           rep & UI_LIST_KEYS);
            if (ev.kind != UI_LIST_REATTACHED) {
                if (rep & KEY_DOWN) move_to(cat, cat->selected + 1, true);
                if (rep & KEY_UP) move_to(cat, cat->selected - 1, true);
                if (rep & KEY_RIGHT) move_to(cat, cat->selected + CAT_ROWS, false);
                if (rep & KEY_LEFT) move_to(cat, cat->selected - CAT_ROWS, false);
            }
            follow_viewport(cat);

            // A tap on the selected row installs (after the usual prompt)
            bool tap_install = ev.kind == UI_LIST_TAP_SELECTED && !cat->error[0];
            if (hit == CAT_BAR_X || (down & KEY_X)) cat_toggle_ra(cat);
            if (hit == CAT_BAR_Y || (down & (KEY_Y | KEY_ZR))) cat_search(cat);
            if (down & KEY_B) cat_clear_search(cat);
            if ((down & KEY_SELECT) && cat->nsystems > 1) cat_set_system(cat, (cat->sys + 1) % cat->nsystems);
            else if (seg_hit >= 0) cat_set_system(cat, seg_hit);
            if (hit == CAT_BAR_A || tap_install || (down & KEY_A)) cat_do_a(cat);
        }

        cat->held = in.held;
        draw_frame(cat);
        cat->held = 0;
        cat->tabs.held = 0;
    }

    ui_restore_backdrop(prev);
    if (saves_changed) *saves_changed = cat->nds_installed;
    cat->nds_installed = false;
    return nav;
}

static void wipe_cache(void) {
    for (int i = 0; cat_systems[i]; i++) {
        char path[MAX_PATH_LEN], part[MAX_PATH_LEN + 8];
        cache_path(cat_systems[i], path, sizeof(path));
        snprintf(part, sizeof(part), "%s.part", path);
        remove(path);
        remove(part);
    }
}

// GET /roms/scan on the worker: the server answers when its rescan is done
typedef struct {
    AppConfig config;
    u32 status;
    int count;
} ScanJob;

static bool scan_job(void *ctx) {
    ScanJob *j = ctx;
    u32 len = 0;
    u8 *body = network_get_timeout(&j->config, "/roms/scan", &len, &j->status, RESCAN_TIMEOUT_S);
    if (!body || j->status < 100 || j->status > 599) j->status = 0;
    j->count = (body && j->status == 200) ? cat_parse_scan_count((const char *)body, len) : -1;
    free(body);
    return true;
}

void catalog_refresh(const AppConfig *config, char *report, size_t size) {
    Catalog *cat = get_catalog(config);
    if (!cat) {
        snprintf(report, size, "Out of memory.");
        return;
    }

    // 1. Ask the server to walk its ROM folder again (it answers when done,
    // which can take minutes: the card offers Cancel once it has waited)
    u32 status = 0;
    int count = -1;
    ScanJob *j = ui_job_ctx(sizeof(ScanJob));
    if (j) {
        j->config = *config;
        UiProgress p = { 0 };
        p.title = "Refresh catalog";
        p.status = "Asking the server to rescan its ROMs...";
        p.frac = -1;
        bool cancelled = false;
        ui_run_job(scan_job, j, &p, true, &cancelled);
        if (cancelled) {
            // The cached catalog stays as it is
            snprintf(report, size, "Refresh cancelled.\n\n" UI_DIM
                     "The server may still finish its rescan. The cached catalog was kept; "
                     "run Refresh catalog again later to fetch it.");
            return;
        }
        status = j->status;
        count = j->count;
        ui_job_release(j);
    }
    char rescan[128];
    if (status == 200 && count >= 0)
        snprintf(rescan, sizeof(rescan), "Server rescan: %d ROMs.", count);
    else if (status == 200)
        snprintf(rescan, sizeof(rescan), "Server rescan: done.");
    else if (status == 403)
        snprintf(rescan, sizeof(rescan), "The server only lets admins rescan: fetched its current list.");
    else if (status == 404 || status == 405)
        snprintf(rescan, sizeof(rescan), "This server can't rescan on request: fetched its current list.");
    else if (status == 0)
        snprintf(rescan, sizeof(rescan), "Server rescan: no answer, continuing.");
    else
        snprintf(rescan, sizeof(rescan), "Server rescan failed (HTTP %lu), continuing.", (unsigned long)status);

    // 2. Throw the cached copy away and fetch every system again
    wipe_cache();
    LoadSummary sum;
    open_catalog(cat, &sum);

    int pos = snprintf(report, size, "%s\n\n", rescan);
    if (pos < 0 || (size_t)pos >= size) return;
    if (!cat->ready) {
        snprintf(report + pos, size - pos, UI_HI "Couldn't load the catalog:\n%s", cat->error);
    } else if (!cat->cached) {
        snprintf(report + pos, size - pos, UI_DIM "This server doesn't publish catalog fingerprints, "
                 "so the catalog isn't cached: it is read live from the server.");
    } else if (cat->nsystems == 0) {
        snprintf(report + pos, size - pos, "The server has no 3DS or DS games.");
    } else {
        pos += snprintf(report + pos, size - pos, "Downloaded:");
        for (int i = 0; i < cat->nsystems && pos < (int)size; i++)
            pos += snprintf(report + pos, size - pos, "%s %s %d", i ? "," : "", cat->systems[i],
                            sum.rows[i] >= 0 ? sum.rows[i] : 0);
        if (pos < (int)size) pos += snprintf(report + pos, size - pos, " games.");
        if (sum.failed && pos < (int)size)
            snprintf(report + pos, size - pos, "\n" UI_HI "%d system%s failed: %s", sum.failed,
                     sum.failed == 1 ? "" : "s", cat->notice[0] ? cat->notice : "see the catalog");
    }
}

void catalog_reset(void) {
    if (!g_cat) return;
    unload_lists(g_cat);
    cat_names_free(&g_cat->files);
    cat_cia_map_free(&g_cat->cia_map);
    free(g_cat->titles);
    g_cat->titles = NULL;
    g_cat->title_count = 0;
    g_cat->installed_loaded = false;
}

void catalog_exit(void) {
    if (!g_cat) return;
    catalog_reset();
    free(g_cat->win);
    free(g_cat);
    g_cat = NULL;
}
