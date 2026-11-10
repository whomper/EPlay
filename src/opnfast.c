/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opnfast.c - the YM2612 as the DSP plays it (opnfast.h). Each step as
 * dsp/opn.a56 does it; the comments name its instructions.
 */
#include <string.h>
#include <math.h>
#include "opnfast.h"

enum { M_ATTACK, M_DECAY, M_SUSTAIN, M_RELEASE, M_OFF };

#define W24(v) ((long)(((unsigned long)(v)) & 0xffffff))
#define S24(v) ((long)((((unsigned long)(v)) & 0xffffff) ^ 0x800000) - 0x800000)
#define LIM 8388607L	/* the limiter's: the attenuation's top (its gain 0 from 832 steps) */

typedef long long acc_t;	/* an accumulator: here the product's 48 bits, x 2 */

/* mpy x,y: the product, as the accumulator holds it (2^47 = 1.0) */
static acc_t prod(long a, long b)
{
	return (acc_t)S24(a) * S24(b) * 2;
}

/* its top word (a1: toward minus infinity) */
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

void opnf_init(OPNF *f)
{
	int i, c, k;
	memset(f, 0, sizeof(*f));
	for (i = 0; i < 1024; i++)
		f->sin_tab[i] = (long)floor(sin((i + 0.5) * M_PI / 512) * 8364000.0 + 0.5);
	/* (below -78 dB the chip's output is 0: its exponent's 13 octaves) */
	for (i = 0; i < OPNF_GAINS; i++)
		f->gain_tab[i] = i >= 832 ? 0 : (long)floor(pow(2, -i / 64.0) * 8388607.0 + 0.5);
	/* the LFO's step s: the tremolo's triangle x 2, the vibrato's x 2^15 */
	for (i = 0; i < 128; i++) {
		f->triw[i] = 2 * (i < 64 ? i * 2 : (127 - i) * 2);
		f->pmw[i] = W24((long)(i < 32 ? i * 4 : i < 96 ? (64 - i) * 4 : (i - 128) * 4) << 15);
	}
	for (c = 0; c < 6; c++)
		for (k = 0; k < 4; k++) {
			f->ch[c].op[k].att = LIM;
			f->ch[c].op[k].mode = M_OFF;
		}
	f->upd = 1;
}

/* ---- the commands */

static int cmd_len(const OPNF *f)
{
	long c = f->cw[0];
	if (c == OPNC_DACS)
		return f->cn < 2 ? 2 : 2 + (int)((f->cw[1] + 2) / 3);
	switch (c) {
	case OPNC_OP:
		return 10;
	case OPNC_CH:
		return 8;
	case OPNC_KEY:
	case OPNC_LFO:
	case OPNC_DAC:
	case OPNC_DRATE:
		return 3;
	}
	return 1;
}

