/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsplock.h - the DSP's lock (XBIOS Dsp_Lock, Dsp_Unlock) as EPlay's own
 * DSP programs take it, and whether EPlay holds it now: term.c gives it
 * back if EPlay ends while it does. TOS keeps one flag for the lock, with
 * no owner, so nothing but EPlay's own note can tell it is EPlay's (a
 * tester's CT60 under FreeMiNT, 15 Nov 2026: after a crash the DSP and the
 * sound stayed locked until a reboot).
 */
#ifndef EPLAY_DSPLOCK_H
#define EPLAY_DSPLOCK_H

#include "tos.h"

extern volatile short dsp_held;		/* (term.c's) */

static inline long dsp_lock(void)
{
	long r = trap14_w(104);		/* Dsp_Lock */
	if (r != -1)
		dsp_held = 1;
	return r;
}

static inline void dsp_unlock(void)
{
	dsp_held = 0;
	trap14_w(105);			/* Dsp_Unlock */
}

#endif
