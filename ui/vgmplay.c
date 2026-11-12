/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmplay.c - a VGM or VGZ file played on the YM2149, the DSP's SN76489,
 * the DSP's YM2612 or the DSP's YM3812 (vgmplay.h).
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
#include "dspopl.h"
#include "feed.h"
#include "snd.h"
#include "elog.h"

/* the file's samples a tick, Q16: 44,100 / (2,457,600 / 16 / 154); the
   YM2612's tick 500 times a second, 44,100 / (2,457,600 / 64 / 77): its
   fixed part halved on a stock Falcon's 68030, whose cache doesn't hold
   it (the DSP's ring holds 31 ms) */
#define STEP 2897647UL
#define STEP500 5795328UL
static unsigned long step;
static long period_us;		/* the tick's period */

static unsigned char *mem;
static long mem_n;
static VGMRUN run;
static unsigned long acc;
static volatile int ended;
static volatile int att_due = -1;	/* the volume's steps for the tick to take (GEM can't reach the chip) */
static int playing, open_, use_ay, use_sn, use_opn, use_opl;

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

/* the run as it was at places in the file, a copy each snap_every samples
   (the tick's), for a seek or Play after Pause to start from: running the
   file from its start to there took a stock Falcon seconds of GEM's time */
#define NSNAP 64
static VGMRUN *snap;
static unsigned char snap_ok[NSNAP];
static long snap_every;

/* the last file unpacked, kept after it stops (vp_open, vp_forget) */
static void *kept;
static long kept_n, kept_size = -1;
static char kept_name[64];
static unsigned char kept_head[64];

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

static void opn_sn(void *u, int val)
{
	(void)u;
	dspopn_sn(val);
}

static void opn(void *u, int port, int reg, int val)
{
	(void)u;
	dspopn_write(port, reg, val);
}

static void opl(void *u, int port, int reg, int val)
{
	(void)u;
	dspopl_write(port, reg, val);
}

/* (the YM2612's tick: the file run, its place for the DAC's samples) */
static long t_all, t_max, t_n, t_over;	/* the tick's times (us): the whole, its longest; past 1 ms */
static long t_lost;		/* ticks not made up (no time left) */

static int runn(void *v, long n)
{
	return vgm_run(v, n);
}

static long at(void *v)
{
	return ((VGMRUN *)v)->at;
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
	if (use_opl)
		dspopl_hush();
}

/* the tick (an interrupt): the commands of its 44.2 samples */
static long seen;		/* feed_ticks when the tick last ran */

static void tick(void)
{
	long n, due, k;
	if (ended)
		return;
	feed_t0();
	if (att_due >= 0) {
		vgm_set_att(&run, att_due);
		att_due = -1;
	}
	if (use_sn)
		dspsn_poll();
	/* the ticks since the last one ran, this one too: a tick held up (TOS's
	   own level-6 handlers break into it, up to 60 ms) left out the ones
	   that came meanwhile, and their samples are made up here, a tick's at
	   a time, eight at most, and only while this one has taken less than
	   60 % of its period: where the 68030 itself is short of time, the music slows
	   rather than GEM stopping (EPlay 1.18 lost them all: Mega Drive drums
	   at 40 % of their speed, Erez, 11 Nov 2026) */
	due = feed_ticks - seen;
	seen += due;
	if (due > 8) {
		t_lost += due - 8;
		due = 8;
	}
	for (k = 0; k < due; k++) {
		if (k && feed_us() >= period_us * 6 / 10) {
			t_lost += due - k;
			break;
		}
		acc += step;
		n = (long)(acc >> 16);
		acc &= 0xffff;
		if (!(use_opn ? dspopn_tick(runn, at, &run, n) : use_opl ? dspopl_tick(runn, &run, n) :
		      vgm_run(&run, n))) {
			ended = 1;
			hush();
			break;
		}
	}
	if (snap && !ended) {
		/* (a copy of the run, the first in each part of the file) */
		long s = run.at / snap_every;
		if (s < NSNAP && !snap_ok[s]) {
			snap[s] = run;
			snap_ok[s] = 1;
		}
	}
	{
		long t = feed_us();
		t_all += t;
		t_n++;
		if (t > t_max)
			t_max = t;
		if (t >= period_us)
			t_over++;
	}
}

/* where the music plays, for the bar's badge: the DSP's sound on the FDI's
   clock or the Falcon's ("" for the YM2149 alone: the chip itself) */
