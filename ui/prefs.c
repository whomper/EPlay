/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * prefs.c - (once settings.c) Settings and EPLAY.INF (settings.h). One page, so no sidebar:
 * a sentence on what Output is, then its three choices as radio buttons
 * (Erez, 2 Nov 2026: the choices need no notes). (Quality, Normal and
 * High, was the second: chosen by itself since 4 Nov 2026, Erez.) A
 * choice the machine hasn't is greyed: the FDI without its driver, the
 * DSP without a DSP.
 * (The 68030 is no longer a choice: Erez, 3 Nov 2026, as no 68030 keeps up
 * with MP3; an INF's OUTPUT=68030 reads as Auto.) The INF is written
 * as EPLAY.IN$ first and renamed over EPLAY.INF (the guidelines, 2.3), both
 * named in lowercase, as every file of the family's (6.6: MiNT's ext2).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "dialog.h"
#include "settings.h"
#include "inf.h"
#include "snd.h"
#include "elog.h"
#include "prefs.h"
#include "rgain.h"
#include "alert.h"

static int output = OUT_AUTO;
static int volume = VOL_FULL, volume_saved = VOL_FULL;	/* the bar's (0 to 16), saved at the end */
static char add_dir[FP_PATH], recent[FP_RECENT][FP_PATH];	/* the Add files dialog's */
static int recent_dirty;
static int mini, mini_saved;		/* the mini player (View > Mini player), saved at the end */
static short win[4], win_saved[4];	/* the window's place (WINDOW=x,y,w,h; D43): 0 wide, none */
static int mp3cpu;			/* MP3CPU=: the rigs' (read, never written). 1: on a 68060 with its FPU, MP3 on the CPU (MP3FPU.OVL) though the DSP is there: Hatari's DSP can't be taken away under EmuTOS */
static int qlate = 1;			/* QLATE=: the rigs' (read, never written). 1: Normal at the first granule late past the start (3 let a heavy file crackle six times first: Erez, 5 Nov 2026) */
static int shuffle, shuffle_saved, repeat, repeat_saved;	/* the Play menu's, saved at the end */
static const char *const rnames[3] = { "OFF", "ALL", "ONE" };
#define NOUT 3
static const char *const names[NOUT] = { "AUTO", "FDI", "DSP" };
static const char *const labels[NOUT] = { "Auto", "FDI", "DSP" };
/* phase 7: how a station is reached (the Options menu: Direct connection
   or Through the Gateway; Erez, 8 Nov 2026), the Gateway's address and
   port (Settings), and the last stations opened (Open URL). The INF's keys
   are the family's: FALCON= (1: Direct connection), GATE=, GATEPORT= */
static int gateway, gateway_saved;
/* ReplayGain (phase 8b, Erez, 9 Nov 2026): Auto by default */
#define NRG 4
static int rgain = RG_AUTO;
static const char *const rg_names[NRG] = { "OFF", "TRACK", "ALBUM", "AUTO" };
static const char *const rg_labels[NRG] = { "Off", "Track", "Album", "Auto" };
static char gate[64];
static long gateport = 8580;	/* (the radio service's port: Erez, 8 Nov 2026) */
static char urls[URL_RECENT][URL_MAX];
static char url_typed[URL_MAX];	/* Open URL's field as last typed (URLTYPED=) */
static char find[96];			/* Find station's last search (FIND=) */
static int urls_dirty, gate_dirty;

#define INF_MAX 8192		/* (ten stations' addresses: up to 2.5 KB) */
static char inf[INF_MAX], *inf2, *inf3;	/* (the two a save writes into: taken for it) */
static long inf_n;

static void inf_path(char *p, int n, const char *file)
{
	snprintf(p, (size_t)n, "%s%s", app_dir(), file);
}

/* the machine has it (a choice it hasn't is greyed, and Auto plays) */
static int has(int out)
{
	switch (out) {
	case OUT_FDI:
		return snd_fdi;
	case OUT_DSP:
		return (get_cookie(0x5F534E44L) & 8) != 0;	/* _SND bit 3: the DSP */
	}
	return 1;
}

