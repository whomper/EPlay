/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmcpu.c - a VGM's chips made on the CPU (vgmcpu.h).
 */
#include "vgmcpu.h"

static void sn_put(void *u, long w)
{
	sncpu_word(&((VGMCPU *)u)->sn, w);
}

void vgmcpu_sn(void *u, int val)
{
	sncmd_write(&((VGMCPU *)u)->psg, val);
}

void vgmcpu_init(VGMCPU *c, const VGMHEAD *h, long rate)
{
	c->rate = rate;
	c->step = (unsigned long)(((unsigned long long)VGM_RATE << 16) / (unsigned long)rate);
	c->acc = 0;
	c->has_sn = h->sn_clock != 0;
	sncpu_init(&c->sn);
	if (c->has_sn)
		sncmd_init(&c->psg, h->sn_clock, rate, h->sn_fb, h->sn_w, 0, 0, sn_put, c);
}

int vgmcpu_render(VGMCPU *c, VGMRUN *r, short *lr, int n)
{
	int made = 0;
	while (made < n) {
		/* the file's commands a block of 32 at a time (0.7 ms at 49,170
		   Hz: the DSP's tick runs every 2), then its samples */
		int k, blk = n - made < 32 ? n - made : 32;
		c->acc += c->step * (unsigned long)blk;
		if (!vgm_run(r, (long)(c->acc >> 16)))
			break;
		c->acc &= 0xffff;
		for (k = 0; k < blk; k++) {
			short v = c->has_sn ? (short)(sncpu_sample(&c->sn) >> 8) : 0;
			lr[2 * (made + k)] = v;
			lr[2 * (made + k) + 1] = v;
		}
		made += blk;
	}
	return made;
}
