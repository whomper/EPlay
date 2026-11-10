/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmfile.h - a VGM or VGZ file read whole into memory, for playing it and
 * for the list (phase 9): a VGZ unpacked (gzip: its header passed over,
 * then common's inflate.c a piece at a time); its length and GD3 tag for
 * the list's row. Through the tags' callback, so the host's tests read
 * files as the Atari does.
 */
#ifndef EPLAY_VGMFILE_H
#define EPLAY_VGMFILE_H

#include "tags.h"

/* a file EPlay plays at most, unpacked: past it a VGZ's size is taken as
   damaged (its gzip trailer); below it the memory decides (1.16: 8 MB
   refused a VGM with sampled drums: Erez, 10 Nov 2026) */
#define VGM_MAX (64L * 1024 * 1024)

/* 1 a VGM, 2 gzipped (maybe a VGZ), 0 neither, from its first bytes */
int vgm_magic(const unsigned char *b, long n);

/* the whole file (unpacked when gzipped) in memory from alloc (free it with
   the matching free): its bytes in *n; 0 with why set (no memory, damaged,
   bigger than VGM_MAX, not a VGM) */
/* the bytes the last vgm_load asked for (unpacked), 0 when it didn't get
   that far: for the log and the alert */
extern long vgm_need;
unsigned char *vgm_load(TAG_READ rd, void *u, long size, long *n, void *(*alloc)(long), void (*release)(void *),
			const char **why);

/* the list's row: its length in ms (0 not known) and its GD3 tag into t;
   -1 when it isn't a VGM */
long vgm_probe(TAG_READ rd, void *u, long size, TAGS *t, void *(*alloc)(long), void (*release)(void *));

#endif
