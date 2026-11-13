/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cmp3fb.c - MP3 on the FireBee's ColdFire (EPLAYFB.PRG): cmp3.h's calls
 * with minimp3 (third_party/minimp3, CC0, unchanged) built into the program
 * for the V4e and its FPU, as MP3FPU.OVL is for a 68060 (cmp3.c): one
 * program, no overlay to keep beside it. minimp3's scratch (some 16 KB) is
 * on the program's stack, 64 KB in this edition (the Makefile's).
 */
#include <stddef.h>
#include <stdint.h>
#include "cmp3.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"

static mp3dec_t dec;

int cmp3_can(void)
{
	return 1;
}

int cmp3_load(char *err, int n)
{
	(void)err;
	(void)n;
	mp3dec_init(&dec);
	return 1;
}

const char *cmp3_what(void)
{
	return "minimp3 on the ColdFire's FPU";
}

void cmp3_reset(void)
{
	mp3dec_init(&dec);
}

int cmp3_frame(const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info)
{
	mp3dec_frame_info_t fi;
	int ns = mp3dec_decode_frame(&dec, p, n, pcm, &fi);
	info->frame_bytes = fi.frame_bytes;
	info->channels = fi.channels;
	info->hz = fi.hz;
	info->bitrate_kbps = fi.bitrate_kbps;
	return ns;
}
