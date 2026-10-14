/*
 * GameSync PS5 client - a disk write-speed test, on request.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_DISKBENCH_H
#define GAMESYNC_DISKBENCH_H

#include <stdbool.h>

/* <data>/bench.request exists (made over FTP to ask for the test). */
bool diskbench_requested(void);
/* Runs the write tests into gamesync.log; tick(mode name) as each starts. */
void diskbench_run(void (*tick)(const char *mode, void *user), void *user);

#endif
