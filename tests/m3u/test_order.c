/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_order.c - src/order.c: the list's order and Shuffle's, Repeat off,
 * all and one, Previous, rows taken out and added.
 */
#include <stdio.h>
#include <string.h>
#include "order.h"

static int bad;
static void check(int ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	bad += !ok;
}

/* from first, n steps of Next (ended: by themselves) */
static const char *run(ORDER *o, int first, int steps, int ended)
{
	static char s[400];
	int cur = first, k;
	s[0] = 0;
	order_played(o, cur);
	sprintf(s, "%d", cur);
	for (k = 0; k < steps; k++) {
		cur = order_next(o, cur, ended);
		sprintf(s + strlen(s), " %d", cur);
		if (cur < 0)
			break;
		order_played(o, cur);
	}
	return s;
}

int main(void)
{
	ORDER o;
	int i, ok, seen[8];
	order_init(&o, 7);
	order_size(&o, 4);
	check(!strcmp(run(&o, 0, 4, 1), "0 1 2 3 -1"), "in order, Repeat off: the end after the last");
	order_clear(&o);
	o.repeat = REP_ALL;
	check(!strcmp(run(&o, 2, 4, 1), "2 3 0 1 2"), "Repeat all: the first again after the last");
	order_clear(&o);
	o.repeat = REP_ONE;
	check(!strcmp(run(&o, 1, 2, 1), "1 1 1"), "Repeat one: the file again when it ends");
	order_clear(&o);
	check(!strcmp(run(&o, 1, 2, 0), "1 2 3"), "Repeat one: Next still goes on");
	o.repeat = REP_OFF;
	check(order_prev(&o, 0) < 0 && (o.repeat = REP_ALL, order_prev(&o, 0) == 3) && order_prev(&o, 2) == 1,
	      "Previous: the one before; the last from the first with Repeat all");
	/* shuffled: each once, then the end (Repeat off) */
	order_init(&o, 12345);
	order_size(&o, 8);
	o.shuffle = 1;
	memset(seen, 0, sizeof(seen));
	{
		int cur = 3, k;
		order_played(&o, cur);
		seen[cur]++;
		for (k = 0; k < 7; k++) {
			cur = order_next(&o, cur, 1);
			if (cur >= 0) {
				seen[cur]++;
				order_played(&o, cur);
			}
		}
		ok = order_next(&o, cur, 1) < 0;
		for (i = 0; i < 8; i++)
			ok &= seen[i] == 1;
		check(ok, "Shuffle: each of 8 once, then the end (Repeat off)");
		/* Previous: back the way it came */
		{
			int h[8], n = o.nh, b, j;
			for (j = 0; j < 8; j++)
				h[j] = o.hist[j];
			b = order_prev(&o, cur);
			check(n == 8 && b == h[6] && order_prev(&o, b) == h[5], "Shuffle: Previous back the way it came");
		}
		o.repeat = REP_ALL;
		order_clear(&o);
		order_played(&o, cur);
		memset(seen, 0, sizeof(seen));
		{
			/* rounds of 7 after the first file: in each, 7 different files,
			   none the one the round began from (the last of the one before) */
			int r, from = cur;
			ok = 1;
			for (r = 0; r < 3 && ok; r++) {
				memset(seen, 0, sizeof(seen));
				for (k = 0; k < 7; k++) {
					int nx = order_next(&o, cur, 1);
					ok &= nx >= 0 && nx != cur && nx != from && !seen[nx];
					if (!ok)
						break;
					seen[nx] = 1;
					order_played(&o, nx);
					cur = nx;
				}
				from = cur;
			}
		}
		check(ok, "Shuffle with Repeat all: rounds of every other file once, never one twice running");
	}
	/* rows out and in */
	order_init(&o, 3);
	order_size(&o, 5);
	order_played(&o, 0);
	order_played(&o, 3);
	order_removed(&o, 1);
	check(o.n == 4 && o.played[0] && o.played[2] && !o.played[1] && o.left == 2 && o.nh == 2 && o.hist[1] == 2,
	      "a row taken out: the played ones move up with their rows");
	order_size(&o, 6);
	check(o.n == 6 && o.left == 4, "rows added: not played yet");
	o.shuffle = 1;
	for (i = 0, ok = 1; i < 4; i++) {
		int nx = order_next(&o, 2, 1);
		ok &= nx >= 0 && !o.played[nx];
		order_played(&o, nx);
	}
	check(ok && order_next(&o, 2, 1) < 0, "Shuffle after: only the unplayed, then the end");
	check(!strcmp(order_repeat_name(REP_ALL), "Repeat: all"), "the menu's names");
	order_init(&o, 1);
	check(order_next(&o, -1, 0) < 0 && order_prev(&o, -1) < 0, "an empty list: none");
	return bad != 0;
}
