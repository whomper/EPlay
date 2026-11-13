/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opnd.c - the YM2612's registers for the DSP (opnd.h). The chip's tables
 * as opn.c has them (its detune, key codes, envelope increments); the
 * envelope's steps the average of the chip's increments over its envelope
 * clocks in an update (OPN_UPD samples at the output's rate: the chip's
 * rate / 3 clocks a second), the attack's factor their product. Integers
 * only: it runs on the 68030 (and EPlay has no floating point).
 */
#include <string.h>
#include "opnd.h"
#include "tickcode.h"
#include "egmath.h"

static const unsigned char dt_tab[4][32] = {
	{ 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8 },
	{ 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16 },
	{ 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22 },
};
static const unsigned char fk_tab[16] = { 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3 };
static const unsigned char eg_lo[4][8] = {
	{ 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 } };
static const unsigned char eg_hi[4][8] = {
	{ 1, 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 2, 1, 1, 1, 2 }, { 1, 2, 1, 2, 1, 2, 1, 2 }, { 1, 2, 2, 2, 1, 2, 2, 2 } };
static const int slot_op[4] = { 0, 2, 1, 3 };

TICK_CODE("6Dopnd_eff")
static int eff(int r, int ksr)
{
	int v;
	if (!r)
		return 0;
	v = 2 * r + ksr;
	return v > 63 ? 63 : v;
}

/* each rate's increments over the chip's pattern: their mean an update
   (x OPN_Q) and the attack's factor an update (Q23), for the envelope's
   clocks an update (the chip's rate / 3 a second, OPN_UPD samples at the
   output's): integers only (EPlay has no floating point to spare) */
static void rate_tables(OPND *d)
{
	/* the envelope's clocks an update: clock / 144 / 3 / rate x OPN_UPD,
	   as a fraction egk_n / egk_d */
	unsigned long long egk_n = (unsigned long long)d->clock * OPN_UPD, egk_d = 432ULL * (unsigned long)d->rate;
	int r, k;
	for (r = 0; r < 64; r++) {
		int period, sum = 0;
		long long l2 = 0;
		int at_once = 0;
		if (r < 4) {
			d->r_mean[r] = 0;
			d->r_arf[r] = 8388607L;	/* (no attack: its factor 1) */
			continue;
		}
		if (r >= 60) {
			d->r_mean[r] = (long)((8ULL * egk_n * OPN_Q + egk_d / 2) / egk_d);
			d->r_arf[r] = 0;	/* (at once, as good as: the attack's last rates are) */
			continue;
		}
		if (r < 48) {
			period = 8 << (11 - (r >> 2));
			for (k = 0; k < 8; k++) {
				int inc = eg_lo[r & 3][k];
				sum += inc;
				l2 += eg_log2_att[inc];
			}
		} else {
			period = 8;
			for (k = 0; k < 8; k++) {
				int inc = eg_hi[r & 3][k] << ((r >> 2) - 12);
				sum += inc;
				if (inc >= 16)
					at_once = 1;
				else
					l2 += eg_log2_att[inc];
			}
		}
		d->r_mean[r] = (long)(((unsigned long long)sum * egk_n * OPN_Q + (unsigned long long)period * egk_d / 2) /
				      ((unsigned long long)period * egk_d));
		/* af^egk = 2^(l2 / period x egk) */
		d->r_arf[r] = at_once ? 0 : eg_exp2(l2 * (long long)egk_n / ((long long)period * (long long)egk_d));
		if (d->r_arf[r] > 8388607L)
			d->r_arf[r] = 8388607L;
	}
}

TICK_CODE("6Copnd_op_rates")
static void op_rates(OPND *d, OPND_OP *op)
{
	int r = eff(op->ar, op->ksr);
	op->arf = r >= 62 ? 0 : !r ? 8388607L : d->r_arf[r];
	op->d1 = d->r_mean[eff(op->d1r, op->ksr)];
	op->d2 = d->r_mean[eff(op->d2r, op->ksr)];
	r = op->rrr * 4 + 2 + op->ksr;
	op->rr = d->r_mean[r > 63 ? 63 : r];
}

