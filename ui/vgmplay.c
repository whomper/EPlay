/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmplay.c - a VGM or VGZ file played on the YM2149 and the DSP's SN76489
 * (vgmplay.h).
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "vgm.h"
#include "vgmfile.h"
#include "vgmplay.h"
#include "ym.h"
#include "dspsn.h"
#include "feed.h"
#include "snd.h"
#include "elog.h"

/* the file's samples a tick, Q16: 44,100 / (2,457,600 / 16 / 154) */
#define STEP 2897647UL

static unsigned char *mem;
static long mem_n;
static VGMRUN run;
static unsigned long acc;
static volatile int ended;
static volatile int att_due = -1;	/* the volume's steps for the tick to take (GEM can't reach the chip) */
static int playing, open_, use_ay, use_sn;

static void *take(long n)
{
	void *p = (void *)Mxalloc(n, 3);	/* TT RAM where there is some */
	return (long)p > 0 ? p : 0;
}

static void give(void *p)
{
	if (p)
		Mfree(p);
}

typedef struct {
	int fh;
} FH;

static long rd(void *u, long at, unsigned char *buf, long n)
{
	FH *f = u;
	if (Fseek(at, (short)f->fh, 0) != at)
		return -1;
	return Fread((short)f->fh, n, buf);
}

static void ay(void *u, int reg, int val)
{
	(void)u;
	ym_write(reg, val);
}

static void sn(void *u, int val)
{
	(void)u;
	dspsn_write(val);
}

/* both chips silent (the tick's, at the end) */
static void hush(void)
{
	int r;
	if (use_ay)
		for (r = 8; r <= 10; r++)
			ym_write(r, 0);
	if (use_sn)
		for (r = 0; r < 4; r++)
			dspsn_write(0x9f | r << 5);
}

/* the tick (an interrupt): the commands of its 44.2 samples */
static void tick(void)
{
	long n;
	if (ended)
		return;
	if (att_due >= 0) {
		vgm_set_att(&run, att_due);
		att_due = -1;
	}
	if (use_sn)
		dspsn_poll();
	acc += STEP;
	n = (long)(acc >> 16);
	acc &= 0xffff;
	if (!vgm_run(&run, n)) {
		ended = 1;
		hush();
	}
}

int vp_on(void)
{
	return open_;
}

static void chip_name(char *s, int n, const char *what, unsigned long clock)
{
	snprintf(s, (size_t)n, "%s  %lu.%02lu MHz", what, clock / 1000000UL, clock % 1000000UL / 10000UL);
}

