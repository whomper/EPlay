/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * huff68.c - a granule's Huffman codes on the 68030 (huff68.h). The same
 * tables as the DSP's (t_huff, t_hsel: tools/mkmp3tab.py): a lookup of n
 * bits at a table's root, then of up to 4 at each node below. The bits
 * are held at the top of a long, refilled a byte at a time to 25 or more
 * before each lookup (a step takes 19 bits at most, linbits 13), so no
 * read of a bit is a loop of its own.
 */
#include <string.h>
#include "huff68.h"
#include "mp3tab.h"

typedef struct {
	const unsigned char *p;		/* the next byte to come in */
	unsigned int c;			/* the bits, from the top (32 of them) */
	int left;			/* how many of them */
	long pos;			/* the bit at c's top */
} HB;

#define REFILL(b) \
	while ((b)->left <= 24) { \
		(b)->c |= (unsigned int)*(b)->p++ << (24 - (b)->left); \
		(b)->left += 8; \
	}
#define PEEK(b, n) ((b)->c >> (32 - (n)))
#define SKIP(b, n) do { (b)->c <<= (n); (b)->left -= (n); (b)->pos += (n); } while (0)

/* one code from the table sel: its value, or -1 */
static int code(HB *b, unsigned long sel)
{
	int n = (int)(sel >> 16) & 15;
	const unsigned long *t = t_huff + (sel & 0xffff);
	for (;;) {
		unsigned long e;
		REFILL(b);
		e = t[PEEK(b, n)];
		if (e & 0x800000) {
			SKIP(b, n);
			n = (int)(e >> 16) & 15;
			t = t_huff + (e & 0xffff);
			continue;
		}
		if (!e)
			return -1;
		SKIP(b, (int)(e >> 16) & 31);
		return (int)(e & 0xff);
	}
}

/* x (0..15), with linbits lin more when it is 15, and its sign */
static int value(HB *b, int x, int lin)
{
	if (x == 15 && lin) {
		REFILL(b);
		x += (int)PEEK(b, lin);
		SKIP(b, lin);
	}
	if (x) {
		REFILL(b);
		if (b->c & 0x80000000U)
			x = -x;
		SKIP(b, 1);
	}
	return x;
}

void huff68_granule(const unsigned long *g, const unsigned char *d, long start, long end, short *is)
{
	HB b;
	int bv = (int)g[1], r1 = (int)g[6], r2 = (int)g[7], i = 0, k, v;
	unsigned long sel[3], s1;

	memset(is, 0, 576 * sizeof(short));
	b.p = d + (start >> 3);
	b.c = 0;
	b.left = 0;
	b.pos = start & ~7L;
	REFILL(&b);
	SKIP(&b, (int)(start & 7));
	sel[0] = t_hsel[g[5] & 31];
	sel[1] = t_hsel[(g[5] >> 5) & 31];
	sel[2] = t_hsel[(g[5] >> 10) & 31];
	for (k = 0; k < 3; k++) {
		/* the region's lines: [i, top) */
		int top = k == 0 ? (r1 < bv ? r1 : bv) : k == 1 ? (r2 < bv ? r2 : bv) : bv;
		unsigned long s = sel[k];
		int lin = (int)(s >> 20);
		if (!s) {
			if (top > i)
				i = (top + 1) & ~1;
			continue;
		}
		for (; i < top; i += 2) {
			if ((v = code(&b, s)) < 0)
				goto count1;
			is[i] = (short)value(&b, v >> 4, lin);
			is[i + 1] = (short)value(&b, v & 15, lin);
		}
	}
count1:
	s1 = t_hsel[32 + ((g[4] >> 5) & 1)];
	while (i <= 572 && b.pos < end) {
		int q[4];
		if ((v = code(&b, s1)) < 0)
			break;
		for (k = 0; k < 4; k++) {
			q[k] = (v >> (3 - k)) & 1;
			if (q[k]) {
				REFILL(&b);
				if (b.c & 0x80000000U)
					q[k] = -1;
				SKIP(&b, 1);
			}
		}
		if (b.pos > end)
			break;		/* the last one ran over: it isn't there */
		for (k = 0; k < 4; k++)
			is[i + k] = (short)q[k];
		i += 4;
	}
}