TICK_CODE("6Bopnd_ch_steps")
static void ch_steps(OPND *d, int c)
{
	OPND_CH *ch = &d->ch[c];
	int i;
	for (i = 0; i < 4; i++) {
		OPND_OP *op = &ch->op[i];
		int fnum = ch->fnum, block = ch->block, kc, fc, dd;
		long inc;
		if (c == 2 && (d->ch3_mode & 0x40) && i < 3) {
			fnum = ch->fnum3[i];
			block = ch->block3[i];
		}
		kc = block << 2 | fk_tab[fnum >> 7];
		fc = (fnum << block) >> 1;
		dd = dt_tab[op->dt & 3][kc];
		fc += op->dt & 4 ? -dd : dd;
		fc &= 0x1ffff;
		inc = (long)fc * (op->mul ? op->mul * 2 : 1) >> 1;
		inc &= 0xfffff;
		/* (the chip's 20 bits a turn, 2^21 here: x 2, at the output's rate:
		   clock / 144 / rate, as stepk (Q16): two 32-bit multiplies, its
		   10-bit halves, not a 64-bit divide for each operator) */
		{
			unsigned long t = (unsigned long)(inc >> 10) * d->stepk;
			op->step = (long)((t >> 6) + ((((t & 63) << 10) + (unsigned long)(inc & 1023) * d->stepk +
						     32768) >> 16)) & 0xffffff;
		}
		if (op->ksr != kc >> (3 - op->ks)) {
			op->ksr = kc >> (3 - op->ks);
			op_rates(d, op);
			d->op_dirty |= 1UL << (c * 4 + i);
		} else
			d->step_dirty |= 1UL << (c * 4 + i);	/* (its step alone: STEP's 3 words, not OP's 10) */
	}
}

void opnd_init(OPND *d, unsigned long clock, long rate)
{
	int c, i;
	memset(d, 0, sizeof(*d));
	d->clock = clock;
	d->rate = rate;
	d->outg = OPN_OUTG;
	rate_tables(d);
	d->dstep = (long)((44100ULL << 16) / (unsigned long)rate);
	d->stepk = (unsigned long)(((2ULL * clock << 16) + 72ULL * (unsigned long)rate) / (144ULL * (unsigned long)rate));
	for (c = 0; c < 6; c++) {
		d->ch[c].pan = 3;
		for (i = 0; i < 4; i++) {
			OPND_OP *op = &d->ch[c].op[i];
			op->mul = 1;
			op->ksr = -1;
			op->sl = 0;
			op_rates(d, op);
		}
		ch_steps(d, c);
	}
	d->op_dirty = 0xffffff;
	d->ch_dirty = 0x3f;
}

void opnd_gain(OPND *d, long g)
{
	d->outg = g;
	d->ch_dirty = 0x3f;
}

