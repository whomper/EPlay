/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cmp3.h - MP3 decoded on the CPU, through MP3FPU.OVL or MP3INT.OVL
 * (src/mp3ovl.h), where there is no Falcon DSP free: a 68060 or a 68080,
 * with its FPU or without one (DESIGN.md, MP3 on the CPU). play.c then
 * plays the samples as it plays a WAV.
 */
#ifndef CMP3_H
#define CMP3_H

#include "mp3ovl.h"

#define CMP3_NONE 0
#define CMP3_FPU 1		/* MP3FPU.OVL: minimp3 on the FPU */
#define CMP3_INT 2		/* MP3INT.OVL: libmad in integers, no FPU */
/* what the machine can (asked once): a 68060 or a 68080, not a ColdFire,
   its FPU or none */
int cmp3_kind(void);
int cmp3_can(void);
const char *cmp3_cpu(void);	/* "68060" or "68080" */
int cmp3_is080(void);
/* MP3FPU.OVL loaded (once), its decoder ready; 0 with why in err */
int cmp3_load(char *err, int n);
const char *cmp3_what(void);
void cmp3_reset(void);
/* a frame from p (n bytes) to pcm: mp3ovl.h's frame */
int cmp3_frame(const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info);

#endif
