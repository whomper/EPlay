/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * oplfast.c - the YM3812 and the YMF262 as the DSP plays them (oplfast.h).
 * Each step as dsp/opl.a56 does it.
 */
#include <string.h>
#include <math.h>
#include "oplfast.h"

enum { M_ATTACK, M_DECAY, M_SUSTAIN, M_RELEASE, M_OFF };

#define W24(v) ((long)(((unsigned long)(v)) & 0xffffff))
#define OPLF_AK (OPL_Q * 9 / 2)
#define S24(v) ((long)((((unsigned long)(v)) & 0xffffff) ^ 0x800000) - 0x800000)

typedef long long acc_t;	/* an accumulator: the product's 48 bits, x 2 */

/* mpy x,y: the product, as the accumulator holds it (2^47 = 1.0) */
static acc_t prod(long a, long b)
{
	return (acc_t)S24(a) * S24(b) * 2;
}

/* its top word (a1) */
static long top(acc_t a)
{
	return (long)(a >> 24);
}

/* move a,x: the limiter */
static long lim(acc_t a)
{
	long v = top(a);
	return v > 8388607L ? 8388607L : v < -8388608L ? -8388608L : v;
}

/* the chip's tremolo, envelope steps, at its 210 steps of 64 samples: 0
   for 7, up to 26 (each 4, 26 for 3) and down */
static int trem(int e)
{
	return e < 7 ? 0 : e < 107 ? 1 + (e - 7) / 4 : e < 110 ? 26 : 25 - (e - 110) / 4;
}

/* the logarithmic saw's p (0-511): 2^(-p/32) of the full scale */
long oplf_ramp(int p)
{
	return (long)floor(pow(2, -p / 32.0) * OPLF_FULL + 0.5);
}

void oplf_init(OPLF *f)
{
	int i, c, k;
	static const long pmw[8] = { 4194304, 2097152, 0, -2097152, -4194304, -2097152, 0, 2097152 };
	memset(f, 0, sizeof(*f));
	for (i = 0; i < 1024; i++) {
		long s = (long)floor(sin((i + 0.5) * M_PI / 512) * 8364000.0 + 0.5);
		f->wave[0][i] = W24(s);
		f->wave[1][i] = i < 512 ? s : 0;		/* the half sine */
		f->wave[2][i] = i < 512 ? s : -s;		/* its absolute value */
		f->wave[3][i] = (i & 256) ? 0 : i < 512 ? s : -s;	/* its rising quarters */
		f->wave[2][i] = W24(f->wave[2][i]);
		f->wave[3][i] = W24(f->wave[3][i]);
	}
	/* the OPL3's (Nuked-OPL3's indexes): 4 the sine at twice the rate in the
	   first half (its mirrored quarters' index ^ $FE: the chip's), nothing in
	   the second; 5 its absolute value; 6 a square; 7 the logarithmic saw,
	   2^(-p/32) and its mirror negative. From the first quarter of 0 (and
	   oplf_ramp), so the 68030 makes them from it */
	for (i = 0; i < 1024; i++) {
		long q = (i & 0x80) ? f->wave[0][((i ^ 0xff) << 1) & 0xff] : f->wave[0][(i << 1) & 0xff];
		int neg = (i & 0x300) == 0x100;
		long r = oplf_ramp(i & 0x200 ? (i & 0x1ff) ^ 0x1ff : i);
		f->wave[4][i] = W24(i & 0x200 ? 0 : neg ? -q : q);
		f->wave[5][i] = W24(i & 0x200 ? 0 : q);
		f->wave[6][i] = W24(i & 0x200 ? -OPLF_FULL : OPLF_FULL);
		f->wave[7][i] = W24(i & 0x200 ? -r : r);
	}
	/* (below -78 dB the chip's output is 0: its 13 bits) */
	for (i = 0; i < OPLF_GAINS; i++)
		f->gain_tab[i] = i >= 832 ? 0 : (long)floor(pow(2, -i / 64.0) * 8388607.0 + 0.5);
	/* the tremolo in half steps, its 256 here its 210 */
	for (i = 0; i < 256; i++) {
		int v = trem(i * 210 / 256);
		f->am_tab[0][i] = 2 * (v >> 2);
		f->am_tab[1][i] = 2 * v;
	}
	for (i = 0; i < 8; i++)
		f->pmw[i] = W24(pmw[i]);
	for (c = 0; c < OPL_CH; c++)
		for (k = 0; k < 2; k++) {
			f->ch[c].op[k].att = OPL_ATTMAX;
			f->ch[c].op[k].mode = M_OFF;
		}
	f->upd = 1;
	f->noise = 1;
	f->nsteps = 1;
	f->nch = 9;
}

