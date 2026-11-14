/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opncpu.h - the YM2612 made on the CPU (phase 11): src/opncpu.c, the DSP's
 * model (opnfast.h's state, opnd_send's commands) in 32-bit arithmetic for
 * a 68060 (no 64-bit multiply). Its tables the 68030's (build/opntab.h:
 * the sine and the gains, 24-bit words), given at its start.
 */
#ifndef EPLAY_OPNCPU_H
#define EPLAY_OPNCPU_H

#include "opnfast.h"

void opnc_init(OPNF *f, const unsigned long *sin24, const unsigned long *gain24);
void opnc_word(OPNF *f, long w);
void opnc_words(OPNF *f, long *lr, int n);
void opnc_run(OPNF *f, short *out, int n);

#endif
