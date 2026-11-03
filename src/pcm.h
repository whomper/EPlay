/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pcm.h - WAV and AIFF: what a file holds (from its first bytes), and its
 * samples made into what the Falcon's sound DMA plays, 16-bit stereo,
 * big-endian, left first. Runs on the Atari and in the tests.
 */
#ifndef PCM_H
#define PCM_H

enum { PCM_NONE, PCM_WAV, PCM_AIFF };

typedef struct {
	int kind;		/* PCM_WAV or PCM_AIFF */
	long rate;		/* Hz */
	int channels;		/* 1 or 2 */
	int bits;		/* 8, 16, 24 or 32 */
	int bytes;		/* a frame's: channels * bits / 8 */
	int big;		/* big-endian samples (AIFF) */
	int sign8;		/* 8-bit samples are signed (AIFF) */
	long data;		/* where the samples start in the file */
	long frames;		/* how many frames */
} PCMFMT;

/* the format from the file's first n bytes (n >= 12; up to 4 KB looked
   at): 1 and f filled, 0 if it isn't a WAV or AIFF EPlay plays (PCM
   only: 8, 16, 24 or 32 bits, mono or stereo), with why in *why */
int pcm_probe(const unsigned char *p, long n, PCMFMT *f, const char **why);

/* n frames of f's samples from src into dst as 16-bit stereo frames
   (dst holds n * 2 shorts): 8 bits made 16, 24 and 32 cut to 16, mono
   made stereo */
void pcm_convert(const PCMFMT *f, const unsigned char *src, short *dst, long n);
/* the same, each frame step shorts after the last (2: packed; the sound
   ring's four tracks: 8, the other six words left as they are) */
void pcm_convert_to(const PCMFMT *f, const unsigned char *src, short *dst, long n, int step);

#endif
