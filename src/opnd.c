/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opnd.c - the YM2612's registers for the DSP (opnd.h). The chip's tables
 * as opn.c has them (its detune, key codes, envelope increments); the
 * envelope's steps the average of the chip's increments over its envelope
 * clocks in an update (OPN_UPD samples at the output's rate: the chip's
 * rate / 3 clocks a second), the attack's factor their product.
 */
#include <string.h>
#include <math.h>
#include "opnd.h"

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

static int eff(int r, int ksr)
{
	int v;
	if (!r)
		return 0;
	v = 2 * r + ksr;
	return v > 63 ? 63 : v;
}

/* the chip's increments for rate r: their mean an envelope clock, and the
   attack's factor a clock (the product's root), over its pattern */
static void rate_avg(int r, double *mean, double *af)
{
	int k, period;
	double sum = 0, lp = 0;
	if (r < 4) {
		*mean = 0;
		*af = 1;
		return;
	}
	if (r >= 60) {
		*mean = 8;
		*af = 0;	/* (at once, as good as: the attack's last two rates are) */
		return;
	}
	if (r < 48) {
		int shift = 11 - (r >> 2);
		period = 8 << shift;
		for (k = 0; k < 8; k++) {
			int inc = eg_lo[r & 3][k];
			sum += inc;
			lp += log(1 - inc / 16.0);
		}
	} else {
		period = 8;
		for (k = 0; k < 8; k++) {
			int inc = eg_hi[r & 3][k] << ((r >> 2) - 12);
			sum += inc;
			lp += inc >= 16 ? -50 : log(1 - inc / 16.0);
		}
	}
	*mean = sum / period;
	*af = exp(lp / period);
}

static void op_rates(OPND *d, OPND_OP *op)
{
	double mean, af;
	int r;
	r = eff(op->ar, op->ksr);
	rate_avg(r, &mean, &af);
	op->arf = r >= 62 || af == 0 ? 0 : !r ? 8388607L : (long)(pow(af, d->egk) * 8388608.0);
	if (op->arf > 8388607L)
		op->arf = 8388607L;
	rate_avg(eff(op->d1r, op->ksr), &mean, &af);
	op->d1 = (long)(mean * d->egk * OPN_Q + 0.5);
	rate_avg(eff(op->d2r, op->ksr), &mean, &af);
	op->d2 = (long)(mean * d->egk * OPN_Q + 0.5);
	r = op->rrr * 4 + 2 + op->ksr;
	rate_avg(r > 63 ? 63 : r, &mean, &af);
	op->rr = (long)(mean * d->egk * OPN_Q + 0.5);
}

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
		/* (the chip's 20 bits a turn, 2^21 here: x 2, at the output's rate) */
		op->step = (long)(inc * 2.0 * d->ratio + 0.5) & 0xffffff;
		if (op->ksr != kc >> (3 - op->ks)) {
			op->ksr = kc >> (3 - op->ks);
			op_rates(d, op);
		}
		d->op_dirty |= 1UL << (c * 4 + i);
	}
}

void opnd_init(OPND *d, unsigned long clock, long rate)
{
	int c, i;
	memset(d, 0, sizeof(*d));
	d->clock = clock;
	d->rate = rate;
	d->ratio = (double)clock / 144 / rate;
	d->egk = (double)clock / 144 / 3 / rate * OPN_UPD;
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
				if (on != op->key)
					d->key_event[c * 4 + i] = on ? OPND_KEYON : OPND_KEYOFF;
				op->key = on;
			}
			break;
		case 0x2a:
			d->dac = val;
			d->dac_dirty = 1;
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

void opnd_send_dac(OPND *d, void (*put)(void *u, long w), void *u)
{
	if (d->dac_dirty) {
		put(u, OPNC_DAC);
		put(u, d->dac_on);
		put(u, ((long)(d->dac - 128) << 16) & 0xffffff);
		d->dac_dirty = 0;
	}
}

void opnd_send(OPND *d, void (*put)(void *u, long w), void *u)
{
	int c, k;
	for (c = 0; c < 6; c++) {
		OPND_CH *ch = &d->ch[c];
		if (d->ch_dirty & (1u << c)) {
			put(u, OPNC_CH);
			put(u, c);
			put(u, ch->alg);
			put(u, ch->fb ? 1L << (ch->fb + 14) : 0);
			put(u, ch->pan & 2 ? OPN_OUTG : 0);
			put(u, ch->pan & 1 ? OPN_OUTG : 0);
			put(u, ch->ams ? 1L << (22 - ams_shift[ch->ams]) : 0);
			put(u, ch->pms ? (long)(pms_depth[ch->pms] * 8388608.0 / 500 + 0.5) : 0);
		}
		for (k = 0; k < 4; k++) {
			int i = c * 4 + k;
			const OPND_OP *op = &ch->op[k];
			if (d->op_dirty & (1UL << i)) {
				put(u, OPNC_OP);
				put(u, i);
				put(u, op->step);
				put(u, op->tl);
				put(u, op->arf);
				put(u, op->d1);
				put(u, op->d2);
				put(u, op->rr);
				put(u, op->sl);
				put(u, op->am);
			}
			if (d->key_event[i]) {
				put(u, OPNC_KEY);
				put(u, i);
				put(u, d->key_event[i]);
				d->key_event[i] = 0;
			}
		}
	}
	if (d->lfo_dirty) {
		put(u, OPNC_LFO);
		put(u, d->lfo_on ? (long)(d->ratio * OPN_UPD / lfo_per[d->lfo_rate] * 131072 + 0.5) : 0);
		put(u, !d->lfo_on);
	}
	opnd_send_dac(d, put, u);
	d->op_dirty = 0;
	d->ch_dirty = 0;
	d->lfo_dirty = 0;
}