/* ---- the commands */

static int cmd_len(const OPLF *f)
{
	switch (f->cw[0]) {
	case OPLC_OP:
		return 12;
	case OPLC_CH:
		return 7;
	case OPLC_LFO:
		return 4;
	case OPLC_KEY:
	case OPLC_STEP:
	case OPLC_RHY:
		return 3;
	case OPLC_NCH:
		return 2;
	}
	return 1;
}

static void cmd(OPLF *f, const long *w)
{
	OPLF_CH *ch;
	OPLF_OP *op;
	switch (w[0]) {
	case OPLC_OP:
		op = &f->ch[w[1] / 2].op[w[1] % 2];
		op->step_e = op->step = w[2];
		op->tl = w[3];
		op->arf = w[4];
		op->d1 = w[5];
		op->d2 = w[6];
		op->rr = w[7];
		op->sl = w[8];
		op->am = (int)w[9];
		op->vib = (int)w[10];
		op->ws = (int)w[11] & 7;
		break;
	case OPLC_STEP:
		op = &f->ch[w[1] / 2].op[w[1] % 2];
		op->step_e = op->step = w[2];
		break;
	case OPLC_CH:
		ch = &f->ch[w[1]];
		ch->cnt = (int)w[2];
		ch->kf = w[3];
		ch->pl = w[4];
		ch->pr = w[5];
		ch->dw = w[6];
		break;
	case OPLC_KEY:
		ch = &f->ch[w[1] / 2];
		op = &ch->op[w[1] % 2];
		if (w[2] == OPLD_KEYON) {
			op->phase = 0;
			if (op->arf)
				op->mode = M_ATTACK;
			else {
				op->att = 0;
				op->mode = M_DECAY;
			}
			ch->live = 1;
		} else if (op->mode != M_OFF)
			op->mode = M_RELEASE;
		break;
	case OPLC_LFO:
		f->am_step = w[1];
		f->pm_step = w[2];
		f->am_deep = (int)w[3] & 1;
		break;
	case OPLC_RHY:
		f->rhy = (int)w[1];
		f->nsteps = (int)w[2];
		break;
	case OPLC_NCH:
		f->nch = w[1] > 9 ? OPL_CH : 9;
		break;
	}
}

void oplf_word(OPLF *f, long w)
{
	f->cw[f->cn++] = W24(w);
	if (f->cn >= cmd_len(f)) {
		cmd(f, f->cw);
		f->cn = 0;
	}
}

