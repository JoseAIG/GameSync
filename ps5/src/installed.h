/*
 * GameSync PS5 client - games installed in the PS5 emulators.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_INSTALLED_H
#define GAMESYNC_INSTALLED_H

#include "config.h"
#include "naming.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    Emulator emu;
    char system[12];
    char name[160];      /* file name without extension */
    char filename[200];
    char path[PATH_LEN];
    bool own_folder;     /* SwanStation: the game's folder holds only it */
    uint64_t size;
} InstalledGame;

typedef struct
{
    InstalledGame *items;
    int count;
    int cap;
} InstalledList;

/* RetroArch content/<system>/..., SwanStation games/<game>/..., PS5SX2 games/. */
void installed_scan(const Config *config, InstalledList *list);
void installed_free(InstalledList *list);
/* Deletes the game file (SwanStation: its whole game folder). */
bool installed_delete(const InstalledGame *game, char *error, size_t size);

#endif
