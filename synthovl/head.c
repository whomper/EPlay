/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * head.c - SYNTH.OVL's side of src/synthovl.h: the SYNTHOVL table at the
 * start of its text (ovl.ld), src/gmsynth.c, src/gmset.c and src/gmsdsp.c
 * (the DSP's blocks), and the one library call they make (memset).
 */
#include <stddef.h>
#include "synthovl.h"

#ifndef SYNTHOVL_VERSION
#define SYNTHOVL_VERSION "0.0"
#endif

void *memset(void *d, int c, size_t n)
{
	unsigned char *p = d;
	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

__attribute__((section(".synthovl"), used))
const SYNTHOVL synthovl = {
	SYNTHOVL_MAGIC, SYNTHOVL_MAJOR, sizeof(SYNTHOVL), SYNTHOVL_VERSION,
	"General MIDI, the Sonivox EAS set (SYNTH.DAT)", sizeof(GMSYN),
	gm_open, gms_prepare, gms_init, gms_midi, gms_render, gms_reset, gms_cap, gms_bench,
	gmsd_points8, gmsd_write
};
