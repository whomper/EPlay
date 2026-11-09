/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * hostirq.h - the DSP's host interrupt on the 68030 (level 6, the vector
 * the host port's IVR names): fn runs when the DSP has a word for the
 * 68030 (RREQ) or, when asked for, room for one (TREQ), at level 5, in
 * supervisor mode. fn sets hirq_want: HIRQ_RX, and HIRQ_TX while it has
 * words to send. The vector, IVR and ICR are taken through Supexec and
 * given back as they were.
 */
#ifndef HOSTIRQ_H
#define HOSTIRQ_H

#define HIRQ_RX 1
#define HIRQ_TX 2

extern volatile unsigned char hirq_want;
void hostirq_on(void (*fn)(void));	/* (GEM's side) */
void hostirq_off(void);
void hostirq_term(void);	/* (supervisor mode: term.c) */

/* vector 24 (a spurious interrupt) counted and let go while playing, in
   place of TOS's bombs; hirq_spur: how many */
extern volatile unsigned long hirq_spur;
void spurious_on(void);
void spurious_off(void);

#endif
