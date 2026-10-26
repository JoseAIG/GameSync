/*
 * GameSync PS5 client - background download queue for catalog installs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_DOWNLOADS_H
#define GAMESYNC_DOWNLOADS_H

#include "catalog.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    DL_QUEUED,
    DL_RUNNING,
    DL_PAUSED,
    DL_DONE,
    DL_FAILED,
} DlState;

typedef struct
{
    int id;
    RomRow rom;
    InstallPlan plan;
    DlState state;
    uint64_t done;  /* bytes this attempt */
    uint64_t total;
    double speed;   /* bytes/s over the last second or two, 0 = not known yet */
    char error[160];
} DlItem;

/* One worker thread, started on the first add. */
void downloads_set_server(const HttpServer *server);
/* false when the same ROM is already queued or running. */
bool downloads_add(const RomRow *rom, const InstallPlan *plan);
/* Copies the queue (oldest first) into out; returns the count. */
int downloads_snapshot(DlItem *out, int max);
int downloads_count(void);
int downloads_active(void); /* queued + running */

void downloads_pause(int id);  /* running or queued -> paused (the .part stays) */
void downloads_resume(int id); /* paused/failed -> queued */
void downloads_remove(int id); /* not while running: pause first */
void downloads_clear_finished(void);

/* Hands the main thread one finished install it hasn't seen yet. */
bool downloads_take_completed(DlItem *out);

const char *download_state_text(DlState state);

#endif
