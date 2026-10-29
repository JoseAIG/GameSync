#include "network.h"
#include "root_certs.h"
#include "config.h"
#include "ui_format.h"
#include <3ds/svc.h>
#include <dirent.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define HTTP_BUF_SIZE 0x1000 // 4KB read chunks
#define MAX_RESPONSE  (2 * 1024 * 1024) // 2MB max response
#define MAX_POST_SIZE 0x70000 // 448KB max POST body (leave headroom in 512KB buffer)

#define TIMEOUT_RESPONSE (15ULL * 1000000000ULL) // 15s for server to respond
#define TIMEOUT_TRANSFER (30ULL * 1000000000ULL) // 30s per data chunk

#define CERTS_DIR      "sdmc:/3ds/3dssync/certs"  // Extra root CAs (*.der)
#define NETWORK_LOG    "sdmc:/3ds/3dssync/network.log"
#define MAX_LOG_SIZE   (64 * 1024)
#define MAX_SD_CERTS   8
#define MAX_CERT_SIZE  (8 * 1024)

// Description of the last failed request (empty if none), for the UI
static char last_error[96];

static void set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, args);
    va_end(args);
}

const char *network_last_error(void) {
    return last_error;
}

// Append a line to sdmc:/3ds/3dssync/network.log (restarted when it grows
// past MAX_LOG_SIZE). Used to diagnose connection and TLS problems on the
// console, where there is no other way to see httpc result codes.
static void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_line(const char *fmt, ...) {
    struct stat st;
    const char *mode = (stat(NETWORK_LOG, &st) == 0 && st.st_size > MAX_LOG_SIZE) ? "w" : "a";
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/3dssync", 0777);
    FILE *f = fopen(NETWORK_LOG, mode);
    if (!f) return;

    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);  // console clock (local time)
    char stamp[24] = "";
    if (tm) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(f, "[%s] ", stamp);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

// Record a failed request step for the UI and the log
static void request_failed(const char *url, bool https, const char *step, Result res) {
    if (https)
        set_error("%s failed: %08lX (TLS? see network.log)", step, res);
    else
        set_error("%s failed: %08lX", step, res);
    log_line("%s failed: result %08lX url=%s", step, res, url);
}

static bool is_https(const char *url) {
    return strncmp(url, "https://", 8) == 0;
}

// Root CAs loaded from CERTS_DIR (loaded once, on the first HTTPS request)
static struct {
    char name[64];
    u8 *der;
    u32 size;
} sd_certs[MAX_SD_CERTS];
static int sd_cert_count;
static bool sd_certs_loaded;

static void load_sd_certs(void) {
    if (sd_certs_loaded) return;
    sd_certs_loaded = true;

    DIR *dp = opendir(CERTS_DIR);
    if (!dp) return;

    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL && sd_cert_count < MAX_SD_CERTS) {
        size_t len = strlen(ent->d_name);
        if (len < 5 || strcasecmp(ent->d_name + len - 4, ".der") != 0) continue;

        char path[MAX_PATH_LEN + 32];
        snprintf(path, sizeof(path), CERTS_DIR "/%.200s", ent->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;

        u8 *der = (u8 *)malloc(MAX_CERT_SIZE);
        size_t size = der ? fread(der, 1, MAX_CERT_SIZE, f) : 0;
        bool too_big = der && size == MAX_CERT_SIZE && fgetc(f) != EOF;
        fclose(f);

        if (!der || size == 0 || too_big) {
            log_line("certs: skipped %s (%s)", ent->d_name,
                     !der ? "out of memory" : size == 0 ? "empty" : "larger than 8 KB");
            free(der);
            continue;
        }

        snprintf(sd_certs[sd_cert_count].name, sizeof(sd_certs[0].name), "%.63s", ent->d_name);
        sd_certs[sd_cert_count].der = der;
        sd_certs[sd_cert_count].size = (u32)size;
        sd_cert_count++;
    }
    closedir(dp);
}

// Roots in the 3DS system store that are still valid and in use
// (Sectigo/ZeroSSL chain to USERTrust/COMODO; DigiCert High Assurance EV)
static const SSLC_DefaultRootCert system_roots[] = {
    SSLC_DefaultRootCert_COMODO,
    SSLC_DefaultRootCert_USERTrust,
    SSLC_DefaultRootCert_DigiCert_EV,
};

