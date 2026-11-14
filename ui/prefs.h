/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * prefs.h - (once settings.h: common has its settings.c, the Settings engine, which builds the dialog) EPlay's Settings (Options > Settings..., ^,) and EPLAY.INF,
 * next to the program (a PRG-only app: the guidelines' 6.1). Output: how
 * EPlay plays (Erez, 2 Nov 2026): Auto, FDI or DSP (the 68030 dropped: Erez,
 * 3 Nov 2026). A change takes
 * effect at the next Play; what plays goes on as it is.
 */
#ifndef EPLAY_PREFS_H
#define EPLAY_PREFS_H

enum { OUT_AUTO, OUT_FDI, OUT_DSP };
/* (Quality is no longer a setting: Erez, 4 Nov 2026. An MP3 plays at High
   and goes to Normal by itself where the DSP falls behind: play.c. An
   INF's QUALITY= is read no more). The granules late at High that make
   it Normal: 3, or QLATE= (the play rig's, 0: at once; never written) */
int settings_mp3cpu(void);	/* MP3CPU=1: the rigs' */
int settings_vgmcpu(void);
long settings_stallat(void);
/* Options > Write a log (LOG= in EPLAY.INF, off until ticked: the
   guidelines' 6.5); its setting at once */
int settings_log(void);
void settings_set_log(int on);	/* STALLAT=ms: the rigs' (an MP3 on the DSP taken as stalled there, once) */	/* VGMCPU=1: the rigs' (a VGM's chips on a 68060's CPU, the DSP there or not) */
int settings_qlate(void);
/* a VGM on the DSP goes on at a lower rate when its ring's least lead is
   under 256 words, or VGMLOW= (the play rig's; never written) */
int settings_vgmlow(void);
/* a WAV or AIFF on the TT's or STE's DMA goes on at a lower rate at its
   first underrun past its first second, or DMA8LOW='s (the play rig's,
   0: at once; never written) */
int settings_dma8low(void);

void settings_load(void);	/* EPLAY.INF, at the start */
void prefs_dialog(void);	/* the dialog (Settings); saved on OK when it changed */
int settings_output(void);	/* OUT_...: as set, or Auto where the machine hasn't it */
const char *settings_output_name(int out);	/* "Auto", "FDI", ... (the log) */

/* ReplayGain (phase 8b, Erez, 9 Nov 2026): RG_OFF, RG_TRACK, RG_ALBUM or
   RG_AUTO (rgain.h; REPLAYGAIN= in the INF, AUTO when not given) */
int settings_rgain(void);

/* the volume (0 silent to VOL_FULL, steps of 1.5 dB), VOLUME= in the INF,
   saved by settings_flush at the end when it changed */
#define VOL_FULL 16
int settings_volume(void);
void settings_set_volume(int v);
void settings_flush(void);
/* Shuffle and Repeat (SHUFFLE=, REPEAT= OFF, ALL or ONE: order.h's REP_),
   saved by settings_flush too */
int settings_shuffle(void);
void settings_set_shuffle(int on);
int settings_repeat(void);
void settings_set_repeat(int r);
/* the mini player (MINI= in the INF), saved by settings_flush too */
int settings_mini(void);
/* the full window's place (WINDOW=x,y,w,h in the INF: D43), saved by
   settings_flush: 1 with it, 0 with none kept */
int settings_window(short *x, short *y, short *w, short *h);
void settings_set_window(short x, short y, short w, short h);
void settings_set_mini(int on);

/* the Add files dialog's folder and its last folders (ADDDIR=, FOLDER1=
   to FOLDER10= in the INF), saved by settings_flush once changed */
#include "filepick.h"
char *settings_add_dir(void);
char (*settings_recent(void))[FP_PATH];
void settings_recent_changed(void);

/* phase 7, stations: Through the Gateway (1) or a Direct connection (0),
   the Options menu's; the Gateway's address and port (Settings; port 0:
   not given); the last stations opened (Open URL), the newest first */
#define URL_RECENT 10
#define URL_MAX 256
int settings_gateway(void);
void settings_set_gateway(int on);
const char *settings_gate(void);
long settings_gateport(void);
char (*settings_urls(void))[URL_MAX];
void settings_url_used(const char *url);
const char *settings_url_typed(void);
void settings_set_url_typed(const char *t);
/* Find station's last search (FIND= "genre,country,language,sort,words") */
const char *settings_find(void);
void settings_set_find(const char *f);

#endif
