/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * midiplay.h - a MIDI file played out of the MIDI port (decided 25 Nov
 * 2026; DESIGN.md, "MIDI files through the MIDI port"): MIDI.OVL makes
 * the file into timed bytes on the GEM side, the tick (feed.h) sends them
 * to the MIDI ACIA, its TDRE flag waited for. play.c hands a MIDI file
 * here as it does a VGM to vgmplay.c.
 */
#ifndef EPLAY_MIDIPLAY_H
#define EPLAY_MIDIPLAY_H

#include "tags.h"

int mp_magic(const unsigned char *b, long n);	/* "MThd" */
int mp_port(void);		/* the machine has the MIDI port (its ACIA) */
/* the file open at fh (size bytes) made ready to play from ms: its tags
   (the first track's name), the bar's info and output, its length. 0
   with err set (no MIDI port, MIDI.OVL missing, no memory, damaged); the
   file isn't closed here */
int mp_open(int fh, long size, long ms, const char *name, TAGS *t, char *info, int ni, char *out, int no,
	    long *len_ms, char *err, int n);
void mp_start(void);		/* the tick on: the reset, the file's state there, then its bytes */
void mp_halt(void);		/* the tick off, every channel's notes off and controllers reset */
void mp_stop(void);		/* the same; the file's bytes kept for its next open */
void mp_forget(void);		/* their memory given back */
/* (a GEM turn's) more of its records made, for up to ms: 1 while there
   are more to make */
int mp_step(int ms);
long mp_ms(void);		/* where it is */
int mp_ended(void);		/* 1 once it played to its end */
/* the volume: att steps of 1.5 dB down (0 to 40), mute: silent, as the
   General MIDI master volume */
void mp_volume(int att, int mute);
/* the list's row: the file's name (its first track's) and length, the
   file read whole and made into bytes only counted (MIDI.OVL loaded):
   its length in ms, or -1 */
long mp_probe(int fh, long size, TAGS *t);
const char *mp_log(void);	/* the log's line at the end: bytes sent, the longest wait */

#endif
