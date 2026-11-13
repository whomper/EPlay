/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opld.h - the YM3812 (OPL2) and the YMF262 (OPL3) for the DSP (phase 10,
 * step 7): the 68030's half, as opnd.h is the YM2612's. The OPL3 as Nuked-
 * OPL3 has it: its second bank's nine channels (registers 0x100-0x1FF), the
 * four-operator pairs (0x104: channels 0 and 3, 1 and 4, 2 and 5, and the
 * second bank's; the pair's frequency and key its first channel's, its
 * connection both channels', its outputs its second's), eight waveforms
 * with NEW (0x105) set, else four (and no WSE: register 1's bit 5 is the
 * OPL2's), each channel to the left and the right by its C0 bits 4 and 5
 * (with NEW clear, to both), its rhythm mode the first bank's. The chip's registers as a VGM writes
 * them, turned into what the DSP's program (dsp/opl.a56, and oplfast.c, its
 * arithmetic in C) works with: each operator's phase step at the output's
 * rate, its total level with its key scaling, its envelope's steps per
 * update (the attack a factor, the decays and the release an increment,
 * from the chip's rate patterns at its envelope's clock, the chip's rate:
 * clock / 72), its sustain level, its waveform; each channel's connection,
 * feedback, gains and vibrato's depth; the keys (a channel's, and in the
 * rhythm mode the drums'). What changed is marked, for the 68030 to send.
 *
 * Its units: an envelope step is the chip's (0.1875 dB, 2^(-1/32)); the
 * DSP's gain table is in half steps (2^(-1/64)), so a total level (TL's
 * 0.75 dB) is 8 of them.
 */
#ifndef EPLAY_OPLD_H
#define EPLAY_OPLD_H

#define OPL_UPD 32		/* samples between the envelopes' updates */
#define OPL_Q 8192L		/* an envelope step in the attenuation's word */
#define OPL_ATTMAX (511L * OPL_Q)	/* the chip's top: silent, the operator off */
#define OPL_CH 18		/* (an OPL2's nine, the first) */

typedef struct {
	/* (for the DSP) */
	long step;		/* the phase's step a sample, 2^21 a turn */
	long tl;		/* total level and key scaling, half steps */
	long arf;		/* the attack: the attenuation's factor an update, Q23 (0: at once) */
	long d1, d2, rr;	/* the decay's, the sustain's (EGT 0: the release's) and the release's increments an update, x OPL_Q */
	long sl;		/* the sustain level, x OPL_Q */
	int am, vib, ws;	/* tremolo, vibrato on it; its waveform (0-3) */
	/* (the chip's registers) */
	int egt, ksr, mult, ksl, tlr, ar, dr, slr, rrr, wsr;
	int rof;		/* the key scaling's rate offset now (-1: to work out) */
	int key;		/* keyed now (a channel's or a drum's) */
} OPLD_OP;

typedef struct {
	OPLD_OP op[2];
	int fnum, block, key, fb, cnt;
	int type;		/* 0 two operators; a four-operator pair's 1 first, 2 second */
	int sa, sb;		/* (OPL3) to the left, to the right (C0's bits 4 and 5) */
} OPLD_CH;

enum { OPLD_KEYON = 1, OPLD_KEYOFF = 2 };

typedef struct {
	OPLD_CH ch[OPL_CH];
	int wse, nts, bd;	/* register 1's waveform select; 8's note select; 0xBD */
	int opl3, newm, fourop;	/* an OPL3; its 0x105 bit 0; 0x104 */
	int nch, nch_dirty;	/* the channels the DSP makes: 9 or 18 (opld_channels) */
	long rate;
	unsigned long clock;
	unsigned long stepk;	/* 2 x clock / 72 / rate (Q16): the operators' steps' factor */
	long r_mean[64], r_arf[64];	/* each rate's increment an update (x OPL_Q), attack factor (Q23) */
	long outg;		/* a channel's gain in the mix (OPL_OUTG: the reference's level) */
	/* what changed since the last opld_send: an operator's (36 bits), a channel's (18) */
	unsigned long long op_dirty, step_dirty, key_dirty;
	unsigned long ch_dirty;
	unsigned char key_event[2 * OPL_CH];
	int lfo_dirty, rhy_dirty;
} OPLD;

/* a channel's gain in the mix (Q23): the operator's full scale (the chip's
   4,096) is 1/8 of the mix's, as the reference's 16 bits have it */
#define OPL_OUTG 8388607L

/* the DSP's commands (dsp/opl.a56), each its number, then its words:
	OPLC_OP	operator i (0-35: channel x 2 + 0 the modulator, 1 the carrier):
		step, tl, arf, d1, d2, rr, sl, am, vib, ws (0-7)
	OPLC_STEP operator i, its step alone
	OPLC_CH	channel c: connection (0 FM, 1 both out; a four-operator
		pair's first 2 + its algorithm 0-3, its second OPL_PAIR2),
		feedback's factor (2^(FB + 14), 0), left's and right's gains,
		vibrato's depth (Q23: (F-number >> 7) / F-number x 2, halved for
		the shallow)
	OPLC_NCH the channels made: 9 (an OPL2's, or an OPL3's first bank
		alone) or 18
	OPLC_KEY operator i, OPLD_KEYON or OPLD_KEYOFF
	OPLC_LFO the tremolo's step an update (2^24 its period), the
		vibrato's (2^24 its), the tremolo deep (1) or not (0)
	OPLC_RHY the rhythm mode on (1) or off (0), the noise's steps a sample
	OPLC_STATUS: one word back, the ring's least lead since PLAY (words)
	OPLC_RUN n (tests), OPLC_PLAY k, OPLC_RESET
   (1, 4 and 6 are LOADX, LOADY and LOADP) */
/* where the 68030 loads the DSP's tables (oplfast.c's: LOADX, LOADY) */
#define OPL_X_GAIN 0x0400
#define OPL_X_AM 0x1000
#define OPL_X_PMW 0x1200
#define OPL_Y_WAVE 0x2000	/* eight waveforms of 1,024 */
#define OPL_PAIR2 6		/* a four-operator pair's second channel: made by its first */

enum { OPLC_OP = 2, OPLC_CH = 3, OPLC_KEY = 5, OPLC_LFO = 7, OPLC_RHY = 8, OPLC_RUN = 9, OPLC_PLAY = 10,
       OPLC_RESET = 12, OPLC_STATUS = 15, OPLC_STEP = 16, OPLC_NCH = 17 };

void opld_init(OPLD *d, unsigned long clock, long rate);
/* an OPL3 (its clock 14,318,180 Hz: its rate clock / 288) */
void opld_init3(OPLD *d, unsigned long clock, long rate);
/* the channels the DSP makes: 9 (the first bank's) or 18 */
void opld_channels(OPLD *d, int n);
/* reg 0x000-0x0FF, an OPL3's second bank's 0x100-0x1FF */
void opld_write(OPLD *d, int reg, int val);
/* a channel's gain in the mix (the volume): each channel's sent again */
void opld_gain(OPLD *d, long g);
/* what changed since, as the DSP's commands, a word at a time to put; the
   marks cleared */
void opld_send(OPLD *d, void (*put)(void *u, long w), void *u);

#endif