void settings_load(void)
{
	char path[200], v[16];
	long h;
	int i;
	inf_path(path, sizeof(path), "eplay.inf");
	inf_n = 0;
	h = Fopen(path, 0);
	if (h < 0) {
		inf_path(path, sizeof(path), "eplay.in$");	/* (a save cut short after the old one went) */
		h = Fopen(path, 0);
	}
	if (h >= 0) {
		long r = Fread((short)h, INF_MAX - 1, inf);
		Fclose((short)h);
		inf_n = r > 0 ? r : 0;
	}
	if (inf_get(inf, inf_n, "OUTPUT", v, sizeof(v)))
		for (i = 0; i < NOUT; i++)
			if (!strcmp(v, names[i]))
				output = i;
	if (inf_get(inf, inf_n, "VOLUME", v, sizeof(v)) && v[0] >= '0' && v[0] <= '9') {
		volume = atoi(v);
		if (volume > VOL_FULL)
			volume = VOL_FULL;
		volume_saved = volume;
	}
	inf_get(inf, inf_n, "ADDDIR", add_dir, sizeof(add_dir));
	for (i = 0; i < FP_RECENT; i++) {
		char k[12];
		snprintf(k, sizeof(k), "FOLDER%d", i + 1);
		inf_get(inf, inf_n, k, recent[i], FP_PATH);
	}
	if (inf_get(inf, inf_n, "MINI", v, sizeof(v)))
		mini = mini_saved = v[0] == '1';
	if (inf_get(inf, inf_n, "WINDOW", v, sizeof(v))) {
		long a[4];
		char *p = v, *e;
		int i, k = 0;
		for (i = 0; i < 4; i++) {
			a[i] = strtol(p, &e, 10);
			if (e == p)
				break;
			k++;
			p = *e == ',' ? e + 1 : e;
		}
		if (k == 4 && a[2] > 0 && a[3] > 0)
			for (i = 0; i < 4; i++)
				win[i] = win_saved[i] = (short)a[i];
	}
	if (inf_get(inf, inf_n, "QLATE", v, sizeof(v)) && v[0] >= '0' && v[0] <= '9')
		qlate = atoi(v);
	if (inf_get(inf, inf_n, "MP3CPU", v, sizeof(v)))
		mp3cpu = v[0] == '1';
	if (inf_get(inf, inf_n, "SHUFFLE", v, sizeof(v)))
		shuffle = shuffle_saved = v[0] == '1';
	if (inf_get(inf, inf_n, "REPEAT", v, sizeof(v)))
		for (i = 0; i < 3; i++)
			if (!strcmp(v, rnames[i]))
				repeat = repeat_saved = i;
	if (inf_get(inf, inf_n, "FALCON", v, sizeof(v)))
		gateway = gateway_saved = v[0] == '0';
	inf_get(inf, inf_n, "GATE", gate, sizeof(gate));
	if (inf_get(inf, inf_n, "GATEPORT", v, sizeof(v)) && atol(v) > 0 && atol(v) < 65536)
		gateport = atol(v);
	for (i = 0; i < URL_RECENT; i++) {
		char k[12];
		snprintf(k, sizeof(k), "URL%d", i + 1);
		inf_get(inf, inf_n, k, urls[i], URL_MAX);
	}
	inf_get(inf, inf_n, "URLTYPED", url_typed, URL_MAX);
	inf_get(inf, inf_n, "FIND", find, sizeof(find));
	if (inf_get(inf, inf_n, "SYSFSEL", v, sizeof(v)))
		filepick_system = v[0] == '1';
	if (inf_get(inf, inf_n, "REPLAYGAIN", v, sizeof(v))) {
		int i;
		for (i = 0; i < NRG; i++)
			if (!strcmp(v, rg_names[i]))
				rgain = i;
	}
}

int settings_mp3cpu(void)
{
	return mp3cpu;
}

int settings_qlate(void)
{
	return qlate;
}

static void save_in(void);

/* (8 KB less of the program's own memory: a stock Falcon under NVDI keeps
   the room a menu needs: the hub, 4 Nov 2026) */
static void save(void)
{
	inf2 = malloc(2 * INF_MAX);
	if (!inf2)
		return;
	inf3 = inf2 + INF_MAX;
	save_in();
	free(inf2);
	inf2 = inf3 = 0;
}

