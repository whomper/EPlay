/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * lockchk.c - for the crash case under FreeMiNT (common's tests/xaaes
 * run.py, --cnf "run c:\\LOCKCHK.PRG"): started by XaAES beside EPlay, it
 * waits (EPlay meanwhile plays and ends itself by an illegal instruction,
 * CRASHAT=), then asks for the sound system and the DSP: Locksnd answers 1
 * and Dsp_Lock 0 where they are free, each given back at once. Its line on
 * the emulator's console: "LOCKCHK sound N dsp N". A tester's CT60 under
 * FreeMiNT found both still locked after EPlay crashed (15 Nov 2026).
 */
#include <stdio.h>
#include "tos.h"
#include "gem.h"

/* (a test program: the emulator's console on, common's tos.h) */
long nf_on = 1;

int main(void)
{
	char b[64];
	long s, d;
	int i;
	appl_init();
	for (i = 0; i < 25; i++)
		evnt_timer_(1000);
	s = trap14_w(128);		/* Locksnd: 1 where it was free */
	if (s == 1)
		trap14_w(129);
	d = trap14_w(104);		/* Dsp_Lock: 0 where it was free */
	if (d == 0)
		trap14_w(105);
	snprintf(b, sizeof(b), "LOCKCHK sound %ld dsp %ld\n", s, d);
	nf_debug(b);
	appl_exit();
	return 0;
}
