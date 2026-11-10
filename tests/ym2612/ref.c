/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ref.c - a VGM's YM2612 as Nuked-OPN2 plays it (the reference, phase 10):
 * EPlay's own VGM reader (src/vgm.c) runs the file, its YM2612 writes go to
 * the emulated chip, which is clocked at its own rate (clock / 144 samples
 * a second, 24 of its clocks each); left and right written to a WAV at that
 * rate (sox then takes it to any other).
 *
 *	ref FILE.VGM OUT.WAV [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vgm.h"
#include "ym3438.h"

static ym3438_t chip;

static void clocks(int n)
{
	Bit16s b[2];
	while (n-- > 0)
		OPN2_Clock(&chip, b);
}

static void opn(void *u, int port, int reg, int val)
{
	(void)u;
	OPN2_Write(&chip, (Bit32u)(port * 2), (Bit8u)reg);
	clocks(4);			/* (the chip takes an address a few clocks) */
	OPN2_Write(&chip, (Bit32u)(port * 2 + 1), (Bit8u)val);
	clocks(24);
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

static short clip(long v)
{
	return (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

int main(int argc, char **argv)
{
	FILE *f, *o;
	unsigned char *b;
	long n, rate, frames = 0, i;
	double secs = argc > 3 ? atof(argv[3]) : 0, acc = 0;
	VGMHEAD h;
	VGMRUN r;
	if (argc < 3) {
		fprintf(stderr, "ref FILE.VGM OUT.WAV [seconds]\n");
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f)
		return 1;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n);
	if (fread(b, 1, (size_t)n, f) != (size_t)n || !vgm_head(b, n, &h) || !h.ym2612_clock) {
		fprintf(stderr, "%s: not a VGM with a YM2612\n", argv[1]);
		return 1;
	}
	fclose(f);
	rate = (long)(h.ym2612_clock / 144);
	OPN2_SetChipType(0);
	OPN2_Reset(&chip);
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
		long l = 0, rr = 0;
		int k;
		/* the file's time, 44,100 samples a second, against the chip's */
		acc += (double)VGM_RATE / rate;
		if (acc >= 1) {
			long s = (long)acc;
			acc -= s;
			if (!vgm_run(&r, s))
				break;
		}
		for (k = 0; k < 24; k++) {
			Bit16s bb[2];
			OPN2_Clock(&chip, bb);
			l += bb[0];
			rr += bb[1];
		}
		/* (the chip's 9-bit DAC, summed over its 24 clocks: to 16 bits) */
		put16(o, (unsigned)clip(l * 11));
		put16(o, (unsigned)clip(rr * 11));
		frames++;
	}
	fseek(o, 4, SEEK_SET);
	put32(o, (unsigned long)(36 + frames * 4));
	fseek(o, 40, SEEK_SET);
	put32(o, (unsigned long)(frames * 4));
	fclose(o);
	printf("%s: %ld frames at %ld Hz\n", argv[2], frames, rate);
	return 0;
}
