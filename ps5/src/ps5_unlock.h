/*
 * GameSync PS5 client - asking the HEN to let this title list /data.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from PSXS5 / SwanStationPS5 by darkxex,
 * https://github.com/darkxex/SwanStationPS5 (src/platform/ps5_unlock.h),
 * GPL-3.0-or-later.
 */
#ifndef GAMESYNC_PS5_UNLOCK_H
#define GAMESYNC_PS5_UNLOCK_H

#include <stdbool.h>

typedef enum
{
    UNLOCK_OK,
    UNLOCK_ALREADY,
    UNLOCK_NO_ANSWER,
    UNLOCK_CANT_REQUEST,
    UNLOCK_SKIPPED_AFTER_CRASH,
} UnlockResult;

bool ps5_data_listable(void);
/* Call first thing in main(), before any thread exists. */
UnlockResult ps5_unlock_etahen(void);
void ps5_unlock_reset_crash_marker(void);
const char *ps5_unlock_describe(UnlockResult result);

#endif
