/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opn.c - the YM2612 model (opn.h). The chip's arithmetic as Yamaha's OPN
 * family documents it and the emulators that hold to the chip have it:
 *
 *	- a phase of 20 bits, on by ((F-number << block) >> 1 + detune) x
 *	  multiple (0: a half) each of its samples; the sine's index its top
 *	  10 bits plus the modulation (an operator's output >> 1, operator 1's
 *	  own two last outputs >> (10 - feedback));
 *	- the output: a log-sine (256 a quarter) plus the attenuation (x 4),
 *	  through the exponent table: 14 bits, signed;
 *	- the envelope: 10 bits of attenuation (0.094 dB a step), its rates
 *	  (2 R + the key's scale, 63 at most; the release 4 R + 2) stepping by
 *	  the chip's increments each envelope clock (the chip's rate / 3): the
 *	  attack an exponential approach, the decays and the release linear;
 *	- the total level (<< 3), the sustain level (<< 5; 15 is all 31);
 *	- the eight algorithms; a channel's carriers summed and held to 14
 *	  bits, then its 9-bit DAC (>> 5), left or right or both.
 */
#include <string.h>
#include <math.h>
#include "opn.h"

enum { OPN_ATTACK, OPN_DECAY, OPN_SUSTAIN, OPN_RELEASE, OPN_OFF };

static unsigned short logsin[256], expt[256];
static int tables;

static const unsigned char dt_tab[4][32] = {
	{ 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8 },
	{ 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16 },
	{ 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22 },
};
static const unsigned char fk_tab[16] = { 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3 };
/* the envelope's increments: below rate 48 one (or none) each 2^shift
   clocks by these patterns; from 48 these, doubled each four rates */
static const unsigned char eg_lo[4][8] = {
	{ 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 } };
static const unsigned char eg_hi[4][8] = {
	{ 1, 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 2, 1, 1, 1, 2 }, { 1, 2, 1, 2, 1, 2, 1, 2 }, { 1, 2, 2, 2, 1, 2, 2, 2 } };
/* the LFO: chip samples a step (of 128), by its rate */
static const unsigned char lfo_per[8] = { 108, 77, 71, 67, 62, 44, 8, 5 };
static const unsigned char ams_shift[4] = { 8, 3, 1, 0 };
/* the vibrato's depth, in 1/1000 of the frequency at the LFO's peak, by PMS */
static const unsigned short pms_depth[8] = { 0, 2, 4, 6, 8, 12, 23, 46 };

static void make_tables(void)
{
	int i;
	for (i = 0; i < 256; i++) {
		logsin[i] = (unsigned short)floor(-log(sin((i + 0.5) * M_PI / 512)) / log(2) * 256 + 0.5);
		expt[i] = (unsigned short)floor((pow(2, i / 256.0) - 1) * 1024 + 0.5);
	}
	tables = 1;
}

/* an operator's output: its sine at idx (10 bits) through its attenuation */
static int op_out(int idx, int env)
{
	int x = idx & 0xff, total, v;
	if (idx & 0x100)
		x = 255 - x;
	total = logsin[x] + (env << 2);
	if (total >= 13 * 256)
		return 0;
	v = ((expt[(total & 0xff) ^ 0xff] | 0x400) << 2) >> (total >> 8);
	return idx & 0x200 ? -v : v;
}

static int rate_of(int r, int ksr)
{
	int v;
	if (!r)
		return 0;
	v = 2 * r + ksr;
	return v > 63 ? 63 : v;
}

