/*
 * GameSync PS5 client - what the app and its helper payload agree on.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GAMESYNC_HELPER_PROTO_H
#define GAMESYNC_HELPER_PROTO_H

/* 127.0.0.1 only */
#define HELPER_PORT 9135
/* bumped whenever the request or reply lines change: a helper answering
 * another number says "E" and the app restarts it (helper_client.c) */
#define HELPER_PROTO 1

#endif
