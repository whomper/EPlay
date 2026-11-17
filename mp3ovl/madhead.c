/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * madhead.c - MP3INT.OVL's side of src/mp3ovl.h: the MP3OVL table at the
 * start of its text (ovl.ld) and libmad (third_party/libmad, GPL-2.0 or
 * later) in integers, for a 68060 or a 68080 with no FPU (Erez, 16 Nov
 * 2026: "if there is a 68060 or 68080 without an FPU, EPlay should decode
 * on the CPU"). libmad's fixed point with fixed.h's FPM_68060: its products
 * from 16-bit pieces, as the 68060 has no 32 x 32 -> 64 multiply. Its
 * memory is all in the state the program gives (no malloc here), and each
 * frame is copied to a buffer of the overlay's own with libmad's 8 bytes
 * of guard after it, so that a file's last frame decodes too.
 */
#include <stddef.h>
#include <stdint.h>
#include "mp3ovl.h"
#include "config.h"
/* (libmad's own headers, as mad.h gathers them: the mad.h it ships was
   made for FPM_INTEL) */
#include "global.h"
#include "fixed.h"
#include "bit.h"
#include "timer.h"
#include "stream.h"
#include "frame.h"
#include "synth.h"

#ifndef MP3OVL_VERSION
#define MP3OVL_VERSION "0.0"
#endif

#ifndef MP3OVL_HOST
/* libmad's few library calls, here (the overlay has no libc); its two
   allocations are never made: their buffers are the state's (init). (On
   the host, tests/mp3/test_mp3int.c, the C library's.) */
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void free(void *p);

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

void *malloc(size_t n)
{
	(void)n;
	return 0;
}

void *calloc(size_t n, size_t m)
{
	(void)n;
	(void)m;
	return 0;
}

void free(void *p)
{
	(void)p;
}

/* (timer.c's mad_timer_string, never called here) */
int sprintf(char *b, const char *f, ...);
int sprintf(char *b, const char *f, ...)
{
	(void)f;
	*b = 0;
	return 0;
}
#else
#include <string.h>
#endif

/* (the largest frame, 1,441 bytes at 320 kbit/s and 32 kHz; a free-format
   one may be larger: what is past this is decoded from the next call's) */
#define IN 4096

typedef struct {
	struct mad_stream st;
	struct mad_frame fr;
	struct mad_synth sy;
	unsigned char main_data[MAD_BUFFER_MDLEN];
	mad_fixed_t overlap[2][32][18];
	unsigned char in[IN + MAD_BUFFER_GUARD];
} DEC;

static void init(void *state)
{
	DEC *d = state;
	memset(d, 0, sizeof(*d));
	mad_stream_init(&d->st);
	mad_frame_init(&d->fr);
	mad_synth_init(&d->sy);
	d->st.main_data = (void *)d->main_data;
	d->fr.overlap = &d->overlap;
}

/* libmad's sample (28 fractional bits) to 16 bits, rounded and clipped */
static inline short s16(mad_fixed_t s)
{
	s += 1L << (MAD_F_FRACBITS - 16);
	if (s >= MAD_F_ONE)
		s = MAD_F_ONE - 1;
	else if (s < -MAD_F_ONE)
		s = -MAD_F_ONE;
	return (short)(s >> (MAD_F_FRACBITS + 1 - 16));
}

static int decode(DEC *d, const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info);

#ifndef MP3OVL_HOST
/* stack.S: f(a) on the stack whose top is top, the caller's put back */
long ovl_on_stack(void *top, long (*f)(void *), void *a);

/* (libmad's layer III keeps some 6 KB on the stack: its own here, as
   MP3FPU.OVL has, not the program's) */
#define STACK (16L * 1024)
static long stack[STACK / 4];

typedef struct {
	DEC *d;
	const unsigned char *p;
	int n;
	short *pcm;
	MP3OVL_INFO *info;
} CALL;

static long on_stack(void *a)
{
	CALL *c = a;
	return decode(c->d, c->p, c->n, c->pcm, c->info);
}
#endif

static int frame(void *state, const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info)
{
#ifdef MP3OVL_HOST
	return decode(state, p, n, pcm, info);
#else
	CALL c;
	c.d = state;
	c.p = p;
	c.n = n;
	c.pcm = pcm;
	c.info = info;
	return (int)ovl_on_stack(stack + STACK / 4, on_stack, &c);
#endif
}

static int decode(DEC *d, const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info)
{
	int k = n < IN ? n : IN, i, ns, nc;
	memcpy(d->in, p, (size_t)k);
	memset(d->in + k, 0, MAD_BUFFER_GUARD);
	/* (the guard counted only where these are the file's last bytes) */
	mad_stream_buffer(&d->st, d->in, (unsigned long)k + (n <= IN ? MAD_BUFFER_GUARD : 0));
	info->frame_bytes = 0;
	/* (a damaged frame, or one whose bit reservoir isn't here: libmad
	   passes it, a recoverable error, and the next in these is tried) */
	while (mad_frame_decode(&d->fr, &d->st) != 0) {
		if (MAD_RECOVERABLE(d->st.error))
			continue;
		/* no whole frame in these (MAD_ERROR_BUFLEN): what libmad has
		   passed over (garbage, a tag, a damaged frame: next_frame) is
		   taken, the rest waits for more */
		info->frame_bytes = (int)(d->st.next_frame - d->in);
		if (info->frame_bytes > n)
			info->frame_bytes = n;
		return 0;
	}
	info->frame_bytes = (int)(d->st.next_frame - d->in);
	if (info->frame_bytes > n)
		info->frame_bytes = n;
	info->channels = MAD_NCHANNELS(&d->fr.header);
	info->hz = (int)d->fr.header.samplerate;
	info->bitrate_kbps = (int)(d->fr.header.bitrate / 1000);
	mad_synth_frame(&d->sy, &d->fr);
	ns = d->sy.pcm.length;
	nc = d->sy.pcm.channels;
	if (nc == 2) {
		const mad_fixed_t *l = d->sy.pcm.samples[0], *r = d->sy.pcm.samples[1];
		for (i = 0; i < ns; i++) {
			*pcm++ = s16(l[i]);
			*pcm++ = s16(r[i]);
		}
	} else {
		const mad_fixed_t *m = d->sy.pcm.samples[0];
		for (i = 0; i < ns; i++)
			*pcm++ = s16(m[i]);
	}
	return ns;
}

__attribute__((section(".mp3ovl"), used))
const MP3OVL mp3ovl = {
	MP3OVL_MAGIC, MP3OVL_MAJOR, sizeof(MP3OVL), MP3OVL_VERSION, "libmad in integers on the CPU",
	sizeof(DEC), init, frame
};