const char *vp_via(void)
{
	if (use_opn)
		return dspopn_ext() ? "FDI" : "DSP";
	if (use_opl)
		return dspopl_ext() ? "FDI" : "DSP";
	if (use_sn)
		return dspsn_ext() ? "FDI" : "DSP";
	return "";
}

int vp_on(void)
{
	return open_;
}

static void chip_name(char *s, int n, const char *what, unsigned long clock)
{
	snprintf(s, (size_t)n, "%s  %lu.%02lu MHz", what, clock / 1000000UL, clock % 1000000UL / 10000UL);
}

/* a big VGZ played as it unpacks: its unpacking (vgmfile.c's), on a handle
   of its own (play.c's closes at Pause); the bytes there so far */
static VGMLOAD *ld;
static FH ld_f = { -1 };
static long avail;
#define VGM_STREAM (256L * 1024)	/* a VGZ (packed) past this: played as it unpacks */
#define VGM_FIRST (256L * 1024)		/* unpacked before it plays */

static void ld_end(void)
{
	vgm_load_close(ld);
	ld = 0;
	if (ld_f.fh >= 0)
		Fclose((short)ld_f.fh);
	ld_f.fh = -1;
}

/* (a seek past what is there: further now, 256 KB at a time) */
static long ld_more(void *mu)
{
	(void)mu;
	if (!ld)
		return 0;
	avail = vgm_load_more(ld, 256L * 1024);
	if (vgm_load_state(ld)) {
		int st = vgm_load_state(ld);
		ld_end();
		return st > 0 ? 0 : -1;
	}
	return avail;
}

