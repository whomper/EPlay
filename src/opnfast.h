/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opnfast.h - the YM2612 as the DSP plays it (phase 10): dsp/opn.a56's
 * arithmetic in C, word for word (24-bit words, the DSP's multiplies and
 * its limiter), so that the program can be held to it bit for bit, as the
 * MP3 decoder is to its model. A sine of 1,024 times a gain from a table of
 * 1,024 (the attenuation's 0.094 dB steps: 2^(-A/64)), one multiply an
 * operator; the modulation the output x 8 (the chip's >> 1 in its 10-bit
 * index), operator 1's feedback its last two x 2^(FB - 6); the envelopes
 * each OPN_UPD samples from the steps opnd.c works out; a channel with
 * every operator silent passed over. Its parameters come only as the
 * 68030 sends them (opnf_take: what opnd.c marked changed).
 */
#ifndef EPLAY_OPNFAST_H
#define EPLAY_OPNFAST_H

#include "opnd.h"

typedef struct {
	long phase;		/* 24 bits */
	long step, tl, arf, d1, d2, rr, sl;	/* as sent */
	long step_e;		/* the step with the vibrato's (each update) */
	long att;		/* x OPN_Q */
	long gain;		/* Q23, from the table */
	long out, out1;		/* the last two outputs, Q23 */
	int mode, am;
} OPNF_OP;

typedef struct {
	OPNF_OP op[4];
	int alg, fb, panl, panr, ams, pms;
	int live;		/* an operator not silent */
} OPNF_CH;

typedef struct {
	OPNF_CH ch[6];
	int dac_on;
	long dac;		/* Q23 */
	int lfo_on;
	long lfo_phase, lfo_step;	/* Q16 of its 128 steps an update */
	int upd;		/* samples to the next update */
	long sin_tab[1024], gain_tab[1024];
} OPNF;

void opnf_init(OPNF *f, const OPND *d);
/* what d marked changed, taken as the 68030 sends it, the marks cleared */
void opnf_take(OPNF *f, OPND *d);
/* n samples, left and right, added into out (2 n shorts) */
void opnf_run(OPNF *f, short *out, int n);

#endif
