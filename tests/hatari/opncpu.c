/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opncpu.c - OPNCPU.PRG: the YM2612's C model (src/opnfast.c, which
 * dsp/opn.a56 is held to bit for bit) timed on the CPU it is built for,
 * the 68060: dspopn's worst case (six channels, each operator on and loud,
 * feedback 7, the LFO, and the Mega Drive's SN76489 sounding), a second of
 * it at 24,585 Hz, algorithm 0 and 7. A measurement for the VGM player on
 * a 68060 with no Falcon DSP (DESIGN.md), not a test.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "opnd.h"
#include "opnfast.h"
#include "sncmd.h"
#include "opntab.h"

#define RATE 24585L

/* (opnf_init makes its tables with sin and pow, and this build has no
   maths library: they are replaced below by opntab.h's, the same) */
double sin(double x);
double pow(double x, double y);
double floor(double x);
double sin(double x) { (void)x; return 0; }
double pow(double x, double y) { (void)x; (void)y; return 0; }
double floor(double x) { return x; }

static OPND chip;
static OPNF model;
static SNCMD psg;
static short out[2 * 512];

static void put(void *u, long w)
{
	(void)u;
	opnf_word(&model, w);
}

static long run(int alg)
{
	int c, k, i;
	u32 t0;
	opnf_init(&model);
	for (i = 0; i < 1024; i++)
		model.sin_tab[i] = (long)((opn_sin[i] ^ 0x800000UL) - 0x800000UL);
	for (i = 0; i < OPNF_GAINS; i++)
		model.gain_tab[i] = (long)opn_gain[i];
	opnf_word(&model, 12);		/* RESET */
	opnd_init(&chip, 7670453, RATE);
	for (c = 0; c < 6; c++) {
		int port = c / 3, ch = c % 3;
		for (k = 0; k < 4; k++) {
			int o = ch + 4 * k;
			opnd_write(&chip, port, 0x30 + o, 0x01 + k);
			opnd_write(&chip, port, 0x40 + o, k == 3 ? 0 : 0x10);
			opnd_write(&chip, port, 0x50 + o, 0x1f);
			opnd_write(&chip, port, 0x60 + o, 0x80);
			opnd_write(&chip, port, 0x80 + o, 0x0f);
		}
		opnd_write(&chip, port, 0xa4 + ch, 0x20 + c);
		opnd_write(&chip, port, 0xa0 + ch, 0x40 + 16 * c);
		opnd_write(&chip, port, 0xb0 + ch, 0x38 | alg);
		opnd_write(&chip, port, 0xb4 + ch, 0xc7);
		opnd_write(&chip, 0, 0x28, 0xf0 | (port ? 4 : 0) | ch);
	}
	opnd_write(&chip, 0, 0x22, 0x0f);
	opnd_send(&chip, put, 0);
	sncmd_init(&psg, 3579545, RATE, 9, 16, OPNC_SN, 1, put, 0);
	{
		static const unsigned char b[] = { 0x8e, 0x0f, 0x90, 0xa5, 0x0c, 0xb0, 0xc3, 0x08, 0xd0, 0xe7, 0xf0 };
		unsigned j;
		for (j = 0; j < sizeof(b); j++)
			sncmd_write(&psg, b[j]);
	}
	t0 = tos_ms();
	for (i = 0; i < RATE; i += 512)
		opnf_run(&model, out, 512);
	return (long)(tos_ms() - t0);
}

extern int div64_cpu60;		/* atari/div64.c */

int main(void)
{
	char line[160];
	long a0, a7;
	div64_cpu60 = 1;		/* (as EPlay sets it on a 68060, from _CPU) */
	a0 = run(0);
	a7 = run(7);
	snprintf(line, sizeof(line), "OPNCPU the YM2612's model, a second at %ld Hz, the worst case: algorithm 0 %ld ms, "
		 "algorithm 7 %ld ms\n", RATE, a0, a7);
	nf_debug(line);
	return 0;
}
