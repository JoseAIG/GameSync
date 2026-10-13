/*
 * GameSync PS5 client - entry point.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Syncs emulator saves (RetroArch, SwanStationPS5, PS5SX2) with a GameSync
 * server and installs games from its ROM catalog. PS5 game saves are not
 * handled by this version.
 */
#include "config.h"
#include "gfx.h"
#include "platform.h"
#include "ui.h"

#include <stdio.h>

int main(void)
{
    /* before anything starts a thread: the HEN grants /data to a
     * single-threaded process (see ps5_unlock.c) */
    char status[160] = "";
    bool listable = plat_unlock_data(status, sizeof(status));

    char error[160] = "";
    if (!gfx_init())
    {
        app_log("out of memory for the screen");
        return 1;
    }
    if (!plat_init(error, sizeof(error)))
    {
        app_log("start-up failed: %s", error);
        plat_notify(error);
        return 1;
    }
    ui_run(listable, status);
    plat_shutdown();
    return 0;
}
