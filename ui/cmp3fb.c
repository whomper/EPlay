/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cmp3fb.c - MP3 on the FireBee's ColdFire (EPLAYFB.PRG): cmp3.h's calls
 * with minimp3 (third_party/minimp3, CC0, unchanged) built into the program
 * for the V4e and its FPU, as MP3FPU.OVL is for a 68060 (cmp3.c): one
 * program, no overlay to keep beside it. minimp3's scratch (some 16 KB) is
 * on the program's stack, 64 KB in this edition (the Makefile's).
 *
 * In EPLAYFB.PRG (EPLAY_FB) EPlay's own decoder in integers is there too
 * (src/mp3/mp3dsp.c, the DSP's C model, ISO 11172-4 full accuracy), its
 * synthesis filter on the V4e's EMAC (Erez, 21 Nov 2026; dspmath.h). No
 * emulator times a V4e, so the FireBee chooses: at the first MP3 the frames
 * in hand are decoded each way, minimp3 on the FPU, the integer decoder in
 * plain C and with the EMAC, the EMAC's output held to the plain C's bit
 * for bit (the self-test: QEMU's EMAC and the manual's differ), and the
 * fastest that is right plays from then on; EPLAY.LOG says what each took.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cmp3.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"

#ifdef EPLAY_FB
#include "mp3frame.h"
#include "mp3dsp.h"
#include "plat.h"
#include "elog.h"
#endif

static mp3dec_t dec;

#ifdef EPLAY_FB
#define DEC_MINI 0
#define DEC_INT 1		/* EPlay's, plain C */
#define DEC_EMAC 2		/* EPlay's, the synthesis filter on the EMAC */
static int which = -1;		/* (-1: not chosen yet, at the first MP3) */
/* a decoder named (the tests', and MP3FB= in EPLAY.INF, read, never
   written: a tester's way to hear each): 0 minimp3, 1 the integer's, 2
   the EMAC's (the integer's where the EMAC isn't right); -1 the fastest */
int cmp3fb_force = -1;
static long emac_mode = -1;	/* em_probe's: the MACSR that multiplies signed */
static MP3PARSE ps;
static MP3DSP md;
static unsigned long pk[PK_MAX];
static dw pcm24[2][1152];

/* a frame through EPlay's decoder: as minimp3's call, junk before it
   passed over; 0 bytes where there is no whole frame in p yet */
static int int_frame(const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info)
{
	MP3HDR h;
	int off = 0, nch, ns, i, ch;
	while (off + 4 <= n && !(mp3_header(p + off, &h) && off + h.length <= n))
		off++;
	if (off + 4 > n) {
		info->frame_bytes = n > 4096 ? n - 4 : 0;	/* (no frame in it: passed) */
		return 0;
	}
	info->frame_bytes = off + h.length;
	info->channels = h.channels;
	info->hz = h.rate;
	info->bitrate_kbps = h.bitrate;
	if (mp3_packet(&ps, &h, p + off, pk) < 0)
		return 0;	/* (its main data not all come yet: the bit reservoir) */
	nch = mp3dsp_frame(&md, pk, pcm24, &ns);
	if (em_disturbed && which == DEC_EMAC) {
		/* (another program used the EMAC during a frame: mp3dsp_frame) */
		which = DEC_INT;
		elog("MP3 on the ColdFire: the EMAC's state was changed by another program during a frame; "
		     "EPlay's decoder in plain C from here on");
	}
	for (i = 0; i < ns; i++)
		for (ch = 0; ch < nch; ch++) {
			long w = ((long)pcm24[ch][i] + 128) >> 8;
			*pcm++ = (short)(w > 32767 ? 32767 : w);
		}
	return ns;
}

static void int_reset(void)
{
	mp3_reset(&ps);
	mp3dsp_reset(&md);
}

/* the frames in p (n bytes) decoded each way, the time each took (200 Hz:
   enough frames for a few ticks), the EMAC's output against plain C's */
static void choose(const unsigned char *p, int n)
{
	static short tmp[1152 * 2];
	unsigned long t[3];
	int frames = 0, same = 1, k, at;
	emac_mode = em_probe();
	/* minimp3 */
	mp3dec_init(&dec);
	t[0] = pf_ms();
	for (at = 0; at < n && frames < 12; ) {	/* (12 frames: some 60 ms a decoder, once) */
		mp3dec_frame_info_t fi;
		mp3dec_decode_frame(&dec, p + at, n - at, tmp, &fi);
		if (fi.frame_bytes <= 0)
			break;
		at += fi.frame_bytes;
		frames++;
	}
	t[0] = pf_ms() - t[0];
	/* EPlay's, plain C, then the EMAC (its output kept frame by frame
	   from plain C's, and held to it) */
	for (k = 1; k <= 2; k++) {
		static dw keep[8][2][1152];
		int f = 0, g;
		if (k == 2 && emac_mode < 0) {
			t[2] = 0;
			break;
		}
		em_macsr = k == 2 ? emac_mode : -1;
		int_reset();
		t[k] = pf_ms();
		for (at = 0, g = 0; at < n && g < frames; g++) {
			MP3OVL_INFO fi;
			int ns = int_frame(p + at, n - at, tmp, &fi);
			if (fi.frame_bytes <= 0)
				break;
			at += fi.frame_bytes;
			if (ns && f < 8) {
				if (k == 1)
					memcpy(keep[f], pcm24, sizeof(pcm24));
				else if (memcmp(keep[f], pcm24, sizeof(pcm24)))
					same = 0;
				f++;
			}
		}
		t[k] = pf_ms() - t[k];
	}
	which = DEC_MINI;
	if (t[1] < t[0])
		which = DEC_INT;
	if (emac_mode >= 0 && same && t[2] < (which == DEC_INT ? t[1] : t[0]))
		which = DEC_EMAC;
	if (cmp3fb_force >= 0)
		which = cmp3fb_force == DEC_EMAC && !(emac_mode >= 0 && same) ? DEC_INT : cmp3fb_force;
	em_macsr = which == DEC_EMAC ? emac_mode : -1;
	if (emac_mode < 0)
		elog("MP3 on the ColdFire, %d frames: minimp3 (FPU) %lu ms, EPlay's integer %lu ms; the EMAC "
		     "doesn't multiply signed either way (not used)", frames, t[0], t[1]);
	else
		elog("MP3 on the ColdFire, %d frames: minimp3 (FPU) %lu ms, EPlay's integer %lu ms, with the "
		     "EMAC %lu ms (MACSR $%02lx, %s); %s", frames, t[0], t[1], t[2], (unsigned long)emac_mode,
		     same ? "bit for bit with the plain integer's" : "NOT the plain integer's: not used",
		     which == DEC_EMAC ? "the EMAC's plays" : which == DEC_INT ? "the integer's plays" :
		     "minimp3 plays");
	mp3dec_init(&dec);
	int_reset();
}
#endif

int cmp3_kind(void)
{
	return CMP3_FPU;
}

int cmp3_can(void)
{
	return 1;
}

int cmp3_is080(void)
{
	return 0;
}

const char *cmp3_cpu(void)
{
	return "ColdFire";
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
#ifdef EPLAY_FB
	if (which == DEC_EMAC)
		return "EPlay's decoder on the ColdFire and its EMAC";
	if (which == DEC_INT)
		return "EPlay's decoder on the ColdFire";
#endif
	return "minimp3 on the ColdFire's FPU";
}

void cmp3_reset(void)
{
	mp3dec_init(&dec);
#ifdef EPLAY_FB
	int_reset();
#endif
}

int cmp3_frame(const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info)
{
	mp3dec_frame_info_t fi;
	int ns;
#ifdef EPLAY_FB
	if (which < 0 && n >= 4096)
		choose(p, n);
	if (which == DEC_INT || which == DEC_EMAC)
		return int_frame(p, n, pcm, info);
#endif
	ns = mp3dec_decode_frame(&dec, p, n, pcm, &fi);
	info->frame_bytes = fi.frame_bytes;
	info->channels = fi.channels;
	info->hz = fi.hz;
	info->bitrate_kbps = fi.bitrate_kbps;
	return ns;
}
