/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3dsp.h - the C model of the DSP's part of an MP3 decode: a packet from
 * mp3frame.c in, 1152 samples a channel out, with the DSP56001's
 * arithmetic (dspmath.h). dsp/mp3.a56 does the same, bit for bit; this is
 * its specification and its test.
 */
#ifndef MP3DSP_H
#define MP3DSP_H

#include "dspmath.h"

/* subband samples and dequantised lines are held as value / 2^MP3_XS */
#define MP3_XS 1

typedef struct {
	dw ov[2][576];		/* the transform's overlap, a granule's */
	dw v[2][16][32];	/* the synthesis filter's last 16 slots */
	int vpos[2];
	unsigned char scf[2][22];	/* granule 0's long scalefactors */
	long sat;		/* values limited on the way (none, in a good file) */
} MP3DSP;

void mp3dsp_reset(MP3DSP *d);

/* a packet's samples, Q23, into pcm[channel][0..1151]; gives the channels */
int mp3dsp_frame(MP3DSP *d, const unsigned long *pk, dw pcm[2][1152]);

#endif
