/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * art.h - the album art in the bar (Erez, 3 Nov 2026: design D "with album
 * art"): the file's own picture (its tags' APIC: JPEG or PNG), made in
 * steps after the file opens, then drawn by one copy from ST RAM.
 */
#ifndef EPLAY_ART_H
#define EPLAY_ART_H

#include "ui.h"

void art_init(short size);	/* at the start: its ST RAM; size: the art's side */
/* the file just opened: its picture (play_tags) made from now on, in
   art_step's; the same file's kept */
void art_begin(const char *path);
int art_pending(void);		/* work left */
/* one slice of the work (about 40 ms); turn() lets GEM have a turn in a
   long decode (0: stop it, it is begun again). 1: the art is ready now */
int art_step(int (*turn)(void));
int art_ready(void);
void art_draw(short x, short y, const GRECT *clip);	/* (ready only) */
const char *art_note(void);	/* how it went, for the log */

#endif
