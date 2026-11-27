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
/* use_synth: played by the internal synth (mp_render) in place of the
   MIDI port; then mp_start and the tick aren't used */
int mp_open(int fh, long size, long ms, const char *name, int use_synth, TAGS *t, char *info, int ni, char *out,
	    int no, long *len_ms, char *err, int n);
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

#ifndef EPLAY_ST
/* the internal synth (decided 26 Nov 2026): SYNTH.DAT's set, made into
   16-bit stereo on the CPU */
int mp_synth_can(void);		/* this machine has it (or, a 68030's, not timed yet) */
int mp_synth_known(void);	/* its speed timed (a 68030's, at its first file) */
/* 1 once mp_open plays on the synth (a 68030 too slow for it goes to the port) */
int mp_on_synth(void);
void mp_synth_rate(long rate);	/* the rate it makes its sound at (before the first mp_render) */
/* n frames of 16-bit stereo, the host's order: fewer at the file's end
   (its notes sounded out) */
int mp_render(short *out, int n);
int mp_render_tick(short *out, int n);	/* (the tick's: -1 when it can't now) */
const char *mp_synth_log(void);
/* the synth on the Falcon's DSP: prepared at rate (half the output's) with
   voices at most (0 with err set); GEM's turns make its records ahead
   (mp_dsp_step); the tick writes each block (mp_dsp_block: -1 not now,
   0 the end, else 1) */
int mp_dsp_prepare(long rate, int voices, char *err, int n);
void mp_dsp_step(int ms);
int mp_dsp_block(unsigned short *w);
int mp_dsp_cap(void);
#endif

#endif