// Configure certificate verification for one request.
// tls_verify=0 keeps the old behaviour (no verification at all). Otherwise
// the server certificate must chain to one of the still-valid system roots
// above, a built-in root (root_certs.c) or a DER file in CERTS_DIR.
static void setup_tls(const AppConfig *config, httpcContext *context) {
    if (!config->tls_verify) {
        httpcSetSSLOpt(context, SSLCOPT_DisableVerify);
        return;
    }

    // Log each certificate the SSL module rejects, but only once per run
    static bool logged;
    for (size_t i = 0; i < sizeof(system_roots) / sizeof(system_roots[0]); i++) {
        Result res = httpcAddDefaultCert(context, system_roots[i]);
        if (R_FAILED(res) && !logged)
            log_line("certs: system root %d rejected: %08lX", (int)system_roots[i], res);
    }
    for (int i = 0; i < root_cert_count; i++) {
        Result res = httpcAddTrustedRootCA(context, root_certs[i].der, root_certs[i].size);
        if (R_FAILED(res) && !logged)
            log_line("certs: built-in %s rejected: %08lX", root_certs[i].name, res);
    }

    load_sd_certs();
    for (int i = 0; i < sd_cert_count; i++) {
        Result res = httpcAddTrustedRootCA(context, sd_certs[i].der, sd_certs[i].size);
        if (R_FAILED(res) && !logged)
            log_line("certs: %s rejected: %08lX", sd_certs[i].name, res);
    }
    logged = true;
}

// Whether the server answered the last request: -1 unknown, 0 no, 1 yes
static int server_state = -1;

static void note_server(bool answered) {
    server_state = answered ? 1 : 0;
}

int network_server_state(void) {
    return server_state;
}

// The console clock's offset from UTC, learned from the Date header of the
// server's answers (requests run on the worker thread too: one aligned word
// each, read on the main thread)
static volatile int detected_offset;
static volatile bool offset_detected;

static void note_server_date(httpcContext *context) {
    char date[40];
    if (R_FAILED(httpcGetResponseHeader(context, "Date", date, sizeof(date)))) return;
    int64_t server_utc;
    if (!ui_parse_http_date(date, &server_utc)) return;
    // osGetTime(): ms since 1900 on the console's clock (local time, no zone)
    int64_t console = (int64_t)(osGetTime() / 1000ULL) - 2208988800LL;
    int minutes;
    if (!ui_clock_offset_minutes(console, server_utc, &minutes)) return;
    if (!offset_detected || detected_offset != minutes)
        log_line("console clock is UTC%+d min (from the server's Date)", minutes);
    detected_offset = minutes;
    offset_detected = true;
}

int network_utc_offset(int configured) {
    if (configured != 0) return configured;
    return offset_detected ? detected_offset : 0;
}

// Whether the request running now should stop (its job was cancelled, or
// the app is closing). NULL = never.
static bool (*abort_check)(void);

void network_set_abort_check(bool (*fn)(void)) {
    abort_check = fn;
}

static bool aborted(void) {
    return abort_check && abort_check();
}

// Brief delay between requests to let httpc clean up
static void request_delay(void) {
    svcSleepThread(50000000LL); // 50ms
}

bool network_init(void) {
    // Shared memory size for POST data (512KB)
    return R_SUCCEEDED(httpcInit(0x80000));
}

void network_exit(void) {
    httpcExit();
}

// Build full URL from config base + path
// (a base typed with a trailing '/' would give "//api/v1", which the server
// answers 404 to everything: the slashes are dropped)
static void build_url(const AppConfig *config, const char *path, char *url, int url_size) {
    const char *base = config_active_url(config);
    int len = (int)strlen(base);
    while (len > 0 && base[len - 1] == '/') len--;
    snprintf(url, url_size, "%.*s/api/v1%s", len, base, path);
}

