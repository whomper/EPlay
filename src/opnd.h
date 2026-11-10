/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opnd.h - the YM2612 for the DSP (phase 10): the 68030's half. The chip's
 * registers as a VGM writes them, turned into what the DSP's program
 * (dsp/opn.a56, and opnfast.c, its arithmetic in C) works with: each
 * operator's phase step at the output's rate, its total level, its
 * envelope's steps per update (the DSP updates the envelopes each OPN_UPD
 * samples: the attack a factor, the decays and the release an increment,
 * from the chip's rate tables at the chip's envelope clock), its sustain
 * level; each channel's algorithm, feedback, panning, AM and PM; the keys.
 * What changed is marked, for the 68030 to send.
 */
#ifndef EPLAY_OPND_H
#define EPLAY_OPND_H

#define OPN_UPD 8		/* samples between the envelopes' updates */
#define OPN_Q 4096L		/* the attenuation's fraction: 1 step (0.094 dB) */

typedef struct {
	/* (for the DSP) */
	long step;		/* the phase's step a sample, 2^24 a turn */
	long tl;		/* the total level, attenuation steps x OPN_Q */
	long arf;		/* the attack: the attenuation's factor an update, Q23 (0: at once) */
	long d1, d2, rr;	/* the decays' and the release's increments an update, x OPN_Q */
	long sl;		/* the sustain level, x OPN_Q */
	int am;			/* the LFO's tremolo on it */
	/* (the chip's registers) */
	int dt, mul, ks, ar, d1r, d2r, rrr, slr, tlr, ksr, key;
} OPND_OP;

typedef struct {
	OPND_OP op[4];
	int fnum, block, alg, fb, pan, ams, pms;
	int fnum3[3], block3[3];
} OPND_CH;

enum { OPND_KEYON = 1, OPND_KEYOFF = 2 };

typedef struct {
	OPND_CH ch[6];
	unsigned char latch[2], latch3;
	int ch3_mode, dac_on, dac, lfo_on, lfo_rate;
	long rate;
	unsigned long clock;
	double ratio, egk;	/* the chip's rate / the output's; the envelope's clocks an update */
	/* what changed since the last opnd_take: an operator's (24 bits), a channel's (6) */
	unsigned long op_dirty;
	unsigned ch_dirty;
	unsigned char key_event[24];	/* OPND_KEYON / OPND_KEYOFF since */
	int lfo_dirty, dac_dirty;
} OPND;

void opnd_init(OPND *d, unsigned long clock, long rate);
void opnd_write(OPND *d, int port, int reg, int val);

#endif