TICK_CODE("69opnd_write")
void opnd_write(OPND *d, int port, int reg, int val)
{
	int c = (reg & 3) + port * 3, i;
	OPND_OP *op;
	if (reg < 0x30) {
		if (port)
			return;
		switch (reg) {
		case 0x22:
			d->lfo_on = (val >> 3) & 1;
			d->lfo_rate = val & 7;
			d->lfo_dirty = 1;
			break;
		case 0x27:
			if ((val ^ d->ch3_mode) & 0x40) {
				d->ch3_mode = val;
				ch_steps(d, 2);
			}
			d->ch3_mode = val;
			break;
		case 0x28:
			if ((val & 3) == 3)
				break;
			c = (val & 3) + (val & 4 ? 3 : 0);
			for (i = 0; i < 4; i++) {
				int on = (val >> (4 + i)) & 1;
				op = &d->ch[c].op[i];
				if (on != op->key) {
					d->key_event[c * 4 + i] = on ? OPND_KEYON : OPND_KEYOFF;
					d->key_dirty |= 1UL << (c * 4 + i);
				}
				op->key = on;
			}
			break;
		case 0x2a:
			if (d->at_fn)
				vgmdac_put(&d->dq, d->at_fn(d->at_v) - d->dq.base, val);
			else
				d->dq.val = val;
			break;
		case 0x2b:
			d->dac_on = val >> 7;
			d->dac_dirty = 1;
			break;
		}
		return;
	}
	if ((reg & 3) == 3)
		return;
	if (reg < 0xa0) {
		op = &d->ch[c].op[slot_op[(reg >> 2) & 3]];
		switch (reg & 0xf0) {
		case 0x30:
			op->dt = (val >> 4) & 7;
			op->mul = val & 15;
			ch_steps(d, c);
			return;
		case 0x40:
			op->tlr = val & 127;
			op->tl = (long)op->tlr << 3;
			break;
		case 0x50:
			op->ks = val >> 6;
			op->ar = val & 31;
			op->ksr = -1;
			ch_steps(d, c);
			return;
		case 0x60:
			op->am = val >> 7;
			op->d1r = val & 31;
			op_rates(d, op);
			break;
		case 0x70:
			op->d2r = val & 31;
			op_rates(d, op);
			break;
		case 0x80:
			op->slr = val >> 4;
			op->sl = (long)((op->slr == 15 ? 31 : op->slr) << 5) * OPN_Q;
			op->rrr = val & 15;
			op_rates(d, op);
			break;
		default:
			return;
		}
		d->op_dirty |= 1UL << (c * 4 + slot_op[(reg >> 2) & 3]);
		return;
	}
	switch (reg & 0xfc) {
	case 0xa0:
		d->ch[c].fnum = (d->latch[port] & 7) << 8 | val;
		d->ch[c].block = (d->latch[port] >> 3) & 7;
		ch_steps(d, c);
		break;
	case 0xa4:
		d->latch[port] = (unsigned char)val;
		break;
	case 0xa8:
		if (!port) {
			static const int k[3] = { 2, 0, 1 };
			int j = k[reg & 3];
			d->ch[2].fnum3[j] = (d->latch3 & 7) << 8 | val;
			d->ch[2].block3[j] = (d->latch3 >> 3) & 7;
			ch_steps(d, 2);
		}
		break;
	case 0xac:
		if (!port)
			d->latch3 = (unsigned char)val;
		break;
	case 0xb0:
		d->ch[c].fb = (val >> 3) & 7;
		d->ch[c].alg = val & 7;
		d->ch_dirty |= 1u << c;
		break;
	case 0xb4:
		d->ch[c].pan = val >> 6;
		d->ch[c].ams = (val >> 4) & 3;
		d->ch[c].pms = val & 7;
		d->ch_dirty |= 1u << c;
		break;
	}
}

static const unsigned char lfo_per[8] = { 108, 77, 71, 67, 62, 44, 8, 5 };
static const unsigned char ams_shift[4] = { 8, 3, 1, 0 };
static const unsigned short pms_depth[8] = { 0, 2, 4, 6, 8, 12, 23, 46 };

void opnd_send_drate(OPND *d, void (*put)(void *u, long w), void *u)
{
	/* 44,100 / rate (Q16), the place 0 */
	put(u, OPNC_DRATE);
	put(u, d->dstep);
	put(u, 0);
}

TICK_CODE("6Aopnd_tick")
int opnd_tick(OPND *d, int (*run)(void *v, long n), long (*at)(void *v), void *v, long n,
	      void (*put)(void *u, long w), void *u)
{
	long nb = n < VGMDAC_MAX ? n : VGMDAC_MAX;
	int live, k;
	d->dq.ne = 0;
	d->dq.base = at(v);
	d->at_fn = at;
	d->at_v = v;
	live = run(v, n);
	d->at_fn = 0;
	opnd_send(d, put, u);
	/* the DAC's writes (the drums): the DSP makes the samples from them */
	if (d->dq.ne && d->dac_on) {
		long out[3 + VGMDAC_MAX];
		out[0] = OPNC_DACE;
		out[1] = nb << 16;
		out[2] = d->dq.ne;
		for (k = 0; k < d->dq.ne; k++)
			out[3 + k] = d->dq.ev[k];
		if (d->putn)
			d->putn(u, out, 3 + d->dq.ne);
		else
			for (k = 0; k < 3 + d->dq.ne; k++)
				put(u, out[k]);
	}
	return live;
}