// Download data with timeout - wraps httpcReceiveDataTimeout + httpcGetDownloadSizeState
static Result download_data_timeout(httpcContext *context, u8 *buffer, u32 size, u32 *downloadedsize, u64 timeout) {
    u32 pos_before = 0, contentsize = 0;
    Result ret = httpcGetDownloadSizeState(context, &pos_before, &contentsize);
    if (R_FAILED(ret)) return ret;

    ret = httpcReceiveDataTimeout(context, buffer, size, timeout);

    u32 pos_after = 0;
    httpcGetDownloadSizeState(context, &pos_after, &contentsize);
    if (downloadedsize) *downloadedsize = pos_after - pos_before;

    return ret;
}

// Read full response body with dynamic buffer
static u8 *read_response(httpcContext *context, u32 *out_size) {
    u32 size = 0;
    u32 buf_cap = HTTP_BUF_SIZE;
    u8 *buf = (u8 *)malloc(buf_cap);
    if (!buf) {
        set_error("out of memory");
        return NULL;
    }

    Result res;
    do {
        // Grow buffer if needed
        if (size + HTTP_BUF_SIZE > buf_cap) {
            buf_cap *= 2;
            if (buf_cap > MAX_RESPONSE) {
                set_error("response larger than %d MB", MAX_RESPONSE / (1024 * 1024));
                free(buf);
                return NULL;
            }
            u8 *new_buf = (u8 *)realloc(buf, buf_cap);
            if (!new_buf) {
                set_error("out of memory");
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }

        u32 read = 0;
        res = download_data_timeout(context, buf + size, HTTP_BUF_SIZE, &read, TIMEOUT_TRANSFER);
        size += read;

        if (res == (Result)HTTPC_RESULTCODE_TIMEDOUT) {
            set_error("timed out receiving data");
            free(buf);
            return NULL;
        }
    } while (res == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

    if (R_FAILED(res) && res != HTTPC_RESULTCODE_DOWNLOADPENDING) {
        set_error("receive failed: %08lX", res);
        free(buf);
        return NULL;
    }

    *out_size = size;
    return buf;
}

// Open a context for url and set TLS options and the common headers.
// `send_key`: add X-API-Key (every endpoint but GET /status needs it; the
// status probe also goes to an address that may not be the server yet).
// Returns false (with the error recorded) if the context can't be opened.
static bool open_request(const AppConfig *config, httpcContext *context,
                         HTTPC_RequestMethod method, const char *url, bool send_key) {
    Result res = httpcOpenContext(context, method, url, 0);
    if (R_FAILED(res)) {
        request_failed(url, is_https(url), "open", res);
        return false;
    }

    if (is_https(url))
        setup_tls(config, context);
    else
        httpcSetSSLOpt(context, SSLCOPT_DisableVerify);  // Not used over http://
    httpcSetKeepAlive(context, HTTPC_KEEPALIVE_DISABLED);
    httpcAddRequestHeaderField(context, "User-Agent", "3DSSaveSync/" APP_VERSION);
    if (send_key) httpcAddRequestHeaderField(context, "X-API-Key", config->api_key);
    httpcAddRequestHeaderField(context, "X-Console-ID", config->console_id);
    httpcAddRequestHeaderField(context, "Connection", "close");
    return true;
}

// Send the request, wait for the status code and read the body.
// Always closes the context. Returns NULL (with the error recorded) on failure.
static u8 *finish_request(httpcContext *context, const char *url,
                          u32 *out_size, u32 *out_status, u64 response_timeout) {
    // The TLS handshake (and certificate check) happens here for https://
    Result res = httpcBeginRequest(context);
    if (R_FAILED(res)) {
        note_server(false);
        request_failed(url, is_https(url), "connect", res);
        httpcCancelConnection(context);
        httpcCloseContext(context);
        return NULL;
    }

    // Wait in one-second slices so a cancelled request stops within a
    // second instead of holding the next request (and app exit) for up to
    // the whole timeout (300 s for a ROM rescan)
    u64 left = response_timeout;
    for (;;) {
        u64 slice = left < 1000000000ULL ? left : 1000000000ULL;
        res = httpcGetResponseStatusCodeTimeout(context, out_status, slice);
        if (res != (Result)HTTPC_RESULTCODE_TIMEDOUT) break;
        left -= slice;
        if (left == 0) break;
        if (aborted()) {
            set_error("cancelled");
            log_line("cancelled while waiting: url=%s", url);
            httpcCancelConnection(context);
            httpcCloseContext(context);
            return NULL;
        }
    }
    note_server(R_SUCCEEDED(res));
    if (R_FAILED(res)) {
        request_failed(url, is_https(url), "response", res);
        httpcCancelConnection(context);
        httpcCloseContext(context);
        return NULL;
    }

    note_server_date(context);
    u8 *body = read_response(context, out_size);
    if (!body) log_line("read failed: %s url=%s", last_error, url);
    httpcCancelConnection(context);
    httpcCloseContext(context);
    return body;
}

u8 *network_get(const AppConfig *config, const char *path,
                u32 *out_size, u32 *out_status) {
    return network_get_timeout(config, path, out_size, out_status, 0);
}

u8 *network_get_timeout(const AppConfig *config, const char *path,
                        u32 *out_size, u32 *out_status, u32 response_timeout_s) {
    u64 response_timeout = response_timeout_s ? response_timeout_s * 1000000000ULL : TIMEOUT_RESPONSE;
    request_delay(); // Let previous request fully clean up
    last_error[0] = '\0';

    char url[MAX_URL_LEN + 128];
    build_url(config, path, url, sizeof(url));

    httpcContext context;
    if (!open_request(config, &context, HTTPC_METHOD_GET, url, strcmp(path, "/status") != 0)) return NULL;
    return finish_request(&context, url, out_size, out_status, response_timeout);
}

// POST a body with the given content type
static u8 *post_data(const AppConfig *config, const char *path,
                     const u8 *body, u32 body_size, const char *content_type,
                     u32 *out_size, u32 *out_status) {
    char url[MAX_URL_LEN + 128];
    build_url(config, path, url, sizeof(url));

    httpcContext context;
    if (!open_request(config, &context, HTTPC_METHOD_POST, url, true)) return NULL;
    httpcAddRequestHeaderField(&context, "Content-Type", content_type);

    Result res = httpcAddPostDataRaw(&context, (u32 *)body, body_size);
    if (R_FAILED(res)) {
        request_failed(url, false, "post data", res);
        httpcCancelConnection(&context);
        httpcCloseContext(&context);
        return NULL;
    }

    return finish_request(&context, url, out_size, out_status, TIMEOUT_RESPONSE);
}

u8 *network_post(const AppConfig *config, const char *path,
                 const u8 *body, u32 body_size,
                 u32 *out_size, u32 *out_status) {
    request_delay(); // Let previous request fully clean up
    last_error[0] = '\0';

    // Reject oversized POST bodies that would overflow httpc buffer
    if (body_size > MAX_POST_SIZE) {
        set_error("upload larger than %d KB", MAX_POST_SIZE / 1024);
        return NULL;
    }

    return post_data(config, path, body, body_size, "application/octet-stream",
                     out_size, out_status);
}

u8 *network_post_json(const AppConfig *config, const char *path,
                      const char *json_body,
                      u32 *out_size, u32 *out_status) {
    request_delay(); // Let previous request fully clean up
    last_error[0] = '\0';

    u32 json_len = strlen(json_body);
    if (json_len > MAX_POST_SIZE) {
        set_error("request larger than %d KB", MAX_POST_SIZE / 1024);
        return NULL;
    }

    return post_data(config, path, (const u8 *)json_body, json_len, "application/json",
                     out_size, out_status);
}

// The "version" string of a GET /status answer (a flat JSON object)
static void status_version(const u8 *body, u32 size, char *out, int out_size) {
    out[0] = '\0';
    char text[256];
    u32 n = size < sizeof(text) - 1 ? size : sizeof(text) - 1;
    memcpy(text, body, n);
    text[n] = '\0';
    const char *p = strstr(text, "\"version\"");
    if (!p) return;
    p += strlen("\"version\"");
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) out[i++] = *p++;
    out[i] = '\0';
}

bool network_probe_status(const AppConfig *config, char *version, int version_size) {
    u32 size = 0, status = 0;
    if (version && version_size > 0) version[0] = '\0';
    u8 *resp = network_get(config, "/status", &size, &status);
    bool ok = resp && status == 200;
    if (ok && version && version_size > 0) status_version(resp, size, version, version_size);
    if (resp) free(resp);
    return ok;
}

static bool probe_status(const AppConfig *config) {
    return network_probe_status(config, NULL, 0);
}

bool network_select_server(AppConfig *config) {
    config->use_alt_url = false;
    if (probe_status(config)) return true;
    if (!config->server_url_alt[0]) return false;

    config->use_alt_url = true;
    if (probe_status(config)) {
        log_line("primary server unreachable, using server_url_alt=%s", config->server_url_alt);
        return true;
    }
    config->use_alt_url = false;  // neither answered: keep the primary
    log_line("no server answered (primary=%s alt=%s)", config->server_url, config->server_url_alt);
    return false;
}

// ---------------------------------------------------------------------------
// Streaming download
// ---------------------------------------------------------------------------

static void close_context(httpcContext *context) {
    httpcCancelConnection(context);
    httpcCloseContext(context);
}

NetDlResult network_download(const AppConfig *config, const char *path,
                             u32 max_wait_s, NetWaitCb wait, NetSinkCb sink,
                             void *user, NetDlInfo *info) {
    memset(info, 0, sizeof(*info));
    request_delay();
    last_error[0] = '\0';

    char url[MAX_URL_LEN + 1024];
    build_url(config, path, url, sizeof(url));

    // Same TLS setup (tls_verify) and headers as every other request
    httpcContext context;
    if (!open_request(config, &context, HTTPC_METHOD_GET, url, true))
        return NET_DL_CONNECT;

    // The TLS handshake (and certificate check) happens here for https://
    Result begin = httpcBeginRequest(&context);
    if (R_FAILED(begin)) {
        note_server(false);
        request_failed(url, is_https(url), "connect", begin);
        close_context(&context);
        return NET_DL_CONNECT;
    }

    // The server only answers once a conversion (?extract=cia) is done, so
    // wait in short slices and let the caller show progress / cancel. Short
    // enough that the caller sees a normal B press or tap on Cancel (it
    // samples the input when called), not only a held one.
    u32 status = 0;
    u32 slices = 0, waited = 0;
    while (1) {
        Result res = httpcGetResponseStatusCodeTimeout(&context, &status, 100000000ULL);  // 100 ms
        if (R_SUCCEEDED(res)) break;
        if (res != (Result)HTTPC_RESULTCODE_TIMEDOUT) {
            note_server(false);
            request_failed(url, is_https(url), "response", res);
            close_context(&context);
            return NET_DL_CONNECT;
        }
        waited = ++slices / 10;
        if (wait && wait(waited, user)) {
            close_context(&context);
            return NET_DL_CANCELLED;
        }
        if (waited >= max_wait_s) {
            close_context(&context);
            return NET_DL_TIMEOUT;
        }
    }
    info->status = status;
    note_server(true);

    u8 *buf = (u8 *)malloc(NET_DL_CHUNK);
    if (!buf) {
        close_context(&context);
        return NET_DL_SINK;
    }

    if (status != 200) {
        // Keep the start of the error text (FastAPI sends plain text/JSON)
        u32 got = 0;
        download_data_timeout(&context, buf, sizeof(info->error) - 1, &got, TIMEOUT_RESPONSE);
        if (got > sizeof(info->error) - 1) got = sizeof(info->error) - 1;
        memcpy(info->error, buf, got);
        info->error[got] = '\0';
        free(buf);
        close_context(&context);
        return NET_DL_STATUS;
    }

    u32 pos = 0, content = 0;
    httpcGetDownloadSizeState(&context, &pos, &content);
    info->total = content;

    NetDlResult result = NET_DL_OK;
    Result res;
    do {
        u32 got = 0;
        res = download_data_timeout(&context, buf, NET_DL_CHUNK, &got, TIMEOUT_TRANSFER);
        if (got > 0) {
            info->received += got;
            int s = sink(buf, got, info->received, info->total, user);
            if (s != 0) {
                result = (s > 0) ? NET_DL_CANCELLED : NET_DL_SINK;
                break;
            }
        }
        if (res == (Result)HTTPC_RESULTCODE_TIMEDOUT) {
            result = NET_DL_SHORT;
            break;
        }
    } while (res == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

    if (result == NET_DL_OK) {
        if (R_FAILED(res) && res != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING)
            result = NET_DL_SHORT;
        else if (info->total && info->received < info->total)
            result = NET_DL_SHORT;
    }

    free(buf);
    close_context(&context);
    return result;
}
