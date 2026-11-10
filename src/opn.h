/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opn.h - the YM2612 (the Mega Drive's FM, phase 10, Erez, 10 Nov 2026), as
 * EPlay plays it: a model of the chip's own arithmetic (its log-sine and
 * exponent tables, its envelope generator's rates, its eight algorithms,
 * operator 1's feedback, the LFO, channel 3's special mode, the DAC) run at
 * any output rate (the Falcon's 24,585 Hz on the DSP), not at its own
 * 53,267: each operator's phase on by its step scaled to the rate, the
 * envelope generator's steps by its own clock (the chip's rate / 3) against
 * the output's. The C model the DSP program (dsp/opn.a56) is held to, and
 * tested against Nuked-OPN2 (tests/ym2612).
 */
#ifndef EPLAY_OPN_H
#define EPLAY_OPN_H

typedef struct {
	unsigned long phase;	/* 20 bits of the chip's, << 12 (32 bits) */
	unsigned long step;	/* a sample's, the same units */
	int att;		/* the envelope's attenuation, 0 (full) to 1023 */
	int state;		/* OPN_ATTACK ... OPN_OFF */
	int tl, sl;		/* total level (<< 3), sustain level (<< 5) */
	int ar, d1r, d2r, rr, ks, mul, dt, am;	/* as written */
	int ksr;		/* the key scale's part of the rates (keycode >> (3 - KS)) */
	int key;		/* the key on */
	int out[2];		/* the last two outputs (operator 1's feedback) */
} OPN_OP;

typedef struct {
	OPN_OP op[4];		/* operators 1 to 4 */
	int fnum, block, kc;	/* the channel's frequency, its key code */
	int alg, fb, pan, ams, pms;
	int out;		/* the last output (14 bits) */
	int fnum3[3], block3[3];	/* channel 3's special mode: operators 1-3's own */
} OPN_CH;

typedef struct {
	OPN_CH ch[6];
	unsigned char latch[2];	/* a frequency's high byte waits for its low */
	int ch3_mode;		/* $27 bits 6-7 */
	int dac_on, dac;	/* $2B bit 7, $2A as written */
	int lfo_on, lfo_rate;	/* $22 */
	unsigned long lfo_phase, lfo_step;
	long rate;		/* the output's rate */
	unsigned long clock;	/* the chip's */
	unsigned long eg_acc, eg_step;	/* the envelope generator's clock against the output's, Q16 */
	unsigned long eg_count;	/* its count (the chip's), its rates' timing */
	unsigned long ratio;	/* the chip's rate / the output's, Q16: the phases' steps */
} OPN;

/* the chip at clock (Hz) playing at rate (Hz), silent */
void opn_init(OPN *o, unsigned long clock, long rate);
void opn_write(OPN *o, int port, int reg, int val);
/* n samples, left and right, 16 bits, added into out (2 n shorts) */
void opn_run(OPN *o, short *out, int n);

#endif
