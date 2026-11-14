/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opncpu.c - the YM2612 made on the CPU (opncpu.h; phase 11, Erez, 14 Nov
 * 2026: VGM on a 68060 without the Falcon's DSP). src/opnfast.c (the DSP's
 * arithmetic, opn.a56's word for word), its commands, envelopes, DAC and
 * SN76489 as they are, but in 32 bits: the DSP's 24 x 24-bit products
 * (48 bits, which a 68060 makes from 16-bit pieces: 3 to 5 times real
 * time) made of each word's top 16 bits, 16 x 16 into 32, its accumulators
 * the products' top words; the factors that are small integers (the
 * tremolo's triangle, the attenuation's shift) exact. Held to Nuked-OPN2
 * by tests/ym2612 within the limits the DSP's model is.
 */
#include <string.h>
#include "opncpu.h"

enum { M_ATTACK, M_DECAY, M_SUSTAIN, M_RELEASE, M_OFF };

#define W24(v) ((long)(((unsigned long)(v)) & 0xffffff))
#define S24(v) ((long)((((unsigned long)(v)) & 0xffffff) ^ 0x800000) - 0x800000)
#define LIM 8388607L	/* the limiter's: the attenuation's top (its gain 0 from 832 steps) */

typedef long acc_t;	/* an accumulator: the DSP's top word (2^23 = 1.0), in 32 bits */

/* mpy x,y: the product's top word, from the two words' top 16 bits */
static acc_t prod(long a, long b)
{
	return (acc_t)((S24(a) >> 8) * (S24(b) >> 8)) >> 7;
}

static long top(acc_t a)
{
	return a;
}

/* move a,x: the limiter */
static long lim(acc_t a)
{
	return a > 8388607L ? 8388607L : a < -8388608L ? -8388608L : a;
}

void opnc_init(OPNF *f, const unsigned long *sin24, const unsigned long *gain24)
{
	int i, c, k;
	memset(f, 0, sizeof(*f));
	for (i = 0; i < 1024; i++)
		f->sin_tab[i] = S24(sin24[i]);
	for (i = 0; i < OPNF_GAINS; i++)
		f->gain_tab[i] = (long)gain24[i];
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
	f->stopb = f->slfsr = 0x8000;
	f->staps = 9;
	f->snwh = 1;
}

/* ---- the commands */

static int cmd_len(const OPNF *f)
{
	long c = f->cw[0];
	if (c == OPNC_DACE)
		return f->cn < 3 ? 3 : 3 + (int)f->cw[2];
	if (c >= OPNC_SN)
		return 2;
	switch (c) {
	case OPNC_OP:
		return 10;
	case OPNC_CH:
		return 8;
	case OPNC_KEY:
	case OPNC_LFO:
	case OPNC_DAC:
	case OPNC_DRATE:
	case OPNC_STEP:
		return 3;
	}
	return 1;
}

/* sn.a56's command c, value v */
static void sn_cmd(OPNF *f, int c, long v)
{
	f->sn_on = 1;
	if (c < 4)
		f->sinc[c] = v;
	else if (c < 8) {
		f->samp[c - 4] = v;
		if (c < 7)
			f->ssg[c - 4] = S24(f->ssg[c - 4]) < 0 ? W24(-v) : v;
	} else if (c == 8) {
		f->snwh = v;
		f->slfsr = f->stopb;
	} else if (c == 9)
		f->staps = v;
	else if (c == 10)
		f->stopb = v;
	else if (c == 11 && v < 3)
		f->ssg[v] = f->samp[v];
}

