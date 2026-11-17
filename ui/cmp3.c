/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cmp3.c - the program's side of MP3FPU.OVL and MP3INT.OVL (cmp3.h): the
 * one this CPU takes, loaded from beside EPLAY.PRG the first time an MP3 is
 * to play on the CPU, kept after.
 */
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "ovl.h"
#include "elog.h"
#include "cmp3.h"
#include "xalloc.h"

static const MP3OVL *ov;
static void *st;

/* (atari/cpu080.S, by Supexec: where _CPU says 40) */
long cpu_is080(void);

static int kind = -1, is080;

int cmp3_kind(void)
{
	long cpu;
	if (kind >= 0)
		return kind;
	kind = CMP3_NONE;
	/* (FireTOS says _CPU 60 on a ColdFire: _CF_ first, FBPROBE 1) */
	if (get_cookie(0x5F43465FL))		/* '_CF_' */
		return kind;
	cpu = get_cookie(0x5F435055L);		/* '_CPU' */
	/* a 68060; a 68080 (a Vampire's), for which EmuTOS says 40, told
	   from a 68040 as EmuTOS tells it (Erez, 16 Nov 2026: "if there is a
	   68060 or 68080 without an FPU, EPlay should decode on the CPU") */
	if (cpu == 40)
		is080 = Supexec(cpu_is080) == 1;
	if (cpu < 60 && !is080)
		return kind;
	/* '_FPU': its type in the high word, 0 for none (an LC060 or EC060,
	   a 68080 without one); TOS says 16 for the 68060's own, others 8 */
	kind = (get_cookie(0x5F465055L) >> 16) != 0 ? CMP3_FPU : CMP3_INT;
	return kind;
}

int cmp3_can(void)
{
	return cmp3_kind() != CMP3_NONE;
}

int cmp3_is080(void)
{
	cmp3_kind();
	return is080;
}

const char *cmp3_cpu(void)
{
	return cmp3_is080() ? "68080" : "68060";
}

int cmp3_load(char *err, int n)
{
	long taken;
	const MP3OVL *o;
	const char *path = cmp3_kind() == CMP3_FPU ? "mp3fpu.ovl" : "mp3int.ovl";
	const char *name = cmp3_kind() == CMP3_FPU ? "MP3FPU.OVL" : "MP3INT.OVL";
	if (ov)
		return 1;
	o = ovl_load(path, &taken, err, n);
	if (!o) {
		snprintf(err, n, "MP3 on a %s needs %s beside EPlay.", cmp3_cpu(), name);
		return 0;
	}
	ovl_flush();
	if (o->magic != MP3OVL_MAGIC || o->major != MP3OVL_MAJOR || o->size < (short)sizeof(MP3OVL)) {
		free((void *)o);
		snprintf(err, n, "%s isn't this EPlay's: copy it from its release.", name);
		return 0;
	}
	st = x_alloc(o->state_size, 3);
	if ((long)st <= 0) {
		st = 0;
		free((void *)o);
		snprintf(err, n, "There isn't the memory to play.");
		return 0;
	}
	ov = o;
	o->init(st);
	elog("%s %s: %s, %ld bytes", name, o->version, o->what, taken);
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