/* (opnd_send's words, sent together: put each, or putn the lot) */
static long ob[400];
static int on_;
#define EMIT(w)	(ob[on_++] = (w))

TICK_CODE("6Eopnd_send")
void opnd_send(OPND *d, void (*put)(void *u, long w), void *u)
{
	int c, k;
	unsigned long ops = d->op_dirty | d->key_dirty | d->step_dirty;
	/* (most ticks change nothing but the DAC: the stock Falcon's 68030
	   passes over the 30 channels' and operators' tests) */
	if (!ops && !d->ch_dirty && !d->lfo_dirty && !d->dac_dirty)
		return;
	on_ = 0;
	for (c = 0; c < 6; c++) {
		OPND_CH *ch = &d->ch[c];
		if (!((ops >> (c * 4)) & 15) && !(d->ch_dirty & (1u << c)))
			continue;
		if (d->ch_dirty & (1u << c)) {
			EMIT(OPNC_CH);
			EMIT(c);
			EMIT(ch->alg);
			EMIT(ch->fb ? 1L << (ch->fb + 14) : 0);
			EMIT(ch->pan & 2 ? d->outg : 0);
			EMIT(ch->pan & 1 ? d->outg : 0);
			EMIT(ch->ams ? 1L << (22 - ams_shift[ch->ams]) : 0);
			EMIT(ch->pms ? (pms_depth[ch->pms] * 8388608L + 250) / 500 : 0);
		}
		for (k = 0; k < 4; k++) {
			int i = c * 4 + k;
			const OPND_OP *op = &ch->op[k];
			if (d->op_dirty & (1UL << i)) {
				EMIT(OPNC_OP);
				EMIT(i);
				EMIT(op->step);
				EMIT(op->tl);
				EMIT(op->arf);
				EMIT(op->d1);
				EMIT(op->d2);
				EMIT(op->rr);
				EMIT(op->sl);
				EMIT(op->am);
			} else if (d->step_dirty & (1UL << i)) {
				EMIT(OPNC_STEP);
				EMIT(i);
				EMIT(op->step);
			}
			if (d->key_event[i]) {
				EMIT(OPNC_KEY);
				EMIT(i);
				EMIT(d->key_event[i]);
				d->key_event[i] = 0;
			}
		}
	}
	if (d->lfo_dirty) {
		EMIT(OPNC_LFO);
		/* (clock / 144 / rate x OPN_UPD / its period, x 2^17) */
		EMIT(d->lfo_on ? (long)(((unsigned long long)d->clock * OPN_UPD * 131072 +
					 72ULL * (unsigned long)d->rate * lfo_per[d->lfo_rate]) /
					(144ULL * (unsigned long)d->rate * lfo_per[d->lfo_rate])) : 0);
		EMIT(!d->lfo_on);
	}
	if (d->dac_dirty && d->dac_on != d->dac_sent) {
		EMIT(OPNC_DAC);
		EMIT(d->dac_on);
		EMIT(((long)(d->dq.val - 128) << 16) & 0xffffff);
		d->dac_sent = d->dac_on;
	}
	d->dac_dirty = 0;
	d->op_dirty = 0;
	d->step_dirty = 0;
	d->key_dirty = 0;
	d->ch_dirty = 0;
	d->lfo_dirty = 0;
	if (d->putn)
		d->putn(u, ob, on_);
	else
		for (k = 0; k < on_; k++)
			put(u, ob[k]);
}
