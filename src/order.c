/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * order.c - which file plays next (order.h).
 */
#include <string.h>
#include "order.h"

void order_init(ORDER *o, unsigned long seed)
{
	memset(o, 0, sizeof(*o));
	o->seed = seed ? seed : 1;
}

void order_clear(ORDER *o)
{
	memset(o->played, 0, sizeof(o->played));
	o->left = o->n;
	o->nh = 0;
}

void order_size(ORDER *o, int n)
{
	if (n > ORDER_MAX)
		n = ORDER_MAX;
	if (n < o->n) {
		o->n = n;
		order_clear(o);
		return;
	}
	o->left += n - o->n;		/* (the new ones not played) */
	o->n = n;
}

void order_played(ORDER *o, int i)
{
	if (i < 0 || i >= o->n)
		return;
	if (!o->played[i]) {
		o->played[i] = 1;
		o->left--;
	}
	if (o->nh == ORDER_MAX) {
		memmove(o->hist, o->hist + 1, sizeof(o->hist) - sizeof(o->hist[0]));
		o->nh--;
	}
	o->hist[o->nh++] = (short)i;
}

void order_removed(ORDER *o, int i)
{
	int k, j;
	if (i < 0 || i >= o->n)
		return;
	if (!o->played[i])
		o->left--;
	memmove(o->played + i, o->played + i + 1, (size_t)(o->n - i - 1));
	o->n--;
	o->played[o->n] = 0;
	for (k = j = 0; k < o->nh; k++)
		if (o->hist[k] != i)
			o->hist[j++] = (short)(o->hist[k] > i ? o->hist[k] - 1 : o->hist[k]);
	o->nh = j;
}

static unsigned long rnd(ORDER *o)
{
	o->seed = o->seed * 1103515245UL + 12345UL;
	return (o->seed >> 8) & 0xffffff;
}

/* shuffled: one not played this time round, at random (cur aside) */
static int pick(ORDER *o, int cur)
{
	int c = o->left - (cur >= 0 && cur < o->n && !o->played[cur]), i, k;
	if (c <= 0)
		return -1;
	k = (int)(rnd(o) % (unsigned long)c);
	for (i = 0; i < o->n; i++)
		if (!o->played[i] && i != cur && !k--)
			return i;
	return -1;
}

int order_next(ORDER *o, int cur, int ended)
{
	int i;
	if (o->n <= 0)
		return -1;
	if (ended && o->repeat == REP_ONE && cur >= 0 && cur < o->n)
		return cur;
	if (!o->shuffle) {
		if (cur + 1 < o->n)
			return cur + 1;
		return o->repeat != REP_OFF ? 0 : -1;
	}
	i = pick(o, cur);
	if (i < 0 && o->repeat != REP_OFF && o->n > 1) {
		/* all played: round again (not the one just played first) */
		order_clear(o);
		if (cur >= 0 && cur < o->n) {
			o->played[cur] = 1;
			o->left--;
			o->hist[o->nh++] = (short)cur;
		}
		i = pick(o, cur);
	} else if (i < 0 && o->repeat != REP_OFF)
		i = 0;
	return i;
}

int order_prev(ORDER *o, int cur)
{
	if (o->n <= 0)
		return -1;
	if (o->shuffle) {
		/* back the way it came: the one played before cur */
		int k = o->nh - 1;
		while (k >= 0 && o->hist[k] == cur)
			k--;
		if (k < 0)
			return -1;
		o->nh = k;		/* (order_played puts it back on as it starts) */
		return o->hist[k];
	}
	if (cur > 0)
		return cur - 1;
	return o->repeat != REP_OFF && o->n > 1 ? o->n - 1 : -1;
}

const char *order_repeat_name(int repeat)
{
	return repeat == REP_ALL ? "Repeat: all" : repeat == REP_ONE ? "Repeat: one" : "Repeat: off";
}

int order_has_next(const ORDER *o, int cur)
{
	if (o->n <= 0)
		return 0;
	if (o->repeat != REP_OFF)
		return 1;
	if (o->shuffle)
		return o->left - (cur >= 0 && cur < o->n && !o->played[cur]) > 0;
	return cur + 1 < o->n;
}

int order_has_prev(const ORDER *o, int cur)
{
	int k;
	if (o->n <= 0)
		return 0;
	if (o->shuffle) {
		for (k = o->nh - 1; k >= 0; k--)
			if (o->hist[k] != cur)
				return 1;
		return 0;
	}
	return cur > 0 || (o->repeat != REP_OFF && o->n > 1);
}
