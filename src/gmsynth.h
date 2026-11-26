/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gmsynth.h - EPlay's General MIDI synth (DESIGN.md, "The synth"): MIDI
 * messages into 16-bit stereo, from the Sonivox EAS sound set (gmset.h),
 * as Sonivox's own engine makes it: each note a voice reading its key
 * range's sample with its loop, at its pitch (the note, the range's
 * tuning, the bend, the vibrato), its gain (the velocity, the range's,
 * the channel's volume and expression, the master volume, the envelope)
 * and the channel's pan. The envelopes and the vibrato are stepped every
 * 128 samples of 22,050 Hz (5.8 ms), Sonivox's update, the gains ramped
 * across it. Integer arithmetic only (no FPU), host-testable, libc-free.
 */
#ifndef EPLAY_GMSYNTH_H
#define EPLAY_GMSYNTH_H

#include "gmset.h"

#define GMS_VOICES 32		/* the most at once */

typedef struct {
	short eg1, eg2;		/* the envelopes now (Q15) */
	unsigned char eg1st, eg2st;	/* attack, decay, sustain, release, off */
	short eg1inc, eg2inc;	/* this state's step (attack) or factor (Q15) */
	short lfo, lfoph;	/* the vibrato's value (Q15) and phase (negative: its delay) */
	unsigned char ch, note, vel, held;	/* held: let go under the sustain pedal */
	unsigned char drums, kgroup;
	unsigned char on;	/* sounding: its envelope, then its last ramp to 0 */
	int region;
	const unsigned char *w;	/* its wave's first sample point */
	long len, ls, le;	/* the wave's length, its loop (le: past its end; ls == le none) */
	long pos;		/* the sample point now */
	unsigned long frac;	/* and between it and the next (Q16) */
	unsigned long inc;	/* the points a sample (Q16) */
	long gl, gr, dgl, dgr;	/* the gains now and their steps a sample (Q30) */
	unsigned long age;
	GM_REGION r;
	GM_ART a;
} GMS_VOICE;

typedef struct {
	unsigned char prog, vol, expr, pan, sus, mod, rpn_lo, rpn_hi;
	short bend;		/* -8192 to 8191 */
	short range;		/* the bend's range, cents */
} GMS_CHAN;

typedef struct {
	const GMSET *g;
	long rate;		/* the output's */
	int voices;		/* the most at once (GMS_VOICES at most) */
	int cap;		/* the most new notes take now (gms_cap: the CPU's time) */
	long upd;		/* samples an update (128 of 22,050 Hz at this rate) */
	long to_upd;		/* samples to the next update */
	unsigned long ratio;	/* 22,050 / rate (Q16): the set's rate to the output's */
	long master;		/* Q15 */
	unsigned long age;
	int live;		/* voices sounding */
	unsigned long notes, stolen;	/* (the log's) */
	GMS_CHAN c[16];
	GMS_VOICE v[GMS_VOICES];
} GMSYN;

/* the set's 16-bit points made the engine's (halved, the host's order),
   in place, once before gms_init: pool is the set's file as gm_open read
   it, writable */
void gms_prepare(GMSET *g, unsigned char *pool);
/* the synth over the set g, at rate Hz, with voices at once at most */
void gms_init(GMSYN *s, const GMSET *g, long rate, int voices);
/* a MIDI message (a channel's whole message, or a system exclusive one
   from F0 to F7): applied now */
void gms_midi(GMSYN *s, const unsigned char *m, int n);
/* n frames of 16-bit stereo (interleaved, the host's byte order) */
void gms_render(GMSYN *s, short *out, int n);
/* new notes in the first n voices alone (1 to voices); those past them
   ended, their gain ramped to 0 by the next update */
void gms_cap(GMSYN *s, int n);
/* every note silent at once, the controllers as GM's reset leaves them */
void gms_reset(GMSYN *s);

#endif