static void save_in(void)
{
	static const char note[] =
	    "; Output: how EPlay plays the music. AUTO (the FDI if its driver answers,\r\n"
	    "; else the DSP), FDI or DSP\r\n";
	static const char mnote[] = "; Mini player: 1 for the bar alone (View > Mini player), 0 with the list\r\n";
	static const char vnote[] =
	    "; Volume: 0 (silent) to 16 (full), steps of 1.5 dB; as the bar was left\r\n";
	char path[200], tmp[200], vs[8];
	long n = inf_set(inf, inf_n, "OUTPUT", names[output], note, inf2, INF_MAX), h;
	if (n < 0)
		return;
	snprintf(vs, sizeof(vs), "%d", volume);
	n = inf_set(inf2, n, "VOLUME", vs, vnote, inf3, INF_MAX);
	if (n < 0)
		return;
	memcpy(inf2, inf3, (size_t)n);	/* (the folders' keys start from inf2) */
	{
		/* the Add files dialog's folders, a key at a time between the two
		   buffers, ending in inf2 */
		static const char dnote[] = "; Add files: the folder it opens at, and the last folders added from\r\n";
		char *a = inf2, *b = inf3, *t;
		int i;
		for (i = -1; i < FP_RECENT && n >= 0; i++) {
			char k[12];
			if (i < 0)
				n = inf_set(a, n, "ADDDIR", add_dir, dnote, b, INF_MAX);
			else {
				snprintf(k, sizeof(k), "FOLDER%d", i + 1);
				n = inf_set(a, n, k, recent[i], 0, b, INF_MAX);
			}
			t = a;
			a = b;
			b = t;
		}
		if (n < 0)
			return;
		if (a != inf2)
			memcpy(inf2, a, (size_t)n);
	}
	n = inf_set(inf2, n, "MINI", mini ? "1" : "0", mnote, inf3, INF_MAX);
	if (n < 0)
		return;
	if (win[2] > 0) {
		static const char wnote[] = "; The window's place: x,y,width,height (the full window's, not the mini player's)\r\n";
		char ws[32];
		snprintf(ws, sizeof(ws), "%d,%d,%d,%d", win[0], win[1], win[2], win[3]);
		n = inf_set(inf3, n, "WINDOW", ws, wnote, inf2, INF_MAX);
		if (n < 0)
			return;
		memcpy(inf3, inf2, (size_t)n);
	}
	{
		static const char snote[] = "; Shuffle: 1 for a random order (Play > Shuffle), 0 the list's\r\n";
		static const char rnote[] = "; Repeat: OFF, ALL (the list again) or ONE (the file again)\r\n";
		n = inf_set(inf3, n, "SHUFFLE", shuffle ? "1" : "0", snote, inf2, INF_MAX);
		if (n < 0)
			return;
		n = inf_set(inf2, n, "REPEAT", rnames[repeat], rnote, inf3, INF_MAX);
		if (n < 0)
			return;
	}
	{
		static const char gnote[] =
		    "; ReplayGain: OFF, TRACK, ALBUM or AUTO (the album's in order, the track's\r\n"
		    "; shuffled); louder only as far as the peak lets it go\r\n";
		static const char fnote[] = "; Files: 1 for the system's file selector, 0 EPlay's own (Settings)\r\n";
		n = inf_set(inf3, n, "REPLAYGAIN", rg_names[rgain], gnote, inf2, INF_MAX);
		if (n < 0)
			return;
		n = inf_set(inf2, n, "SYSFSEL", filepick_system ? "1" : "0", fnote, inf3, INF_MAX);
		if (n < 0)
			return;
	}
	{
		/* the stations' keys, a key at a time between the two buffers,
		   ending in inf3 */
		static const char cnote[] = "; Stations: 1 for a Direct connection, 0 Through the Gateway (Options menu)\r\n";
		static const char gnote[] = "; Through the Gateway: its address and port (Settings)\r\n";
		static const char unote[] = "; Open URL: the last stations opened\r\n";
		char *a = inf3, *b = inf2, *t, ps[8];
		int i;
		snprintf(ps, sizeof(ps), "%ld", gateport);
		for (i = -5; i < URL_RECENT && n >= 0; i++) {
			char k[12];
			if (i == -5)
				n = inf_set(a, n, "FIND", find, 0, b, INF_MAX);
			else if (i == -4)
				n = inf_set(a, n, "URLTYPED", url_typed, 0, b, INF_MAX);
			else if (i == -3)
				n = inf_set(a, n, "FALCON", gateway ? "0" : "1", cnote, b, INF_MAX);
			else if (i == -2)
				n = inf_set(a, n, "GATE", gate, gnote, b, INF_MAX);
			else if (i == -1)
				n = inf_set(a, n, "GATEPORT", gateport ? ps : "", 0, b, INF_MAX);
			else {
				snprintf(k, sizeof(k), "URL%d", i + 1);
				n = inf_set(a, n, k, urls[i], i ? 0 : unote, b, INF_MAX);
			}
			t = a;
			a = b;
			b = t;
		}
		if (n < 0)
			return;
		if (a != inf3)
			memcpy(inf3, a, (size_t)n);
	}
	inf_path(path, sizeof(path), "eplay.inf");
	inf_path(tmp, sizeof(tmp), "eplay.in$");
	h = Fcreate(tmp, 0);
	if (h < 0)
		return;
	if (Fwrite((short)h, n, inf3) != n) {
		Fclose((short)h);
		Fdelete(tmp);
		return;
	}
	Fclose((short)h);
	Fdelete(path);			/* (TOS doesn't rename over a file) */
	if (Frename(tmp, path) < 0)
		return;
	memcpy(inf, inf3, (size_t)n);
	inf_n = n;
	volume_saved = volume;
	recent_dirty = 0;
	mini_saved = mini;
	memcpy(win_saved, win, sizeof(win));
	shuffle_saved = shuffle;
	repeat_saved = repeat;
	gateway_saved = gateway;
	urls_dirty = gate_dirty = 0;
}

