#ifndef NETWORK_H
#define NETWORK_H

#include "common.h"

// Initialize httpc service. Call once at startup.
bool network_init(void);

// Cleanup httpc service. Call at shutdown.
void network_exit(void);

// When server_url_alt is set, probe GET /status on server_url and, if it
// doesn't answer, on server_url_alt; use whichever answers (primary wins
// ties, and stays selected if neither answers). Returns true if a server
// answered.
bool network_select_server(AppConfig *config);

// GET /status on the address requests go to now (config_active_url): true
// when the server answers 200. `version` (may be NULL) gets the "version"
// it reports, "" if none.
bool network_probe_status(const AppConfig *config, char *version, int version_size);

// Did the server answer the most recent request? -1 nothing sent yet,
// 0 no answer (down / unreachable), 1 answered (any HTTP status).
int network_server_state(void);

// The console clock's offset from UTC in minutes: `configured`
// (utc_offset_minutes) when it isn't 0, else the offset learned from the
// server's clock (the Date header of its answers, to the quarter hour), else
// 0 until a server has answered.
int network_utc_offset(int configured);

// `fn` says whether the request running now should stop (a cancelled
// background job, or the app closing). Requests then give up while waiting
// for the server's answer, within about a second, and fail with
// network_last_error() "cancelled". NULL: never.
void network_set_abort_check(bool (*fn)(void));

// HTTP GET - returns malloc'd response body, sets out_size and out_status.
// Returns NULL on failure. Caller must free.
u8 *network_get(const AppConfig *config, const char *path,
                u32 *out_size, u32 *out_status);

// Same, waiting up to `response_timeout_s` for the server to start answering
// (0 = the default 15 s), for requests the server is slow to answer
// (e.g. a ROM folder rescan).
u8 *network_get_timeout(const AppConfig *config, const char *path,
                        u32 *out_size, u32 *out_status, u32 response_timeout_s);

// HTTP POST with binary body - returns malloc'd response body.
// Returns NULL on failure. Caller must free.
u8 *network_post(const AppConfig *config, const char *path,
                 const u8 *body, u32 body_size,
                 u32 *out_size, u32 *out_status);

// HTTP POST with JSON body - convenience wrapper.
u8 *network_post_json(const AppConfig *config, const char *path,
                      const char *json_body,
                      u32 *out_size, u32 *out_status);

// Short description of why the last request failed (empty string if the
// last request reached the server). Includes httpc result codes.
const char *network_last_error(void);

// ---------------------------------------------------------------------------
// Streaming download (ROM catalog): the body is handed to a sink chunk by
// chunk and never held in RAM as a whole.
// ---------------------------------------------------------------------------

typedef enum {
    NET_DL_OK = 0,
    NET_DL_CONNECT,     // couldn't connect / no response at all
    NET_DL_TIMEOUT,     // server never started answering within max_wait_s
    NET_DL_STATUS,      // HTTP status other than 200 (info->status, info->error)
    NET_DL_CANCELLED,   // wait or sink asked to stop
    NET_DL_SINK,        // sink failed (write error etc.)
    NET_DL_SHORT,       // connection lost before the whole body arrived
} NetDlResult;

typedef struct {
    u32 status;         // HTTP status code, 0 if none
    u64 total;          // Content-Length, 0 if unknown
    u64 received;       // body bytes passed to the sink
    char error[256];    // start of the error body for NET_DL_STATUS
} NetDlInfo;

// Called about ten times a second while the server prepares the response (a
// CIA conversion can take minutes), with the whole seconds waited so far.
// Return true to cancel.
typedef bool (*NetWaitCb)(u32 seconds, void *user);

// Called for every received chunk. Return 0 to continue, 1 to cancel,
// -1 on error.
typedef int (*NetSinkCb)(const u8 *data, u32 size, u64 done, u64 total, void *user);

#define NET_DL_CHUNK 0x10000  // 64 KB per receive

NetDlResult network_download(const AppConfig *config, const char *path,
                             u32 max_wait_s, NetWaitCb wait, NetSinkCb sink,
                             void *user, NetDlInfo *info);

#endif // NETWORK_H
