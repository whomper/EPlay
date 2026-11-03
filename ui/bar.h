/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bar.h - EPlay's window as Erez chose it (design D, 3 Nov 2026;
 * docs/DESIGN.md, "Phase 3"): the bar across the top (the album art, the
 * title and artist with the kbps and kHz labels, the transport, the
 * position bar with the times, the output and the volume), the list
 * under it.
 */
#ifndef EPLAY_BAR_H
#define EPLAY_BAR_H

#include "ui.h"

/* what a press is on */
enum { BH_NONE, BH_SHUFFLE, BH_PREV, BH_PLAY, BH_NEXT, BH_REPEAT, BH_POS, BH_VOL, BH_LIST };

/* what is drawn again: the parts, or-ed */
#define BP_TIME  1		/* the times and the position bar */
#define BP_PLAY  2		/* Play/Pause */
#define BP_TEXT  4		/* the album art, the title, the labels, the output */
#define BP_LIST  8
#define BP_ART   16		/* the album art alone */
#define BP_VOL   32		/* the speaker and the volume alone */
#define BP_ALL   0xff

void bar_colours(void);		/* the pens, from the screen (at the start) */
void bar_colours_back(void);	/* pens 16 and up as they were (at the end) */
void bar_size(short *w, short *h);	/* the work area it is laid out for */
void bar_layout(const GRECT *work);
/* the parts' area (to draw again just those) */
void bar_area(int parts, GRECT *r);
/* the parts that meet clip */
void bar_draw(const GRECT *clip, int parts);
int bar_hit(short x, short y);	/* BH_... */
short bar_art_side(void);
/* the list: the rows it shows, one row drawn again (within clip), its
   area, the file at y (-1: none), a row's height */
int bar_rows(void);
void bar_draw_row(const GRECT *clip, int i);
void bar_row_area(int i, GRECT *r);
int bar_row_at(short y);
short bar_row_h(void);	/* the album art's side, in pixels */
/* the position bar: x's place as a share of the file, 0 to 1000 */
int bar_pos_at(short x);
int bar_vol_at(short x);	/* the volume bar: x's place, 0 to VOL_MAX */

extern int bar_held;		/* the control drawn pressed (5.8), BH_NONE */
extern int bar_vdrag;		/* the volume dragged: its place; -1 not */
extern int bar_drag;		/* the position bar dragged: its place, 0 to 1000; -1 not */

#endif