static void cmd(OPNF *f, const long *w)
{
	OPNF_CH *ch;
	OPNF_OP *op;
	switch (w[0]) {
	case OPNC_OP:
		op = &f->ch[w[1] / 4].op[w[1] % 4];
		op->step = w[2];
		op->tl = w[3];
		op->arf = w[4];
		op->d1 = w[5];
		op->d2 = w[6];
		op->rr = w[7];
		op->sl = w[8];
		op->am = (int)w[9];
		break;
	case OPNC_CH:
		ch = &f->ch[w[1]];
		ch->alg = (int)w[2];
		ch->kf = w[3];
		ch->pl = w[4];
		ch->pr = w[5];
		ch->amk = w[6];
		ch->dw = w[7];
		break;
	case OPNC_KEY:
		ch = &f->ch[w[1] / 4];
		op = &ch->op[w[1] % 4];
		if (w[2] == OPND_KEYON) {
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
	case OPNC_LFO:
		f->lfo_step = w[1];
		if (w[2])
			f->lfo_phase = 0;
		break;
	case OPNC_DAC:
		f->dac_on = (int)w[1];
		f->dac = S24(w[2]);
		break;
	case OPNC_DRATE:
		f->dsi = w[1];
		f->dsf = w[2];
		break;
	case OPNC_DACS: {
		long n = w[1], k;
		for (k = 0; k < n; k++) {
			int b = (int)(w[2 + k / 3] >> (8 * (k % 3)) & 255);
			if (f->dcnt >= 1000)
				continue;
			f->dq[f->dwp] = (long)(b - 128) << 16;
			f->dwp = (f->dwp + 1) & 1023;
			f->dcnt++;
		}
		break;
	}
	}
}

/* channel 6 while the DAC is on: the queue's next at DACRATE, the last
   held while it is empty */
static long dac_next(OPNF *f)
{
	if (f->dcnt > 0) {
		long adv = f->dsi;
		f->dac = f->dq[f->drp];
		f->drf += f->dsf;
		if (f->drf & 0x800000L) {
			f->drf &= 0x7fffff;
			adv++;
		}
		f->drp = (int)((f->drp + adv) & 1023);
		f->dcnt -= (int)adv;
		if (f->dcnt < 0) {
			f->dcnt = 0;
			f->drp = f->dwp;
		}
	}
	return f->dac;
}

void opnf_word(OPNF *f, long w)
{
	f->cw[f->cn++] = W24(w);
	if (f->cn >= cmd_len(f)) {
		cmd(f, f->cw);
		f->cn = 0;
	}
}

/* ---- the envelopes, the gains, the LFO: each OPN_UPD samples */
static void update(OPNF *f)
{
	int c, k;
	long s, tri2, pmw;
	f->lfo_phase = W24(f->lfo_phase + f->lfo_step);
	/* (mpy by 2^-17: signed, -64..63, the tables' middle at 0) */
	s = S24(f->lfo_phase) >> 17;
	tri2 = f->triw[s & 127];
	pmw = f->pmw[s & 127];
	for (c = 0; c < 6; c++) {
		OPNF_CH *ch = &f->ch[c];
		long amt = top(prod(tri2, ch->amk));
		long vib = top(prod(pmw, ch->dw));
		int live = 0;
		for (k = 0; k < 4; k++) {
			OPNF_OP *op = &ch->op[k];
			acc_t a;
			long g, v;
			/* (an operator off passed over: its gain 0 since it went off) */
			if (op->mode == M_OFF)
				continue;
			live = 1;
			switch (op->mode) {
			case M_ATTACK:
				v = top(prod(op->att, op->arf));
				if (v < OPN_Q) {
					v = 0;
					op->mode = M_DECAY;
				}
				break;
			case M_DECAY:
				v = op->att + op->d1;
				if (v >= op->sl)
					op->mode = M_SUSTAIN;
				break;
			case M_SUSTAIN:
				v = op->att + op->d2;
				break;
			default:
				v = op->att + op->rr;
				if (v >= LIM)
					op->mode = M_OFF;
				break;
			}
			/* (stored through the limiter) */
			op->att = v > LIM ? LIM : v;
			/* the gain: att x 2^-12 + tl (in steps), the tremolo's on it */
			g = top(prod(op->att, 0x800)) + op->tl;
			if (op->am)
				g += amt;
			op->gain = f->gain_tab[g];
			/* the vibrato: step + step x vib */
			a = prod(op->step, vib) + ((acc_t)S24(op->step) << 24);
			op->step_e = W24(top(a));
		}
		ch->live = live;
	}
	f->upd = OPN_UPD;
}

/* ---- a sample */

/* an operator: its phase on (stored), its modulation added, the index
   (mpy by $1000: bits 20-11, signed; the table's 8 copies take it), the
   sine times its gain */
static acc_t op_calc(OPNF *f, OPNF_OP *op, long mod1, long mod2, acc_t extra)
{
	acc_t b = ((acc_t)S24(op->phase) << 24) + ((acc_t)S24(op->step_e) << 24);
	long idx;
	op->phase = W24(top(b));
	b += ((acc_t)S24(mod1) << 24) + ((acc_t)S24(mod2) << 24) + extra;
	idx = top(prod(top(b), 0x1000));
	return prod(f->sin_tab[idx & 1023], op->gain);
}

static long ch_calc(OPNF *f, OPNF_CH *ch)
{
	OPNF_OP *o = ch->op;
	acc_t a, p1, p2, p3;
	long o1, o2, o3;
	/* operator 1: its feedback, its last two outputs x the factor (mac) */
	p1 = op_calc(f, &o[0], 0, 0, prod(ch->o1, ch->kf) + prod(ch->o1p, ch->kf));
	ch->o1p = ch->o1;
	o1 = ch->o1 = lim(p1);
	switch (ch->alg) {
	case 0:
		o2 = lim(op_calc(f, &o[1], o1, 0, 0));
		o3 = lim(op_calc(f, &o[2], o2, 0, 0));
		a = op_calc(f, &o[3], o3, 0, 0);
		break;
	case 1:
		o2 = lim(op_calc(f, &o[1], 0, 0, 0));
		o3 = lim(op_calc(f, &o[2], o1, o2, 0));
		a = op_calc(f, &o[3], o3, 0, 0);
		break;
	case 2:
		o2 = lim(op_calc(f, &o[1], 0, 0, 0));
		o3 = lim(op_calc(f, &o[2], o2, 0, 0));
		a = op_calc(f, &o[3], o1, o3, 0);
		break;
	case 3:
		o2 = lim(op_calc(f, &o[1], o1, 0, 0));
		o3 = lim(op_calc(f, &o[2], 0, 0, 0));
		a = op_calc(f, &o[3], o2, o3, 0);
		break;
	case 4:
		a = op_calc(f, &o[1], o1, 0, 0);
		o3 = lim(op_calc(f, &o[2], 0, 0, 0));
		a += op_calc(f, &o[3], o3, 0, 0);
		break;
	case 5:
		a = op_calc(f, &o[1], o1, 0, 0);
		a += op_calc(f, &o[2], o1, 0, 0);
		a += op_calc(f, &o[3], o1, 0, 0);
		break;
	case 6:
		a = op_calc(f, &o[1], o1, 0, 0);
		a += op_calc(f, &o[2], 0, 0, 0);
		a += op_calc(f, &o[3], 0, 0, 0);
		break;
	default:
		p2 = op_calc(f, &o[1], 0, 0, 0);
		p3 = op_calc(f, &o[2], 0, 0, 0);
		a = p1 + p2 + p3 + op_calc(f, &o[3], 0, 0, 0);
		break;
	}
	return lim(a);
}

void opnf_words(OPNF *f, long *lr, int n)
{
	int k, c;
	for (k = 0; k < n; k++) {
		acc_t l = 0, r = 0;
		if (!--f->upd)
			update(f);
		for (c = 0; c < 6; c++) {
			OPNF_CH *ch = &f->ch[c];
			long v;
			if (c == 5 && f->dac_on)
				v = dac_next(f);
			else if (!ch->live)
				v = 0;
			else
				v = ch_calc(f, ch);
			l += prod(v, ch->pl);
			r += prod(v, ch->pr);
		}
		lr[2 * k] = W24(lim(l));
		lr[2 * k + 1] = W24(lim(r));
	}
}

void opnf_run(OPNF *f, short *out, int n)
{
	int k, j;
	for (k = 0; k < n; k++) {
		long lr[2];
		opnf_words(f, lr, 1);
		for (j = 0; j < 2; j++) {
			long v = (S24(lr[j]) >> 8) + out[2 * k + j];
			out[2 * k + j] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
	}
}
