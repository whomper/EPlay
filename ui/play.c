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
#include "../src/pcm.h"
#include "mp3frame.h"
#include "elog.h"

#define FBUF (64L * 1024)	/* the file's start: what it is, where it starts */
#define PIECE 32768L		/* the most read at once (guidelines 6.5) */
#define QMIN (128L * 1024)
#define QMAX (8L * 1024 * 1024)

char play_name[64], play_info[80], play_out[80];

static int state = PS_IDLE, kind, fh = -1, eof;
static volatile int loading;	/* MP3: the DSP's program still going in (the tick's);
				   2 while GEM's steps reset the DSP, after Play's draw */
static PCMFMT fmt;
static long rate, out_rate, len_ms, file_size;
static unsigned char *fbuf;	/* the file's first bytes, fpos..fend */
static long fpos, fend;
static MP3PARSE parse;		/* the tick's */
static unsigned long pk[PK_MAX];	/* the tick's: a packet */
static unsigned char fr[2048];	/* the tick's: a frame, whole */
static long last_sec = -1;
static long data_left;		/* bytes still to read into the queue */
static long bitrate;		/* MP3: the first frame's, kbit/s */

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

/* the tick's own */
static volatile int t_end, t_dsp, t_failed;
static volatile long t_ended_at;	/* the frames written when the file's end came; -1 before */
static long t_samples;		/* the frame the DSP decodes: its samples */
static volatile long t_frames;	/* MP3 frames sent to the DSP */

static unsigned long ms_now(void)
{
	return tos_ms();
}

/* the buffer topped up from the file: what is left moved to its start */
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
	n = Fread(fh, (FBUF - fend) > 32768 ? 32768 : FBUF - fend, fbuf + fend);
	if (n <= 0)
		eof = 1;
	else
		fend += n;
}

static void close_file(void)
{
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
	if (at >= fend) {		/* a tag longer than what was read: past it */
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
		pcm_convert(&fmt, q + q_roff, p, k);
		adv(kb);
		snd_wrote(k);
		room -= k;
		cap -= k;
	}
}

/* n bytes of the queue from q_r on, into fr (the queue's end crossed) */
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
		if (have < 4) {
			if (src_done)
				src_out = 1;
			return;
		}
		peek(4);
		if (!mp3_header(fr, &h)) {
			adv(1);
			continue;
		}
		if (have < h.length) {
			if (src_done)
				src_out = 1;	/* (a frame cut short at the file's end) */
			return;
		}
		peek(h.length);
		np = mp3_packet(&parse, &h, fr, pk);
		adv(h.length);
		if (np > 0) {
			pk_n = np;
			pk_samples = h.samples;
		}
	}
}

/* the DSP's frame read into the ring when decoded, and the next packet
   sent at once; the one after it is made while the DSP decodes */
static void tick_mp3(void)
{
	if (t_dsp) {
		long k, n = t_samples;
		short *p;
		if (!dspmp3_tick_ready()) {
			if (!pk_n)
				next_packet();
			return;			/* (decoding) */
		}
		p = snd_ptr(&k);
		if (k > n)
			k = n;
		dspmp3_tick_read(p, k);
		snd_wrote(k);
		if (n > k) {		/* the rest at the ring's start */
			long k2;
			p = snd_ptr(&k2);
			dspmp3_tick_read(p, n - k);
			snd_wrote(n - k);
		}
		t_dsp = 0;
	}
	if (!pk_n)
		next_packet();
	if (!pk_n) {
		if (src_out)
			t_end = 1;	/* (the last frame read, none to come) */
		return;
	}
	if (snd_room() < pk_samples + 64)
		return;
	t_samples = pk_samples;
	dspmp3_tick_send(pk, pk_n);
	t_frames++;
	pk_n = 0;
	t_dsp = 1;
	next_packet();
}

