/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * head.c - MP3FPU.OVL's side of src/mp3ovl.h: the MP3OVL table at the start
 * of its text (ovl.ld) and minimp3 (third_party/minimp3, CC0, unchanged)
 * built for the 68060 and its FPU. minimp3 keeps some 16 KB of scratch on
 * the stack, more than EPlay's whole stack: each frame is decoded on the
 * overlay's own (stack.S).
 */
#include <stddef.h>
#include <stdint.h>
#include "mp3ovl.h"

#ifndef MP3OVL_VERSION
#define MP3OVL_VERSION "0.0"
#endif

/* minimp3's few library calls, here (the overlay has no libc) */
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
void *memmove(void *d, const void *s, size_t n);

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	while (n--)
		*a++ = *b++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	if (a < b)
		while (n--)
			*a++ = *b++;
	else
		while (n--)
			a[n] = b[n];
	return d;
}

void *memset(void *d, int c, size_t n)
{
	unsigned char *a = d;
	while (n--)
		*a++ = (unsigned char)c;
	return d;
}

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"

/* stack.S: f(a) on the stack whose top is top, the caller's put back */
long ovl_on_stack(void *top, long (*f)(void *), void *a);

#define STACK (32L * 1024)
static long stack[STACK / 4];

typedef struct {
	mp3dec_t *dec;
	const unsigned char *p;
	int n;
	short *pcm;
	MP3OVL_INFO *info;
} CALL;

static long decode(void *a)
{
	CALL *c = a;
	mp3dec_frame_info_t fi;
	int ns = mp3dec_decode_frame(c->dec, c->p, c->n, c->pcm, &fi);
	c->info->frame_bytes = fi.frame_bytes;
	c->info->channels = fi.channels;
	c->info->hz = fi.hz;
	c->info->bitrate_kbps = fi.bitrate_kbps;
	return ns;
}

static void init(void *state)
{
	mp3dec_init((mp3dec_t *)state);
}

static int frame(void *state, const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info)
{
	CALL c;
	c.dec = state;
	c.p = p;
	c.n = n;
	c.pcm = pcm;
	c.info = info;
	return (int)ovl_on_stack(stack + STACK / 4, decode, &c);
}

__attribute__((section(".mp3ovl"), used))
const MP3OVL mp3ovl = {
	MP3OVL_MAGIC, MP3OVL_MAJOR, sizeof(MP3OVL), MP3OVL_VERSION, "minimp3 on the 68060's FPU",
	sizeof(mp3dec_t), init, frame
};
