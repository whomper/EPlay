/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fast.c - a VGM's YM3812 as the DSP plays it (src/opld.c, the 68030's
 * half, and src/oplfast.c, the DSP's arithmetic), at a rate given, to a
 * WAV, the model taking only the DSP's commands, a tick at a time as the
 * 68030 sends them. Its own small VGM reader (the waits and 0x5A), as
 * ref.c's: src/vgm.c learns the YM3812 in step 7's fourth part.
 *
 *	fast FILE.VGM OUT.WAV RATE
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opld.h"
#include "oplfast.h"

static OPLD chip;
static OPLF dsp;
static unsigned char *b;
static long n, p, wait;

#define TICK_HZ (2457600.0 / 16 / 154)
#define STEP 2897647UL		/* the file's samples a tick, Q16 (vgmplay.c's) */

static unsigned long u32(const unsigned char *q)
{
	return (unsigned long)q[0] | (unsigned long)q[1] << 8 | (unsigned long)q[2] << 16 | (unsigned long)q[3] << 24;
}

/* the file's next k samples: its writes to the chip; 0 at its end */
static int run(long k)
{
	while (p < n) {
		unsigned c = b[p];
		if (wait >= k) {
			wait -= k;
			return 1;
		}
		k -= wait;
		wait = 0;
		if (c == 0x5A || c == 0x5E || c == 0x5F) {
			opld_write(&chip, b[p + 1] | (c == 0x5F ? 0x100 : 0), b[p + 2]);
			p += 3;
		} else if (c == 0x66)
			return 0;
		else if (c == 0x61)
			wait = b[p + 1] | b[p + 2] << 8, p += 3;
		else if (c == 0x62)
			wait = 735, p++;
		else if (c == 0x63)
			wait = 882, p++;
		else if ((c & 0xf0) == 0x70)
			wait = (c & 15) + 1, p++;
		else {
			fprintf(stderr, "fast: command $%02X at %ld\n", c, p);
			exit(1);
		}
	}
	return 0;
}

static void put(void *u, long w)
{
	oplf_word(u, w);
}

static void put16(FILE *f, unsigned v)
{
	fputc((int)(v & 255), f);
	fputc((int)(v >> 8 & 255), f);
}

static void put32(FILE *f, unsigned long v)
{
	put16(f, (unsigned)(v & 0xffff));
	put16(f, (unsigned)(v >> 16));
}

int main(int argc, char **argv)
{
	FILE *f, *o;
	long rate, frames = 0;
	double tick = 1;
	unsigned long vacc = 0;
	int live = 1;
	if (argc < 4) {
		fprintf(stderr, "fast FILE.VGM OUT.WAV RATE\n");
		return 2;
	}
	rate = atol(argv[3]);
	f = fopen(argv[1], "rb");
	if (!f)
		return 1;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n)
		return 1;
	fclose(f);
	p = 0x34 + (long)u32(b + 0x34);
	if (u32(b + 0x50))
		opld_init(&chip, u32(b + 0x50), rate);
	else {
		/* an OPL3: its channels 18 where its second bank is keyed (EPlay's
		   choice by the file: vgm.c), else its first nine */
		long q;
		int bank2 = 0;
		opld_init3(&chip, u32(b + 0x5c), rate);
		for (q = p; q + 2 < n && b[q] != 0x66; ) {
			unsigned c = b[q];
			if (c == 0x5F && (b[q + 1] & 0xf0) == 0xb0 && (b[q + 1] & 15) < 9 && (b[q + 2] & 0x20))
				bank2 = 1;
			q += c == 0x5E || c == 0x5F || c == 0x5A || c == 0x61 ? 3 : 1;
		}
		opld_channels(&chip, bank2 ? 18 : 9);
	}
	oplf_init(&dsp);
	opld_send(&chip, put, &dsp);
	o = fopen(argv[2], "wb");
	if (!o)
		return 1;
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, o);
	put32(o, 16);
	put16(o, 1);
	put16(o, 2);
	put32(o, (unsigned long)rate);
	put32(o, (unsigned long)rate * 4);
	put16(o, 4);
	put16(o, 16);
	fwrite("data\0\0\0\0", 1, 8, o);
	/* the tick as the 68030's, its commands before the samples after it */
	while (live) {
		short s[2] = { 0, 0 };
		tick += TICK_HZ / rate;
		if (tick >= 1) {
			tick -= 1;
			vacc += STEP;
			live = run((long)(vacc >> 16));
			opld_send(&chip, put, &dsp);
			vacc &= 0xffff;
		}
		oplf_run(&dsp, s, 1);
		put16(o, (unsigned)s[0]);
		put16(o, (unsigned)s[1]);
		frames++;
	}
	fseek(o, 4, SEEK_SET);
	put32(o, (unsigned long)(36 + frames * 4));
	fseek(o, 40, SEEK_SET);
	put32(o, (unsigned long)(frames * 4));
	fclose(o);
	return 0;
}