int vp_open(int fh, long size, long ms, TAGS *t, char *info, int ni, char *out, int no, long *len_ms, char *err,
	    int n, const char *name, const char *path)
{
	FH f;
	const char *why;
	VGMHEAD h;
	long len;
	vp_stop();
	f.fh = fh;
	/* the file unpacked before, kept, when it is the same (Play after
	   Pause, a seek: a stock Falcon's 68030 takes seconds to unpack a VGZ
	   again) */
	{
		unsigned char head[sizeof(kept_head)];
		long got = rd(&f, 0, head, (long)sizeof(head));
		if (kept && size == kept_size && got == (long)sizeof(head) && !strcmp(name, kept_name) &&
		    !memcmp(head, kept_head, sizeof(head))) {
			mem = kept;
			mem_n = kept_n;
			kept = 0;
			elog("VGM: unpacked as before (kept)");
		} else {
			vp_forget();
			memset(snap_ok, 0, sizeof(snap_ok));
			if (size > VGM_STREAM && vgm_magic(head, got) == 2 && path) {
				/* a big VGZ: its first part unpacked, it plays, the
				   rest as it plays (vp_step); a 33 MB one took two
				   minutes before it played (Erez, 11 Nov 2026) */
				why = "can't be read";
				ld_f.fh = (int)Fopen(path, 0);
				ld = ld_f.fh >= 0 ? vgm_load_open(rd, &ld_f, size, &mem, &mem_n, take, give, &why) : 0;
				if (!ld) {
					if (ld_f.fh >= 0)
						Fclose((short)ld_f.fh);
					ld_f.fh = -1;
					mem = 0;
				} else {
					avail = vgm_load_more(ld, VGM_FIRST);
					if (vgm_load_state(ld) < 0) {
						give(mem);
						mem = 0;
						why = "is damaged";
						ld_end();
					} else if (vgm_load_state(ld) > 0)
						ld_end();
					else
						elog("VGM: %ld KB unpacked of %ld; it plays, the rest as it plays", avail / 1024,
						     mem_n / 1024);
				}
			} else
				mem = vgm_load(rd, &f, size, &mem_n, take, give, &why);
			if (mem) {
				kept_size = size;
				snprintf(kept_name, sizeof(kept_name), "%s", name);
				if (got == (long)sizeof(head))
					memcpy(kept_head, head, sizeof(head));
				else
					kept_size = -1;
			}
		}
	}
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
	elog("VGM %lx.%02lx: %ld bytes; AY %lu Hz (type $%02X), SN76489 %lu Hz, YM2612 %lu Hz, YM3812 %lu Hz%s; %ld "
	     "samples, loop %ld from %ld", h.version >> 8, h.version & 0xff, mem_n, h.ay_clock, h.ay_type, h.sn_clock,
	     h.ym2612_clock, h.ym3812_clock, h.other ? ", other chips" : "", h.total, h.loop_samples, h.loop_at);
	use_ay = h.ay_clock != 0;
	use_sn = use_opn = use_opl = 0;
	if (h.ym2612_clock) {
		/* the YM2612 on the DSP (the DSP rule: locked while it plays) */
		const char *why;
		if (dspopn_open(h.ym2612_clock, h.sn_clock, h.sn_fb, h.sn_w, &why))
			use_opn = 1;
		else if (!use_ay) {
			snprintf(err, (size_t)n, "%s is music for the Mega Drive's YM2612:|EPlay %s.", name, why);
			vp_stop();
			return 0;
		} else
			elog("VGM: its YM2612 left out (%s)", why);
	}
	if (h.ym3812_clock && !use_opn) {
		/* the YM3812 on the DSP (one DSP program a file: with a YM2612
		   too, the YM2612's) */
		const char *why;
		if (dspopl_open(h.ym3812_clock, &why))
			use_opl = 1;
		else if (!use_ay && !h.sn_clock) {
			snprintf(err, (size_t)n, "%s is music for the YM3812 (OPL2):|EPlay %s.", name, why);
			vp_stop();
			return 0;
		} else
			elog("VGM: its YM3812 left out (%s)", why);
	}
	if (h.sn_clock && use_opn)
		;			/* (the Mega Drive's SN76489: in opn.a56 with it) */
	else if (h.sn_clock && use_opl)
		elog("VGM: its SN76489 left out (the DSP plays the YM3812)");
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
	if (!use_ay && !use_sn && !use_opn && !use_opl) {
		snprintf(err, (size_t)n, "%s is music for a sound chip|EPlay doesn't play yet.", name);
		vp_stop();
		return 0;
	}
	tags_clear(t);
	if (!ld)		/* (at its end: once it has unpacked, vp_tags) */
		vgm_gd3(mem, mem_n, &h, t->title, t->album, t->artist, TAG_TEXT);
	len = vgm_length(&h);
	if (!snap)
		snap = take((long)NSNAP * (long)sizeof(VGMRUN));
	snap_every = len / NSNAP + 1;
	if (snap_every < 2 * VGM_RATE)
		snap_every = 2 * VGM_RATE;
	*len_ms = len / VGM_RATE * 1000 + len % VGM_RATE * 1000 / VGM_RATE;
	if (use_opn)
		chip_name(info, ni, "VGM  YM2612", h.ym2612_clock);
	else if (use_opl)
		chip_name(info, ni, "VGM  YM3812", h.ym3812_clock);
	else if (use_ay)
		chip_name(info, ni, h.ay_type >= 0x10 ? "VGM  YM2149" : "VGM  AY-3-8910", h.ay_clock);
	else
		chip_name(info, ni, "VGM  SN76489", h.sn_clock);
	{
		char dsp[24];
		long r = use_sn ? dspsn_rate() : use_opn ? dspopn_synth() : use_opl ? dspopl_synth() : 0;
		int dsp_on = use_sn || use_opn || use_opl;
		snprintf(dsp, sizeof(dsp), "DSP  %ld.%ld kHz", r / 1000, r % 1000 / 100);
		snprintf(out, (size_t)no, "%s%s%s%s", use_ay ? "YM2149" : "", use_ay && dsp_on ? " + " : "",
			 dsp_on ? dsp : "",
			 h.other || (h.ym2612_clock && !use_opn) || (h.ym3812_clock && !use_opl) ||
			 (h.sn_clock && use_opl) ? " (its part alone)" : "");
		if (use_opn)
			elog("VGM: the YM2612%s on the DSP at %ld Hz (the SSI's %ld)",
			     dspopn_psg() ? " and the SN76489" : "", dspopn_synth(), dspopn_rate());
		if (use_opl)
			elog("VGM: the YM3812 on the DSP at %ld Hz (the SSI's %ld)", dspopl_synth(), dspopl_rate());
	}
	vgm_begin(&run, mem, mem_n, &h, use_ay ? ay : 0, use_sn ? sn : use_opn && dspopn_psg() ? opn_sn : 0, 0);
	run.opn_fn = use_opn ? opn : 0;
	run.opl_fn = use_opl ? opl : 0;
	run.avail = ld ? avail : 0;	/* (still unpacking: so far; a seek unpacks further) */
	run.more = ld ? ld_more : 0;
	acc = 0;
	ended = 0;
	att_due = -1;
	if (ms > 0) {
		long to = ms / 1000 * VGM_RATE + ms % 1000 * VGM_RATE / 1000;
		VGM_SN s = run.sn_fn;
		VGM_OPN o = run.opn_fn;
		VGM_OPL l = run.opl_fn;
		run.ay_fn = 0;		/* (the chips written once it plays: vp_start) */
		run.sn_fn = 0;
		run.opn_fn = 0;
		run.opl_fn = 0;
		{
			/* (from the last copy at or before the place) */
			long k = snap && snap_every ? to / snap_every : -1;
			if (k >= NSNAP)
				k = NSNAP - 1;
			while (k >= 0 && (!snap_ok[k] || snap[k].at > to))
				k--;
			vgm_seek_from(&run, k >= 0 ? &snap[k] : 0, to);
		}
		run.sn_fn = s;
		run.opn_fn = o;
		run.opl_fn = l;
	}
	run.dac = use_opn ? dspopn_dac() : 0;	/* (the drums' samples straight to the tick's DAC) */
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
	t_all = t_max = t_n = t_over = t_lost = 0;
	seen = 0;		/* (feed_on counts from 0) */
	step = use_opn || use_opl ? STEP500 : STEP;
	period_us = use_opn || use_opl ? 2005 : 1003;
	feed_on(tick, use_opn || use_opl ? 500 : 1000);
}

