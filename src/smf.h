/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * smf.h - a Standard MIDI File (types 0, 1 and 2) made into the bytes for
 * the MIDI port, each with the millisecond it goes out at: MIDI.OVL's
 * work (midiovl.h), on the GEM side, so that the tick that sends them has
 * only to copy bytes (DESIGN.md, "MIDI files through the MIDI port").
 *
 * The stream: records of a time (4 bytes, big-endian, ms from the file's
 * start), a length (1 byte, 1 to 255) and that many bytes, in time order:
 * the channel messages of one ms together in a record (255 bytes at
 * most), a system exclusive message a record (or more) of its own.
 * Running status is written out (each message whole), meta events are
 * left out (the tempo taken into the times), a system exclusive message
 * goes as it is in the file (F0 and its bytes; an F7 packet's bytes as
 * they are), split into records of 255 bytes at most.
 */
#ifndef EPLAY_SMF_H
#define EPLAY_SMF_H

#define SMF_REC_HEAD 5		/* a record's time and length */

typedef struct {
	int type;		/* 0, 1 or 2 */
	int tracks;		/* in the file */
	int division;		/* ticks a quarter note, or the SMPTE word as it is (negative) */
	long len_ms;		/* to the last track's end */
	long events;		/* the messages in the stream */
	long bytes;		/* the stream's size */
	int channels;		/* a bit for each channel the file uses */
	char title[48];		/* the first track's name (meta 3), else its first text */
	char copyright[48];	/* meta 2 */
} SMF_INFO;

/* "MThd" at its start: 1 */
int smf_magic(const unsigned char *b, long n);

/* the file (b, n bytes) made into the stream at out (max bytes; out 0:
   only counted, and the info filled). Returns the stream's bytes, -1
   with err set (not a MIDI file, or damaged past its header: a track cut
   short plays to where it ends), or -2 when max is too small */
long smf_render(const unsigned char *b, long n, unsigned char *out, long max, SMF_INFO *info, char *err, int errlen);

/* the same a piece at a time (the state in st, the caller's; b, out,
   info and err kept till the end): smf_begin reads the header (0, or -1 with err set);
   each smf_more makes most messages more, then says how far the stream
   is (made: its bytes, whole records; made_ms: the time it reached): 1
   more to come, 0 all made (info's length and counts then filled), -2
   max too small (err set) */
typedef struct SMF_ST SMF_ST;
long smf_state_size(void);	/* the bytes an SMF_ST takes (the caller's) */
int smf_begin(SMF_ST *st, const unsigned char *b, long n, unsigned char *out, long max, SMF_INFO *info, char *err,
	      int errlen);
int smf_more(SMF_ST *st, long most, long *made, unsigned long *made_ms);
/* the file's length (ms), its name and copyright, with no stream made:
   quicker than counting one (smf_render with out 0) by far; -1 with err
   set, or -2 for more tempo changes than it keeps (then smf_render's) */
long smf_length(SMF_ST *st, const unsigned char *b, long n, SMF_INFO *info, char *err, int errlen);

#endif
