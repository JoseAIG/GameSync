/*
 * GameSync PS5 client - the interface.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_UI_H
#define GAMESYNC_UI_H

#include <stdbool.h>

/* Runs the app until the user quits. data_status describes /data access. */
void ui_run(bool data_listable, const char *data_status);

#endif
