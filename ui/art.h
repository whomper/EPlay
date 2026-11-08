/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * art.h - the album art in the bar (Erez, 3 Nov 2026: design D "with album
 * art"; "decode on the DSP. Store a cache version on file in EPlay cache
 * folder"): the file's own picture (its tags' APIC: JPEG or PNG), unpacked
 * on the DSP, kept in cache\ next to the program, made in steps, then drawn
 * by one copy from ST RAM.
 */
#ifndef EPLAY_ART_H
#define EPLAY_ART_H

#include "ui.h"

void art_init(short size);	/* at the start: its ST RAM; size: the art's side */
/* the file about to play: its picture made from now on, in art_step's.
   ART_FIRST: an MP3 whose picture the DSP unpacks first (its music waits
   for art_pending to end); ART_NOW: play it now (a WAV or AIFF, a picture
   from the cache, the same file's, or none) */
enum { ART_NOW, ART_FIRST };
int art_begin(const char *path);
int art_pending(void);		/* work left */
int art_needs_dsp(void);	/* its work still wants the DSP (the music waits) */
/* one slice of the work (about 40 ms); turn() gives GEM its turn in a long
   piece (0: EPlay quits). 1: the art is ready now */
int art_step(int (*turn)(void));
int art_ready(void);
/* a station's picture (sart.c): from the cache by its address's hash, or
   from its bytes, unpacked by the 68030; art_clear: the disc again */
int art_station_cached(const char *name, unsigned long ukey);
int art_station(const char *name, unsigned long ukey, const unsigned char *d, long len);
void art_clear(void);
void art_draw(short x, short y, const GRECT *clip);	/* (ready only) */

#endif
