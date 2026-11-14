/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fbstub.c - the FireBee's edition (EPLAYFB.PRG, EPLAY_FB): what the Falcon
 * has and the FireBee hasn't, answered. No DSP56001 (its host port a bus
 * error: FBPROBE), so the DSP's programs (the MP3 decoder, the SN76489,
 * the YM2612, the YM3812) are never there, as on a Falcon whose DSP is
 * missing; no YM2149 is taken to be there (a VGM of the AY plays nothing
 * yet); no DSP host port interrupt. The FireBee's sound is sndfb.c's; the
 * DSP's programs are dspstub.c's.
 */
#include <stddef.h>
#include "ym.h"
#include "hostirq.h"

/* ---- the YM2149: none taken to be there (written nowhere) */
void ym_begin(void) { }
void ym_write(int reg, int val) { (void)reg; (void)val; }
void ym_silence(void) { }
void ym_end(void) { }
void ym_term(void) { }

/* ---- the DSP host port's interrupt: none */
volatile unsigned char hirq_want;
volatile unsigned long hirq_spur;
void hostirq_on(void (*fn)(void)) { (void)fn; }
void hostirq_off(void) { }
void hostirq_term(void) { }
void spurious_on(void) { }
void spurious_off(void) { }

/* (div64.c's switch: the ColdFire's 64-bit helpers are common's ll64.c) */
int div64_cpu60;