static void cmd(OPNF *f, const long *w)
{
	OPNF_CH *ch;
	OPNF_OP *op;
	if (w[0] >= OPNC_SN) {
		sn_cmd(f, (int)(w[0] - OPNC_SN), w[1]);
		return;
	}
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
	case OPNC_STEP:
		f->ch[w[1] / 4].op[w[1] % 4].step = w[2];
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
		f->dword = f->dac;
		break;
	case OPNC_DRATE:
		f->dsi = w[1];
		f->dsf = w[2];
		break;
	case OPNC_DACE: {
		/* opn.a56's hc_dacs: each output sample's place in the tick
		   takes the last event's byte at or before it */
		long lim = w[1], ne = w[2], k = 0, n = 0, epos, ev = 0;
		long pos = f->dsf;
#define NEXT()	do { if (k < ne) { long e_ = w[3 + k++]; epos = e_ & 0xff0000; \
			ev = S24((e_ << 8) & 0xff0000); } else epos = 0x7fffff; } while (0)
		NEXT();
		while (pos < lim) {
			while (pos >= epos) {
				f->dword = ev;
				NEXT();
			}
			f->dq[f->dwp] = f->dword;
			f->dwp = (f->dwp + 1) & 1023;
			n++;
			pos += f->dsi;
		}
		while (epos != 0x7fffff) {
			f->dword = ev;
			NEXT();
		}
#undef NEXT
		f->dsf = pos - lim;
		f->dcnt += (int)n;
		if (f->dcnt > 1000) {
			/* (too many: the oldest dropped) */
			f->dcnt = 1000;
			f->drp = (f->dwp - 1000) & 1023;
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
		f->dac = f->dq[f->drp];
		f->drp = (f->drp + 1) & 1023;
		f->dcnt--;
	}
	return f->dac;
}

void opnc_word(OPNF *f, long w)
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
		long amt = (tri2 * (S24(ch->amk) >> 4)) >> 19;	/* (a small integer: exact) */
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
				v = (long)(((op->att >> 7) * (S24(op->arf) >> 8)) >> 8);	/* (the attenuation's top 16 bits) */
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
			g = (op->att >> 12) + op->tl;
			if (op->am)
				g += amt;
			op->gain = f->gain_tab[g];
			/* the vibrato: step + step x vib */
			a = prod(op->step, vib) + S24(op->step);
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
	acc_t b = S24(op->phase) + S24(op->step_e);
	long idx;
	op->phase = W24(b);
	b += S24(mod1) + S24(mod2) + extra;
	idx = S24(b) >> 11;
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

/* the SN76489's sample (sn.a56's sample, its words as there) */
static void sn_shift(OPNF *f)
{
	long y = f->slfsr, fb;
	if (f->snwh & 1) {
		long t = y & f->staps;
		fb = !(t == 0 || t == f->staps);
	} else
		fb = y & 1;
	f->slfsr = (y >> 1) | (fb ? f->stopb : 0);
}

static long sn_sample(OPNF *f)
{
	long b = 0, a;
	int t;
	for (t = 0; t < 3; t++) {
		a = f->sph[t] + f->sinc[t];
		if (a & 0x800000L) {
			a &= 0x7fffff;
			f->ssg[t] = W24(-S24(f->ssg[t]));
		}
		f->sph[t] = W24(a);
		b += S24(f->ssg[t]);
	}
	a = f->sph[3] + f->sinc[3];
	if (a & 0x800000L) {
		a &= 0x7fffff;
		f->sph[3] = W24(a);
		sn_shift(f);
	} else
		f->sph[3] = W24(a);
	a = S24(f->samp[3]);
	b += f->slfsr & 1 ? a : -a;
	return lim(b);
}

void opnc_words(OPNF *f, long *lr, int n)
{
	int k, c;
	for (k = 0; k < n; k++) {
		acc_t l = 0, r = 0;
		long sn = 0;
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
			if (c == 5 && f->sn_on)
				sn = sn_sample(f);	/* (made after the channels, before the mix) */
			l += prod(v, ch->pl);
			r += prod(v, ch->pr);
		}
		/* (added at the accumulators' top word, as opn.a56's add) */
		l += sn;
		r += sn;
		lr[2 * k] = W24(lim(l));
		lr[2 * k + 1] = W24(lim(r));
	}
}

void opnc_run(OPNF *f, short *out, int n)
{
	int k, j;
	for (k = 0; k < n; k++) {
		long lr[2];
		opnc_words(f, lr, 1);
		for (j = 0; j < 2; j++) {
			long v = (S24(lr[j]) >> 8) + out[2 * k + j];
			out[2 * k + j] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
	}
}