int settings_output(void)
{
	return has(output) ? output : OUT_AUTO;
}

const char *settings_output_name(int out)
{
	return out >= 0 && out < NOUT ? labels[out] : "?";
}

/* ---- the dialog: the family's Settings (common's settings.c, the
   guidelines' 6.1; D24, D25), three pages: Playing (Output and
   ReplayGain, radio buttons, a choice the machine hasn't greyed),
   Stations (the Gateway's address and port), Files (the system's file
   selector or EPlay's own) ---- */

static short d_out, d_rg, d_fsel;
static char d_gate[sizeof(gate)], d_port[6];
static short f_port;

static void page_playing(SETTINGS *s, short x, short y, void *user)
{
	short r;
	int i;
	(void)y;
	(void)user;
	set_text(s, x, set_y(s, 0), "Output:");
	r = set_radio(s, x, set_y(s, 1), 8, labels, NOUT, &d_out);
	for (i = 0; i < NOUT; i++)
		if (r && !has(i))
			set_radio_enable(s, r, (short)i, 0);
	set_text(s, x, set_y(s, 3), "ReplayGain:");
	set_radio(s, x, set_y(s, 4), 8, rg_labels, NRG, &d_rg);
}

static void page_stations(SETTINGS *s, short x, short y, void *user)
{
	(void)y;
	(void)user;
	set_text(s, x, set_y(s, 0), "Through the Gateway:");
	set_text(s, x, set_y(s, 1), "Address:");
	set_long(s, (short)(x + 10), set_y(s, 1), d_gate, 30, (short)(sizeof(d_gate) - 1), 0);
	set_text(s, x, set_y(s, 2), "Port:");
	f_port = set_field(s, (short)(x + 10), set_y(s, 2), d_port, 5, '9');
}

static void page_files(SETTINGS *s, short x, short y, void *user)
{
	(void)y;
	(void)user;
	set_tick(s, x, set_y(s, 0), "Use the system file selector", &d_fsel);
}

/* an out-of-range port refused, saying what to type (D30, C21); left
   empty: the radio service's port */
static int check(SETTINGS *s, void *user)
{
	long pv = atol(d_port);
	(void)user;
	if (!d_port[0] || (pv > 0 && pv <= 65535))
		return 1;
	alert(1, "[1][The port is a number from 1 to 65535.|The Gateway's radio service listens|on 8580.][ OK ]");
	set_show(s, 1);
	set_focus(s, f_port);
	return 0;
}

