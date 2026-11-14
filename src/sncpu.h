/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sncpu.h - the SN76489 made on the CPU (phase 11: VGM on a 68060 without
 * the Falcon's DSP, Erez, 14 Nov 2026): dsp/sn.a56's arithmetic in C, word
 * for word, from the same commands (src/sncmd.c's: a command's number,
 * then its value), so that the DSP's program and this sound alike and
 * tests/dspsn holds the two to each other bit for bit. In 32 bits alone:
 * each of the three tones a phase of 23 bits that turns its sign over
 * where it wraps, the noise a shift register shifted where its phase
 * wraps, their sum a 24-bit word (the DSP's sample; its top 16 bits the
 * output's).
 */
#ifndef EPLAY_SNCPU_H
#define EPLAY_SNCPU_H

typedef struct {
	long ph[4], inc[4], amp[4], sg[4];	/* phases, steps, amplitudes, the tones' signed levels */
	long lfsr, taps, topb, white;
	long cmd;		/* a command's number, waiting for its value (-1: none) */
} SNCPU;

void sncpu_init(SNCPU *s);
/* one of the commands' words (sncmd's put) */
void sncpu_word(SNCPU *s, long w);
/* the next sample: the DSP's 24-bit word */
long sncpu_sample(SNCPU *s);

#endif
