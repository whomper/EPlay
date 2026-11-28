/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * lib.h - the music library's one interface to the platforms that serve
 * one (Jellyfin first: libjf.c). A platform only makes requests and reads
 * the answers; the window, the network and EPlay's list are the library's
 * own, the same for every platform. Text in and out is Atari text; the
 * platform turns it to and from what its server speaks.
 *
 * A song in EPlay's list is a line "<tag>:<id>" (jellyfin:4f1c...): the
 * server, the user and the key come from Settings when it plays, so a
 * playlist never carries them.
 */
#ifndef LIB_H
#define LIB_H

#define LIB_ID 40			/* an item's id, as the server gives it */
#define LIB_NAME 96
#define LIB_SUB 64

enum { LK_ARTIST, LK_ALBUM, LK_PLAYLIST, LK_SONG, LK_NKINDS };

typedef struct {
	char id[LIB_ID];
	char name[LIB_NAME];		/* the artist, album, playlist or song */
	char artist[LIB_SUB];		/* a song's or an album's artist */
	char album[LIB_SUB];		/* a song's album */
	long len_ms;			/* a song's length, or an album's or a playlist's in all; 0 unknown */
	int kind;			/* LK_* */
	int count;			/* songs in an album or a playlist; 0 unknown */
	int year;			/* 0 unknown */
	int track;			/* a song's number on its album; 0 unknown */
	char pic[LIB_ID];		/* the item whose picture shows it (a song: its album's); "" none */
} LIB_ITEM;

/* who is signed in where; kept in EPLAY.INF but for the key, which only
   lives in EPLAY.INF's own key line and never in a playlist */
typedef struct {
	char server[128];		/* http://192.168.1.x:8096, no / at the end */
	char user[64];			/* the name signed in with */
	char uid[LIB_ID];		/* the server's id for the user */
	char token[72];			/* the key the sign-in gave; "" signed out */
	char device[40];		/* this Atari's id to the server (made once) */
	int kbps;			/* MP3 for the Falcon: 128, 192 or 320 */
} LIB_ACCOUNT;

/* what to list */
enum { LQ_ARTISTS, LQ_ALBUMS, LQ_PLAYLISTS, LQ_SONGS, LQ_IN };
typedef struct {
	int what;			/* LQ_*; LQ_IN: what is in parent */
	const char *parent;		/* LQ_IN: an artist (its albums), album or playlist (their songs) */
	int parent_kind;		/* LK_* of parent */
	const char *find;		/* names with this in them; 0 or "" for all */
	long start;			/* the first to give, from 0 */
	int count;			/* at most this many */
} LIB_QUERY;

/* a request: the library's network code sends it to account.server */
typedef struct {
	int post;			/* POST with body, else GET */
	char path[640];			/* from the server's root: /Users/... */
	char auth[320];			/* the Authorization header's value */
	char body[384];			/* POST: JSON */
} LIB_REQ;

typedef struct {
	const char *name;		/* as Settings shows it: "Jellyfin" */
	const char *tag;		/* a song line's prefix: "jellyfin" */
	/* 0, or -1 if it won't fit */
	int (*signin)(const LIB_ACCOUNT *a, const char *password, const char *version, LIB_REQ *r);
	/* the answer to signin: uid and token into a; 1 when signed in */
	int (*signed_in)(LIB_ACCOUNT *a, const char *text, long len);
	int (*list)(const LIB_ACCOUNT *a, const LIB_QUERY *q, const char *version, LIB_REQ *r);
	/* the answer to list: up to max into it; the number given; *total
	   the number there is in all (-1 unknown) */
	int (*items)(const char *text, long len, LIB_ITEM *it, int max, long *total);
	/* a song as MP3 at a->kbps, from at_ms on: the URL; -1 if it won't fit */
	int (*stream)(const LIB_ACCOUNT *a, const char *id, long at_ms, char *out, int n);
	/* an item's picture, px wide at most, as JPEG */
	int (*picture)(const LIB_ACCOUNT *a, const char *id, int px, char *out, int n);
} LIB_PLATFORM;

extern const LIB_PLATFORM lib_jellyfin;

/* the platforms, for Settings; 0 after the last */
const LIB_PLATFORM *lib_platform(int i);
/* by tag (a song line's prefix up to its ':'); 0 for none */
const LIB_PLATFORM *lib_by_tag(const char *tag, int n);

/* a song's line "<tag>:<id>" into out; -1 if it won't fit */
int lib_line(const LIB_PLATFORM *p, const char *id, char *out, int n);
/* a list line's platform and id (into id, LIB_ID bytes); 0 if it isn't one */
const LIB_PLATFORM *lib_line_id(const char *line, char *id);

/* text %-encoded (UTF-8) onto out at k: the new length, -1 if it won't fit */
int lib_enc(char *out, int k, int n, const char *atari);

#endif
