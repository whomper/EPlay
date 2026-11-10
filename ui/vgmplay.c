/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmplay.c - a VGM or VGZ file played on the YM2149, the DSP's SN76489 or
 * the DSP's YM2612 (vgmplay.h).
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "vgm.h"
#include "vgmfile.h"
#include "vgmplay.h"
#include "ym.h"
#include "dspsn.h"
#include "dspopn.h"
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
static int playing, open_, use_ay, use_sn, use_opn;

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

static void opn(void *u, int port, int reg, int val)
{
	(void)u;
	dspopn_write(port, reg, val);
}

/* (the YM2612's tick: the file a sample at a time, for the DAC's) */
static int one(void *v)
{
	return vgm_run(v, 1);
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
	if (use_opn)
		dspopn_hush();
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
	if (!(use_opn ? dspopn_tick(one, &run, n) : vgm_run(&run, n))) {
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
		elog("VGM: %s %s (%ld bytes, %ld unpacked; the largest free block %ld)", name, why, size, vgm_need,
		     (long)Mxalloc(-1L, 3));
		if (vgm_need > 0 && strstr(why, "memory"))
			snprintf(err, (size_t)n, "%s needs %ld KB of memory|to play, which isn't free.", name,
				 (vgm_need + 1023) / 1024);
		else
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
	use_sn = use_opn = 0;
	if (h.ym2612_clock) {
		/* the YM2612 on the DSP (the DSP rule: locked while it plays) */
		const char *why;
		if (dspopn_open(h.ym2612_clock, &why))
			use_opn = 1;
		else if (!use_ay) {
			snprintf(err, (size_t)n, "%s is music for the Mega Drive's YM2612:|EPlay %s.", name, why);
			vp_stop();
			return 0;
		} else
			elog("VGM: its YM2612 left out (%s)", why);
	}
	if (h.sn_clock && use_opn)
		/* (one DSP program at a time: the Mega Drive's PSG not yet) */
		elog("VGM: its SN76489 left out (the DSP plays the YM2612)");
	else if (h.sn_clock) {
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
	if (!use_ay && !use_sn && !use_opn) {
		snprintf(err, (size_t)n, "%s is music for a sound chip|EPlay doesn't play yet.", name);
		vp_stop();
		return 0;
	}
	tags_clear(t);
	vgm_gd3(mem, mem_n, &h, t->title, t->album, t->artist, TAG_TEXT);
	len = vgm_length(&h);
	*len_ms = len / VGM_RATE * 1000 + len % VGM_RATE * 1000 / VGM_RATE;
	if (use_opn)
		chip_name(info, ni, "VGM  YM2612", h.ym2612_clock);
	else if (use_ay)
		chip_name(info, ni, h.ay_type >= 0x10 ? "VGM  YM2149" : "VGM  AY-3-8910", h.ay_clock);
	else
		chip_name(info, ni, "VGM  SN76489", h.sn_clock);
	{
		char dsp[24];
		long r = use_sn ? dspsn_rate() : use_opn ? dspopn_synth() : 0;
		int dsp_on = use_sn || use_opn;
		snprintf(dsp, sizeof(dsp), "DSP  %ld.%ld kHz", r / 1000, r % 1000 / 100);
		snprintf(out, (size_t)no, "%s%s%s%s", use_ay ? "YM2149" : "", use_ay && dsp_on ? " + " : "",
			 dsp_on ? dsp : "",
			 h.other || (use_opn && h.sn_clock) || (h.ym2612_clock && !use_opn) ? " (its part alone)" : "");
		if (use_opn)
			elog("VGM: the YM2612 on the DSP at %ld Hz (the SSI's %ld)", dspopn_synth(), dspopn_rate());
	}
	vgm_begin(&run, mem, mem_n, &h, use_ay ? ay : 0, use_sn ? sn : 0, 0);
	run.opn_fn = use_opn ? opn : 0;
	acc = 0;
	ended = 0;
	att_due = -1;
	if (ms > 0) {
		long to = ms / 1000 * VGM_RATE + ms % 1000 * VGM_RATE / 1000;
		VGM_SN s = run.sn_fn;
		VGM_OPN o = run.opn_fn;
		run.ay_fn = 0;		/* (the chips written once it plays: vp_start) */
		run.sn_fn = 0;
		run.opn_fn = 0;
		vgm_seek(&run, to);
		run.sn_fn = s;
		run.opn_fn = o;
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
	if (use_opn) {
		dspopn_close();
		use_opn = 0;
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
	snd_dac_volume(0, 0);		/* (the DSP's chip alone: its own level, the FDI's way too) */
	if (use_opn)
		dspopn_volume(att, mute);
	else
		dspsn_volume(att, mute);
}
