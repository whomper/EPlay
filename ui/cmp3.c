/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cmp3.c - the program's side of MP3FPU.OVL (cmp3.h): loaded from beside
 * EPLAY.PRG the first time an MP3 is to play on the CPU, kept after.
 */
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "ovl.h"
#include "elog.h"
#include "cmp3.h"

static const char path[] = "mp3fpu.ovl";
static const MP3OVL *ov;
static void *st;

int cmp3_can(void)
{
	/* (FireTOS says _CPU 60 on a ColdFire: _CF_ first, FBPROBE 1) */
	if (get_cookie(0x5F43465FL))		/* '_CF_' */
		return 0;
	/* '_FPU': its type in the high word, 0 for none (an LC060 or EC060);
	   TOS says 16 for the 68060's own, others 8 (a 68040's) */
	return get_cookie(0x5F435055L) >= 60 &&	/* '_CPU' */
	       (get_cookie(0x5F465055L) >> 16) != 0;
}

int cmp3_load(char *err, int n)
{
	long taken;
	const MP3OVL *o;
	if (ov)
		return 1;
	o = ovl_load(path, &taken, err, n);
	if (!o) {
		snprintf(err, n, "MP3 on a 68060 needs MP3FPU.OVL beside EPlay.");
		return 0;
	}
	ovl_flush();
	if (o->magic != MP3OVL_MAGIC || o->major != MP3OVL_MAJOR || o->size < (short)sizeof(MP3OVL)) {
		free((void *)o);
		snprintf(err, n, "MP3FPU.OVL isn't this EPlay's: copy it from its release.");
		return 0;
	}
	st = Mxalloc(o->state_size, 3);
	if ((long)st <= 0) {
		st = 0;
		free((void *)o);
		snprintf(err, n, "There isn't the memory to play.");
		return 0;
	}
	ov = o;
	o->init(st);
	elog("MP3FPU.OVL %s: %s, %ld bytes", o->version, o->what, taken);
	return 1;
}

const char *cmp3_what(void)
{
	return ov ? ov->what : "";
}

void cmp3_reset(void)
{
	if (ov)
		ov->init(st);
}

int cmp3_frame(const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info)
{
	return ov->frame(st, p, n, pcm, info);
}
