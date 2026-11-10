/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * list.h - the files in EPlay's list (Erez, 3 Nov 2026: files added in
 * bulk, by dragging them onto the window or several at a time from the
 * menu; playlists later). Each is shown by its name until its tags and
 * length are read, one file a step in the background (one open a file, a
 * few short reads: the guidelines' 6.5).
 *
 * Its memory grows with it: records of 20 bytes, 64 to a block, and the
 * names and tags in blocks of 8 KB, taken as files come (an empty list
 * takes none: a stock Falcon under NVDI keeps the room the AES needs to
 * drop a menu: the hub, 4 Nov 2026).
 */
#ifndef EPLAY_LIST_H
#define EPLAY_LIST_H

#define LIST_MAX 1024
#include "order.h"
extern ORDER list_order;	/* Shuffle's, Repeat's: its size the list's (list.c keeps it) */
#define LIST_RESERVE (40L * 1024)	/* ST RAM left free for the AES, whatever the list */

typedef struct {
	const char *path;
	const char *title, *artist, *album;	/* "" until read (or none) */
	long len_ms;		/* 0: not known */
	unsigned char read;	/* its tags and length read */
	unsigned char bad;	/* not a file EPlay plays */
	unsigned char sel;	/* selected (D21: several may be) */
} ENTRY;

int list_count(void);
ENTRY *list_at(int i);
/* a file added at the end: its index, or -1 (the list full, or no
   memory); a file already in it isn't added again (its index) */
int list_add(const char *path);
void list_set_title(int i, const char *t);	/* a station's name for its row (phase 7) */
void list_remove(int i);
int list_remove_selected(void);	/* the selected rows out: the first of them, -1 none */
void list_move(int from, int to);	/* row from to row to, the rows between closing up */
void list_clear(void);
void list_keep_only(int i);	/* all but file i out (it stays, as the first) */
int list_find(const char *path);	/* its index, -1 if not in */
/* one file's tags and length read: the next not read yet. 1 if one was
   (its row to draw again: *row), 0 when none is left */
int list_step(int *row);
const char *list_name(const ENTRY *e);	/* its title, else its file's name */
long list_memory(void);		/* the bytes it takes (the log) */

extern int list_full;		/* a file was left out: no memory but the reserve (cleared by list_clear) */
extern int list_cur;		/* the one playing (or last played), -1 none */
extern int list_sel;		/* the row last clicked or moved to (the anchor of a
				   range, the one Return plays), -1 none */
/* the selection (D21, the guidelines' 5.8): any rows, each its sel; how
   many are (kept here, as rows come and go) */
extern int list_nsel;
void list_select(int i, int on);	/* row i selected or not */
void list_select_none(void);
void list_select_all(void);
extern int list_top;		/* the first row shown */

#endif
