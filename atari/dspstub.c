/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspstub.c - an edition for a machine with no DSP56001 (the STE's
 * EPLAYST.PRG, the FireBee's EPLAYFB.PRG): the DSP's programs (the MP3
 * decoder, the SN76489, the YM2612, the YM3812 and YMF262) answered as
 * never there, as on a Falcon whose DSP is missing, so that the programs
 * and their tables aren't carried (the STE's start: 14 Nov 2026).
 */
#include <stddef.h>
#include "dspmp3.h"
#include "dspsn.h"
#include "dspopn.h"
#include "dspopl.h"

static const char no_dsp[] = "needs the Falcon's DSP";

/* ---- the MP3 decoder on the DSP: not there */
int dspmp3_open(void) { return DSPMP3_NONE; }
int dspmp3_reset_step(void) { return 1; }
int dspmp3_tick_reset(int after, int wait) { (void)after; (void)wait; return 1; }
int dspmp3_tick_load(void) { return 1; }
void dspmp3_close(void) { }
void dspmp3_reset(void) { }
void dspmp3_tick_send(const unsigned long *pk, int n) { (void)pk; (void)n; }
int dspmp3_tick_ready(void) { return 0; }
void dspmp3_tick_read(short *to, long frames) { (void)to; (void)frames; }
int dspmp3_failed(void) { return 0; }
void dspmp3_tick_stream(long rin, long rout, int pcm, int sblim) { (void)rin; (void)rout; (void)pcm; (void)sblim; }
void dspmp3_tick_status(void) { }
void dspmp3_tick_push(void) { }
int dspmp3_tick_asks(void) { return 0; }
int dspmp3_tick_pushing(void) { return 0; }
void dspmp3_tick_offer(const unsigned long *pk, int n) { (void)pk; (void)n; }
void dspmp3_stream_firsts(long *late_at, long *rsy_at) { *late_at = *rsy_at = -1; }
int dspmp3_stream_slots(void) { return -1; }
long dspmp3_stream_polls(void) { return -1; }
long dspmp3_stream_fps1000(void) { return 0; }
void dspmp3_stream_info(long *frames, long *late, long *wmin, long *wavg) { *frames = *late = *wmin = *wavg = 0; }
long dspmp3_stream_resyncs(void) { return 0; }
int dspmp3_stream_stalled(unsigned long hz) { (void)hz; return 0; }
void dspmp3_stall_test(void) { }
void dspmp3_stall_note(char *b, int n) { if (n > 0) b[0] = 0; }
void dspmp3_fail_info(int *what, long *at, long *of, int *hisr) { *what = 0; *at = *of = 0; *hisr = 0; }

/* ---- the SN76489, the YM2612, the YM3812 on the DSP: not there */
int dspsn_open(unsigned long clock, int fb, int width, const char **why)
{
	(void)clock; (void)fb; (void)width;
	*why = no_dsp;
	return 0;
}
void dspsn_write(int byte) { (void)byte; }
void dspsn_volume(int att, int mute) { (void)att; (void)mute; }
void dspsn_poll(void) { }
void dspsn_close(void) { }
int dspsn_ext(void) { return 0; }
long dspsn_rate(void) { return 0; }
void dspsn_term(void) { }

int dspopn_open(unsigned long clock, unsigned long sn_clock, int sn_fb, int sn_w, long want, const char **why)
{
	(void)clock; (void)sn_clock; (void)sn_fb; (void)sn_w; (void)want;
	*why = no_dsp;
	return 0;
}
void dspopn_sn(int byte) { (void)byte; }
int dspopn_psg(void) { return 0; }
const unsigned long *dspopn_tab(int which) { (void)which; return NULL; }
VGMDAC *dspopn_dac(void) { return NULL; }
void dspopn_write(int port, int reg, int val) { (void)port; (void)reg; (void)val; }
int dspopn_tick(int (*run)(void *v, long n), long (*at)(void *v), void *v, long n) { (void)at; return run(v, n); }
void dspopn_volume(int att, int mute) { (void)att; (void)mute; }
void dspopn_hush(void) { }
void dspopn_close(void) { }
int dspopn_ext(void) { return 0; }
long dspopn_least(void) { return -1; }
long dspopn_rate(void) { return 0; }
long dspopn_synth(void) { return 0; }
void dspopn_term(void) { }

int dspopl_open(unsigned long clock, int opl3, int nch, long want, const char **why)
{
	(void)clock;
	(void)opl3;
	(void)nch;
	(void)want;
	*why = no_dsp;
	return 0;
}
void dspopl_write(int port, int reg, int val) { (void)port; (void)reg; (void)val; }
int dspopl_tick(int (*run)(void *v, long n), void *v, long n) { return run(v, n); }
void dspopl_volume(int att, int mute) { (void)att; (void)mute; }
void dspopl_hush(void) { }
void dspopl_close(void) { }
int dspopl_ext(void) { return 0; }
long dspopl_least(void) { return -1; }
int dspopl_late2(void) { return 0; }
long dspopl_rate(void) { return 0; }
long dspopl_synth(void) { return 0; }
void dspopl_term(void) { }