static void tick(void)
{
	snd_update();
	if (loading) {			/* the DSP's program first, a piece a tick */
		int r;
		if (loading == 2)
			return;		/* (Play's draw first: its stretch, 75 ms to 50) */
		r = dspmp3_tick_load();
		if (r < 0)
			t_failed = 1;
		else if (r)
			loading = 0;
		return;
	}
	if (!t_end && !t_failed) {
		if (kind)
			tick_pcm();
		else
			tick_mp3();
		if (!kind && dspmp3_failed())
			t_failed = 1;
	}
	if (t_end) {
		/* silence after the end, so that the ring isn't played again */
		long room = snd_room(), k;
		if (t_ended_at < 0)
			t_ended_at = snd_written();
		while (room > 0) {
			short *p = snd_ptr(&k);
			if (k > room)
				k = room;
			memset(p, 0, (size_t)k * 4);
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

/* the file read into the queue, a piece at a time, for about ms */
static void fill(unsigned long t0, int ms)
{
	while (!src_done && ms_now() - t0 < (unsigned long)ms) {
		long free = q_size - (q_w - q_r), at = q_woff, n = q_size - at, r;
		if (n > free)
			n = free;
		if (n > PIECE)
			n = PIECE;
		if (n > data_left)
			n = data_left;
		/* a whole piece, else wait for the room (but the file's end and
		   the queue's end are read as they come) */
		if (n <= 0 || (n < PIECE && n < data_left && at + n < q_size))
			break;
		r = Fread(fh, n, q + at);
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

int play_open(const char *path, char *err, int n)
{
	const char *why;
	char kr[20];
	int ext;

	play_stop();
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
	fh = (int)Fopen(path, 0);
	if (fh < 0) {
		snprintf(err, n, "%s can't be opened.", base_name(path));
		fh = -1;
		return 0;
	}
	file_size = Fseek(0, fh, 2);
	Fseek(0, fh, 0);
	fpos = fend = 0;
	eof = 0;
	refill();
	snprintf(play_name, sizeof(play_name), "%s", base_name(path));
	elog("open %s (%ld bytes)", path, file_size);
	if (pcm_probe(fbuf, fend, &fmt, &why)) {
		kind = fmt.kind;
		rate = fmt.rate;
		len_ms = fmt.frames / rate * 1000 + fmt.frames % rate * 1000 / rate;	/* (no overflow past 13 minutes) */
		data_left = fmt.frames > 0 ? fmt.frames * fmt.bytes : file_size - fmt.data;
		Fseek(fmt.data, fh, 0);		/* the samples read from here into the queue */
		if (!take_queue(fmt.bytes)) {
			snprintf(err, n, "There isn't the memory to play.");
			close_file();
			return 0;
		}
		khz(kr, sizeof(kr), rate);
		snprintf(play_info, sizeof(play_info), "%s  %d bits  %s  %s", kind == PCM_WAV ? "WAV" : "AIFF",
			 fmt.bits, kr, fmt.channels == 2 ? "stereo" : "mono");
		elog("%s: %ld Hz, %d bits, %d ch, %ld frames from %ld; %ld ms", kind == PCM_WAV ? "WAV" : "AIFF",
		     fmt.rate, fmt.bits, fmt.channels, fmt.frames, fmt.data, len_ms);
	} else {
		MP3HDR h;
		long at = find_mp3(&h);
		if (at < 0) {
			snprintf(err, n, "%s isn't a file EPlay plays: %s, nor an MP3.", play_name, why);
			close_file();
			return 0;
		}
		kind = 0;
		rate = h.rate;
		bitrate = h.bitrate;
		{
			/* a Xing, Info or VBRI frame: the file's frames, so its
			   length (a VBR file's first frame's bit rate is no
			   guide); the frame holds no sound and isn't played */
			int vbr;
			long nf = first_at + h.length <= fend ? mp3_info_frames(fbuf + first_at, &h, &vbr) : 0;
			long bytes = file_size - at;
			if (nf > 0) {
				long fs = nf * h.samples;
				at += h.length;
				bytes -= h.length;
				len_ms = fs / rate * 1000 + fs % rate * 1000 / rate;
				if (vbr && len_ms > 0)
					bitrate = bytes / len_ms * 8 + (bytes % len_ms) * 8 / len_ms;
			} else {
				vbr = 0;
				len_ms = h.bitrate ? bytes * 8 / h.bitrate : 0;
			}
			khz(kr, sizeof(kr), rate);
			snprintf(play_info, sizeof(play_info), "MP3  %s%ld kbit/s  %s  %s", vbr ? "VBR " : "",
				 bitrate, kr, h.channels == 2 ? "stereo" : "mono");
			elog("MP3 at %ld: MPEG-%s, %d kbit/s first frame, %d Hz, %d ch, CRC %d; header frames %ld%s; %ld ms",
			     at, h.version == 1 ? "1" : h.version == 2 ? "2" : "2.5", h.bitrate, h.rate, h.channels,
			     h.crc, nf, vbr ? " (VBR)" : "", len_ms);
		}
		data_left = file_size - at;
		Fseek(at, fh, 0);		/* the frames read from here into the queue */
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
	}
	out_rate = snd_rate_for(rate, &ext);
	if (!snd_open(out_rate, ext)) {
		snprintf(err, n, "There isn't the ST RAM to play.");
		play_stop();
		return 0;
	}
	khz(kr, sizeof(kr), out_rate);
	if (ext)
		snprintf(play_out, sizeof(play_out), "External clock  %s", kr);
	else if (out_rate == rate)
		snprintf(play_out, sizeof(play_out), "Falcon  %s", kr);
	else
		snprintf(play_out, sizeof(play_out), "Falcon  %s (not the file's rate)", kr);
	elog("output: %ld Hz, %s; queue %ld bytes", out_rate, ext ? "the external clock" : "the Falcon's clock", q_size);
	t_end = t_dsp = t_failed = 0;
	t_frames = 0;
	pk_n = src_out = 0;
	t_ended_at = -1;
	last_sec = -1;
	return 1;
}


void play_start(void)
{
	if (fh >= 0 && state != PS_PLAYING) {
		state = PS_PLAYING;
		loading = kind ? 0 : 2;	/* (the tick loads the DSP first: not waiting for GEM) */
		feed_on(tick, kind ? 250 : 1000);	/* (WAV: 1024 frames a tick, 4 times what
						   plays; MP3: the DSP's next packet at once) */
	}
}

void play_stop(void)
{
	feed_off();			/* first: the tick no longer runs */
	loading = 0;
	snd_close();
	t_dsp = 0;			/* (a frame it was decoding is left: the DSP
					   is reset by whoever takes it next) */
	if (!kind)
		dspmp3_close();
	close_file();
	free_queue();
	state = PS_IDLE;
}

int play_state(void)
{
	return state;
}

long play_ms(void)
{
	long f = snd_played();
	return rate ? (f / rate) * 1000 + (f % rate) * 1000 / rate : 0;
}

/* frames at this rate as ms (-1 stays -1) */
static long f_ms(long f)
{
	return f < 0 || !rate ? -1 : (f / rate) * 1000 + (f % rate) * 1000 / rate;
}

/* how the sound kept up, for the log: the first gap and the least lead */
const char *play_keep_up(void)
{
	static char b[96];
	if (snd_underruns)
		snprintf(b, sizeof(b), "the first at %ld ms; the least lead %ld ms", f_ms(snd_first_under),
			 f_ms(snd_least_lead));
	else
		snprintf(b, sizeof(b), "the least lead %ld ms", f_ms(snd_least_lead));
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
	if (loading == 2 && dspmp3_reset_step())
		loading = 1;		/* (drawn, the DSP reset: the tick may load it) */
	if (t_failed) {
		int what, hisr;
		long at, of;
		dspmp3_fail_info(&what, &at, &of, &hisr);
		elog("the DSP stopped answering %s word %ld of %ld (host status $%02X), frame %ld; %ld ms played, "
		     "%ld underruns", what == 1 ? "sending" : what == 2 ? "reading" : "loading", at, of, hisr,
		     t_frames, play_ms(), snd_underruns);
		elog("the sound: %s", play_keep_up());
		play_stop();
		elog_save();
		snprintf(play_out, sizeof(play_out), "The DSP stopped answering.");
		return 1;
	}
	fill(t0, ms);
	if (!snd_playing() && (snd_written() >= out_rate / 4 || t_end))
		snd_play();
	if (t_end && t_ended_at >= 0 && snd_played() >= t_ended_at) {
		char l[60];
		snprintf(l, sizeof(l), "EPLAY ended: %ld underruns\n", snd_underruns);
		nf_debug(l);			/* (the rigs; nothing on the Atari) */
		elog("played to the end: %ld ms, %ld frames to the DSP, %ld underruns (%s)", play_ms(), t_frames,
		     snd_underruns, play_keep_up());
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
