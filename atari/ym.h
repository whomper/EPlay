/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ym.h - the Atari's own YM2149 for a VGM file (phase 9): its registers 0
 * to 13 written from the tick (feed.h), register 7's I/O bits (the
 * floppy's and the printer's ports) kept as the machine has them, and 14
 * and 15 never touched. While it plays the keyboard's click and the bell
 * are off (TOS sounds them on the same chip), and on a Falcon the chip is
 * heard through the codec (its ADC input set to the PSG, added to the
 * DAC's output); all put back as they were at the end.
 */
#ifndef EPLAY_YM_H
#define EPLAY_YM_H

void ym_begin(void);		/* (from GEM) the chip silent, ready */
void ym_write(int reg, int val);	/* (from the tick, in supervisor mode) */
void ym_silence(void);		/* (from GEM) the volumes 0, the tones off */
void ym_end(void);		/* (from GEM) silent, the machine's own again */
void ym_term(void);		/* (supervisor: term.c) silent, however EPlay ends */

#endif
