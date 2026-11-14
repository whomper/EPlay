/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmplay.h - a VGM or VGZ file played (phase 9, Erez, 9 Nov 2026): read
 * whole into memory (TT RAM where there is some), then its commands run
 * by the tick (feed.h, 997 times a second: 44.2 of the file's samples a
 * tick), its AY writes to the Atari's own YM2149 (ym.h) on the Falcon, TT
 * and STE. play.c hands a VGM file here and asks it what plays.
 */
#ifndef EPLAY_VGMPLAY_H
#define EPLAY_VGMPLAY_H

#include "tags.h"

/* the file open at fh (size bytes) read and ready to play from ms; its
   tags, the bar's info ("VGM  YM2149  1.77 MHz") and output ("YM2149"),
   its length. 0 with err set (a chip EPlay doesn't play yet, no memory,
   damaged); the file isn't closed here */
/* path: the file again, for a big VGZ's unpacking as it plays (0: not) */
int vp_open(int fh, long size, long ms, TAGS *t, char *info, int ni, char *out, int no, long *len_ms, char *err,
	    int n, const char *name, const char *path);
/* a big VGZ's unpacking as it plays, for up to ms: 0 none going, 1 going,
   2 done now (vp_tags: its GD3), 3 damaged */
int vp_step(int ms);
int vp_tags(TAGS *t);		/* its GD3 once unpacked: 1 */
void vp_start(void);		/* the tick on, the chip sounding */
void vp_halt(void);		/* the tick off and the chip silent, where it is (Pause: vp_ms then that place) */
void vp_stop(void);		/* silent, the tick off; the file's memory kept for its next open */
void vp_forget(void);
/* the file's tags and length (ms) from it unpacked, when it is the one open
   or kept (name, size, its first 64 bytes): 1; else 0, to be read */
int vp_probe(const char *name, long size, const unsigned char *head, TAGS *t, long *ms);		/* that memory given back (another kind of file opened) */
long vp_ms(void);		/* where it is */
int vp_ended(void);		/* 1 once it played to its end (its fade too) */
/* the volume: att steps of 1.5 dB down (0 to 15), mute: silent. The
   Falcon's codec and the STE's LMC1992 take it as for any file (play.c);
   the TT's YM2149 alone (no mixer of its own): its volumes 3 dB a step */
void vp_volume(int att, int mute);
const char *vp_via(void);	/* the badge: "FDI", "DSP" or "" */
int vp_on(void);		/* a VGM is open */
/* the DSP fell behind (its ring nearly dry), or an OPL3 made with nine
   channels keyed its second bank: 1, for play.c to open it again where
   it is, at the next lower rate or with 18 channels (Erez, 14 Nov 2026),
   until vp_new_track */
int vp_lower(void);
void vp_new_track(void);	/* another track: the most again */

#endif