void vp_stop(void)
{
	if (playing) {
		feed_off();
		if (ld || run.waits)
			elog("VGM: %ld KB unpacked of %ld so far; the music waited for it %ld times", ld ? avail / 1024 :
			     mem_n / 1024, mem_n / 1024, run.waits);
		elog("VGM: the tick %ld times, %ld of them left out (the one before still ran), %ld of those not made up "
		     "(the music slowed); %ld past its period (%ld us); %ld us on average, the longest %ld us",
		     feed_ticks, feed_missed, t_lost, t_over, period_us, t_n ? t_all / t_n : 0, t_max);
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
	if (use_opl) {
		dspopl_close();
		use_opl = 0;
	}
	if (mem) {
		/* (kept for the same file's next open: vp_forget gives it back) */
		give(kept);
		kept = mem;
		kept_n = mem_n;
		mem = 0;
	}
	open_ = 0;
}

void vp_forget(void)
{
	ld_end();
	give(kept);
	kept = 0;
	memset(snap_ok, 0, sizeof(snap_ok));
}

int vp_step(int ms)
{
	unsigned long t0;
	if (!ld)
		return 0;
	/* (close to where it plays: a turn's longest a job's, 45 ms) */
	if (avail - run.pos < 256L * 1024 && ms < 45)
		ms = 45;
	t0 = tos_ms();
	do
		avail = vgm_load_more(ld, 8192);
	while (!vgm_load_state(ld) && (long)(tos_ms() - t0) < ms);
	if (vgm_load_state(ld)) {
		int st = vgm_load_state(ld);
		ld_end();
		run.avail = 0;
		run.more = 0;
		elog("VGM: unpacked%s (%ld KB)", st > 0 ? "" : ", damaged at its end", avail / 1024);
		return st > 0 ? 2 : 3;
	}
	run.avail = avail;
	return 1;
}

int vp_tags(TAGS *t)
{
	const unsigned char *b = mem ? mem : kept;
	long n = mem ? mem_n : kept_n;
	VGMHEAD h;
	if (!b || ld || !vgm_head(b, n, &h))
		return 0;
	tags_clear(t);
	vgm_gd3(b, n, &h, t->title, t->album, t->artist, TAG_TEXT);
	return 1;
}

int vp_probe(const char *name, long size, const unsigned char *head, TAGS *t, long *ms)
{
	const unsigned char *b = mem ? mem : kept;
	long n = mem ? mem_n : kept_n, len;
	VGMHEAD h;
	if (!b || ld || size != kept_size || strcmp(name, kept_name) || memcmp(head, kept_head, sizeof(kept_head)) ||
	    !vgm_head(b, n, &h))
		return 0;
	tags_clear(t);
	vgm_gd3(b, n, &h, t->title, t->album, t->artist, TAG_TEXT);
	len = vgm_length(&h);
	*ms = len / VGM_RATE * 1000 + len % VGM_RATE * 1000 / VGM_RATE;
	return 1;
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
	else if (use_opl)
		dspopl_volume(att, mute);
	else
		dspsn_volume(att, mute);
}