/* the operators' steps and key scales from the channel's frequency */
static void ch_steps(OPN *o, int c)
{
	OPN_CH *ch = &o->ch[c];
	int i;
	for (i = 0; i < 4; i++) {
		OPN_OP *op = &ch->op[i];
		int fnum = ch->fnum, block = ch->block, kc, fc, d;
		long inc;
		if (c == 2 && (o->ch3_mode & 0x40) && i < 3) {
			fnum = ch->fnum3[i];
			block = ch->block3[i];
		}
		kc = block << 2 | fk_tab[fnum >> 7];
		if (i == 3)
			ch->kc = kc;	/* (operator 4 always the channel's own) */
		fc = (fnum << block) >> 1;
		d = dt_tab[op->dt & 3][kc];
		fc += op->dt & 4 ? -d : d;
		fc &= 0x1ffff;
		inc = (long)fc * (op->mul ? op->mul * 2 : 1) >> 1;
		inc &= 0xfffff;
		op->step = (unsigned long)(((unsigned long long)inc << 12) * o->ratio >> 16);
		op->ksr = kc >> (3 - op->ks);
	}
}

void opn_init(OPN *o, unsigned long clock, long rate)
{
	int c, i;
	if (!tables)
		make_tables();
	memset(o, 0, sizeof(*o));
	o->clock = clock;
	o->rate = rate;
	/* the chip's sample rate: clock / 144 */
	o->ratio = (unsigned long)((double)clock / 144 / rate * 65536 + 0.5);
	o->eg_step = (unsigned long)((double)clock / 144 / 3 / rate * 65536 + 0.5);
	for (c = 0; c < 6; c++) {
		o->ch[c].pan = 3;
		for (i = 0; i < 4; i++) {
			o->ch[c].op[i].att = 1023;
			o->ch[c].op[i].state = OPN_OFF;
			o->ch[c].op[i].mul = 1;
		}
	}
}

static const int slot_op[4] = { 0, 2, 1, 3 };	/* registers' order: S1, S3, S2, S4 */

void opn_write(OPN *o, int port, int reg, int val)
{
	int c = (reg & 3) + port * 3, i;
	OPN_OP *op;
	if (reg < 0x30) {
		if (port)
			return;
		switch (reg) {
		case 0x22:
			o->lfo_on = (val >> 3) & 1;
			o->lfo_rate = val & 7;
			o->lfo_step = (unsigned long)((double)o->clock / 144 / o->rate / lfo_per[o->lfo_rate] * 65536);
			if (!o->lfo_on)
				o->lfo_phase = 0;
			break;
		case 0x27:
			if ((val ^ o->ch3_mode) & 0x40) {
				o->ch3_mode = val;
				ch_steps(o, 2);
			}
			o->ch3_mode = val;
			break;
		case 0x28:
			c = (val & 3) + (val & 4 ? 3 : 0);
			if ((val & 3) == 3)
				break;
			for (i = 0; i < 4; i++) {
				int on = (val >> (4 + i)) & 1;
				op = &o->ch[c].op[i];
				if (on && !op->key) {
					op->phase = 0;
					op->state = OPN_ATTACK;
					if (rate_of(op->ar, op->ksr) >= 62)
						op->att = 0;
				} else if (!on && op->key)
					op->state = OPN_RELEASE;
				op->key = on;
			}
			break;
		case 0x2a:
			o->dac = val;
			break;
		case 0x2b:
			o->dac_on = val >> 7;
			break;
		}
		return;
	}
	if ((reg & 3) == 3)
		return;
	if (reg < 0xa0) {
		op = &o->ch[c].op[slot_op[(reg >> 2) & 3]];
		switch (reg & 0xf0) {
		case 0x30:
			op->dt = (val >> 4) & 7;
			op->mul = val & 15;
			ch_steps(o, c);
			break;
		case 0x40:
			op->tl = (val & 127) << 3;
			break;
		case 0x50:
			op->ks = val >> 6;
			op->ar = val & 31;
			ch_steps(o, c);
			break;
		case 0x60:
			op->am = val >> 7;
			op->d1r = val & 31;
			break;
		case 0x70:
			op->d2r = val & 31;
			break;
		case 0x80:
			op->sl = ((val >> 4) == 15 ? 31 : val >> 4) << 5;
			op->rr = val & 15;
			break;
		}
		return;
	}
	switch (reg & 0xfc) {
	case 0xa0:
		o->ch[c].fnum = (o->latch[port] & 7) << 8 | val;
		o->ch[c].block = (o->latch[port] >> 3) & 7;
		ch_steps(o, c);
		break;
	case 0xa4:
		o->latch[port] = (unsigned char)val;
		break;
	case 0xa8:
		if (!port) {
			/* channel 3's operators: $A9 operator 1, $AA 2, $A8 3 */
			static const int k[3] = { 2, 0, 1 };
			int j = k[reg & 3];
			o->ch[2].fnum3[j] = (o->ch[2].fnum3[j] & 0x700) | val;
			ch_steps(o, 2);
		}
		break;
	case 0xac:
		if (!port) {
			static const int k[3] = { 2, 0, 1 };
			int j = k[reg & 3];
			o->ch[2].fnum3[j] = (val & 7) << 8 | (o->ch[2].fnum3[j] & 0xff);
			o->ch[2].block3[j] = (val >> 3) & 7;
		}
		break;
	case 0xb0:
		o->ch[c].fb = (val >> 3) & 7;
		o->ch[c].alg = val & 7;
		break;
	case 0xb4:
		o->ch[c].pan = val >> 6;
		o->ch[c].ams = (val >> 4) & 3;
		o->ch[c].pms = val & 7;
		break;
	}
}