int vp_open(int fh, long size, long ms, TAGS *t, char *info, int ni, char *out, int no, long *len_ms, char *err,
	    int n, const char *name)
{
	FH f;
	const char *why;
	VGMHEAD h;
	long len;
	vp_stop();
	f.fh = fh;
	mem = vgm_load(rd, &f, size, &mem_n, take, give, &why);
	if (!mem) {
		snprintf(err, (size_t)n, "%s %s.", name, why);
		return 0;
	}
	if (!vgm_head(mem, mem_n, &h)) {
		snprintf(err, (size_t)n, "%s isn't a VGM file.", name);
		vp_stop();
		return 0;
	}
	elog("VGM %lx.%02lx: %ld bytes; AY %lu Hz (type $%02X), SN76489 %lu Hz, YM2612 %lu Hz%s; %ld samples, loop %ld "
	     "from %ld", h.version >> 8, h.version & 0xff, mem_n, h.ay_clock, h.ay_type, h.sn_clock, h.ym2612_clock,
	     h.other ? ", other chips" : "", h.total, h.loop_samples, h.loop_at);
	use_ay = h.ay_clock != 0;
	use_sn = 0;
	if (h.sn_clock) {
		/* the SN76489 on the DSP (locked while it plays: the DSP rule) */
		const char *why;
		if (dspsn_open(h.sn_clock, h.sn_fb, h.sn_w, &why))
			use_sn = 1;
		else if (!use_ay) {
			snprintf(err, (size_t)n, "%s is music for the SN76489:|EPlay %s.", name, why);
			vp_stop();
			return 0;
		} else
			elog("VGM: its SN76489 left out (%s)", why);
	}
	if (!use_ay && !use_sn) {
		const char *chip = h.ym2612_clock ? "the Mega Drive's YM2612" : "a sound chip";
		snprintf(err, (size_t)n, "%s is music for %s,|which EPlay doesn't play yet.", name, chip);
		vp_stop();
		return 0;
	}
	tags_clear(t);
	vgm_gd3(mem, mem_n, &h, t->title, t->album, t->artist, TAG_TEXT);
	len = vgm_length(&h);
	*len_ms = len / VGM_RATE * 1000 + len % VGM_RATE * 1000 / VGM_RATE;
	if (use_ay)
		chip_name(info, ni, h.ay_type >= 0x10 ? "VGM  YM2149" : "VGM  AY-3-8910", h.ay_clock);
	else
		chip_name(info, ni, "VGM  SN76489", h.sn_clock);
	{
		char dsp[24];
		long r = use_sn ? dspsn_rate() : 0;
		snprintf(dsp, sizeof(dsp), "DSP  %ld.%ld kHz", r / 1000, r % 1000 / 100);
		snprintf(out, (size_t)no, "%s%s%s%s", use_ay ? "YM2149" : "", use_ay && use_sn ? " + " : "",
			 use_sn ? dsp : "", h.other ? " (its part alone)" : "");
	}
	vgm_begin(&run, mem, mem_n, &h, use_ay ? ay : 0, use_sn ? sn : 0, 0);
	acc = 0;
	ended = 0;
	att_due = -1;
	if (ms > 0) {
		long to = ms / 1000 * VGM_RATE + ms % 1000 * VGM_RATE / 1000;
		VGM_SN s = run.sn_fn;
		run.ay_fn = 0;		/* (the chips written once it plays: vp_start) */
		run.sn_fn = 0;
		vgm_seek(&run, to);
		run.sn_fn = s;
	}
	open_ = 1;
	return 1;
}

static long write_all(void)
{
	vgm_resend(&run);
	return 0;
}

void vp_start(void)
{
	if (!open_ || playing)
		return;
	snd_take();			/* (the Falcon's codec: its volume) */
	if (use_ay)
		ym_begin();
	run.ay_fn = use_ay ? ay : 0;
	if (run.at > 0) {
		/* (from a seek: the registers as they are there, the volumes
		   then as the fade and the volume have them, by the tick) */
		Supexec(write_all);
		if (att_due < 0)
			att_due = run.att;
	}
	playing = 1;
	feed_on(tick, 1000);
}

void vp_stop(void)
{
	if (playing) {
		feed_off();
		if (use_ay)
			ym_end();
		playing = 0;
	}
	if (use_sn) {
		dspsn_close();
		use_sn = 0;
	}
	if (mem) {
		give(mem);
		mem = 0;
	}
	open_ = 0;
}

long vp_ms(void)
{
	long at = run.at;
	return at / VGM_RATE * 1000 + at % VGM_RATE * 1000 / VGM_RATE;
}

int vp_ended(void)
{
	return open_ && ended;
}

void vp_volume(int att, int mute)
{
	if (!open_)
		return;
	if (use_ay && snd_dma8 && snd_dma8_interp) {
		att_due = mute ? 15 : (att + 1) / 2;	/* (the TT: the chip's own volumes) */
		return;
	}
	if (use_ay) {
		/* the Falcon's codec, the STE's LMC1992: the DSP's sound
		   through the codec with it */
		snd_dac_volume(mute ? 15 : att, mute);
		if (use_sn)
			dspsn_volume(0, mute);
		return;
	}
	snd_dac_volume(0, 0);		/* (the SN alone: its amplitudes, the FDI's way too) */
	dspsn_volume(att, mute);
}
