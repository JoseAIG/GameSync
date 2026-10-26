#ifndef UPDATE_H
#define UPDATE_H

#include "common.h"

// Update check result
typedef struct {
    bool available;
    char latest_version[16];
    char download_url[256];
    u32 file_size;
    char sha256[65];   // Expected SHA-256 of the CIA (hex), empty if not published
} UpdateInfo;

// Check for updates from server.
// Returns true if check succeeded (regardless of whether update is available).
bool update_check(const AppConfig *config, UpdateInfo *info);

// Download update to SD card.
// The download must match info->file_size and, if published, info->sha256.
// Returns true on success; error_out receives a description of what failed.
// Progress callback receives percentage (0-100).
typedef void (*UpdateProgressCb)(int percent);
bool update_download(const AppConfig *config, const UpdateInfo *info,
                     UpdateProgressCb progress, char *error_out, int error_size);

// Install CIA from SD card using AM service.
// Returns true on success.
// error_out receives a description of what failed (if not NULL).
bool update_install(UpdateProgressCb progress, char *error_out, int error_size);

// Relaunch the application (works for CIA apps only).
// This function does not return on success.
void update_relaunch(void);

#endif // UPDATE_H