/* an envelope clock for one operator */
static void eg_clock(OPN *o, OPN_OP *op)
{
	int r, inc;
	switch (op->state) {
	case OPN_ATTACK:
		r = rate_of(op->ar, op->ksr);
		break;
	case OPN_DECAY:
		r = rate_of(op->d1r, op->ksr);
		break;
	case OPN_SUSTAIN:
		r = rate_of(op->d2r, op->ksr);
		break;
	case OPN_RELEASE:
		r = op->rr * 4 + 2 + op->ksr;
		if (r > 63)
			r = 63;
		break;
	default:
		return;
	}
	if (r < 4)
		return;
	if (r < 48) {
		int shift = 11 - (r >> 2);
		if (o->eg_count & ((1UL << shift) - 1))
			return;
		inc = eg_lo[r & 3][(o->eg_count >> shift) & 7];
	} else if (r < 60)
		inc = eg_hi[r & 3][o->eg_count & 7] << ((r >> 2) - 12);
	else
		inc = 8;
	if (!inc)
		return;
	switch (op->state) {
	case OPN_ATTACK:
		if (r >= 62)
			op->att = 0;
		else
			op->att += ((-op->att - 1) * inc) >> 4;
		if (op->att <= 0) {
			op->att = 0;
			op->state = OPN_DECAY;
		}
		break;
	case OPN_DECAY:
		op->att += inc;
		if (op->att >= op->sl)
			op->state = OPN_SUSTAIN;
		break;
	default:
		op->att += inc;
		if (op->att >= 1023) {
			op->att = 1023;
			if (op->state == OPN_RELEASE)
				op->state = OPN_OFF;
		}
		break;
	}
}

