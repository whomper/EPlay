/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * oplfast.h - the YM3812 (OPL2) as the DSP plays it (phase 10, step 7):
 * dsp/opl.a56's arithmetic in C, word for word (24-bit words, the DSP's
 * multiplies, its accumulators and its limiter), as opnfast.h is the
 * YM2612's, so that the program can be held to it bit for bit. It takes
 * only the words the 68030 sends (opld_send's commands).
 *
 * Each operator: its phase (2^21 a turn, in a 24-bit word) on by its step,
 * plus its modulation (the modulator's output as it is: the chip's 4,096 in
 * its 10-bit index), its waveform at bits 20-11 (four tables of 1,024)
 * times its gain (a table of 2^(-A/64), A the attenuation's half steps).
 * The modulator's feedback its last two outputs x 2^(FB - 9). A channel the
 * sum of its outputs / 8 (the rhythm mode's / 4: its drums twice as loud),
 * limited; the channels mixed into left and right by their gains, limited:
 * the SSI's words (16 bits at their top). In the rhythm mode channel 7's
 * operators are the hi-hat and the snare, 8's the tom and the cymbal: the
 * hi-hat's, the snare's and the cymbal's phases made from bits of the
 * hi-hat's and the cymbal's phases and the noise (a 23-bit LFSR). The
 * envelopes, the gains and the LFOs each OPL_UPD samples, an operator that
 * is off passed over; a channel with both off passed over.
 */
#ifndef EPLAY_OPLFAST_H
#define EPLAY_OPLFAST_H

#include "opld.h"

#define OPLF_GAINS 2048		/* the gain table: att / OPL_Q x 2 + tl + tremolo at most */

typedef struct {
	long phase, gain, step_e;	/* (the sample's) */
	long att, step, tl, arf, d1, d2, rr, sl;
	int mode, am, vib, ws;
} OPLF_OP;

typedef struct {
	OPLF_OP op[2];
	long kf, pl, pr, dw;
	long o1, o1p;		/* the modulator's last two outputs */
	int cnt, live;
} OPLF_CH;

typedef struct {
	OPLF_CH ch[OPL_CH];
	long am_phase, am_step, pm_phase, pm_step;	/* 2^24 a period */
	int am_deep, rhy, nsteps;
	long noise;
	long v8;		/* (the rhythm mode's channel 8, made with 7's) */
	int upd;		/* samples to the next update */
	/* the tables the 68030 loads */
	long wave[4][1024], gain_tab[OPLF_GAINS], am_tab[2][256], pmw[8];
	/* the words of a command still coming */
	long cw[16];
	int cn;
} OPLF;

void oplf_init(OPLF *f);
/* a word the 68030 sends (opld_send's put) */
void oplf_word(OPLF *f, long w);
/* n samples: the SSI's words, left and right (2 n) */
void oplf_words(OPLF *f, long *lr, int n);
/* n samples, left and right, added into out (2 n shorts) */
void oplf_run(OPLF *f, short *out, int n);

#endif
