/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmcpu.h - a VGM's chips made on the CPU (phase 11, Erez, 14 Nov 2026:
 * a 68060 with no Falcon DSP free): the file's commands run a block of
 * samples at a time (vgm.c), the chips' C forms make that block's
 * samples, 16-bit stereo at the output's rate. The SN76489 so far
 * (sncpu.c: sn.a56's arithmetic from sncmd.c's commands). No hardware:
 * ui/vgmplay.c feeds play.c's queue with it; tests/vgm checks it.
 */
#ifndef EPLAY_VGMCPU_H
#define EPLAY_VGMCPU_H

#include "vgm.h"
#include "sncmd.h"
#include "sncpu.h"

typedef struct {
	long rate;		/* the output's */
	unsigned long step, acc;	/* the file's samples an output sample, Q16 */
	SNCMD psg;
	SNCPU sn;
	int has_sn;
} VGMCPU;

/* the chips h has (that it can make) at rate; r's callbacks set for them */
void vgmcpu_init(VGMCPU *c, const VGMHEAD *h, long rate);
/* the callbacks for vgm_begin (u: the VGMCPU) */
void vgmcpu_sn(void *u, int val);
/* n frames into lr (left, right): the file run alongside; the frames made,
   fewer at its end */
int vgmcpu_render(VGMCPU *c, VGMRUN *r, short *lr, int n);

#endif