void prefs_dialog(void)
{
	static const SET_PAGE pages[] = { { "Playing", page_playing }, { "Stations", page_stations },
					   { "Files", page_files } };
	SET_DEF d;
	long pv;
	int eff = settings_output();
	d_out = (short)eff;
	d_rg = (short)rgain;
	d_fsel = filepick_system;
	snprintf(d_gate, sizeof(d_gate), "%s", gate);
	snprintf(d_port, sizeof(d_port), "%ld", gateport > 0 ? gateport : 8580L);
	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.pages = pages;
	d.npages = 3;
	d.height = 16;
	d.check = check;
	if (!settings_dialog(&d))
		return;
	pv = d_port[0] ? atol(d_port) : 8580;
	if (strcmp(d_gate, gate) || pv != gateport) {
		snprintf(gate, sizeof(gate), "%s", d_gate);
		gateport = pv;
		gate_dirty = 1;
		save();
		elog("Settings: the Gateway at %s, port %ld", gate[0] ? gate : "(none)", gateport);
	}
	if (d_rg != rgain) {
		rgain = d_rg;
		save();
		elog("Settings: ReplayGain %s", rg_labels[rgain]);
	}
	if (d_fsel != filepick_system) {
		filepick_system = d_fsel;
		save();
		elog("Settings: the system file selector %s", filepick_system ? "on" : "off");
	}
	/* (as it was, nothing saved: an FDI chosen and its driver not loaded
	   this time stays chosen) */
	if (d_out != eff) {
		output = d_out;
		save();
		elog("Settings: output %s (from the next Play)", labels[settings_output()]);
	}
}

int settings_rgain(void)
{
	return rgain;
}

int settings_volume(void)
{
	return volume;
}

void settings_set_volume(int v)
{
	volume = v;
}

/* at the end: the volume saved if it changed (not at each step of the
   bar: a write each time) */
void settings_flush(void)
{
	if (volume != volume_saved || mini != mini_saved || memcmp(win, win_saved, sizeof(win)) || recent_dirty ||
	    shuffle != shuffle_saved ||
	    repeat != repeat_saved || gateway != gateway_saved || urls_dirty || gate_dirty)
		save();
}

char *settings_add_dir(void)
{
	return add_dir;
}

char (*settings_recent(void))[FP_PATH]
{
	return recent;
}

void settings_recent_changed(void)
{
	recent_dirty = 1;
}

int settings_window(short *x, short *y, short *w, short *h)
{
	if (win[2] <= 0)
		return 0;
	*x = win[0];
	*y = win[1];
	*w = win[2];
	*h = win[3];
	return 1;
}

void settings_set_window(short x, short y, short w, short h)
{
	win[0] = x;
	win[1] = y;
	win[2] = w;
	win[3] = h;
}

int settings_mini(void)
{
	return mini;
}

void settings_set_mini(int on)
{
	mini = on != 0;
}

int settings_shuffle(void)
{
	return shuffle;
}

void settings_set_shuffle(int on)
{
	shuffle = on != 0;
}

int settings_repeat(void)
{
	return repeat;
}

void settings_set_repeat(int r)
{
	repeat = r >= 0 && r < 3 ? r : 0;
}

int settings_gateway(void)
{
	return gateway;
}

void settings_set_gateway(int on)
{
	gateway = on != 0;
}

const char *settings_gate(void)
{
	return gate;
}

long settings_gateport(void)
{
	return gateport;
}

char (*settings_urls(void))[URL_MAX]
{
	return urls;
}

/* Find station's last search: "genre,country,language,sort,words" */
const char *settings_find(void)
{
	return find;
}

void settings_set_find(const char *f)
{
	if (strlen(f) >= sizeof(find) || !strcmp(find, f))
		return;
	snprintf(find, sizeof(find), "%s", f);
	urls_dirty = 1;
}

/* Open URL's field: what was last typed in it, given back next time */
const char *settings_url_typed(void)
{
	return url_typed;
}

void settings_set_url_typed(const char *t)
{
	if (strlen(t) >= URL_MAX || !strcmp(url_typed, t))
		return;
	snprintf(url_typed, URL_MAX, "%s", t);
	urls_dirty = 1;
}

/* url to the top of the last stations (once: moved up if it was there) */
void settings_url_used(const char *url)
{
	int i, at = URL_RECENT - 1;
	if (strlen(url) >= URL_MAX)
		return;
	for (i = 0; i < URL_RECENT; i++)
		if (!strcmp(urls[i], url)) {
			at = i;
			break;
		}
	if (at == 0 && !strcmp(urls[0], url))
		return;
	memmove(urls[1], urls[0], (size_t)at * URL_MAX);
	snprintf(urls[0], URL_MAX, "%s", url);
	urls_dirty = 1;
}
