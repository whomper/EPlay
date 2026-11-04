/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * probe.h - how long a file plays, for the list (Erez, 3 Nov 2026: files
 * added in bulk), without playing it: a WAV's or AIFF's header, or an
 * MP3's first frame after its ID3v2 tag (its Xing, Info or VBRI frame's
 * count, else its bit rate over the file's bytes), as play.c works it out
 * when it plays. Two short reads through the tags' callback.
 */
#ifndef EPLAY_PROBE_H
#define EPLAY_PROBE_H

#include "tags.h"

#define PROBE_BUF 4096		/* the bytes it reads at a time (buf's size) */

/* the length in ms (0: not known; -1: not a file EPlay plays); size the
   file's, id3_len the ID3v2 tag's at its start (tags_read's) */
long probe_len_ms(TAG_READ rd, void *u, long size, long id3_len, unsigned char *buf);

#endif
