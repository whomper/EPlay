/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspmp3.h - the MP3 decoder on the Falcon's DSP (dsp/mp3.a56): locked
 * and loaded, packets sent, frames read back as the sound DMA plays them.
 */
#ifndef DSPMP3_H
#define DSPMP3_H

enum { DSPMP3_OK, DSPMP3_BUSY, DSPMP3_NONE, DSPMP3_FAILED };

/* the DSP locked (Dsp_Lock: refused, DSPMP3_BUSY: another program has it);
   then reset by dspmp3_reset_step from GEM's steps (1 when done), and
   the decoder and its tables loaded by dspmp3_tick_load,
   32 words a tick (about 21,500 in all): 1 when it is loaded, 0 while
   more is to come, -1 if the DSP stopped answering */
int dspmp3_open(void);
int dspmp3_reset_step(void);
int dspmp3_tick_reset(int after, int wait);	/* the same from the tick: 1 when done */
int dspmp3_tick_load(void);
void dspmp3_close(void);	/* unlocked: another program may have it */
void dspmp3_reset(void);	/* its overlap and filter state cleared (a new file) */

/* PLAY: a frame's packet (pk, n words) sent; when ready, its samples
   read as frames (two words, left then right, each the sample's top 16
   bits). In supervisor mode: from the tick (atari/feed.c) */
void dspmp3_tick_send(const unsigned long *pk, int n);
int dspmp3_tick_ready(void);
void dspmp3_tick_read(short *to, long frames);
int dspmp3_failed(void);	/* it stopped answering */

/* STREAM: the DSP plays from its SSI (the matrix: DSPXMIT to the DAC and
   the FDI, on the clock it is given), frames as it asks; resampled first
   (RSSET) when the clock isn't the file's rate. From the tick:
   dspmp3_tick_stream once loaded; then each tick dspmp3_tick_status (its
   word after a frame), dspmp3_tick_push (what may go), and when
   dspmp3_tick_asks a packet offered (not copied: kept as it is until
   dspmp3_tick_pushing says it has gone). A packet of count 0 ends it. dspmp3_stream_info: frames
   decoded, granules late (the sound played the one before again), the
   least and the mean of its waits a frame (its time to spare: 0 none) */
/* from rin to rout Hz (resampled if they differ); pcm: PSTREAM, WAV's
   and AIFF's packets (PK_PCM) */
void dspmp3_tick_stream(long rin, long rout, int pcm, int sblim);	/* sblim: the subbands kept (32 all: Quality High) */
void dspmp3_tick_status(void);
void dspmp3_tick_push(void);
int dspmp3_tick_asks(void);
int dspmp3_tick_pushing(void);
void dspmp3_tick_offer(const unsigned long *pk, int n);
void dspmp3_stream_firsts(long *late_at, long *rsy_at);	/* frames at the first of each, -1 none */
int dspmp3_stream_slots(void);	/* the DSP port's frame, time slots (0: no frame sync, -1: not told) */
long dspmp3_stream_polls(void);	/* the DSP's polls in 256 of the port's slots (-1: not told) */
long dspmp3_stream_fps1000(void);	/* frames a second x 1000, as they came (0: too few yet) */
void dspmp3_stream_info(long *frames, long *late, long *wmin, long *wavg);
long dspmp3_stream_resyncs(void);	/* packets found broken (a word lost or doubled): the stream taken up again */
int dspmp3_stream_stalled(unsigned long hz);	/* GEM: no frame for 2 s (hz: the 200 Hz clock) */
void dspmp3_stall_note(char *b, int n);	/* what both sides were doing then, for the log */
/* where: what 1 sending a packet, 2 reading a frame, 3 loading; the word
   it waited for, of how many; the host port's status then. 4: no frame
   from the stream for two seconds (at: the frames) */
void dspmp3_fail_info(int *what, long *at, long *of, int *hisr);

#endif
