/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gmset.h - the synth's General MIDI sound set, Sonivox EAS's, read from
 * its data file (tools/mkgmset.py writes it; its layout there): the
 * numbers checked once, then found in place, nothing copied (DESIGN.md,
 * "The synth"). Host-testable, libc-free.
 */
#ifndef EPLAY_GMSET_H
#define EPLAY_GMSET_H

#define GM_KEYGROUP(f) (((f) >> 8) & 15)
#define GM_LOOPED 0x0001
#define GM_LAST 0x8000		/* a program's last region */
#define GM_DRUMS 0x780000L	/* the drum kit's locale (MSB 120): MIDI channel 10 */
#define GM_MELODIC 0x7900	/* the instruments' bank's locale (MSB 121) */

typedef struct {
	unsigned short flags;	/* GM_LOOPED, GM_LAST, the key group (GM_KEYGROUP) */
	unsigned char lo, hi;	/* its keys */
	short tuning;		/* cents */
	short gain;		/* 32767 = 1 */
	unsigned long loop_start, loop_end;	/* sample points from the wave's start */
	unsigned short wave, art;
} GM_REGION;

typedef struct {
	short eg1[4], eg2[4];	/* attack, decay, sustain, release (Sonivox's units) */
	short lfo_to_pitch, lfo_delay, lfo_freq, eg2_to_pitch, eg2_to_fc, cutoff;
	signed char lfo_to_gain;
	unsigned char q;
	signed char pan;
} GM_ART;

typedef struct {
	const unsigned char *b;	/* the file, in memory */
	long n;
	int bits;		/* 8 or 16 */
	int prepared;		/* its points made the synth's (gms_prepare) */
	long rate;		/* its samples' rate (22050) */
	int nbank, nprog, nreg, nart, nwave;
	long points;		/* the pool's sample points */
	long o_bank, o_prog, o_reg, o_art, o_len, o_off, o_smp;	/* the sections' places */
} GMSET;

/* the file (b, n bytes) checked: 1, or 0 with why set */
int gm_open(GMSET *g, const unsigned char *b, long n, const char **why);
/* the region that plays key on program prog of a channel (drums: the kit,
   whatever prog), 0 for none; *index its number */
int gm_find(const GMSET *g, int drums, int prog, int key, GM_REGION *r, int *index);
/* region k, articulation k */
void gm_region(const GMSET *g, int k, GM_REGION *r);
void gm_art(const GMSET *g, int k, GM_ART *a);
/* wave w: its first sample point's address (signed 8-bit or big-endian
   16-bit) and its length in points */
const unsigned char *gm_wave(const GMSET *g, int w, long *len);

#endif
