/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gmsdsp.h - the internal synth on the Falcon's DSP (DESIGN.md, "The synth
 * on the DSP: through the DMA"). The 68030 runs gmsynth.c's control and
 * writes, for each block of GMSD_N synth samples (2 x GMSD_N frames of the
 * DMA's, 8 words a frame), each sounding voice's header and the 8-bit
 * points its pitch reaches in the block into the sound DMA's ring, which
 * plays them into the DSP's SSI receive; the DSP (dsp/gms.a56) renders the
 * block. Here: the block's words (gmsd_write, the 68030's) and the DSP's
 * arithmetic (gmsd_dsp, its C model: the .a56 is held to it bit for bit).
 *
 * A block, 16-bit words (the DMA's; the DSP takes each as its top 16 bits):
 *   0       the voices in it (0 to GMSD_VOICES)
 *   then each voice's:
 *     0     its number (0 to GMSD_VOICES - 1)
 *     1     its place's fraction at the block's start (Q16, of its first point)
 *     2, 3  its step a sample (Q16: its whole part, its fraction)
 *     4, 5  its left and right gain at the block's end (Q15), ramped to from
 *           where the last block left it (0 where the voice wasn't in it)
 *     6     the points that follow (2 to GMSD_PTS)
 *     ...   the points, 8-bit signed, two a word (the first in the high byte)
 *   the rest of the block's GMSD_WORDS words not read
 */
#ifndef EPLAY_GMSDSP_H
#define EPLAY_GMSDSP_H

#include "gmsynth.h"

#define GMSD_N 64		/* synth samples a block */
#define GMSD_FRAMES (2 * GMSD_N)	/* the DMA's frames a block (the output twice the synth's rate) */
#define GMSD_WORDS (8 * GMSD_FRAMES)	/* its words: 4 stereo tracks */
#define GMSD_VOICES 32
#define GMSD_HEAD 7
#define GMSD_PTS 512		/* a voice's points in a block at most (a step of 8) */

/* the set's 16-bit points (gms_prepare's, halved) made 8-bit into pts8
   (g->points bytes), once */
void gmsd_points8(const GMSET *g, const unsigned char *pool, signed char *pts8);

/* the next block's words into w (GMSD_WORDS at most): the control at its
   start, each sounding voice's header and points, its place moved on;
   a voice whose points would pass the block's room is left out of it (its
   place moved on all the same). pool is the set's file as prepared,
   pts8 its points made 8-bit. Returns the words written */
int gmsd_write(GMSYN *s, const unsigned char *pool, const signed char *pts8, unsigned short *w);

/* the DSP's state between blocks, and a block rendered: lr the block's
   GMSD_N synth samples, left and right, 24-bit (the DSP's words) */
typedef struct {
	long gl[GMSD_VOICES], gr[GMSD_VOICES];	/* each voice's gain now (Q23) */
	unsigned char seen[GMSD_VOICES];	/* (in the last block) */
} GMSD_DSP;
void gmsd_dsp_reset(GMSD_DSP *d);
void gmsd_dsp(GMSD_DSP *d, const unsigned short *w, long *lr);

#endif
