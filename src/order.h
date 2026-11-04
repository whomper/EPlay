/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * order.h - which file of the list plays next (phase 4; Erez, 4 Nov 2026):
 * the list's order, or Shuffle's (a random order, the list as arranged:
 * each file once before any again); Repeat off, all (the list again after
 * its last), or one (the file playing again when it ends; Next still goes
 * on). Previous goes back the way it came while shuffled.
 */
#ifndef EPLAY_ORDER_H
#define EPLAY_ORDER_H

#define ORDER_MAX 1024		/* the list's most (list.h's LIST_MAX) */

enum { REP_OFF, REP_ALL, REP_ONE };

typedef struct {
	int n;				/* the list's files */
	int shuffle, repeat;
	int left;			/* shuffled: files not yet played this time round */
	int nh;				/* shuffled: the files played, in turn (Previous's way back) */
	unsigned long seed;
	unsigned char played[ORDER_MAX];
	short hist[ORDER_MAX];
} ORDER;

void order_init(ORDER *o, unsigned long seed);
void order_size(ORDER *o, int n);	/* the list has n files now (added at its end) */
void order_clear(ORDER *o);		/* a new time round: none played */
void order_played(ORDER *o, int i);	/* file i begins to play */
void order_removed(ORDER *o, int i);	/* row i taken out of the list */
/* the file after cur (-1 none; cur -1: the first). ended: cur came to its
   end by itself (Repeat one plays it again), else Next */
int order_next(ORDER *o, int cur, int ended);
int order_prev(ORDER *o, int cur);	/* the file before cur, -1 none */
const char *order_repeat_name(int repeat);	/* "Repeat: off" ... (the Play menu's) */
int order_has_next(const ORDER *o, int cur);	/* Next would go somewhere (its button lit) */
int order_has_prev(const ORDER *o, int cur);

#endif
