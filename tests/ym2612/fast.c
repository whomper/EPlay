/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fast.c - a VGM's YM2612 as the DSP plays it (src/opnd.c, the 68030's
 * half, and src/opnfast.c, the DSP's arithmetic), at a rate given, to a
 * WAV, the model taking only the DSP's commands, a tick at a time as the
 * 68030 sends them (opnd_tick: what changed, the tick's DAC samples).
 *
 *	fast FILE.VGM OUT.WAV RATE [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vgm.h"
#include "opnd.h"
#include "opnfast.h"
#ifdef OPN_CPU
/* (cpu: the same with the CPU's model, src/opncpu.c, phase 11) */
#include "opncpu.h"
#define opnf_word opnc_word
#define opnf_run opnc_run
#endif

static OPND chip;
static OPNF dsp;

static void opn(void *u, int port, int reg, int val)
{
	(void)u;
	opnd_write(&chip, port, reg, val);
}

#define TICK_HZ (2457600.0 / 16 / 154)
#define STEP 2897647UL		/* the file's samples a tick, Q16 (vgmplay.c's) */

static int step(void *v, long n)
{
	return vgm_run(v, n);
}

static long at(void *v)
{
	return ((VGMRUN *)v)->at;
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
	double secs, tick = 1;
	unsigned long vacc = 0;
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
#ifdef OPN_CPU
	{
		static OPNF t;
		t = dsp;
		opnc_init(&dsp, (const unsigned long *)t.sin_tab, (const unsigned long *)t.gain_tab);
	}
#endif
	opnd_send_drate(&chip, put, &dsp);
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
	/* the tick as the 68030's (2,457,600 / 16 / 154 Hz: 44.2 of the file's
	   samples), its commands before the samples after it */
	for (i = 0; i < (long)(secs * rate); i++) {
		short s[2] = { 0, 0 };
		tick += TICK_HZ / rate;
		if (tick >= 1) {
			tick -= 1;
			vacc += STEP;
			if (!opnd_tick(&chip, step, at, &r, (long)(vacc >> 16), put, &dsp))
				break;
			vacc &= 0xffff;
		}
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
