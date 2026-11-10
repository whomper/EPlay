/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opnfast.c - the YM2612 as the DSP plays it (opnfast.h).
 */
#include <string.h>
#include <math.h>
#include "opnfast.h"

enum { M_ATTACK, M_DECAY, M_SUSTAIN, M_RELEASE, M_OFF };

#define W24(v) ((long)(((unsigned long)(v)) & 0xffffff))
#define S24(v) ((long)((((unsigned long)(v)) & 0xffffff) ^ 0x800000) - 0x800000)
#define QMAX (1023L * OPN_Q)

static const unsigned char lfo_per[8] = { 108, 77, 71, 67, 62, 44, 8, 5 };
static const unsigned char ams_shift[4] = { 8, 3, 1, 0 };
static const unsigned short pms_depth[8] = { 0, 2, 4, 6, 8, 12, 23, 46 };

/* the DSP's multiply of two Q23 fractions, the result's top word (as mpy
   then a1: truncated toward minus infinity) */
static long mpy(long a, long b)
{
	return (long)(((long long)a * b * 2) >> 24);
}

static long sat(long v)
{
	return v > 8388607L ? 8388607L : v < -8388608L ? -8388608L : v;
}

void opnf_init(OPNF *f, const OPND *d)
{
	int i, c, k;
	memset(f, 0, sizeof(*f));
	for (i = 0; i < 1024; i++) {
		f->sin_tab[i] = (long)floor(sin((i + 0.5) * M_PI / 512) * 8364000.0 + 0.5);
		/* (below -78 dB the chip's output is 0: its exponent's 13 octaves) */
		f->gain_tab[i] = i >= 832 ? 0 : (long)floor(pow(2, -i / 64.0) * 8388607.0 + 0.5);
	}
	for (c = 0; c < 6; c++) {
		f->ch[c].panl = f->ch[c].panr = 1;
		for (k = 0; k < 4; k++) {
			f->ch[c].op[k].att = QMAX;
			f->ch[c].op[k].mode = M_OFF;
		}
	}
	f->upd = 1;
	(void)d;
}

void opnf_take(OPNF *f, OPND *d)
{
	int c, k;
	for (c = 0; c < 6; c++) {
		OPNF_CH *ch = &f->ch[c];
		if (d->ch_dirty & (1u << c)) {
			ch->alg = d->ch[c].alg;
			ch->fb = d->ch[c].fb;
			ch->panl = (d->ch[c].pan >> 1) & 1;
			ch->panr = d->ch[c].pan & 1;
			ch->ams = d->ch[c].ams;
			ch->pms = d->ch[c].pms;
		}
		for (k = 0; k < 4; k++) {
			int i = c * 4 + k;
			OPNF_OP *op = &ch->op[k];
			const OPND_OP *s = &d->ch[c].op[k];
			if (d->op_dirty & (1UL << i)) {
				op->step = s->step;
				op->tl = s->tl;
				op->arf = s->arf;
				op->d1 = s->d1;
				op->d2 = s->d2;
				op->rr = s->rr;
				op->sl = s->sl;
				op->am = s->am;
			}
			if (d->key_event[i] == OPND_KEYON) {
				op->phase = 0;
				if (op->arf) {
					op->mode = M_ATTACK;
				} else {
					op->att = 0;
					op->mode = M_DECAY;
				}
				ch->live = 1;
			} else if (d->key_event[i] == OPND_KEYOFF && op->mode != M_OFF)
				op->mode = M_RELEASE;
			d->key_event[i] = 0;
		}
	}
	if (d->lfo_dirty) {
		f->lfo_on = d->lfo_on;
		f->lfo_step = d->lfo_on ? (long)(d->ratio * OPN_UPD / lfo_per[d->lfo_rate] * 65536 + 0.5) : 0;
		if (!d->lfo_on)
			f->lfo_phase = 0;
	}
	if (d->dac_dirty) {
		f->dac_on = d->dac_on;
		f->dac = (long)(d->dac - 128) << 16;
	}
	d->op_dirty = 0;
	d->ch_dirty = 0;
	d->lfo_dirty = d->dac_dirty = 0;
}

