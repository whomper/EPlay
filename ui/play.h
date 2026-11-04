/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * play.h - a file played: WAV and AIFF converted, MP3 decoded on the DSP,
 * into the sound ring (atari/snd.c) by the tick (atari/feed.c), from a
 * queue the GEM loop's steps read the file into.
 */
#ifndef PLAY_H
#define PLAY_H

enum { PS_IDLE, PS_PLAYING };

/* the file opened and the sound made ready, nothing played yet: 0 and
   why in err if it can't be played */
int play_open(const char *path, char *err, int n);
void play_start(void);
/* the file open again from ms on (its first whole MP3 frame there), playing
   on if it played: 0 and why in err if it can't be */
int play_seek(long ms, char *err, int n);
/* Pause: the sound stopped where it is and the DSP given back (the DSP
   rule); play_resume takes it up there (an MP3 at the frame that played).
   play_paused: 1 between the two */
void play_pause(void);
int play_resume(char *err, int n);
int play_paused(void);
extern int play_note_new;	/* (main: what plays changed by itself; the bar and info line to draw again) */
void play_stop(void);		/* and the file closed, the DSP given back */
/* work for the GEM loop, at most about ms milliseconds (the file read
   into the queue; the sound itself is fed by the tick): 1 when something
   shown changed (the time, the state) */
int play_step(int ms);
int play_wait_ms(void);		/* the GEM loop's timer until the next step */
int play_state(void);
int play_is_open(void);
/* n bytes of the open file from at (the album art), its place for the
   queue kept: how many, or -1 */
long play_read_at(long at, void *buf, long n);		/* opened and not stopped: Play goes on from where it is */

extern char play_name[64];	/* the file's name */
#include "tags.h"
extern TAGS play_tags;		/* what it says it is: title, artist, album, album art */
extern char play_info[80];	/* "MP3  128 kbit/s  44.1 kHz  stereo" */
extern char play_out[80];	/* "FDI  44.1 kHz" */
extern long play_kbps, play_hz;	/* the file's bit rate (a VBR file's average), its rate */
extern const char *play_via;	/* "FDI", "DSP" or "DMA": where it plays */
long play_ms(void);		/* how far it has played */
long play_tick_share(void);		/* the tick's share of the 68030, % (the log) */
const char *play_keep_up(void);	/* the first underrun and the least lead, for the log */
const char *play_stream_note(void);	/* the tick's longest gap, the stream's first troubles, for the log */
const char *play_disk_note(void);	/* how fast the file was read, the queue's least lead, for the log */
void play_log_stop(void);	/* "stopped at" and how it kept up, into the log, if it plays */
long play_len_ms(void);
/* the volume: 0 silent to VOL_MAX (0 dB), steps of 1.5 dB, on what plays
   now and what plays next */
#define VOL_MAX 16
void play_set_volume(int v);
int play_volume(void);		/* how long it is (an MP3's: from its Xing or VBRI frame, else its first frame's bit rate) */

#endif
