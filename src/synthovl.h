/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * synthovl.h - SYNTH.OVL, beside EPLAY.PRG (common's ovl.c loads it, as
 * MIDI.OVL), loaded only once a MIDI file plays on the internal synth:
 * the synth's engine (gmsynth.c) and the set's reader (gmset.c), kept out
 * of the program so that its start and its other paths are as they were
 * (26 Nov 2026: in the program, a WAV's start under NVDI came out 20 ms
 * slower). 68020-60 code in the standard package, the ColdFire's in the
 * FireBee's, under the one name. The table is at the start of its text;
 * the state (GMSET, GMSYN) is the program's.
 */
#ifndef SYNTHOVL_H
#define SYNTHOVL_H

#include "gmsynth.h"

#define SYNTHOVL_MAGIC 0x53594E54L	/* 'SYNT' */
#define SYNTHOVL_MAJOR 1

typedef struct {
	long magic;		/* SYNTHOVL_MAGIC */
	short major;		/* SYNTHOVL_MAJOR */
	short size;		/* sizeof(SYNTHOVL) as it was built */
	const char *version;
	const char *what;
	long state_size;	/* sizeof(GMSYN) as it was built */
	int (*open)(GMSET *g, const unsigned char *b, long n, const char **why);
	void (*prepare)(GMSET *g, unsigned char *pool);
	void (*init)(GMSYN *s, const GMSET *g, long rate, int voices);
	void (*midi)(GMSYN *s, const unsigned char *m, int n);
	void (*render)(GMSYN *s, short *out, int n);
	void (*reset)(GMSYN *s);
	void (*cap)(GMSYN *s, int n);
	long (*bench)(int v, int n);
} SYNTHOVL;

#endif
