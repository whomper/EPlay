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
void play_stop(void);		/* and the file closed, the DSP given back */
/* work for the GEM loop, at most about ms milliseconds (the file read
   into the queue; the sound itself is fed by the tick): 1 when something
   shown changed (the time, the state) */
int play_step(int ms);
int play_wait_ms(void);		/* the GEM loop's timer until the next step */
int play_state(void);

extern char play_name[64];	/* the file's name */
extern char play_info[80];	/* "MP3  128 kbit/s  44.1 kHz  stereo" */
extern char play_out[80];	/* "FDI  44.1 kHz" */
long play_ms(void);		/* how far it has played */
long play_tick_share(void);		/* the tick's share of the 68030, % (the log) */
const char *play_keep_up(void);	/* the first underrun and the least lead, for the log */
const char *play_stream_note(void);	/* the tick's longest gap, the stream's first troubles, for the log */
void play_log_stop(void);	/* "stopped at" and how it kept up, into the log, if it plays */
long play_len_ms(void);		/* how long it is (an MP3's: from its Xing or VBRI frame, else its first frame's bit rate) */

#endif
