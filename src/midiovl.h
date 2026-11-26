/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * midiovl.h - MIDI.OVL, beside EPLAY.PRG (common's ovl.c loads it, as
 * MP3FPU.OVL), loaded only once a MIDI file is to be played or read
 * (decided 25 Nov 2026): a Standard MIDI File made into the MIDI port's
 * timed bytes (smf.h), on the GEM side, a piece at a time as it plays.
 * 68000 code in the standard package, the ColdFire's in the FireBee's,
 * under the one name. The MIDIOVL table is at the start of its text; a
 * file's state is the program's (state_size bytes), so the list can read
 * one file's length while another plays.
 */
#ifndef MIDIOVL_H
#define MIDIOVL_H

#include "smf.h"

#define MIDIOVL_MAGIC 0x4D49444FL	/* 'MIDO' */
#define MIDIOVL_MAJOR 1			/* a change the program must know of */

typedef struct {
	long magic;		/* MIDIOVL_MAGIC */
	short major;		/* MIDIOVL_MAJOR */
	short size;		/* sizeof(MIDIOVL) as it was built */
	const char *version;	/* the overlay's own */
	const char *what;	/* "Standard MIDI Files, types 0, 1 and 2" */
	/* smf.h's */
	long (*render)(const unsigned char *b, long n, unsigned char *out, long max, SMF_INFO *info, char *err,
		       int errlen);
	long (*state_size)(void);
	int (*begin)(SMF_ST *st, const unsigned char *b, long n, unsigned char *out, long max, SMF_INFO *info, char *err,
		     int errlen);
	int (*more)(SMF_ST *st, long most, long *made, unsigned long *made_ms);
	long (*length)(SMF_ST *st, const unsigned char *b, long n, SMF_INFO *info, char *err, int errlen);
} MIDIOVL;

#endif
