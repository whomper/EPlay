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

/* a VGZ unpacked a piece at a time, for playing it as it unpacks (a 33 MB
   one took two minutes before it played: Erez, 11 Nov 2026): open (the
   gzip header read, the whole buffer and the unpacking's own taken: *buf
   its len bytes, to fill), more (up to max bytes more: the bytes there are
   now), the state (0 going, 1 done, -1 damaged or not a VGM), close (the
   unpacking's memory given back, not the buffer). rd's file stays open
   until close */
typedef struct vgm_load VGMLOAD;
VGMLOAD *vgm_load_open(TAG_READ rd, void *u, long size, unsigned char **buf, long *len, void *(*alloc)(long),
		       void (*release)(void *), const char **why);
long vgm_load_more(VGMLOAD *l, long max);
int vgm_load_state(const VGMLOAD *l);
void vgm_load_close(VGMLOAD *l);

/* the list's row: its length in ms (0 not known) and its GD3 tag into t;
   -1 when it isn't a VGM */
long vgm_probe(TAG_READ rd, void *u, long size, TAGS *t, void *(*alloc)(long), void (*release)(void *));

#endif