/* a channel's sample: its 14-bit output */
static int ch_sample(OPN *o, OPN_CH *ch, int am, long pm)
{
	int env[4], out[4], i, m, s;
	unsigned long ph[4];
	for (i = 0; i < 4; i++) {
		OPN_OP *op = &ch->op[i];
		int e = op->att + op->tl + (op->am ? am >> ams_shift[ch->ams] : 0);
		env[i] = e > 1023 ? 1023 : e;
		ph[i] = op->phase >> 22;
		if (pm && ch->pms)
			op->phase += (unsigned long)((long long)op->step * (1000 + pm * pms_depth[ch->pms] / 128) / 1000);
		else
			op->phase += op->step;
	}
	/* operator 1, with its feedback */
	m = ch->fb ? (ch->op[0].out[0] + ch->op[0].out[1]) >> (10 - ch->fb) : 0;
	out[0] = op_out((int)ph[0] + m, env[0]);
	ch->op[0].out[1] = ch->op[0].out[0];
	ch->op[0].out[0] = out[0];
#define MOD(v) ((v) >> 1)
	switch (ch->alg) {
	case 0:
		out[1] = op_out((int)ph[1] + MOD(out[0]), env[1]);
		out[2] = op_out((int)ph[2] + MOD(out[1]), env[2]);
		out[3] = op_out((int)ph[3] + MOD(out[2]), env[3]);
		s = out[3];
		break;
	case 1:
		out[1] = op_out((int)ph[1], env[1]);
		out[2] = op_out((int)ph[2] + MOD(out[0] + out[1]), env[2]);
		out[3] = op_out((int)ph[3] + MOD(out[2]), env[3]);
		s = out[3];
		break;
	case 2:
		out[1] = op_out((int)ph[1], env[1]);
		out[2] = op_out((int)ph[2] + MOD(out[1]), env[2]);
		out[3] = op_out((int)ph[3] + MOD(out[0] + out[2]), env[3]);
		s = out[3];
		break;
	case 3:
		out[1] = op_out((int)ph[1] + MOD(out[0]), env[1]);
		out[2] = op_out((int)ph[2], env[2]);
		out[3] = op_out((int)ph[3] + MOD(out[1] + out[2]), env[3]);
		s = out[3];
		break;
	case 4:
		out[1] = op_out((int)ph[1] + MOD(out[0]), env[1]);
		out[2] = op_out((int)ph[2], env[2]);
		out[3] = op_out((int)ph[3] + MOD(out[2]), env[3]);
		s = out[1] + out[3];
		break;
	case 5:
		out[1] = op_out((int)ph[1] + MOD(out[0]), env[1]);
		out[2] = op_out((int)ph[2] + MOD(out[0]), env[2]);
		out[3] = op_out((int)ph[3] + MOD(out[0]), env[3]);
		s = out[1] + out[2] + out[3];
		break;
	case 6:
		out[1] = op_out((int)ph[1] + MOD(out[0]), env[1]);
		out[2] = op_out((int)ph[2], env[2]);
		out[3] = op_out((int)ph[3], env[3]);
		s = out[1] + out[2] + out[3];
		break;
	default:
		out[1] = op_out((int)ph[1], env[1]);
		out[2] = op_out((int)ph[2], env[2]);
		out[3] = op_out((int)ph[3], env[3]);
		s = out[0] + out[1] + out[2] + out[3];
		break;
	}
#undef MOD
	if (s > 8191)
		s = 8191;
	if (s < -8192)
		s = -8192;
	return s;
}

void opn_run(OPN *o, short *out, int n)
{
	int k, c, i;
	for (k = 0; k < n; k++) {
		long l = 0, r = 0;
		int am = 0;
		long pm = 0;
		/* the envelope generator's clocks due by now */
		o->eg_acc += o->eg_step;
		while (o->eg_acc >= 65536) {
			o->eg_acc -= 65536;
			o->eg_count++;
			for (c = 0; c < 6; c++)
				for (i = 0; i < 4; i++)
					eg_clock(o, &o->ch[c].op[i]);
		}
		if (o->lfo_on) {
			int step;
			o->lfo_phase += o->lfo_step;
			step = (int)((o->lfo_phase >> 16) & 127);
			/* AM: a triangle, 0 to 126; PM: a triangle, -128 to 128 */
			am = step < 64 ? step * 2 : (127 - step) * 2;
			pm = step < 32 ? step * 4 : step < 96 ? (64 - step) * 4 : (step - 128) * 4;
		}
		for (c = 0; c < 6; c++) {
			OPN_CH *ch = &o->ch[c];
			int s = c == 5 && o->dac_on ? (o->dac - 128) << 6 : ch_sample(o, ch, am, pm);
			s >>= 5;	/* (its 9-bit DAC) */
			if (ch->pan & 2)
				l += s;
			if (ch->pan & 1)
				r += s;
		}
		l = out[2 * k] + l * 33;	/* (the reference's level: its DAC summed over its 24 clocks) */
		r = out[2 * k + 1] + r * 33;
		out[2 * k] = (short)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
		out[2 * k + 1] = (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
	}
}