/* ---- the envelopes, the gains, the LFOs: each OPL_UPD samples */
static void update(OPLF *f)
{
	int c, k;
	long amt, pmv;
	f->am_phase = W24(f->am_phase + f->am_step);
	amt = f->am_tab[f->am_deep][f->am_phase >> 16];
	f->pm_phase = W24(f->pm_phase + f->pm_step);
	pmv = f->pmw[f->pm_phase >> 21];
	for (c = 0; c < OPL_CH; c++) {
		OPLF_CH *ch = &f->ch[c];
		long vib = top(prod(pmv, ch->dw));
		int live = 0;
		for (k = 0; k < 2; k++) {
			OPLF_OP *op = &ch->op[k];
			long g, v;
			if (op->mode == M_OFF)
				continue;
			live = 1;
			switch (op->mode) {
			case M_ATTACK:
				/* (the chip's step rounded up, (att + 1) / 8 x inc: as
				   att + 4.5 steps by the factor) */
				v = top(prod(op->att + OPLF_AK, op->arf)) - OPLF_AK;
				if (v <= 0) {
					v = 0;
					op->mode = M_DECAY;
				}
				break;
			case M_DECAY:
				v = op->att + op->d1;
				if (v >= op->sl) {
					v = op->sl;
					op->mode = M_SUSTAIN;
				}
				break;
			case M_SUSTAIN:
				v = op->att + op->d2;
				break;
			default:
				v = op->att + op->rr;
				if (v >= OPL_ATTMAX)
					op->mode = M_OFF;
				break;
			}
			op->att = v > OPL_ATTMAX ? OPL_ATTMAX : v;
			/* the gain: att x 2^-12 (half steps) + tl, the tremolo's on it */
			g = top(prod(op->att, 0x800)) + op->tl;
			if (op->am)
				g += amt;
			op->gain = f->gain_tab[g];
			/* the vibrato: step + step x vib */
			op->step_e = op->vib ? W24(op->step + top(prod(op->step, vib))) : op->step;
		}
		ch->live = live;
	}
	f->upd = OPL_UPD;
}

/* ---- a sample */

/* an operator's phase on (stored) */
static long advance(OPLF_OP *op)
{
	op->phase = W24(op->phase + op->step_e);
	return op->phase;
}

/* an operator: its phase on, its modulation added, the index (mpy by
   $1000: bits 20-11), its waveform times its gain */
static acc_t op_calc(OPLF *f, OPLF_OP *op, long mod, acc_t extra)
{
	acc_t b = ((acc_t)S24(advance(op)) << 24) + ((acc_t)S24(mod) << 24) + extra;
	long idx = top(prod(top(b), 0x1000));
	return prod(f->wave[op->ws][idx & 1023], op->gain);
}

/* a drum at a phase index made for it */
static acc_t op_at(OPLF *f, OPLF_OP *op, long idx)
{
	return prod(f->wave[op->ws][idx], op->gain);
}

static long ch_calc(OPLF *f, OPLF_CH *ch, int bd)
{
	OPLF_OP *o = ch->op;
	acc_t a, p1;
	/* the modulator: its feedback, its last two outputs x the factor (mac) */
	p1 = op_calc(f, &o[0], 0, prod(ch->o1, ch->kf) + prod(ch->o1p, ch->kf));
	ch->o1p = ch->o1;
	ch->o1 = lim(p1);
	if (ch->cnt != 1)	/* (FM; a four-operator pair's algorithm 1, 3 and 7, its two FM too) */
		a = op_calc(f, &o[1], ch->o1, 0);
	else if (bd)
		a = op_calc(f, &o[1], 0, 0);	/* (the bass drum's: the carrier alone) */
	else
		a = p1 + op_calc(f, &o[1], 0, 0);
	return lim(a >> (bd ? 2 : 3));
}

/* a four-operator pair, as two channels in their order, each its own
   output (/ 8, limited): its first (A, with its feedback, and B) and its
   second (C and D), three channels on, B's output its link to C where the
   algorithm has one (an output used as a modulation limited). 0: A > B >
   C > D (the first silent); 1: A > B, C > D (each channel FM, the
   second's without feedback: ch_calc); 2: A, B > C > D; 3: A, B > C + D */
static long pair1(OPLF *f, OPLF_CH *ch, OPLF_CH *pr)
{
	OPLF_OP *a = &ch->op[0], *b = &ch->op[1];
	acc_t pa;
	if (ch->cnt == 3)
		return ch_calc(f, ch, 0);
	pa = op_calc(f, a, 0, prod(ch->o1, ch->kf) + prod(ch->o1p, ch->kf));
	ch->o1p = ch->o1;
	ch->o1 = lim(pa);
	if (ch->cnt == 2) {
		pr->link = lim(op_calc(f, b, ch->o1, 0));
		return 0;
	}
	pr->link = lim(op_calc(f, b, 0, 0));
	return lim(pa >> 3);
}

