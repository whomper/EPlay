/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * model.c - a VGM's YM2612 as EPlay's model plays it (src/opn.c), at a
 * rate given (24,585: the Falcon's; the chip's own 53,267 to hold it to
 * the reference's samples), to a WAV.
 *
 *	model FILE.VGM OUT.WAV RATE [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vgm.h"
#include "opn.h"

static OPN chip;

static void opn(void *u, int port, int reg, int val)
{
	(void)u;
	opn_write(&chip, port, reg, val);
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
	unsigned char *b;
	long n, rate, frames = 0, i;
	double secs, acc = 0;
	VGMHEAD h;
	VGMRUN r;
	if (argc < 4) {
		fprintf(stderr, "model FILE.VGM OUT.WAV RATE [seconds]\n");
		return 2;
	}
	rate = atol(argv[3]);
	secs = argc > 4 ? atof(argv[4]) : 0;
	f = fopen(argv[1], "rb");
	if (!f)
		return 1;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n);
	if (fread(b, 1, (size_t)n, f) != (size_t)n || !vgm_head(b, n, &h) || !h.ym2612_clock)
		return 1;
	fclose(f);
	opn_init(&chip, h.ym2612_clock, rate);
	vgm_begin(&r, b, n, &h, 0, 0, 0);
	r.opn_fn = opn;
	o = fopen(argv[2], "wb");
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, o);
	put32(o, 16);
	put16(o, 1);
	put16(o, 2);
	put32(o, (unsigned long)rate);
	put32(o, (unsigned long)rate * 4);
	put16(o, 4);
	put16(o, 16);
	fwrite("data\0\0\0\0", 1, 8, o);
	if (secs <= 0)
		secs = (double)vgm_length(&h) / VGM_RATE;
	for (i = 0; i < (long)(secs * rate); i++) {
		short s[2] = { 0, 0 };
		acc += (double)VGM_RATE / rate;
		if (acc >= 1) {
			long k = (long)acc;
			acc -= k;
			if (!vgm_run(&r, k))
				break;
		}
		opn_run(&chip, s, 1);
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
