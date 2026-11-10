/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sncmd.h - the SN76489's writes turned into sn.a56's commands (phase 9;
 * the Mega Drive's PSG in opn.a56 too, phase 10): the chip's three tones'
 * periods and its noise as steps of a 23-bit phase each output sample, its
 * volumes (2 dB a step) as amplitudes. A byte with bit 7 set chooses a
 * register (channel, tone or volume) and gives its low 4 bits; one
 * without gives a tone's high 6 bits, or the volume again. No hardware
 * here: the words go to put (atari/dspsn.c's host port, atari/dspopn.c's,
 * or a test's).
 */
#ifndef EPLAY_SNCMD_H
#define EPLAY_SNCMD_H

typedef struct {
	long q11;		/* clock / 16 / rate, Q11 */
	int period[3], vol[4], latch, nctl;
	int att, mute;		/* the volume: 1.5 dB steps down; silent */
	int base;		/* the commands' first number (0: sn.a56's, 20: opn.a56's) */
	int shift;		/* the amplitudes x 2^-shift (the Mega Drive's PSG under its FM) */
	void (*put)(void *u, long w);
	void *u;
} SNCMD;

/* the chip at clock (Hz) for an output at rate, fb its noise's taps and
   width its register's bits (the VGM header's: 0 for the Master System's,
   $0009 and 16); its set-up sent, all silent */
void sncmd_init(SNCMD *s, unsigned long clock, long rate, int fb, int width, int base, int shift,
		void (*put)(void *u, long w), void *u);
void sncmd_write(SNCMD *s, int byte);
void sncmd_reamp(SNCMD *s);	/* the amplitudes sent again (the volume changed) */

#endif