static long pair2(OPLF *f, OPLF_CH *ch)
{
	OPLF_OP *c = &ch->op[0], *d = &ch->op[1];
	acc_t x;
	if (ch->cnt == OPL_PAIR2 + 1)
		return ch_calc(f, ch, 0);
	x = op_calc(f, c, ch->link, 0);
	if (ch->cnt == OPL_PAIR2 + 3)
		x += op_calc(f, d, 0, 0);
	else
		x = op_calc(f, d, lim(x), 0);
	return lim(x >> 3);
}

/* the rhythm mode's channels 7 and 8: the hi-hat and the snare, the tom
   and the cymbal (each phase on, off or not). The hi-hat's, the snare's and
   the cymbal's phase indexes made from the hi-hat's phase's bits 2, 3, 7
   and 8, the cymbal's 3 and 5, and the noise, as the chip's die shows */
static long drums(OPLF *f)
{
	OPLF_OP *hh = &f->ch[7].op[0], *sd = &f->ch[7].op[1], *tt = &f->ch[8].op[0], *cy = &f->ch[8].op[1];
	long h = advance(hh) >> 11 & 1023, y, n = f->noise & 1, x;
	acc_t a, b;
	advance(sd);
	b = op_calc(f, tt, 0, 0);
	y = advance(cy) >> 11 & 1023;
	x = (((h >> 2) ^ (h >> 7)) | ((h >> 3) ^ (y >> 5)) | ((y >> 3) ^ (y >> 5))) & 1;
	/* (an operator off: its gain 0) */
	b += op_at(f, cy, x << 9 | 0x80);
	f->v8 = lim(b >> 2);
	a = op_at(f, hh, x << 9 | ((x ^ n) ? 0xd0 : 0x34));
	a += op_at(f, sd, (h >> 8 & 1) << 9 | ((h >> 8 ^ n) & 1) << 8);
	return lim(a >> 2);
}

void oplf_words(OPLF *f, long *lr, int n)
{
	int k, c, j;
	for (k = 0; k < n; k++) {
		acc_t l = 0, r = 0;
		if (!--f->upd)
			update(f);
		if (f->rhy)
			for (j = 0; j < f->nsteps; j++) {
				long b = (f->noise ^ (f->noise >> 14)) & 1;
				f->noise = f->noise >> 1 | b << 22;
			}
		for (c = 0; c < f->nch; c++) {
			OPLF_CH *ch = &f->ch[c];
			long v;
			if (f->rhy && c >= 7 && c < 9)
				v = c == 7 ? drums(f) : f->v8;
			else if (ch->cnt >= 2 && ch->cnt < OPL_PAIR2)
				v = ch->live || f->ch[c + 3].live ? pair1(f, ch, &f->ch[c + 3]) : 0;
			else if (ch->cnt >= OPL_PAIR2)
				v = ch->live || f->ch[c - 3].live ? pair2(f, ch) : 0;
			else if (!ch->live)
				v = 0;
			else
				v = ch_calc(f, ch, f->rhy && c == 6);
			l += prod(v, ch->pl);
			r += prod(v, ch->pr);
		}
		lr[2 * k] = W24(lim(l));
		lr[2 * k + 1] = W24(lim(r));
	}
}

void oplf_run(OPLF *f, short *out, int n)
{
	int k, j;
	for (k = 0; k < n; k++) {
		long lr[2];
		oplf_words(f, lr, 1);
		for (j = 0; j < 2; j++) {
			long v = (S24(lr[j]) >> 8) + out[2 * k + j];
			out[2 * k + j] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
	}
}
