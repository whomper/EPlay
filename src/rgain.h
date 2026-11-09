/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * rgain.h - ReplayGain (phase 8b, Erez, 9 Nov 2026): the track's and the
 * album's gain and peak, as the tags give them (ID3v2's TXXX
 * REPLAYGAIN_TRACK_GAIN and the rest, else LAME's tag), and the gain to
 * play at: Off, Track, Album, or Auto (the album's in order, the track's
 * shuffled), boosted only as far as the peak lets it go without clipping.
 * In whole steps of 2^(1/4), 1.505 dB: the volume's, so that an MP3's
 * global_gain takes it as it is.
 */
#ifndef EPLAY_RGAIN_H
#define EPLAY_RGAIN_H

#define RG_NONE (-32768)	/* no gain given */

typedef struct {
	int track, album;	/* hundredths of a dB (RG_NONE: none) */
	long tpeak, apeak;	/* the peaks, 1.0 as 65536 (0: none) */
} RGAIN;

enum { RG_OFF, RG_TRACK, RG_ALBUM, RG_AUTO };

#define RG_BOOST 8		/* steps up at most (12 dB) */
#define RG_CUT 16		/* steps down at most (24 dB) */

void rg_clear(RGAIN *g);
/* "-6.54 dB", "+2.1", "3 dB": hundredths of a dB, RG_NONE if no number */
int rg_parse_db(const char *s);
/* "0.988547": 65536 as 1.0, 0 if no number */
long rg_parse_peak(const char *s);
/* an IEEE single's bits (LAME's peak) as 65536 for 1.0 (0: none) */
long rg_float_q16(unsigned long bits);
/* LAME's 16-bit gain field: hundredths of a dB if its name code is
   name (1 the track's, "radio"; 2 the album's, "audiophile"), else RG_NONE */
int rg_lame(int field, int name);
/* the gain in steps (more than 0: louder), rounded to the nearest, then
   lowered until gain times the peak is 1.0 at most; with no peak known,
   never above 0 */
int rg_steps(int cdb, long peak);
/* the steps to play at: mode RG_, shuffled for Auto; the other gain where
   the one asked for is missing; 0 with neither */
int rg_pick(const RGAIN *g, int mode, int shuffled);

#endif
