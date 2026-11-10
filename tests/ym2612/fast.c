/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fast.c - a VGM's YM2612 as the DSP plays it (src/opnd.c, the 68030's
 * half, and src/opnfast.c, the DSP's arithmetic), at a rate given, to a
 * WAV, the model taking only the DSP's commands (opnd_send's words): the
 * parameters each 1 ms tick (the 68030's Timer A), the DAC's samples as
 * they come (they are streamed).
 *
 *	fast FILE.VGM OUT.WAV RATE [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vgm.h"
#include "opnd.h"
#include "opnfast.h"

static OPND chip;
static OPNF dsp;

static void opn(void *u, int port, int reg, int val)
{
	(void)u;
	opnd_write(&chip, port, reg, val);
}

static void put(void *u, long w)
{
	opnf_word(u, w);
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
	double secs, acc = 0, tick = 0;
	VGMHEAD h;
	VGMRUN r;
	if (argc < 4) {
		fprintf(stderr, "fast FILE.VGM OUT.WAV RATE [seconds]\n");
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
	opnd_init(&chip, h.ym2612_clock, rate);
	opnf_init(&dsp);
	opnd_send(&chip, put, &dsp);
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
		tick += 1000.0 / rate;
		if (tick >= 1) {
			tick -= 1;
			opnd_send(&chip, put, &dsp);
		} else
			opnd_send_dac(&chip, put, &dsp);
		opnf_run(&dsp, s, 1);
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
