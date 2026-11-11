/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opnfast.h - the YM2612 as the DSP plays it (phase 10): dsp/opn.a56's
 * arithmetic in C, word for word (24-bit words, the DSP's multiplies, its
 * accumulators and its limiter), so that the program can be held to it bit
 * for bit, as the MP3 decoder is to its model. It takes only the words the
 * 68030 sends (opnd_send's commands), as the program does.
 *
 * Each operator: its phase (2^21 a turn, in a 24-bit word) on by its step,
 * plus its modulation (the outputs it takes, as they are: the chip's output
 * >> 1 in its 10-bit index), the sine at its bits 20-11 (a table of 1,024)
 * times its gain (a table of 2^(-A/64), A the attenuation's steps). Operator
 * 1's feedback its last two outputs x 2^(FB - 9). The carriers summed in an
 * accumulator, the channel the sum limited; the channels mixed into left
 * and right by their gains, limited: the SSI's words (16 bits at their top).
 * The envelopes, the gains and the LFO each OPN_UPD samples, an operator
 * that is off passed over (its attenuation goes up to the limiter's top,
 * its gain 0 long before); a channel with every operator off passed over.
 */
#ifndef EPLAY_OPNFAST_H
#define EPLAY_OPNFAST_H

#include "opnd.h"

#define OPNF_GAINS 3200		/* the gain table: (att + tl) / OPN_Q + tremolo at most, and some */

typedef struct {
	long phase, gain, step_e;	/* (the sample's: internal X and Y) */
	long att, step, tl, arf, d1, d2, rr, sl;
	int mode, am;
} OPNF_OP;

typedef struct {
	OPNF_OP op[4];
	long kf, pl, pr, amk, dw;
	long o1, o1p;		/* operator 1's last two outputs */
	int alg, live;
} OPNF_CH;

typedef struct {
	OPNF_CH ch[6];
	int dac_on;
	long dac;
	long lfo_phase, lfo_step;	/* 2^24: the LFO's 128 steps */
	int upd;		/* samples to the next update */
	/* the tables the 68030 loads */
	long sin_tab[1024], gain_tab[OPNF_GAINS], triw[128], pmw[128];
	/* the DAC's queue (DACS), played at DACRATE */
	long dq[1024];
	int dwp, drp, dcnt;
	long drf, dsi, dsf, dword;	/* (DACE) the place's step and the place (Q16), the byte now */
	/* the Mega Drive's SN76489 (sn.a56's, in opn.a56): on once written */
	int sn_on;
	long sph[4], sinc[4], samp[4], ssg[3], slfsr, staps, stopb, snwh;
	/* the words of a command still coming */
	long cw[64];
	int cn;
} OPNF;

void opnf_init(OPNF *f);
/* a word the 68030 sends (opnd_send's put) */
void opnf_word(OPNF *f, long w);
/* n samples: the SSI's words, left and right (2 n) */
void opnf_words(OPNF *f, long *lr, int n);
/* n samples, left and right, added into out (2 n shorts) */
void opnf_run(OPNF *f, short *out, int n);

#endif
