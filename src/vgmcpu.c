/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmcpu.c - a VGM's chips made on the CPU (vgmcpu.h).
 */
#include <string.h>
#include "vgmcpu.h"

static void sn_put(void *u, long w)
{
	sncpu_word(&((VGMCPU *)u)->sn, w);
}

static void opn_put(void *u, long w)
{
	opnc_word(&((VGMCPU *)u)->fm, w);
}

void vgmcpu_sn(void *u, int val)
{
	sncmd_write(&((VGMCPU *)u)->psg, val);
}

static void opn_write(void *u, int port, int reg, int val)
{
	opnd_write(&((VGMCPU *)u)->opn, port, reg, val);
}

static void opl_put(void *u, long w)
{
	oplc_word(&((VGMCPU *)u)->fl, w);
}

static void opl_write(void *u, int port, int reg, int val)
{
	opld_write(&((VGMCPU *)u)->dl, (port & 1) << 8 | (reg & 0xff), val);
}

static int run_fn(void *v, long n)
{
	return vgm_run(v, n);
}

static long at_fn(void *v)
{
	return ((VGMRUN *)v)->at;
}

void vgmcpu_init(VGMCPU *c, const VGMHEAD *h, long rate, const unsigned long *opn_sin,
		 const unsigned long *opn_gain, const OPLC_TABS *opl)
{
	memset(c, 0, sizeof(*c));
	c->rate = rate;
	c->step = (unsigned long)(((unsigned long long)VGM_RATE << 16) / (unsigned long)rate);
	c->has_opn = h->ym2612_clock != 0;
	c->has_opl = !c->has_opn && (h->ym3812_clock || h->ymf262_clock) && opl;
	/* (with the OPL, its SN76489 left out: as on the DSP) */
	c->has_sn = h->sn_clock != 0 && !c->has_opl;
	sncpu_init(&c->sn);
	if (c->has_opl) {
		oplc_init(&c->fl, opl);
		if (h->ymf262_clock) {
			opld_init3(&c->dl, h->ymf262_clock, rate);
			opld_channels(&c->dl, OPL_CH);
		} else
			opld_init(&c->dl, h->ym3812_clock, rate);
		opld_send(&c->dl, opl_put, c);
	}
	if (c->has_opn) {
		opnc_init(&c->fm, opn_sin, opn_gain);
		opnd_init(&c->opn, h->ym2612_clock, rate);
		opnd_send_drate(&c->opn, opn_put, c);
		opnd_send(&c->opn, opn_put, c);
	}
	if (c->has_sn) {
		/* (the Mega Drive's in the YM2612's model, its amplitudes halved:
		   opn.a56's) */
		if (c->has_opn)
			sncmd_init(&c->psg, h->sn_clock, rate, h->sn_fb, h->sn_w, OPNC_SN, 1, opn_put, c);
		else
			sncmd_init(&c->psg, h->sn_clock, rate, h->sn_fb, h->sn_w, 0, 0, sn_put, c);
	}
}

void vgmcpu_attach(VGMCPU *c, VGMRUN *r)
{
	if (c->has_opn) {
		r->opn_fn = opn_write;
		r->dac = &c->opn.dq;	/* (the drums' samples straight to its queue) */
	}
	if (c->has_opl)
		r->opl_fn = opl_write;
}

int vgmcpu_render(VGMCPU *c, VGMRUN *r, short *lr, int n)
{
	int made = 0, most = (int)(c->rate * 64 / VGM_RATE);
	/* (a block's drums' places in 96 of the file's samples at most:
	   VGMDAC_MAX; 64 of them a block) */
	if (most > 32)
		most = 32;
	if (most < 8)
		most = 8;
	while (made < n) {
		/* the file's commands a block of 32 at a time (0.7 ms at 49,170
		   Hz, 1.3 at 24,585: the DSP's tick runs every 2), then its
		   samples */
		int k, blk = n - made < most ? n - made : most;
		short *o = lr + 2 * made;
		long fs;
		c->acc += c->step * (unsigned long)blk;
		fs = (long)(c->acc >> 16);
		c->acc &= 0xffff;
		if (c->has_opn) {
			if (!opnd_tick(&c->opn, run_fn, at_fn, r, fs, opn_put, c))
				break;
			memset(o, 0, (size_t)blk * 4);
			opnc_run(&c->fm, o, blk);
		} else if (c->has_opl) {
			int live = vgm_run(r, fs);
			opld_send(&c->dl, opl_put, c);
			if (!live)
				break;
			memset(o, 0, (size_t)blk * 4);
			oplc_run(&c->fl, o, blk);
		} else {
			if (!vgm_run(r, fs))
				break;
			for (k = 0; k < blk; k++) {
				short v = c->has_sn ? (short)(sncpu_sample(&c->sn) >> 8) : 0;
				o[2 * k] = v;
				o[2 * k + 1] = v;
			}
		}
		made += blk;
	}
	return made;
}