/* the envelopes, the gains, the LFO: each OPN_UPD samples */
static void update(OPNF *f)
{
	int c, k, tri = 0;
	long pm = 0;
	if (f->lfo_on) {
		int s;
		f->lfo_phase += f->lfo_step;
		s = (int)((f->lfo_phase >> 16) & 127);
		tri = s < 64 ? s * 2 : (127 - s) * 2;
		pm = s < 32 ? s * 4 : s < 96 ? (64 - s) * 4 : (s - 128) * 4;
	}
	for (c = 0; c < 6; c++) {
		OPNF_CH *ch = &f->ch[c];
		int live = 0;
		for (k = 0; k < 4; k++) {
			OPNF_OP *op = &ch->op[k];
			long a;
			switch (op->mode) {
			case M_ATTACK:
				op->att = mpy(op->att, op->arf);
				if (op->att < OPN_Q) {
					op->att = 0;
					op->mode = M_DECAY;
				}
				break;
			case M_DECAY:
				op->att += op->d1;
				if (op->att >= op->sl)
					op->mode = M_SUSTAIN;
				if (op->att > QMAX)
					op->att = QMAX;
				break;
			case M_SUSTAIN:
				op->att += op->d2;
				if (op->att > QMAX)
					op->att = QMAX;
				break;
			case M_RELEASE:
				op->att += op->rr;
				if (op->att >= QMAX) {
					op->att = QMAX;
					op->mode = M_OFF;
				}
				break;
			}
			a = (op->att + op->tl) / OPN_Q + (op->am && f->lfo_on ? tri >> ams_shift[ch->ams] : 0);
			op->gain = f->gain_tab[a > 1023 ? 1023 : a];
			if (op->mode != M_OFF)
				live = 1;
		}
		ch->live = live;
		/* the vibrato: the steps x (1 + its depth), an update's worth */
		for (k = 0; k < 4; k++) {
			OPNF_OP *op = &ch->op[k];
			op->step_e = op->step;
			if (pm && ch->pms)
				op->step_e = W24(op->step + mpy(op->step, (long)pm * pms_depth[ch->pms] * 8388608L / 128000));
		}
	}
	f->upd = OPN_UPD;
}

/* an operator's output: its sine at its phase plus mod, times its gain;
   its phase on */
static long op_calc(OPNF *f, OPNF_OP *op, long mod)
{
	long idx = (W24(op->phase + mod) >> 14) & 1023;
	long out = mpy(f->sin_tab[idx], op->gain);
	op->phase = W24(op->phase + op->step_e);
	return out;
}

/* the modulation of an output: x 8 (the chip's >> 1 in its 10-bit index),
   in 24 bits */
#define MOD(v) W24((v) << 3)

static long ch_calc(OPNF *f, OPNF_CH *ch)
{
	OPNF_OP *o = ch->op;
	long m, s, o1, o2, o3, o4;
	/* operator 1: its feedback, its last two outputs x 2^(FB - 6) */
	m = 0;
	if (ch->fb) {
		long fs = o[0].out + o[0].out1;
		m = W24(ch->fb >= 6 ? fs << (ch->fb - 6) : fs >> (6 - ch->fb));
	}
	o1 = op_calc(f, &o[0], m);
	o[0].out1 = o[0].out;
	o[0].out = o1;
	switch (ch->alg) {
	case 0:
		o2 = op_calc(f, &o[1], MOD(o1));
		o3 = op_calc(f, &o[2], MOD(o2));
		s = op_calc(f, &o[3], MOD(o3));
		break;
	case 1:
		o2 = op_calc(f, &o[1], 0);
		o3 = op_calc(f, &o[2], MOD(o1 + o2));
		s = op_calc(f, &o[3], MOD(o3));
		break;
	case 2:
		o2 = op_calc(f, &o[1], 0);
		o3 = op_calc(f, &o[2], MOD(o2));
		s = op_calc(f, &o[3], MOD(o1 + o3));
		break;
	case 3:
		o2 = op_calc(f, &o[1], MOD(o1));
		o3 = op_calc(f, &o[2], 0);
		s = op_calc(f, &o[3], MOD(o2 + o3));
		break;
	case 4:
		o2 = op_calc(f, &o[1], MOD(o1));
		o3 = op_calc(f, &o[2], 0);
		o4 = op_calc(f, &o[3], MOD(o3));
		s = o2 + o4;
		break;
	case 5:
		o2 = op_calc(f, &o[1], MOD(o1));
		o3 = op_calc(f, &o[2], MOD(o1));
		o4 = op_calc(f, &o[3], MOD(o1));
		s = o2 + o3 + o4;
		break;
	case 6:
		o2 = op_calc(f, &o[1], MOD(o1));
		o3 = op_calc(f, &o[2], 0);
		o4 = op_calc(f, &o[3], 0);
		s = o2 + o3 + o4;
		break;
	default:
		o2 = op_calc(f, &o[1], 0);
		o3 = op_calc(f, &o[2], 0);
		o4 = op_calc(f, &o[3], 0);
		s = o1 + o2 + o3 + o4;
		break;
	}
	return sat(s);
}

/* a channel's level in the mix: the chip's 14 bits through its 9-bit DAC,
   at the reference's level (opn.c's x 33 of 16 bits), Q23 */
#define OUTG 2162688L	/* 0.2578 */

void opnf_run(OPNF *f, short *out, int n)
{
	int k, c;
	for (k = 0; k < n; k++) {
		long l = 0, r = 0;
		if (!--f->upd)
			update(f);
		for (c = 0; c < 6; c++) {
			OPNF_CH *ch = &f->ch[c];
			long v;
			if (c == 5 && f->dac_on)
				v = f->dac;
			else if (!ch->live)
				continue;
			else
				v = ch_calc(f, ch);
			v = mpy(v, OUTG);
			if (ch->panl)
				l += v;
			if (ch->panr)
				r += v;
		}
		l = sat(l) >> 8;
		r = sat(r) >> 8;
		l += out[2 * k];
		r += out[2 * k + 1];
		out[2 * k] = (short)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
		out[2 * k + 1] = (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
	}
}
