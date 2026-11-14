/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * oplcpu.h - the YM3812 (OPL2) and the YMF262 (OPL3) made on the CPU
 * (phase 11, step 3): src/oplcpu.c, the DSP's model (oplfast.h's state,
 * opld_send's commands) in 32-bit arithmetic for a 68060 (no 64-bit
 * multiply), as opncpu.h is the YM2612's. Its tables the ones the 68030
 * loads the DSP with (build/opltab.h's, through dspopl_tab), given at its
 * start: its waveforms made from the sine's first quarter and the saw's
 * factors (oplexp.h), each word's top 16 bits kept, the products' operand.
 */
#ifndef EPLAY_OPLCPU_H
#define EPLAY_OPLCPU_H

#include "oplfast.h"

/* the tables: the gains (ngain of them, the rest 0), the tremolo's (512),
   the vibrato's (8), the sine's first quarter (256), the saw's factors
   (32, 40 bits) */
typedef struct {
	const unsigned long *gain, *am, *pmw, *sinq;
	const unsigned long long *rq;
	int ngain;
} OPLC_TABS;

void oplc_init(OPLF *f, const OPLC_TABS *t);
void oplc_word(OPLF *f, long w);
/* n samples: the SSI's words, left and right (2 n) */
void oplc_words(OPLF *f, long *lr, int n);
/* n samples, left and right, added into out (2 n shorts) */
void oplc_run(OPLF *f, short *out, int n);

#endif
