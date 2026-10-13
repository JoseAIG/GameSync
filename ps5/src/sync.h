/*
 * GameSync PS5 client - comparing with the server and moving saves.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_SYNC_H
#define GAMESYNC_SYNC_H

#include "config.h"
#include "http.h"
#include "catalog.h"
#include "saves.h"

typedef void (*SyncProgressFn)(const char *message, int done, int total, void *user);

typedef struct
{
    Config config;
    HttpServer server;
    KvStore state;     /* title_id -> hash of the last synced version */
    KvStore ids;       /* SYSTEM|rom file -> title_id */
    KvStore installed; /* title_id -> emulator tag, for catalog installs */
    SaveList saves;
    bool compared;     /* statuses come from the server */
    char error[160];
    const Catalog *catalog; /* the UI's, when loaded: names .srm files for server-only saves */
    SyncProgressFn progress;
    void *progress_user;
} SyncContext;

void sync_init(SyncContext *ctx);
void sync_shutdown(SyncContext *ctx);

/* Local scan, title ids for RetroArch ROMs, then the server comparison.
 * false (with ctx->error) when the server couldn't be reached: the local
 * list is still filled. */
bool sync_refresh(SyncContext *ctx);

/* The three-way decision for one entry, from its hashes and the last synced one. */
SaveStatus sync_decide(const SaveEntry *entry, const char *last_synced_hash);

bool sync_upload(SyncContext *ctx, SaveEntry *entry);
bool sync_download(SyncContext *ctx, SaveEntry *entry);
/* Does what the status says: upload, download, or nothing. Conflicts are
 * left alone (the user picks a side). */
bool sync_apply(SyncContext *ctx, SaveEntry *entry);

typedef struct
{
    int uploaded;
    int downloaded;
    int conflicts;
    int failed;
} SyncSummary;

/* Every entry that needs it, except conflicts and server-only PS1/PS2 saves
 * for games that aren't installed here. */
SyncSummary sync_all(SyncContext *ctx);

#endif
