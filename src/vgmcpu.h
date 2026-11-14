/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmcpu.h - a VGM's chips made on the CPU (phase 11, Erez, 14 Nov 2026:
 * a 68060 with no Falcon DSP free): the file's commands run a block of
 * samples at a time (vgm.c), the chips' C forms make that block's
 * samples, 16-bit stereo at the output's rate. The SN76489 (sncpu.c:
 * sn.a56's arithmetic from sncmd.c's commands) and the YM2612 (opncpu.c:
 * the DSP's model in 32 bits, from opnd.c's commands, its drums and the
 * Mega Drive's SN76489 in it as opn.a56 has them). No hardware:
 * ui/vgmplay.c feeds play.c's queue with it; tests/vgm checks it.
 */
#ifndef EPLAY_VGMCPU_H
#define EPLAY_VGMCPU_H

#include "vgm.h"
#include "sncmd.h"
#include "sncpu.h"
#include "opnd.h"
#include "opncpu.h"

typedef struct {
	long rate;		/* the output's */
	unsigned long step, acc;	/* the file's samples an output sample, Q16 */
	SNCMD psg;
	SNCPU sn;
	OPND opn;		/* the YM2612's 68030 half, its commands to fm */
	OPNF fm;
	int has_sn, has_opn;
} VGMCPU;

/* the chips h has (that it can make) at rate, the YM2612's tables (build/
   opntab.h's: its sine and gains, 24-bit words) for it; then vgm_begin
   with vgmcpu_sn and the VGMCPU as u, and vgmcpu_attach */
void vgmcpu_init(VGMCPU *c, const VGMHEAD *h, long rate, const unsigned long *opn_sin,
		 const unsigned long *opn_gain);
/* the run's other callbacks and the drums' queue set for it */
void vgmcpu_attach(VGMCPU *c, VGMRUN *r);
/* (vgm_begin's SN76489 callback, u the VGMCPU) */
void vgmcpu_sn(void *u, int val);
/* n frames into lr (left, right): the file run alongside; the frames made,
   fewer at its end */
int vgmcpu_render(VGMCPU *c, VGMRUN *r, short *lr, int n);

#endif
