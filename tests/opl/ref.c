/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ref.c - a VGM's YM3812 (OPL2) or YMF262 (OPL3) as Nuked-OPL3 plays it (the
 * reference, phase 10 step 7): the file's 0x5A writes (an OPL3's 0x5E and
 * 0x5F, its second bank's) to the emulated chip, run at its own rate (clock
 * / 72: 49,716 Hz from 3,579,545 Hz; an OPL3's clock / 288), left and right to a
 * WAV at that rate (sox then takes it to any other). A small VGM reader of
 * its own (the waits and 0x5A): src/vgm.c learns the YM3812 later.
 *
 *	ref FILE.VGM OUT.WAV
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opl3.h"

static opl3_chip chip;

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

static unsigned long u32(const unsigned char *p)
{
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

int main(int argc, char **argv)
{
	FILE *f, *o;
	unsigned char *b;
	long n, p, frames = 0;
	unsigned long clock, rate;
	double acc = 0, per;
	if (argc < 3)
		return 2;
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
	clock = u32(b + 0x50);
	rate = clock / 72;
	if (!clock && n > 0x60 && u32(b + 0x5c)) {
		clock = u32(b + 0x5c);	/* (an OPL3's clock) */
		rate = clock / 288;
	}
	per = (double)rate / 44100.0;
	p = 0x34 + (long)u32(b + 0x34);
	OPL3_Reset(&chip, (uint32_t)rate);
	o = fopen(argv[2], "wb");
	if (!o)
		return 1;
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, o);
	put32(o, 16);
	put16(o, 1);
	put16(o, 2);
	put32(o, rate);
	put32(o, rate * 4);
	put16(o, 4);
	put16(o, 16);
	fwrite("data\0\0\0\0", 1, 8, o);
	while (p < n) {
		unsigned c = b[p];
		long wait = 0;
		if (c == 0x5A || c == 0x5E || c == 0x5F) {
			OPL3_WriteReg(&chip, (uint16_t)(b[p + 1] | (c == 0x5F ? 0x100 : 0)), b[p + 2]);
			p += 3;
			continue;
		}
		if (c == 0x66)
			break;
		if (c == 0x61)
			wait = b[p + 1] | b[p + 2] << 8, p += 3;
		else if (c == 0x62)
			wait = 735, p++;
		else if (c == 0x63)
			wait = 882, p++;
		else if ((c & 0xf0) == 0x70)
			wait = (c & 15) + 1, p++;
		else {
			fprintf(stderr, "ref: command $%02X at %ld\n", c, p);
			return 1;
		}
		acc += wait * per;
		while (acc >= 1) {
			int16_t s[2];
			OPL3_Generate(&chip, s);
			put16(o, (unsigned)(unsigned short)s[0]);
			put16(o, (unsigned)(unsigned short)s[1]);
			frames++;
			acc -= 1;
		}
	}
	fseek(o, 4, SEEK_SET);
	put32(o, 36 + (unsigned long)frames * 4);
	fseek(o, 40, SEEK_SET);
	put32(o, (unsigned long)frames * 4);
	fclose(o);
	free(b);
	return 0;
}
