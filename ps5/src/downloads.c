/*
 * GameSync PS5 client - background download queue for catalog installs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Catalog's Cross queues a game and stays on the Catalog (as on the Steam
 * Deck and Android clients); one worker thread downloads the queue in order
 * while the UI keeps running. A paused download keeps its .part and resumes
 * with a Range request.
 */
#include "downloads.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    DlItem item;
    bool pause_requested;
    bool reported; /* a finished install handed to the main thread */
} Entry;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static Entry *entries;
static int count, cap;
static int next_id = 1;
static bool started;
static HttpServer server;

void downloads_set_server(const HttpServer *s)
{
    pthread_mutex_lock(&lock);
    server = *s;
    pthread_mutex_unlock(&lock);
}

static Entry *find(int id)
{
    for (int i = 0; i < count; ++i)
        if (entries[i].item.id == id)
            return &entries[i];
    return NULL;
}

typedef struct
{
    int id;
} Progress;

static int on_progress(uint64_t done, uint64_t total, void *user)
{
    Progress *p = user;
    pthread_mutex_lock(&lock);
    Entry *e = find(p->id);
    bool stop = !e || e->pause_requested;
    if (e)
    {
        e->item.done = done;
        if (total)
            e->item.total = total;
    }
    pthread_mutex_unlock(&lock);
    return stop ? 1 : 0;
}

static void *worker(void *arg)
{
    (void)arg;
    for (;;)
    {
        pthread_mutex_lock(&lock);
        Entry *e = NULL;
        while (!e)
        {
            for (int i = 0; i < count && !e; ++i)
                if (entries[i].item.state == DL_QUEUED)
                    e = &entries[i];
            if (!e)
                pthread_cond_wait(&wake, &lock);
        }
        e->item.state = DL_RUNNING;
        e->item.done = 0;
        e->item.error[0] = 0;
        e->pause_requested = false;
        DlItem job = e->item;
        HttpServer srv = server;
        pthread_mutex_unlock(&lock);

        Progress p = {job.id};
        char error[160] = "";
        bool ok = catalog_download(&srv, &job.rom, &job.plan, on_progress, &p, error,
                                   sizeof(error));

        pthread_mutex_lock(&lock);
        e = find(job.id);
        if (e)
        {
            if (ok)
                e->item.state = DL_DONE;
            else if (e->pause_requested)
                e->item.state = DL_PAUSED;
            else
            {
                e->item.state = DL_FAILED;
                snprintf(e->item.error, sizeof(e->item.error), "%s", error);
            }
            e->pause_requested = false;
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

bool downloads_add(const RomRow *rom, const InstallPlan *plan)
{
    pthread_mutex_lock(&lock);
    for (int i = 0; i < count; ++i)
    {
        DlItem *it = &entries[i].item;
        if (strcmp(it->rom.rom_id, rom->rom_id) == 0 &&
            (it->state == DL_QUEUED || it->state == DL_RUNNING))
        {
            pthread_mutex_unlock(&lock);
            return false;
        }
    }
    if (count == cap)
    {
        int ncap = cap ? cap * 2 : 16;
        Entry *grown = realloc(entries, (size_t)ncap * sizeof(Entry));
        if (!grown)
        {
            pthread_mutex_unlock(&lock);
            return false;
        }
        entries = grown;
        cap = ncap;
    }
    Entry *e = &entries[count++];
    memset(e, 0, sizeof(*e));
    e->item.id = next_id++;
    e->item.rom = *rom;
    e->item.plan = *plan;
    e->item.total = rom->size;
    e->item.state = DL_QUEUED;
    if (!started)
    {
        pthread_t t;
        started = pthread_create(&t, NULL, worker, NULL) == 0;
        if (started)
            pthread_detach(t);
    }
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    return true;
}

int downloads_snapshot(DlItem *out, int max)
{
    pthread_mutex_lock(&lock);
    int n = count < max ? count : max;
    for (int i = 0; i < n; ++i)
        out[i] = entries[i].item;
    pthread_mutex_unlock(&lock);
    return n;
}

int downloads_count(void)
{
    pthread_mutex_lock(&lock);
    int n = count;
    pthread_mutex_unlock(&lock);
    return n;
}

int downloads_active(void)
{
    pthread_mutex_lock(&lock);
    int n = 0;
    for (int i = 0; i < count; ++i)
        if (entries[i].item.state == DL_QUEUED || entries[i].item.state == DL_RUNNING)
            n++;
    pthread_mutex_unlock(&lock);
    return n;
}

void downloads_pause(int id)
{
    pthread_mutex_lock(&lock);
    Entry *e = find(id);
    if (e && e->item.state == DL_RUNNING)
        e->pause_requested = true;
    else if (e && e->item.state == DL_QUEUED)
        e->item.state = DL_PAUSED;
    pthread_mutex_unlock(&lock);
}

void downloads_resume(int id)
{
    pthread_mutex_lock(&lock);
    Entry *e = find(id);
    if (e && (e->item.state == DL_PAUSED || e->item.state == DL_FAILED))
    {
        e->item.state = DL_QUEUED;
        e->item.error[0] = 0;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}

static void remove_at(int i)
{
    memmove(&entries[i], &entries[i + 1], (size_t)(count - i - 1) * sizeof(Entry));
    count--;
}

void downloads_remove(int id)
{
    pthread_mutex_lock(&lock);
    for (int i = 0; i < count; ++i)
        if (entries[i].item.id == id && entries[i].item.state != DL_RUNNING)
        {
            remove_at(i);
            break;
        }
    pthread_mutex_unlock(&lock);
}

void downloads_clear_finished(void)
{
    pthread_mutex_lock(&lock);
    for (int i = count - 1; i >= 0; --i)
        if (entries[i].item.state == DL_DONE && entries[i].reported)
            remove_at(i);
    pthread_mutex_unlock(&lock);
}

bool downloads_take_completed(DlItem *out)
{
    bool found = false;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < count && !found; ++i)
        if (entries[i].item.state == DL_DONE && !entries[i].reported)
        {
            entries[i].reported = true;
            *out = entries[i].item;
            found = true;
        }
    pthread_mutex_unlock(&lock);
    return found;
}

const char *download_state_text(DlState state)
{
    switch (state)
    {
    case DL_QUEUED: return "Queued";
    case DL_RUNNING: return "Downloading";
    case DL_PAUSED: return "Paused";
    case DL_DONE: return "Installed";
    case DL_FAILED: return "Failed";
    }
    return "";
}
