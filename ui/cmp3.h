/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cmp3.h - MP3 decoded on the CPU, through MP3FPU.OVL (src/mp3ovl.h),
 * where there is no Falcon DSP free: a 68060 with its FPU (DESIGN.md, MP3
 * on the CPU). play.c then plays the samples as it plays a WAV.
 */
#ifndef CMP3_H
#define CMP3_H

#include "mp3ovl.h"

/* the machine can: a 68060 with its FPU, not a ColdFire */
int cmp3_can(void);
/* MP3FPU.OVL loaded (once), its decoder ready; 0 with why in err */
int cmp3_load(char *err, int n);
const char *cmp3_what(void);
void cmp3_reset(void);
/* a frame from p (n bytes) to pcm: mp3ovl.h's frame */
int cmp3_frame(const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info);

#endif
