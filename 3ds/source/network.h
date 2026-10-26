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

// HTTP GET - returns malloc'd response body, sets out_size and out_status.
// Returns NULL on failure. Caller must free.
u8 *network_get(const AppConfig *config, const char *path,
                u32 *out_size, u32 *out_status);

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

#endif // NETWORK_H
