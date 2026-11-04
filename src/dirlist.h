/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dirlist.h - a folder's entries for the Add files dialog (ui/addfiles.c;
 * Erez, 4 Nov 2026: Selectric's ways, a selection, no check boxes): its
 * folders and files with their sizes and dates, as many as memory holds,
 * sorted (folders first) by name, date, size or type, and the view of
 * them a type and a mask (* and ?) let through, each file selected or
 * not. No TOS here: the dialog reads the folder and hands each entry in,
 * so the host can test it all.
 */
#ifndef EPLAY_DIRLIST_H
#define EPLAY_DIRLIST_H

typedef struct {
	char name[14];		/* 8.3, as GEMDOS gives it */
	long size;
	unsigned short date;	/* GEMDOS's: year - 1980 << 9 | month << 5 | day */
	unsigned short time;
	unsigned char dir;	/* a folder */
	unsigned char sel;	/* selected (files only) */
} DENT;

enum { DT_MUSIC, DT_MP3, DT_WAV, DT_AIFF, DT_ALL, DT_N };	/* the Type pop-up */
enum { DS_NAME, DS_DATE, DS_SIZE, DS_TYPE, DS_N };		/* the Sort pop-up */

typedef struct {
	DENT *e;
	int n, cap;		/* all the folder's entries, sorted */
	int *view, nview;	/* those shown: indexes into e */
	int type, sort;
	char mask[14];		/* "" or "*": everything */
} DLIST;

void dl_init(DLIST *d);
void dl_free(DLIST *d);
void dl_clear(DLIST *d);	/* its entries gone (another folder), the type, sort and mask kept */
/* an entry of the folder read ("." and ".." are left out): 0 when there's
   no memory for it */
int dl_add(DLIST *d, const char *name, long size, unsigned short date, unsigned short time, int dir);
/* sorted (folders first, then the files, each as d->sort has it), and
   the view made again from the type and the mask (folders always) */
void dl_sort(DLIST *d);
void dl_filter(DLIST *d);
DENT *dl_at(DLIST *d, int row);		/* the view's row: 0 past it */

int dl_match(const char *mask, const char *name);	/* * and ?, case aside */
int dl_type_ok(int type, const char *name);
const char *dl_type_name(int type);	/* "Music files", "MP3", ... (the pop-up) */
const char *dl_sort_name(int sort);	/* "By name", ... */

/* the selection: in the view's rows */
void dl_select_only(DLIST *d, int row);
void dl_toggle(DLIST *d, int row);
void dl_select_range(DLIST *d, int a, int b);	/* a to b, and only them */
void dl_select_all(DLIST *d, int on);
int dl_selected(const DLIST *d, long *bytes);	/* how many files, their bytes */

/* "7.1M", "64M", "512K", "12"; "03.11.26" */
void dl_size_text(long size, char *out);
void dl_date_text(unsigned short date, char *out);

#endif
