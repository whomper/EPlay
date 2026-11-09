/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * play.c - a file played (play.h), in two halves that meet in a queue in
 * memory (TT RAM where there is some, as big as a third of the largest
 * free block, 8 MB at most):
 *
 *   GEM's steps (play_step) read the file in pieces of up to 32 KB into
 *   the queue: a WAV's or AIFF's samples as they are, an MP3's frames
 *   made into the DSP's packets (src/mp3/mp3frame.c).
 *
 *   The tick (atari/feed.c, a thousand times a second, in an interrupt)
 *   takes from the queue into the sound ring in ST RAM (atari/snd.c): the
 *   samples converted (src/pcm.c), or a packet sent to the DSP and its
 *   frame read back when decoded (atari/dspmp3.c).
 *
 * So the music doesn't wait for GEM: a menu held open, the file selector,
 * a dialog or another program stop only the reading, and the queue holds
 * seconds to minutes of music (a stock Falcon's 4 MB: about 3 s of CD
 * sound, 12 s of MP3 at 320 kbit/s; Erez's 128 MB TT RAM: 47 s, minutes).
 * The DMA starts when a quarter of a second is in the ring; after the
 * file's end, silence follows until the last sample has played.
 *
 * Phase 1 plays at the file's own rate where the Falcon has it: through an
 * external clock (the FDI's 44.1 or 48 kHz), or the Falcon's own 49170,
 * 32780... Hz. Other rates play at the nearest of those (so faster or
 * slower) until phase 2's resampling.
 */
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "play.h"
#include "snd.h"
#include "feed.h"
#include "dspmp3.h"
#include "hostirq.h"
#include "../src/pcm.h"
#include "mp3frame.h"
#include "elog.h"
#include "settings.h"
#include "mp3dsp.h"
#include "tags.h"
#include "netsrc.h"
#include "plat.h"
#include "conn.h"
#include "busy.h"

#define FBUF (64L * 1024)	/* the file's start: what it is, where it starts */
#define PIECE 32768L		/* the most read at once (guidelines 6.5) */
#define QMIN (128L * 1024)
#define QMAX (8L * 1024 * 1024)

char play_name[64], play_info[80], play_out[80];
int play_nosig;
TAGS play_tags;
long play_kbps, play_hz;
const char *play_via = "";

static int state = PS_IDLE, kind, fh = -1, eof;
static int dsp;			/* the DSP plays it: an MP3, or WAV and AIFF resampled */
/* a WAV or AIFF the DSP resamples (XSTREAM: Erez, 4 Nov 2026, the right
   pitch at any rate): the ring at the DSP's rate, through the matrix to
   its receive, a frame with a sample where the step has one (pcm_spread).
   xs_acc the step's phase; xs_in the file's frames sent; xs_sil the end's
   silence still to send (frames with samples: the last packets out);
   xs_end_at the frames written when it was in (-1: not yet) */
static int xs, xs_dry_now;
static long xs_acc, xs_in, xs_sil, xs_end_at = -1, xs_dry;	/* xs_dry: the queue's dry stretches */
#define XS_BUF 1024
static short xsbuf[XS_BUF * 2];
static int s_ext;		/* on the FDI's clock (the volume digital) */

/* the volume (Erez, 3 Nov 2026: a bar like the position's, and + and -):
   0 silent, VOL_MAX full (0 dB), steps of 1.5 dB. The Falcon's own
   output by the codec's attenuation (its steps, 0 to 22.5 dB; silent: the
   DAC's adder off). The FDI's digital, in the same steps: an MP3's
   global_gain lowered (each is 2^(1/4), 1.505 dB: no DSP time, and the
   DSP's arithmetic as it is), WAV and AIFF scaled as the tick writes
   them. At 0 dB nothing is touched: the samples bit for bit */
static int vol = VOL_MAX;
static volatile long t_gain = 32768;	/* WAV, AIFF on the FDI: Q15; 32768 as they are */
static volatile int t_gsub;		/* MP3 on the FDI: off each global_gain */
static const unsigned short gain_tab[VOL_MAX] = {
	32768, 27554, 23170, 19484, 16384, 13777, 11585, 9742,
	8192, 6889, 5793, 4871, 4096, 3444, 2896, 2435
};

static void apply_volume(void)
{
	int a = VOL_MAX - vol;		/* 0..15 steps down, while vol is 1 or more */
	if ((s_ext || (snd_dma8 && snd_dma8_interp)) && play_is_open()) {
		snd_dac_volume(0, 0);
		t_gain = vol ? gain_tab[a] : 0;
		t_gsub = vol ? a : 255;
	} else {
		snd_dac_volume(vol ? a : VOL_MAX - 1, !vol);
		t_gain = 32768;
		t_gsub = 0;
	}
}

void play_set_volume(int v)
{
	if (v < 0)
		v = 0;
	if (v > VOL_MAX)
		v = VOL_MAX;
	vol = v;
	apply_volume();
}

int play_volume(void)
{
	return vol;
}

static PCMGAIN gs = { 32768, 32768, 0, 0 };	/* the tick's: the volume gliding to t_gain */
static volatile int loading;	/* MP3: the DSP's program still going in (the tick's);
				   2 while GEM's steps reset the DSP, after Play's draw */
static PCMFMT fmt;
static long rate, out_rate, len_ms, file_size;
/* phase 7: a station or stream in place of the file (netsrc.c): its
   bytes from the network, the queue filled with what has come */
static int net;
static long net_pend_at, net_pend_n;	/* the first audio, read with the probe: in fbuf */
int play_net_lost;		/* the connection ended: play_step connects again */
static unsigned char *fbuf;	/* the file's first bytes, fpos..fend */
static long fpos, fend;
static MP3PARSE parse;		/* the tick's */
#define PKS_MAX (1 + 1152 > PK_MAX ? 1 + 1152 : PK_MAX)	/* (a PCM packet: 576 frames) */
static unsigned long pks[2][PKS_MAX];	/* the tick's packets: one going to the DSP, */
static unsigned long *pk = pks[0];	/* the next made in the other */
static unsigned char fr[2048];	/* the tick's: a frame, whole */
static long last_sec = -1;
static long data_left;		/* bytes still to read into the queue */
static char cur_path[256];	/* the file open (play_seek opens it again) */
/* Quality, by itself (Erez, 4 Nov 2026: no longer a setting): an MP3 on
   the DSP and the Falcon's clock plays at High (49,170 Hz); one the DSP
   falls behind with (granules late) goes on at Normal (32,780 Hz, up to
   about 15 kHz) from where it is, until another file opens (granules
   late: settings_qlate's 3) */
static int q_normal;
int play_note_new;		/* what plays changed by itself (Normal): the bar and info line again */
static long seek_ms, base_ms;	/* play_seek's place for play_open; where this play began */
static long seek_byte;		/* MP3: Pause's frame, where Play takes it up (0: by seek_ms) */
static long q_base;		/* MP3: where the queue's first byte is in the file */
#define FR_RING 32
static long fr_at[FR_RING];	/* MP3: the last frames sent, where each is in the file (the tick's) */
static long fr_sent;		/* (and how many: their index) */
static int paused;		/* Pause: stopped where it was, Play goes on from there */
static long pause_ms, pause_byte;
static long bitrate;		/* MP3: the first frame's, kbit/s */
static unsigned char xtoc[100];	/* MP3: its Xing frame's table (VBR), if has_toc */
static int has_toc;
static long xing_at;		/* MP3: where its frames begin (the Xing frame's, where it has one) */

/* the queue: the file's bytes from its first sample or frame on, as
   they are. q_w is GEM's, q_r the tick's: totals */
static unsigned char *q;
static long q_size;		/* bytes (WAV, AIFF: whole sample frames) */
static volatile long q_w, q_r;
static long q_woff, q_roff;	/* where they are in the queue (GEM's, the tick's) */
static int bshift;		/* a sample frame's bytes as a shift; -1 when not a power of 2 */
#define FR2B(k)	(bshift >= 0 ? (k) << bshift : (k) * fmt.bytes)
#define B2FR(b)	(bshift >= 0 ? (b) >> bshift : (b) / fmt.bytes)
static volatile int src_done;	/* the whole file is in the queue */

/* gapless (phase 8a): the next file follows on in the same queue and the
   same stream when its sound is the same (an MP3 at the same rate; a WAV
   or AIFF of the same format), opened as this one's last bytes go into
   the queue; the bar, the title and the time become its own when its
   first sound is heard */
static volatile long chain_q = -1;	/* the queue's total where the next file's bytes begin; -1 none */
static volatile long chain_frame = -1;	/* MP3: the DSP frame its first packet is (t_frames then) */
static volatile int chain_cross;	/* MP3: the tick has reached chain_q (the parser begun again) */
static long chain_at;			/* MP3: the next file's first frame, in its file */
static int chain_asked;			/* play_wants_next said so for this file */
static int chain_heard;			/* the next file's sound has begun: play_chained says so once */
static long fr_base, pcm_base;		/* the DSP's frames and the samples played before this file's */
static struct {
	char path[256], name[64], info[80];
	TAGS tags;
	long len_ms, kbps, bitrate, xing_at;
	int has_toc;
	unsigned char xtoc[100];
} nx;					/* the next file's, until it is heard */
static volatile int pk_chain;		/* MP3: the packet made is the next file's first */
/* gapless, LAME's tag (mp3_info_lame): this file's padding and the next's
   delay, in samples (-1: no tag); the drop (PK_DROP) put on a packet once */
static volatile int cur_pad = -1, nx_delay = -1, nx_pad = -1;
static volatile int drop_set;

/* the tick's own */
static volatile int t_end, t_dsp, t_failed;
static volatile long t_ended_at;	/* the frames written when the file's end came; -1 before */
static volatile long t_frames;	/* MP3 frames sent to the DSP */

static unsigned long ms_now(void)
{
	return tos_ms();
}

/* the buffer topped up from the file: what is left moved to its start */
static long refill_cap = 32768;	/* a read's most (play_chain's: 8 KB, its turn short) */
static void refill(void)
{
	long n;
	if (eof || fend - fpos > FBUF / 2)
		return;
	if (fpos) {
		memmove(fbuf, fbuf + fpos, (size_t)(fend - fpos));
		fend -= fpos;
		fpos = 0;
	}
	n = net ? ns_read(fbuf + fend, (FBUF - fend) > refill_cap ? refill_cap : FBUF - fend, 1) :
		  Fread(fh, (FBUF - fend) > refill_cap ? refill_cap : FBUF - fend, fbuf + fend);
	if (n <= 0)
		eof = 1;
	else
		fend += n;
}

static void close_file(void)
{
	if (net) {
		ns_close();
		net = 0;
	}
	if (fh >= 0) {
		Fclose(fh);
		fh = -1;
	}
}

static const char *base_name(const char *p)
{
	const char *b = p, *s;
	for (s = p; *s; s++)
		if (*s == '\\' || *s == '/' || *s == ':')
			b = s + 1;
	return b;
}

static long tag_read(void *u, long at, unsigned char *buf, long n)
{
	if (Fseek(at, fh, 0) != at)
		return 0;
	return Fread(fh, n, buf);
}

static void khz(char *out, int n, long hz)
{
	if (hz % 1000)
		snprintf(out, n, "%ld.%ld kHz", hz / 1000, (hz % 1000) / 100);
	else
		snprintf(out, n, "%ld kHz", hz / 1000);
}

/* an MP3's first frame, where it is in the file: its header two frames
   running, after any ID3v2 tag */
static long first_at;		/* find_mp3's frame, in fbuf */

static long find_mp3(MP3HDR *h)
{
	long at = 0, base = 0;		/* (where fbuf starts in the file) */
	if (fend >= 10 && !memcmp(fbuf, "ID3", 3))
		at = 10 + ((long)(fbuf[6] & 127) << 21 | (long)(fbuf[7] & 127) << 14 |
			   (long)(fbuf[8] & 127) << 7 | (fbuf[9] & 127));
	while (net && at >= fend && !eof) {	/* (a stream's tag: read past, as it comes) */
		at -= fend;
		fpos = fend = 0;
		refill();
	}
	if (!net && at >= fend) {	/* a tag longer than what was read: past it */
		Fseek(at, fh, 0);
		base = at;
		fpos = fend = 0;
		refill();
		at = 0;
	}
	for (; at + 4 <= fend; at++) {
		MP3HDR h2;
		if (mp3_header(fbuf + at, h) && at + h->length + 4 <= fend &&
		    mp3_header(fbuf + at + h->length, &h2) && h2.rate == h->rate) {
			first_at = at;
			return base + at;
		}
	}
	return -1;
}

/* a frame in fbuf from its start: its header, and the next frame's of the
   same rate after it (so that a place inside a frame isn't taken for
   one); its place in fbuf, or -1 */
static long sync_mp3(int rate_want)
{
	long at;
	for (at = 0; at + 4 <= fend; at++) {
		MP3HDR h, h2;
		if (mp3_header(fbuf + at, &h) && h.rate == rate_want && at + h.length + 4 <= fend &&
		    mp3_header(fbuf + at + h.length, &h2) && h2.rate == rate_want)
			return at;
	}
	return -1;
}

/* ---- the tick: the queue into the ring (interrupt: no TOS, GEM, VDI) ---- */

/* the tick's q_r moved on n bytes */
static void adv(long n)
{
	q_r += n;
	q_roff += n;
	if (q_roff >= q_size)
		q_roff -= q_size;
}

/* (no division or modulo here: the tick runs a thousand times a second) */
static int raw8;		/* the STE's way: snd_put8_raw (set at the play's start) */

static void tick_pcm(void)
{
	long room = snd_room(), cap = 1024;	/* (a tick short: GEM goes on between; 4 times what plays) */
	while (room > 0 && cap > 0) {
		long hb = q_w - q_r, tail = q_size - q_roff, k, kb;
		short *p;
		if (hb < fmt.bytes) {
			if (src_done)
				t_end = 1;
			return;
		}
		if (hb > tail)
			hb = tail;
		if (raw8) {
			/* the STE: the file's bytes straight to the ring (the volume
			   the LMC1992's), as many as fit; no snd_ptr, nor snd_room
			   again: on the 68000 each is a multiply in libgcc */
			long top = fmt.big ? 0 : fmt.bits / 8 - 1;
			k = snd_put8_raw(q + q_roff, B2FR(hb), fmt.bytes, (int)top,
					 (int)(fmt.channels == 2 ? top + fmt.bits / 8 : top),
					 fmt.bits == 8 && !fmt.sign8 ? 0x80 : 0);
			if (!k)
				return;
			adv(FR2B(k));
			room -= k;		/* (snd_put8_raw keeps to the ring's room itself) */
			cap -= k;		/* (a tick short, as below) */
			continue;
		}
		p = snd_ptr(&k);
		if (k > room)
			k = room;
		if (k > cap)
			k = cap;
		kb = FR2B(k);
		if (kb > hb) {
			k = B2FR(hb);
			kb = FR2B(k);
		}
		pcm_convert_to(&fmt, q + q_roff, p, k, SND_STEP);
		if (!pcm_gain_idle(&gs, t_gain))
			pcm_gain(&gs, t_gain, p, k, SND_STEP);
		adv(kb);
		snd_wrote(k);
		room -= k;
		cap -= k;
	}
}

/* n bytes of the queue from q_r on, into fr (the queue's end crossed) */
/* the tick timed for the log (its share of the 68030), by the 200 Hz
   clock and Timer C's count within its 5 ms (192 steps of 26 us): a
   clock that doesn't wrap however long a tick is (Timer A's own count
   did, past a tick's 1 ms) */
#define TCDR (*(volatile unsigned char *)0xfffffa23L)
#define HZ200 (*(volatile unsigned long *)0x4baL)
static volatile unsigned long t_busy, t_parse, t_push, t_t0, t_last;	/* (the tick's: GEM can't read the clocks) */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"	/* (fixed hardware addresses) */
#define IPRB (*(volatile unsigned char *)0xfffffa0dL)
static unsigned long fine_now(void)
{
	unsigned long h, h2;
	unsigned c, c2, p;
	/* Timer C reloaded and its interrupt still pending (the tick runs
	   at level 5, the MFP's 200 Hz at 6, but it may be waiting): HZ200
	   a step behind its count, which once made a time 5 ms early */
	do {
		h = HZ200;
		c = TCDR;
		p = IPRB & 0x20;
		c2 = TCDR;
		h2 = HZ200;
	} while (h != h2 || c2 > c);
	if (p)
		h++;
	return h * 192 + (192 - (c ? c : 192));
}
#pragma GCC diagnostic pop

static void peek(long n)
{
	long at = q_roff, k = q_size - at;
	if (k >= n) {
		memcpy(fr, q + at, (size_t)n);
	} else {
		memcpy(fr, q + at, (size_t)k);
		memcpy(fr + k, q, (size_t)(n - k));
	}
}

/* the next n bytes whole: where they are in the queue, else (round its
   end) copied into fr */
static const unsigned char *bytes_at(long n)
{
	if (q_size - q_roff >= n)
		return q + q_roff;
	peek(n);
	return fr;
}

/* the next frame made into a packet for the DSP (pk, pk_n words): 1
   when there is one; a byte that starts none (a tag, damage) passed,
   4096 at most a call */
static int pk_n;		/* the packet made, waiting to go; 0 for none */
static long pk_samples;
static int src_out;		/* no frame left in the queue, nor to come */

static void next_packet(void)
{
	int scan;
	for (scan = 0; scan < 4096 && !pk_n; scan++) {
		MP3HDR h;
		long have = q_w - q_r;
		int np;
		if (chain_q >= 0 && !chain_cross && q_r >= chain_q) {
			/* the next file's first byte: its frames from here, the
			   bit reservoir begun again (its first frame's is empty) */
			mp3_reset(&parse);
			q_base = chain_at - chain_q;
			chain_cross = 1;
			pk_chain = 2;		/* (the next packet made is its first) */
		}
		if (have < 4) {
			if (src_done)
				src_out = 1;
			return;
		}
		if (!mp3_header(bytes_at(4), &h)) {
			adv(1);
			continue;
		}
		if (have < h.length) {
			if (src_done)
				src_out = 1;	/* (a frame cut short at the file's end) */
			return;
		}
		{
			unsigned long a = fine_now();
			np = mp3_packet(&parse, &h, bytes_at(h.length), pk);
			t_parse += fine_now() - a;
		}
		{
			long at = q_base + q_r;	/* (the frame's place, before it is passed) */
			adv(h.length);
			if (np > 0 && t_gsub && !(pk[0] & PK_MUTE)) {
				/* the volume: each granule's and channel's global_gain */
				int g, n = ((pk[0] & PK_LSF) ? 1 : 2) * ((pk[0] & PK_STEREO) ? 2 : 1);
				unsigned long *gc = pk + 2 + 2;
				for (g = 0; g < n; g++, gc += PK_GC)
					*gc = *gc > (unsigned long)t_gsub ? *gc - t_gsub : 0;
			}
			if (np > 0 && chain_q >= 0 && !drop_set && nx_delay >= 0) {
				/* gapless: the join's silence left out on the DSP
				   (LAME's tag: this file's padding past the decoder's
				   529, then the next's delay and the 529): from this
				   file's last frame, or the next's first */
				long k = (long)nx_delay + (cur_pad >= 0 ? cur_pad : 529), off = -1;
				if (!chain_cross && q_r >= chain_q && cur_pad >= 529) {
					off = (long)h.samples - (cur_pad - 529);
					if (off < 0) {
						k += off;	/* (padding past a frame: what is left of it played) */
						off = 0;
					}
				} else if (pk_chain == 2) {
					/* (the next's first frame: the padding reaching into
					   it, else its delay alone, as the last frame here
					   wasn't found where it should end) */
					off = cur_pad >= 0 && cur_pad < 529 ? 529 - cur_pad : 0;
					if (cur_pad < 0 || cur_pad >= 529)
						k = (long)nx_delay + 529;
				}
				if (off >= 0) {
					if (k > 2047)
						k = 2047;
					if (k > 0) {
						pk[0] |= PK_DROP | (unsigned long)k << 12;
						pk[1] |= (unsigned long)off << 11;
					}
					drop_set = 1;
				}
			}
			if (np > 0) {
				pk_n = np;
				if (pk_chain == 2)
					pk_chain = 1;	/* (offered: its frame noted) */
				pk_samples = h.samples;
				fr_at[fr_sent % FR_RING] = at;
				fr_sent++;
			}
		}
	}
}

/* WAV, AIFF through the DSP (PSTREAM): a packet of 576 frames from the
   queue, the last one filled with silence; each sample a word, (s +
   32768) << 7 (no data word has bit 23). No division here either */
static short pcmbuf[576 * 2];
static long pcm_got;		/* frames in pcmbuf so far */

static void next_pcm_packet(void)
{
	unsigned long *w;
	long i;
	while (pcm_got < 576) {
		long hb = q_w - q_r, tail = q_size - q_roff, k = 576 - pcm_got, kb;
		if (hb < fmt.bytes)
			break;
		if (hb > tail)
			hb = tail;
		kb = FR2B(k);
		if (kb > hb) {
			k = B2FR(hb);
			kb = FR2B(k);
		}
		pcm_convert(&fmt, q + q_roff, pcmbuf + 2 * pcm_got, k);
		if (!pcm_gain_idle(&gs, t_gain))
			pcm_gain(&gs, t_gain, pcmbuf + 2 * pcm_got, k, 2);
		adv(kb);
		pcm_got += k;
	}
	if (pcm_got < 576) {
		if (!src_done)
			return;		/* (more to come: what was taken waits here) */
		if (!pcm_got) {
			src_out = 1;
			return;
		}
		memset(pcmbuf + 2 * pcm_got, 0, (size_t)(576 - pcm_got) * 4);
	}
	w = pk;
	if (fmt.channels == 2) {
		*w++ = PK_PCM | PK_LSF | PK_STEREO;
		for (i = 0; i < 1152; i++)
			*w++ = (unsigned long)((long)pcmbuf[i] + 32768) << 7;
		pk_n = 1153;
	} else {			/* mono: the left of each frame (the DMA's twice) */
		*w++ = PK_PCM | PK_LSF;
		for (i = 0; i < 1152; i += 2)
			*w++ = (unsigned long)((long)pcmbuf[i] + 32768) << 7;
		pk_n = 577;
	}
	pk_samples = 576;
	pcm_got = 0;
}

static void make_packet(void)
{
	if (kind) {
		unsigned long a = fine_now();
		next_pcm_packet();
		t_parse += fine_now() - a;
	} else
		next_packet();
}

/* The DSP plays from its SSI (STREAM; PSTREAM for WAV and AIFF). Its
   packets as it asks for them; at the file's end two frames of silence
   (the ring's two halves), then the count 0, and the end */
static int s_started, s_end;	/* s_end: 1, 2 silences offered, 3 the end */
static int s_sblim = 32;	/* the subbands the DSP keeps (Quality Normal: fewer) */
static unsigned long s_flags, s_mute[1];
static long s_spf;		/* a frame's samples (1152, or 576 for MPEG-2 and 2.5) */

static void tick_mp3(void)
{
	dspmp3_tick_status();
	{
		unsigned long a = fine_now();
		dspmp3_tick_push();
		t_push += fine_now() - a;
	}
	if (dspmp3_tick_asks() && s_end < 3) {
		if (!pk_n)
			make_packet();
		if (pk_n) {
			if (pk_chain == 1) {
				chain_frame = t_frames;	/* (the next file's first frame) */
				pk_chain = 0;
			}
			dspmp3_tick_offer(pk, pk_n);
			s_flags = pk[0];
			s_spf = pk_samples;
			t_frames++;
			pk_n = 0;
			pk = pk == pks[0] ? pks[1] : pks[0];	/* (the next made in the other) */
		} else if (src_out) {
			s_mute[0] = s_flags | 8;	/* (bit 3: silence) */
			dspmp3_tick_offer(s_mute, s_end < 2 ? 1 : 0);
			s_end++;
		}
		{
			unsigned long a = fine_now();
			dspmp3_tick_push();
			t_push += fine_now() - a;
		}
	}
	if (s_end == 3 && !dspmp3_tick_pushing())
		t_end = 1;			/* (the count 0 gone: the DSP's last silence plays) */
	if (!pk_n && !dspmp3_tick_pushing())
		make_packet();		/* (once the one going is in: not while the DSP
					   waits for its words) */
}

/* XSTREAM: the ring filled at the DSP's rate from the queue, the file's
   frames spread over it (pcm_spread); at the file's end two packets of
   silence that has samples (the last packet out, as PSTREAM's two), then
   zeros, and the end word to the DSP once the DMA has played them */
static void tick_xs(void)
{
	long room = snd_room(), cap = 1024;
	dspmp3_tick_push();		/* (the end word, going) */
	while (room > 0 && cap > 0) {
		long kout, kin, used, need;
		short *p = snd_ptr(&kout);
		if (kout > room)
			kout = room;
		if (kout > cap)
			kout = cap;
		need = (xs_acc + kout * rate) / out_rate;	/* the file's frames those take */
		if (need > XS_BUF)
			need = XS_BUF;
		if (s_end == 0) {
			long hb = q_w - q_r, tail = q_size - q_roff;
			if (hb < fmt.bytes) {
				if (src_done) {
					s_end = 1;
					xs_sil = 2 * 576;
					break;
				}
				/* the queue dry (the disk held up): once the ring's
				   lead is short, frames without a sample, so that the
				   DSP waits in silence rather than the DMA playing the
				   ring's old frames again; each such stretch is an
				   underrun */
				if (snd_written() - snd_played() >= out_rate / 20)
					break;
				if (!xs_dry_now) {
					xs_dry_now = 1;
					xs_dry++;
				}
				{
					long i;
					for (i = 0; i < kout; i++, p += SND_STEP)
						*(long *)(void *)p = 0;
				}
				snd_wrote(kout);
				room -= kout;
				cap -= kout;
				continue;
			}
			xs_dry_now = 0;
			if (hb > tail)
				hb = tail;
			kin = B2FR(hb);
			if (kin > need)
				kin = need;
			pcm_convert_to(&fmt, q + q_roff, xsbuf, kin, 2);
			if (!pcm_gain_idle(&gs, t_gain))
				pcm_gain(&gs, t_gain, xsbuf, kin, 2);
			kout = pcm_spread(xsbuf, kin, p, kout, SND_STEP, rate, out_rate, &xs_acc, &used);
			adv(FR2B(used));
			xs_in += used;
			t_frames = xs_in / 576;
		} else if (s_end == 1) {
			kin = need < xs_sil ? need : xs_sil;
			memset(xsbuf, 0, (size_t)kin * 4);
			kout = pcm_spread(xsbuf, kin, p, kout, SND_STEP, rate, out_rate, &xs_acc, &used);
			xs_sil -= used;
			if (!xs_sil)
				s_end = 2;
		} else {
			long i;			/* zeros: no sample, the DSP takes none */
			for (i = 0; i < kout; i++, p += SND_STEP)
				*(long *)(void *)p = 0;
		}
		snd_wrote(kout);
		if (s_end == 2 && xs_end_at < 0)
			xs_end_at = snd_written();
		room -= kout;
		cap -= kout;
		if (!kout)
			break;
	}
	if (s_end == 2 && xs_end_at >= 0 && snd_played() >= xs_end_at) {
		s_mute[0] = PK_PCM | PK_LSF | PK_STEREO;
		dspmp3_tick_offer(s_mute, 0);	/* the count 0: the end */
		dspmp3_tick_push();
		s_end = 3;
	}
	if (s_end == 3 && !dspmp3_tick_pushing())
		t_end = 1;
}

static void tick_body(void);

/* MP3, once the DSP is loaded: the tick feeds it, polling the host port
   (its words read, a packet's sent as there is room). 0.4 to 0.6 had the
   DSP's own host interrupt do it, which a Falcon with the FDI on its
   input's clock answered with spurious interrupts (24 bombs in 0.5; 111
   in 10 s and a hang in 0.6, the host port's requests found off); Cubase
   Audio doesn't use it either */
static long start_stream(void)
{
	dspmp3_tick_stream(rate, out_rate, xs ? 2 : kind != 0, s_sblim);
	return 0;
}

/* what the log says of the stream (play_stream_note): the tick's
   longest gap once the sound runs (interrupts held off, by whom), the
   matrix as another program may have set it while EPlay played */
static volatile unsigned long t_gap;	/* (fine_now's steps) */
static volatile long t_gap_at = -1;	/* t_frames then */
static volatile long m_changed_at = -1;
static unsigned short m_now[4];

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"	/* (fixed hardware addresses) */
static void watch_matrix(void)
{
	volatile unsigned short *r = (volatile unsigned short *)0xffff8930L;
	int i;
	if (m_changed_at >= 0)
		return;
	for (i = 0; i < 4; i++)
		if (r[i] != snd_matrix[i]) {
			for (i = 0; i < 4; i++)
				m_now[i] = r[i];
			m_changed_at = t_frames;
			return;
		}
}
#pragma GCC diagnostic pop

static void tick(void)
{
	unsigned long a = fine_now();
	if (!t_t0)
		t_t0 = a;
	else if (dsp ? s_started && t_frames > 4 : snd_playing()) {
		long d = (long)(a - t_last);	/* (never less than 0, but guarded) */
		if (d > 0 && (unsigned long)d > t_gap) {
			t_gap = (unsigned long)d;
			t_gap_at = dsp ? t_frames : snd_played();	/* (frames: ms for the log) */
		}
		if (dsp)
			watch_matrix();
	}
	tick_body();
	t_last = fine_now();
	t_busy += t_last - a;
}

static void tick_body(void)
{
	if (!dsp || xs)
		snd_update();
	if (loading) {			/* the DSP's program first, a piece a tick */
		int r;
		if (loading == 2) {	/* the DSP's reset: after Play's draw (60 ms), each
					   wait 6 ms at least (here, not in GEM's steps,
					   which the file selector holds) */
			if (dspmp3_tick_reset(30, 3))
				loading = 1;
			return;
		}
		r = dspmp3_tick_load();
		if (r < 0)
			t_failed = 1;
		else if (r)
			loading = 0;
		return;
	}
	if (!t_end && !t_failed && !dsp)
		tick_pcm();
	if (dsp && s_started) {
		dspmp3_tick_status();	/* (its words read even at the end) */
		if (!t_end && !t_failed) {
			if (xs)
				tick_xs();
			else
				tick_mp3();
		}
	}
	if (dsp && dspmp3_failed())
		t_failed = 1;
	if (t_end && !dsp) {
		/* silence after the end, so that the ring isn't played again */
		long room = snd_room(), k;
		if (t_ended_at < 0)
			t_ended_at = snd_written();
		while (room > 0) {
			short *p = snd_ptr(&k);
			if (k > room)
				k = room;
			long i;
			for (i = 0; i < k; i++, p += SND_STEP)
				*(long *)(void *)p = 0;
			snd_wrote(k);
			room -= k;
		}
	}
}

/* ---- GEM's side: the file into the queue ---- */

static void free_queue(void)
{
	if (q) {
		Mfree(q);
		q = 0;
	}
}

/* the queue taken for this file: 0 if there isn't the memory */
static int take_queue(int unit)
{
	long big = (long)Mxalloc(-1L, 3), n = big / 3;
	free_queue();
	if (n > QMAX)
		n = QMAX;
	if (n < QMIN)
		n = QMIN;
	if (n > big - 32768)
		return 0;
	n -= n % unit;
	q = Mxalloc(n, 3);		/* TT RAM where there is some */
	if ((long)q <= 0) {
		q = 0;
		return 0;
	}
	q_size = n;
	q_w = q_r = q_woff = q_roff = 0;
	src_done = 0;
	bshift = unit == 1 ? 0 : unit == 2 ? 1 : unit == 4 ? 2 : unit == 8 ? 3 : -1;
	return 1;
}

/* how the disk kept up, for the log (play_disk_note): the bytes read,
   the time the reads took (the 200 Hz clock's, summed), the queue's
   least lead once a second has played; -1 before */
static long rd_bytes, rd_ms, rd_forced, q_least = -1;

/* the file read into the queue, a piece at a time, for about ms */
/* the queue's lead: ms of music it holds, at the rate it is played (a
   WAV the DMA plays at another rate: at that one) */
static long q_lead_ms(void)
{
	long ahead = q_w - q_r, r = kind && !dsp ? out_rate : rate;
	if (kind)
		return r > 0 ? ahead / fmt.bytes / r * 1000 + ahead / fmt.bytes % r * 1000 / r : 0;
	return bitrate ? ahead / bitrate * 8 : 0;
}

/* the queue filled from the file for ms, a piece at a time. low (the lead
   short): a piece read whatever the turn has left, and pieces of half the
   size, so that a turn stays near its 50 ms (Erez, 4 Nov 2026, a stock
   Falcon: the turns' 20 ms often spent before the reading, which then
   came at 246 KB/s, the music 295: the queue ran dry in 4 s) */
static void fill(unsigned long t0, int ms, int low)
{
	long piece = low ? PIECE / 2 : PIECE;
	int must = low;
	while (!src_done && (must || ms_now() - t0 < (unsigned long)ms)) {
		long free = q_size - (q_w - q_r), at = q_woff, n = q_size - at, r;
		if (n > free)
			n = free;
		if (n > piece)
			n = piece;
		if (n > data_left)
			n = data_left;
		/* a whole piece, else wait for the room (but the file's end and
		   the queue's end are read as they come) */
		if (n <= 0 || (!net && n < piece && n < data_left && at + n < q_size))
			break;		/* (a station: what has come, as it comes) */
		if (must) {
			must = 0;
			if (ms_now() - t0 >= (unsigned long)ms)
				rd_forced++;	/* (the turn's time gone: read all the same) */
		}
		if (net) {
			if (net_pend_n > 0) {
				r = n < net_pend_n ? n : net_pend_n;
				memcpy(q + at, fbuf + net_pend_at, (size_t)r);
				net_pend_at += r;
				net_pend_n -= r;
			} else {
				r = ns_read(q + at, n, 0);
				if (r == 0)
					break;	/* (nothing more yet) */
				if (r < 0) {
					play_net_lost = 1;	/* (play_step connects again) */
					break;
				}
			}
		} else {
			unsigned long a = ms_now();
			r = Fread(fh, n, q + at);
			rd_ms += (long)(ms_now() - a);
		}
		if (r > 0)
			rd_bytes += r;
		if (r <= 0) {
			src_done = 1;
			break;
		}
		q_woff += r;
		if (q_woff >= q_size)
			q_woff -= q_size;
		q_w += r;			/* (last: the tick sees the bytes there) */
		data_left -= r;
		if (data_left <= 0)
			src_done = 1;
	}
}

static int out_set;		/* Settings' Output at this Play */

int play_open(const char *path, char *err, int n)
{
	const char *why;
	char kr[20];
	int ext;

	play_log_stop();		/* (another file while one plays: what the first did) */
	play_stop();
	if (path != cur_path) {
		snprintf(cur_path, sizeof(cur_path), "%s", path);
		q_normal = 0;		/* (a file starts at High; a seek or Play keeps its own) */
	}
	base_ms = 0;
	if (!fbuf) {
		fbuf = Mxalloc(FBUF, 3);	/* TT RAM where there is some */
		if ((long)fbuf <= 0)
			fbuf = Mxalloc(FBUF, 0);
		if ((long)fbuf <= 0) {
			fbuf = 0;
			snprintf(err, n, "There isn't the memory to play.");
			return 0;
		}
	}
	net = surl_is(path);
	if (net) {
		/* a station: connected and its answer read (Esc stops it), the
		   first few KB of its audio for the probe; no tags, no length,
		   no seeking (Erez, 8 Nov 2026: Pause connects again) */
		if (!ns_open(path, err, n)) {
			net = 0;
			return 0;
		}
		play_net_lost = 0;
		file_size = 0x7fffffffL;
		memset(&play_tags, 0, sizeof(play_tags));
		seek_ms = seek_byte = 0;
		fpos = fend = 0;
		eof = 0;
		while (!eof && fend < 8192)
			refill();
		snprintf(play_name, sizeof(play_name), "%s", ns_name());
	} else {
	fh = (int)Fopen(path, 0);
	if (fh < 0) {
		snprintf(err, n, "%s can't be opened.", base_name(path));
		fh = -1;
		return 0;
	}
	file_size = Fseek(0, fh, 2);
	/* what the file says it is (a few short reads: frames passed over by
	   seeking, the album art only found); a seek's or Pause's file again
	   keeps them (the position bar's release on a stock Falcon: 55 ms) */
	if (path != cur_path)
		tags_read(&play_tags, tag_read, 0, file_size);
	Fseek(0, fh, 0);
	fpos = fend = 0;
	eof = 0;
	refill();
	snprintf(play_name, sizeof(play_name), "%s", base_name(path));
	}
	dsp = xs = 0;
	pcm_got = 0;
	elog("open %s (%ld bytes)", path, file_size);
	if (play_tags.title[0] || play_tags.artist[0] || play_tags.album[0] || play_tags.pic_end)
		elog("tags: \"%s\", \"%s\", \"%s\"; album art %ld bytes%s", play_tags.title, play_tags.artist,
		     play_tags.album, play_tags.pic_end - play_tags.pic_at, play_tags.pic_unsync ? " (unsynchronised)" : "");
	if (!net && pcm_probe(fbuf, fend, &fmt, &why)) {
		kind = fmt.kind;
		rate = fmt.rate;
		len_ms = fmt.frames / rate * 1000 + fmt.frames % rate * 1000 / rate;	/* (no overflow past 13 minutes) */
		data_left = fmt.frames > 0 ? fmt.frames * fmt.bytes : file_size - fmt.data;
		{
			/* play_seek's place: whole sample frames on */
			long skip = 0;
			if (seek_ms > 0 && len_ms > 0) {
				skip = seek_ms / 1000 * rate + seek_ms % 1000 * rate / 1000;
				if (skip > data_left / fmt.bytes)
					skip = data_left / fmt.bytes;
				base_ms = seek_ms < len_ms ? seek_ms : len_ms;
				elog("from %ld ms: sample frame %ld", base_ms, skip);
			}
			data_left -= skip * fmt.bytes;
			Fseek(fmt.data + skip * fmt.bytes, fh, 0);	/* the samples read from here into the queue */
		}
		if (!take_queue(fmt.bytes)) {
			snprintf(err, n, "There isn't the memory to play.");
			close_file();
			return 0;
		}
		khz(kr, sizeof(kr), rate);
		play_hz = rate;
		play_kbps = (rate * fmt.channels * fmt.bits + 500) / 1000;
		snprintf(play_info, sizeof(play_info), "%s  %d bits  %s  %s", kind == PCM_WAV ? "WAV" : "AIFF",
			 fmt.bits, kr, fmt.channels == 2 ? "stereo" : "mono");
		elog("%s: %ld Hz, %d bits, %d ch, %ld frames from %ld; %ld ms", kind == PCM_WAV ? "WAV" : "AIFF",
		     fmt.rate, fmt.bits, fmt.channels, fmt.frames, fmt.data, len_ms);
	} else {
		MP3HDR h;
		long at = find_mp3(&h);
		if (at < 0 && net) {
			snprintf(err, n, "%s doesn't send MP3: play it through the Gateway.", play_name);
			close_file();
			return 0;
		}
		if (at < 0) {
			snprintf(err, n, "%s isn't a file EPlay plays: %s, nor an MP3.", play_name, why);
			close_file();
			return 0;
		}
		kind = 0;
		dsp = 1;
		rate = h.rate;
		bitrate = h.bitrate;
		{
			/* a Xing, Info or VBRI frame: the file's frames, so its
			   length (a VBR file's first frame's bit rate is no
			   guide); the frame holds no sound and isn't played */
			int vbr;
			long nf = !net && first_at + h.length <= fend ? mp3_info_frames(fbuf + first_at, &h, &vbr) : 0;
			long bytes = file_size - at;
			if (nf > 0) {
				long fs = nf * h.samples;
				MP3LAME lt;
				cur_pad = mp3_info_lame(fbuf + first_at, &h, &lt) ? lt.padding : -1;
				has_toc = mp3_info_toc(fbuf + first_at, &h, xtoc);
				xing_at = at;
				at += h.length;
				bytes -= h.length;
				len_ms = fs / rate * 1000 + fs % rate * 1000 / rate;
				if (vbr && len_ms > 0)
					bitrate = bytes / len_ms * 8 + (bytes % len_ms) * 8 / len_ms;
			} else {
				vbr = 0;
				cur_pad = -1;
				has_toc = 0;
				xing_at = at;
				len_ms = h.bitrate && !net ? bytes * 8 / h.bitrate : 0;	/* (a station: Live) */
			}
			khz(kr, sizeof(kr), rate);
			play_hz = rate;
			play_kbps = bitrate;
			snprintf(play_info, sizeof(play_info), "MP3  %s%ld kbit/s  %s  %s", vbr ? "VBR " : "",
				 bitrate, kr, h.channels == 2 ? "stereo" : "mono");
			elog("MP3 at %ld: MPEG-%s, %d kbit/s first frame, %d Hz, %d ch, CRC %d; header frames %ld%s; %ld ms",
			     at, h.version == 1 ? "1" : h.version == 2 ? "2" : "2.5", h.bitrate, h.rate, h.channels,
			     h.crc, nf, vbr ? " (VBR)" : "", len_ms);
		}
		if (seek_byte > 0) {
			/* Pause's frame: there, as it is (its header checked) */
			MP3HDR hs;
			unsigned char b4[4];
			if (Fseek(seek_byte, fh, 0) == seek_byte && Fread(fh, 4, b4) == 4 && mp3_header(b4, &hs) &&
			    hs.rate == rate) {
				at = seek_byte;
				base_ms = seek_ms;
				elog("from %ld ms: byte %ld (where Pause was)", seek_ms, at);
			}
		} else if (seek_ms > 0 && len_ms > 0) {
			/* play_seek's place: by the Xing frame's table, else by the
			   bytes (CBR, or a VBRI file: near enough), then the first
			   whole frame from there */
			long ms = seek_ms < len_ms ? seek_ms : len_ms, bytes = file_size - xing_at, to, k;
			if (has_toc) {
				long pc = (long)((long long)ms * 100 / len_ms), r = (long)((long long)ms * 100 % len_ms);
				int fa, fb;
				if (pc > 99)
					pc = 99;
				fa = xtoc[pc];
				fb = pc < 99 ? xtoc[pc + 1] : 256;
				to = xing_at + (long)((long long)bytes * (fa * (long long)len_ms + (fb - fa) * (long long)r) /
						      (256 * (long long)len_ms));
			} else
				to = xing_at + (long)((long long)bytes * ms / len_ms);
			if (to < at)
				to = at;
			/* (4 KB: two frames' headers, 1,441 bytes each at most;
			   refill's 64 KB were half a stock Falcon's seek) */
			Fseek(to, fh, 0);
			fpos = 0;
			fend = Fread(fh, 4096, fbuf);
			if (fend < 0)
				fend = 0;
			eof = 0;
			k = sync_mp3(rate);
			if (k >= 0) {
				at = to + k;
				base_ms = ms;
				elog("from %ld ms: byte %ld%s", ms, at, has_toc ? " (the Xing table's)" : "");
			}
		}
		data_left = file_size - at;
		if (!net && file_size >= 128) {
			/* (an ID3v1 tag at its end isn't sound: out of the queue, so
			   its last frame ends where the next file's begins: gapless) */
			unsigned char tg[3];
			if (Fseek(file_size - 128, fh, 0) == file_size - 128 && Fread(fh, 3, tg) == 3 && !memcmp(tg, "TAG", 3))
				data_left -= 128;
		}
		q_base = at;
		fr_sent = 0;
		if (net) {
			net_pend_at = at;	/* (what the probe read goes first) */
			net_pend_n = fend - at;
		} else
			Fseek(at, fh, 0);	/* the frames read from here into the queue */
		if (!take_queue(1)) {
			snprintf(err, n, "There isn't the memory to play.");
			close_file();
			return 0;
		}
		switch (dspmp3_open()) {
		case DSPMP3_OK:
			break;
		case DSPMP3_BUSY:
			snprintf(err, n, "The DSP is in use by another program.");
			close_file();
			return 0;
		case DSPMP3_NONE:
			snprintf(err, n, "MP3 needs a Falcon's DSP.");
			close_file();
			return 0;
		default:
			snprintf(err, n, "The DSP didn't answer.");
			close_file();
			return 0;
		}
		mp3_reset(&parse);
		if (net) {
			/* a station: about 3 s in the queue before it plays, so the
			   network's pauses don't reach the music (the bee shows;
			   Esc or Undo stops it, as connecting) */
			long want = (bitrate > 0 ? bitrate : 128) * 125 * 3;
			unsigned long t0 = ms_now();
			if (want > q_size / 2)
				want = q_size / 2;
			while (q_w < want && !conn_stop && ms_now() - t0 < 20000) {
				fill(ms_now(), 50, 0);
				if (play_net_lost)
					break;
				if (q_w < want && pf_idle)
					pf_idle();
			}
			if (q_w < want && (conn_stop || play_net_lost)) {
				snprintf(err, n, conn_stop ? "Connecting to the station was stopped." :
					 "The station closed the connection.");
				dspmp3_close();
				close_file();
				return 0;
			}
			elog("the station: %ld KB in the queue before it plays, in %ld ms", q_w / 1024,
			     (long)(ms_now() - t0));
		}
	}
	/* Settings' Output, as it is at this Play (Erez, 2 Nov 2026): Auto and
	   FDI put the FDI at the file's rate where they can; FDI then plays on
	   its clock as it is, resampled; DSP leaves the FDI and its clock out */
	out_set = settings_output();
	s_sblim = 32;
	snd_ext_use = out_set != OUT_DSP;
	snd_ext_only = out_set == OUT_FDI;
	snd_fdi_note[0] = 0;	/* (only this play's tries in the log: 0.15 repeated the last
				   play's with Output DSP, where the FDI isn't touched) */
	if (!kind) {
		/* MP3 from the DSP: the FDI at the file's rate where it can be,
		   else a clock the DSP resamples to (snd_rate_dsp) */
		char mn[100];
		spurious_on();		/* (from here: 0.5 met 24 bombs as it began) */
		snd_mfp_note(mn, sizeof(mn));
		elog("the MFP before: %s", mn);
		if (out_set != OUT_DSP && snd_fdi_set(rate)) {
			ext = 1;
			out_rate = rate;
		} else
			out_rate = snd_rate_dsp(rate, &ext);
		/* Normal (q_normal: the DSP fell behind at High), on the Falcon's
		   clock: 32,780 Hz, the subbands above about 15 kHz left out (the
		   DSP's time that resampling up to 49,170 Hz lacked); the FDI's
		   clock, or a rate of 32 kHz or less, as High */
		if (!ext && q_normal && rate > 32780) {
			out_rate = 32780;
			s_sblim = MP3_SBLIM(rate);
		}
		if (snd_fdi_note[0]) {
			elog("the FDI to %ld Hz: %s", rate, snd_fdi_note);
			snd_mfp_note(mn, sizeof(mn));
			elog("the MFP after: %s", mn);
		}
	} else {
		/* WAV, AIFF: the DMA at the file's rate, the FDI put at it where
		   it can be; else through the DSP, resampled (XSTREAM: the DMA's
		   ring to its receive, no host-port word a sample: a word each
		   was 70 % of a stock Falcon's 68030 for CD's stereo), when it
		   is free and the file's rate is below its output's (a frame
		   carries a sample at most); else the DMA at the nearest rate */
		out_rate = snd_rate_for(rate, &ext);
		if (out_rate != rate && snd_fdi && out_set != OUT_DSP) {
			spurious_on();
			if (snd_fdi_set(rate)) {
				ext = 1;
				out_rate = rate;
			}
			if (snd_fdi_note[0])
				elog("the FDI to %ld Hz: %s", rate, snd_fdi_note);
		}
		snd_in_rate = rate;	/* (the 8-bit DMA resamples from it) */
		raw8 = snd_dma8 && !snd_dma8_interp;
		if (snd_dma8) {
			if (out_rate != rate)
				elog("%ld Hz up to the DMA's %ld Hz by the 68%s0: %s", rate, out_rate,
				     snd_dma8_interp ? "03" : "00", snd_dma8_interp ? "interpolated, dithered to 8 bits" :
				     "the nearest sample");
		} else if (out_rate != rate) {
			int dext, r;
			long dr = snd_rate_dsp(rate, &dext);
			if (dr <= rate)
				elog("%ld Hz is the DSP's output or above: the DMA at the nearest rate", rate);
			else if ((r = dspmp3_open()) == DSPMP3_OK) {
				dsp = xs = 1;
				out_rate = dr;
				ext = dext;
			} else
				elog("the DSP %s: the DMA at the nearest rate", r == DSPMP3_BUSY ? "in use" :
				     r == DSPMP3_NONE ? "not there" : "didn't answer");
		}
		if (xs ? !snd_open_feed(out_rate, ext) : !dsp && !snd_open(out_rate, ext)) {
			snprintf(err, n, "There isn't the ST RAM to play.");
			play_stop();
			return 0;
		}
	}
	khz(kr, sizeof(kr), out_rate);
	play_via = ext ? "FDI" : dsp ? "DSP" : "DMA";
	s_ext = ext;
	apply_volume();
	pcm_gain_set(&gs, t_gain);	/* (a file starts at its volume: no glide) */
	play_nosig = 0;
	if (snd_dma8 && out_rate != rate) {	/* (the TT's or STE's DMA, resampled by the 68030 or 68000) */
		char fr[16];
		khz(fr, sizeof(fr), rate);
		snprintf(play_out, sizeof(play_out), "DMA 8 bits  %s (from %s)", kr, fr);
	} else if (snd_dma8)
		snprintf(play_out, sizeof(play_out), "DMA 8 bits  %s", kr);
	else if (dsp && out_rate != rate) {	/* (the DSP resamples) */
		char fr[16];
		khz(fr, sizeof(fr), rate);
		if (ext && snd_fdi_nosig && out_set != OUT_DSP) {
			/* the FDI couldn't follow its input (Erez, 7 Nov 2026: the
			   words his): the window's info line says it alone */
			snprintf(play_out, sizeof(play_out), "FDI %s (from %s): no signal at the FDI's input", kr, fr);
			play_nosig = 1;
		} else
			snprintf(play_out, sizeof(play_out), "%s  %s (from %s)", ext ? "External clock" : "Falcon", kr,
				 fr);
	} else if (ext)
		snprintf(play_out, sizeof(play_out), "External clock  %s", kr);
	else if (out_rate == rate)
		snprintf(play_out, sizeof(play_out), "Falcon  %s", kr);
	else
		snprintf(play_out, sizeof(play_out), "Falcon  %s (not the file's rate)", kr);
	elog("output: %ld Hz, %s%s%s; queue %ld bytes (Settings: %s)", out_rate,
	     ext ? "the external clock" : "the Falcon's clock", dsp && out_rate != rate ? ", resampled on the DSP" : "",
	     s_sblim < 32 ? ", the subbands above about 15 kHz left out" : "", q_size, settings_output_name(out_set));
	t_end = t_dsp = t_failed = 0;
	t_frames = 0;
	s_started = s_end = 0;
	xs_acc = xs_in = xs_sil = xs_dry = 0;
	xs_dry_now = 0;
	xs_end_at = -1;
	s_spf = 1152;
	t_busy = t_parse = t_push = 0;
	t_t0 = t_last = 0;
	t_gap = 0;
	t_gap_at = m_changed_at = -1;
	rd_bytes = rd_ms = rd_forced = 0;
	q_least = -1;
	pk_n = src_out = 0;
	t_ended_at = -1;
	last_sec = -1;
	chain_q = chain_frame = -1;
	chain_cross = chain_asked = chain_heard = 0;
	pk_chain = 0;
	nx_delay = nx_pad = -1;
	drop_set = 0;
	fr_base = pcm_base = 0;
	return 1;
}

/* gapless: 1 once, when this file's last bytes are in the queue and the
   next could follow on (main.c then chooses it, as at a file's end, and
   gives it to play_chain) */
int play_wants_next(void)
{
	if (chain_asked || !src_done || net || paused || state != PS_PLAYING || chain_q >= 0 || t_end ||
	    (dsp && xs))
		return 0;
	if (q_w - q_r < 32768L)
		return 0;		/* (too near the end to begin anything) */
	chain_asked = 1;
	return 1;
}

/* gapless: path opened to follow on in the queue: 1 when its sound is
   the same as this one's (an MP3 at the same rate, its first frame found
   and its Xing frame passed; a WAV or AIFF of the same format), 0 (and
   nothing changed) when it isn't: it then opens at this one's end as
   before */
int play_chain(const char *path)
{
	int h2 = (int)Fopen(path, 0);
	long size, at, left;
	TAGS t;
	if (h2 < 0)
		return 0;
	size = Fseek(0, (short)h2, 2);
	memset(&nx, 0, sizeof(nx));
	nx_delay = nx_pad = -1;
	drop_set = 0;
	{
		int was = fh;		/* (tag_read and refill read fh: the next file's here) */
		long wpos = fpos, wend = fend;
		int weof = eof;
		fh = h2;
		tags_read(&t, tag_read, 0, size);
		Fseek(0, (short)h2, 0);
		fpos = fend = 0;
		eof = 0;
		refill_cap = 8192;	/* (a header, the Xing frame, a frame or two: the
					   speed rig's Return held 345 ms with 32 KB here) */
		refill();
		if (dsp) {
			MP3HDR h;
			int vbr = 0;
			long nf;
			at = find_mp3(&h);
			if (at < 0 || h.rate != rate) {
				fh = was;
				fpos = wpos;
				fend = wend;
				eof = weof;
				Fclose((short)h2);
				refill_cap = 32768;
				elog("gapless: %s is no MP3 at %ld Hz: it opens at this one's end", base_name(path), rate);
				return 0;
			}
			nf = first_at + h.length <= fend ? mp3_info_frames(fbuf + first_at, &h, &vbr) : 0;
			nx.xing_at = at;
			left = size - at;
			if (nf > 0) {
				long fs = nf * h.samples;
				MP3LAME lt;
				if (mp3_info_lame(fbuf + first_at, &h, &lt)) {
					nx_delay = lt.delay;
					nx_pad = lt.padding;
				}
				nx.has_toc = mp3_info_toc(fbuf + first_at, &h, nx.xtoc);
				at += h.length;		/* (the Xing frame holds no sound) */
				left -= h.length;
				nx.len_ms = fs / rate * 1000 + fs % rate * 1000 / rate;
				nx.bitrate = vbr && nx.len_ms > 0 ? left / nx.len_ms * 8 + (left % nx.len_ms) * 8 / nx.len_ms :
								      h.bitrate;
			} else {
				nx.bitrate = h.bitrate;
				nx.len_ms = h.bitrate ? left * 8 / h.bitrate : 0;
			}
			{
				/* (an ID3v1 tag at its end isn't sound: left out of the queue) */
				unsigned char tg[3];
				if (size >= 128 && Fseek(size - 128, (short)h2, 0) == size - 128 && Fread((short)h2, 3, tg) == 3 &&
				    !memcmp(tg, "TAG", 3))
					left -= 128;
			}
			{
				char kr[20];
				khz(kr, sizeof(kr), rate);
				nx.kbps = nx.bitrate;
				snprintf(nx.info, sizeof(nx.info), "MP3  %s%ld kbit/s  %s  %s", vbr ? "VBR " : "", nx.bitrate,
					 kr, h.channels == 2 ? "stereo" : "mono");
			}
			chain_at = at;
		} else {
			PCMFMT f2;
			const char *why;
			if (!pcm_probe(fbuf, fend, &f2, &why) || f2.kind != fmt.kind || f2.rate != fmt.rate ||
			    f2.channels != fmt.channels || f2.bits != fmt.bits || f2.big != fmt.big || f2.sign8 != fmt.sign8) {
				fh = was;
				fpos = wpos;
				fend = wend;
				eof = weof;
				Fclose((short)h2);
				refill_cap = 32768;
				elog("gapless: %s is not this file's format: it opens at this one's end", base_name(path));
				return 0;
			}
			at = f2.data;
			left = f2.frames > 0 ? f2.frames * f2.bytes : size - f2.data;
			nx.len_ms = f2.frames / f2.rate * 1000 + f2.frames % f2.rate * 1000 / f2.rate;
			nx.kbps = (f2.rate * f2.channels * f2.bits + 500) / 1000;
			{
				char kr[20];
				khz(kr, sizeof(kr), f2.rate);
				snprintf(nx.info, sizeof(nx.info), "%s  %d bits  %s  %s", f2.kind == PCM_WAV ? "WAV" : "AIFF",
					 f2.bits, kr, f2.channels == 2 ? "stereo" : "mono");
			}
		}
		refill_cap = 32768;
		if (was >= 0)
			Fclose((short)was);	/* (its bytes are all in the queue) */
	}
	Fseek(at, (short)h2, 0);
	nx.tags = t;
	snprintf(nx.path, sizeof(nx.path), "%s", path);
	snprintf(nx.name, sizeof(nx.name), "%s", base_name(path));
	chain_cross = 0;
	chain_frame = -1;
	pk_chain = 0;
	data_left = left;
	chain_q = q_w;			/* (its first byte goes there) */
	src_done = 0;			/* (last: the tick sees more to come) */
	elog("gapless: %s follows on (%ld bytes from %ld)", nx.name, left, at);
	return 1;
}

/* gapless: 1 once, when the file that followed on is heard: it is what
   plays now (its name, tags, length, the time from 0) */
int play_chained(void)
{
	if (!chain_heard)
		return 0;
	chain_heard = 0;
	return 1;
}

/* (play_step's: the next file's sound begun? its own from then) */
static void chain_check(void)
{
	if (chain_q < 0)
		return;
	if (dsp) {
		long fr, late, wmin, wavg;
		if (chain_frame < 0)
			return;
		dspmp3_stream_info(&fr, &late, &wmin, &wavg);
		if (fr <= chain_frame)
			return;
		fr_base = chain_frame;
		cur_pad = nx_pad;		/* (its padding: the next join's) */
		nx_delay = -1;
		bitrate = nx.bitrate;
		xing_at = nx.xing_at;
		has_toc = nx.has_toc;
		memcpy(xtoc, nx.xtoc, sizeof(xtoc));
	} else {
		if (snd_played() < B2FR(chain_q))
			return;
		pcm_base = B2FR(chain_q);
	}
	snprintf(cur_path, sizeof(cur_path), "%s", nx.path);
	snprintf(play_name, sizeof(play_name), "%s", nx.name);
	snprintf(play_info, sizeof(play_info), "%s", nx.info);
	play_tags = nx.tags;
	len_ms = nx.len_ms;
	play_kbps = nx.kbps;
	base_ms = 0;
	chain_q = -1;
	chain_asked = 0;
	chain_heard = 1;
	q_normal = 0;
	elog("gapless: %s heard after %ld ms of the queue", play_name, play_ms());
}


int play_seek(long ms, char *err, int n)
{
	int was = state == PS_PLAYING, ok;
	unsigned long t0 = tos_ms(), t1;
	if (!cur_path[0])
		return 0;
	if (!was) {
		/* paused or stopped: the place kept for Play, nothing opened (the
		   DSP is taken only to play) */
		paused = 1;
		pause_ms = ms > 0 ? ms : 0;
		pause_byte = 0;
		return 1;
	}
	paused = 0;
	seek_ms = ms > 0 ? ms : 0;
	ok = play_open(cur_path, err, n);
	seek_ms = 0;
	t1 = tos_ms();
	if (ok && was)
		play_start();
	elog("the seek: opened again in %lu ms, started in %lu", t1 - t0, tos_ms() - t1);
	return ok;
}

void play_pause(void)
{
	long fr, late, wmin, wavg, k;
	if (state != PS_PLAYING)
		return;
	pause_ms = play_ms();
	pause_byte = 0;
	if (dsp && !kind && s_started) {
		/* the frame playing: the DSP's count of frames decoded, less the
		   one it plays (play_ms's), where the tick sent it from */
		dspmp3_stream_info(&fr, &late, &wmin, &wavg);
		k = fr > 1 ? fr - 1 : 0;
		if (k < fr_sent && fr_sent - k <= FR_RING)
			pause_byte = fr_at[k % FR_RING];
	}
	play_log_stop();
	elog("paused at %ld ms%s", pause_ms, pause_byte ? "" : " (Play takes it up by the time)");
	play_stop();			/* (and the DSP given back: the DSP rule) */
	paused = 1;
}

int play_resume(char *err, int n)
{
	int ok;
	if (!paused || !cur_path[0])
		return 0;
	paused = 0;
	seek_ms = pause_ms;
	seek_byte = pause_byte;
	ok = play_open(cur_path, err, n);
	seek_ms = seek_byte = 0;
	if (ok)
		play_start();
	return ok;
}

int play_paused(void)
{
	return paused;
}

void play_start(void)
{
	if ((fh >= 0 || net) && state != PS_PLAYING) {
		state = PS_PLAYING;
		if (dsp) {
			snd_dsp_out(out_rate, out_rate == snd_ext_rate);
			elog("the matrix: $%04X $%04X $%04X $%04X", snd_matrix[0], snd_matrix[1], snd_matrix[2],
			     snd_matrix[3]);
		}
		loading = dsp ? 2 : 0;	/* (the tick loads the DSP first: not waiting for GEM) */
		feed_on(tick, dsp && !xs ? 500 : 250);	/* (the DMA: 1024 frames a tick, 4 times what
						   plays, XSTREAM too; the DSP: its next packet at once) */
	}
}

void play_stop(void)
{
	paused = 0;
	feed_off();			/* first: the tick and the DSP's interrupt no longer run */
	hostirq_off();
	if (hirq_spur)
		elog("spurious interrupts while playing: %lu", (unsigned long)hirq_spur);
	spurious_off();
	loading = 0;
	snd_close();
	t_dsp = 0;
	if (dsp && state == PS_PLAYING)
		snd_dsp_off();		/* (the DSP left as it is: whoever takes it
					   next resets it) */
	if (dsp)
		dspmp3_close();
	close_file();
	free_queue();
	state = PS_IDLE;
}

int play_state(void)
{
	return state;
}

int play_is_open(void)
{
	return fh >= 0 || net;
}

/* a station is what plays or is paused (its address, not a file) */
int play_is_net(void)
{
	return net || (cur_path[0] && surl_is(cur_path));
}

/* the bar's for a station: the song now ("" before its first title) */
/* the station's picture's address ("" none), and whether it changed */
const char *play_net_picture(int *changed)
{
	if (!net) {
		if (changed)
			*changed = 0;
		return "";
	}
	return ns_picture(changed);
}

/* a station's new title: to the log, the rigs' console, the bar */
static void net_titles(void)
{
	int changed;
	const char *t;
	if (!net)
		return;
	t = ns_title(&changed);
	if (changed) {
		char l[200];
		elog("the station's title: %s", t);
		snprintf(l, sizeof(l), "EPLAY title: %s\n", t);
		nf_debug(l);
		play_note_new = 1;	/* (the bar and info line again) */
	}
}

/* a station's queue topped up from what has come, and its titles (sart.c's
   waits: a picture fetched while the station plays); never inside
   play_step */
void play_feed(void)
{
	if (state == PS_PLAYING && net && !src_done) {
		fill(ms_now(), 5, 0);
		net_titles();
	}
}

const char *play_net_title(void)
{
	return net ? ns_title_peek() : "";
}

long play_read_at(long at, void *buf, long n)
{
	long here, r;
	if (fh < 0)
		return -1;
	here = Fseek(0, fh, 1);		/* (the queue's reads go on from here) */
	if (Fseek(at, fh, 0) != at) {
		Fseek(here, fh, 0);
		return -1;
	}
	r = Fread(fh, n, buf);
	Fseek(here, fh, 0);
	return r;
}

long play_ms(void)
{
	long f;
	if (paused)
		return pause_ms;
	if (dsp) {			/* (the file's samples: at its rate, resampled or not) */
		long fr, late, wmin, wavg;
		if (!s_started)
			return base_ms;		/* (the counts are the last stream's until this one's start) */
		dspmp3_stream_info(&fr, &late, &wmin, &wavg);
		fr -= fr_base;		/* (a file followed on: its own frames) */
		f = fr > 1 ? (fr - 1) * s_spf : 0;
		return base_ms + (rate ? (f / rate) * 1000 + (f % rate) * 1000 / rate : 0);
	}
	f = snd_played() - pcm_base;
	return base_ms + (rate ? (f / rate) * 1000 + (f % rate) * 1000 / rate : 0);
}

/* the tick's share of the 68030, % */
long play_tick_share(void)
{
	unsigned long all = t_t0 ? t_last - t_t0 : 0;
	return all ? (long)(t_busy * 100 / all) : 0;
}

long play_push_share(void)
{
	unsigned long all = t_t0 ? t_last - t_t0 : 0;
	return all ? (long)(t_push * 100 / all) : 0;
}

long play_parse_share(void)
{
	unsigned long all = t_t0 ? t_last - t_t0 : 0;
	return all ? (long)(t_parse * 100 / all) : 0;
}

/* frames at this rate as ms (-1 stays -1) */
static long f_ms(long f)
{
	return f < 0 || !rate ? -1 : (f / rate) * 1000 + (f % rate) * 1000 / rate;
}

/* MP3 frames (or the DMA's frames) as ms, for the log (-1 stays -1) */
static long fr_ms(long f)
{
	return f < 0 ? -1 : f_ms(dsp ? f * s_spf : f);
}

/* the stream's events, for the log: the tick's longest gap (what held
   interrupts off), the first late granule and broken packet, the rate
   the frames came at (the clock the DSP played on), the matrix if
   another program changed it */
const char *play_stream_note(void)
{
	static char b[400];	/* (about 300 at most) */
	long g = (long)(t_gap / 192 * 5000UL + t_gap % 192 * 5000UL / 192);	/* (us; no overflow) */
	int n = snprintf(b, sizeof(b), "the tick's longest gap %ld.%ld ms, at %ld ms", g / 1000, g % 1000 / 100,
			 fr_ms(t_gap_at));
	if (dsp) {
		long la, ra, fps = dspmp3_stream_fps1000();
		dspmp3_stream_firsts(&la, &ra);
		if (la >= 0)
			n += snprintf(b + n, sizeof(b) - n, "; the first granule late at %ld ms", fr_ms(la));
		if (ra >= 0)
			n += snprintf(b + n, sizeof(b) - n, "; the first packet broken at %ld ms", fr_ms(ra));
		if (fps)
			n += snprintf(b + n, sizeof(b) - n, "; the file's samples came at %ld Hz", fps * s_spf / 1000);
		if (dspmp3_stream_slots() >= 0)
			n += snprintf(b + n, sizeof(b) - n, "; the DSP port's frame: %d slots (EPlay fills 2), "
				      "%ld polls in 256", dspmp3_stream_slots(), dspmp3_stream_polls());
		if (m_changed_at >= 0)
			snprintf(b + n, sizeof(b) - n, "; the matrix changed by %ld ms: $%04X $%04X $%04X $%04X",
				 fr_ms(m_changed_at), m_now[0], m_now[1], m_now[2], m_now[3]);
		else
			snprintf(b + n, sizeof(b) - n, "; the matrix as EPlay set it");
	}
	return b;
}

const char *play_disk_note(void)
{
	static char b[220];
	long need = kind ? (dsp ? rate : out_rate) * fmt.bytes / 1024 : bitrate * 125 / 1024;	/* KB/s the music takes */
	char sp[24];
	if (rd_ms > 0)
		snprintf(sp, sizeof(sp), "%ld KB/s", rd_bytes / rd_ms * 1000 / 1024);
	else
		snprintf(sp, sizeof(sp), "too fast to time");	/* (the 200 Hz clock's 5 ms) */
	snprintf(b, sizeof(b), "%ld KB read in %ld ms of reads (%s; the music takes %ld KB/s); the queue's least "
		 "lead %ld ms; %ld pieces read past a turn's time", rd_bytes / 1024, rd_ms, sp, need, q_least, rd_forced);
	return b;
}

void play_log_stop(void)
{
	if (state == PS_PLAYING) {
		elog("stopped at %ld ms; %ld underruns (%s)", play_ms(), snd_underruns, play_keep_up());
		elog("the stream: %s", play_stream_note());
		elog("the disk: %s", play_disk_note());
	}
}

/* how the sound kept up, for the log: the first gap and the least lead */
const char *play_keep_up(void)
{
	static char b[260];
	if (dsp) {
		long fr, late, wmin, wavg;
		dspmp3_stream_info(&fr, &late, &wmin, &wavg);
		snprintf(b, sizeof(b), "%ld frames decoded, %ld granules late, %ld packets broken; the DSP's waits a "
			 "frame: least %ld, mean %ld; the tick %ld%% of the 68030 (frames made %ld%%, sent %ld%%)", fr,
			 late, dspmp3_stream_resyncs(), wmin, wavg, play_tick_share(), play_parse_share(),
			 play_push_share());
		return b;
	}
	if (snd_underruns)
		snprintf(b, sizeof(b), "the first at %ld ms; the least lead %ld ms; the tick %ld%% of the 68030",
			 f_ms(snd_first_under), f_ms(snd_least_lead), play_tick_share());
	else
		snprintf(b, sizeof(b), "the least lead %ld ms; the tick %ld%% of the 68030", f_ms(snd_least_lead),
			 play_tick_share());
	return b;
}

long play_len_ms(void)
{
	return len_ms;
}

int play_step(int ms)
{
	unsigned long t0 = ms_now();
	long sec;
	if (state != PS_PLAYING)
		return 0;
	if (dsp && !loading && !s_started && !t_failed) {
		if (xs)
			s_spf = 576;	/* (the DSP's packets, for play_ms) */
		Supexec(start_stream);	/* loaded: the stream, fed by the tick */
		s_started = 1;
	}
	if (xs && s_started && !snd_playing() && (snd_written() >= out_rate / 8 || s_end))
		snd_play();		/* (the DSP listening: its first frames not lost) */
	if (dsp && s_started && dspmp3_stream_stalled(tos_hz200()))
		t_failed = 1;
	if (t_failed) {
		int what, hisr;
		long at, of;
		dspmp3_fail_info(&what, &at, &of, &hisr);
		if (what == 4)
			elog("the DSP stopped answering while it played: no frame for 2 s after its frame %ld (host "
			     "status $%02X), %ld frames sent; %ld ms played", at, hisr, t_frames, play_ms());
		else
			elog("the DSP stopped answering %s word %ld of %ld (host status $%02X), frame %ld; %ld ms "
			     "played, %ld underruns", what == 1 ? "sending" : what == 2 ? "reading" : "loading", at, of,
			     hisr, t_frames, play_ms(), snd_underruns);
		elog("the sound: %s", play_keep_up());
		elog("the stream: %s", play_stream_note());
		if (what == 4) {
			char note[160];
			dspmp3_stall_note(note, sizeof(note));
			elog("the stream then: %s", note);
		}

		play_stop();
		elog_save();
		snprintf(play_out, sizeof(play_out), "The DSP stopped answering.");
		return 1;
	}
	fill(t0, ms, !src_done && q_lead_ms() < 3000);
	net_titles();
	chain_check();
	if (net && play_net_lost && !src_done) {
		/* the station's connection ended: again, every 2 s at most and
		   five times, while the queue plays on from memory (the music
		   doesn't wait for it); the new connection's audio follows on
		   in the queue (the tick finds its first frame by itself) */
		static unsigned long last_try;
		static int tries;
		if (play_net_lost == 1) {
			elog("the station's connection ended after %ld ms played; connecting again", play_ms());
			play_net_lost = 2;
			tries = 0;
			last_try = ms_now() - 2000;
		}
		if (ms_now() - last_try >= 2000) {
			char err[160];
			int ok;
			last_try = ms_now();
			conn_stop = 0;
			busy_bee(1);		/* (Esc or Undo gives it up: the music
						   plays on from memory meanwhile) */
			ok = ns_reopen(err, sizeof(err));
			busy_bee(0);
			if (ok) {
				elog("connected again (try %d)", tries + 1);
				play_net_lost = 0;
			} else if (conn_stop || ++tries >= 5) {
				elog(conn_stop ? "connecting again stopped" : "not connected again after 5 tries: %s", err);
				conn_stop = 0;
				src_done = 1;	/* (what the queue holds plays, then the end) */
				snprintf(play_out, sizeof(play_out), "The station's connection ended.");
			}
		}
	}
	/* (not a station: its frames late are the network's, not the DSP's,
	   and it can't be opened again where it was) */
	if (dsp && !kind && !net && s_started && !s_ext && !q_normal && rate > 32780 && s_sblim == 32) {
		/* High, and the DSP behind: Normal from here (a moment's break) */
		long fr, late, wmin, wavg;
		dspmp3_stream_info(&fr, &late, &wmin, &wavg);
		if (late >= settings_qlate() && fr >= 8) {	/* (past the start: its first frames) */
			char err[160];
			long at = play_ms();
			q_normal = 1;
			elog("the DSP behind at 49,170 Hz (%ld granules late by its frame %ld): Normal from %ld ms, "
			     "32,780 Hz, up to about 15 kHz", late, fr, at);
			if (!play_seek(at, err, sizeof(err)))
				elog("%s", err);
			play_note_new = 1;
			return 1;
		}
	}
	if (!src_done && play_ms() - base_ms > 1000) {
		long ahead = q_w - q_r, lead;
		lead = kind ? ahead / fmt.bytes / rate * 1000 + ahead / fmt.bytes % rate * 1000 / rate :
			      bitrate ? ahead / bitrate * 8 : 0;
		if (q_least < 0 || lead < q_least)
			q_least = lead;
	}
	if (dsp && t_end) {
		long fr, late, wmin, wavg;
		char l[60];
		dspmp3_stream_info(&fr, &late, &wmin, &wavg);
		if (xs)
			late += xs_dry;	/* (fed by the DMA: the queue's dry stretches too) */
		snprintf(l, sizeof(l), "EPLAY ended: %ld underruns\n", late);
		nf_debug(l);			/* (the rigs) */
		elog("played to the end: %ld ms, %ld frames to the DSP (%s)", play_ms(), t_frames, play_keep_up());
		elog("the stream: %s", play_stream_note());
		elog("the disk: %s", play_disk_note());
		if (xs && xs_dry)
			elog("the queue ran dry %ld times (the DSP waited for samples: silence)", xs_dry);
		play_stop();
		elog_save();
		return 1;
	}
	if (!dsp && !snd_playing() && (snd_written() >= out_rate / 4 || t_end))
		snd_play();
	if (t_end && t_ended_at >= 0 && snd_played() >= t_ended_at) {
		char l[60];
		snprintf(l, sizeof(l), "EPLAY ended: %ld underruns\n", snd_underruns);
		nf_debug(l);			/* (the rigs; nothing on the Atari) */
		elog("played to the end: %ld ms, %ld frames to the DSP, %ld underruns (%s)", play_ms(), t_frames,
		     snd_underruns, play_keep_up());
		elog("the disk: %s", play_disk_note());
		play_stop();
		elog_save();
		return 1;
	}
	sec = play_ms() / 1000;
	if (sec != last_sec) {
			last_sec = sec;
		return 1;
	}
	return 0;
}

/* how long the GEM loop may wait before the next step: the queue's
   reading needs no hurry once it holds a few seconds */
int play_wait_ms(void)
{
	long ahead;
	if (state != PS_PLAYING)
		return 500;
	if (src_done)
		return 100;
	ahead = q_w - q_r;		/* seconds of music, near enough */
	ahead = kind ? ahead / fmt.bytes / rate : bitrate ? ahead / (bitrate * 125) : 9;
	return ahead >= 4 || q_size - (q_w - q_r) < PIECE ? 100 : 0;
}
