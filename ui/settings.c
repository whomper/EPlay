/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * settings.c - Settings and EPLAY.INF (settings.h). One page, so no sidebar:
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
#include "field.h"
#include "inf.h"
#include "snd.h"
#include "elog.h"
#include "settings.h"
#include "rgain.h"
#include "alert.h"

static int output = OUT_AUTO;
static int volume = VOL_FULL, volume_saved = VOL_FULL;	/* the bar's (0 to 16), saved at the end */
static char add_dir[FP_PATH], recent[FP_RECENT][FP_PATH];	/* the Add files dialog's */
static int recent_dirty;
static int mini, mini_saved;		/* the mini player (View > Mini player), saved at the end */
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
	if (inf_get(inf, inf_n, "QLATE", v, sizeof(v)) && v[0] >= '0' && v[0] <= '9')
		qlate = atoi(v);
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
	if (inf_get(inf, inf_n, "REPLAYGAIN", v, sizeof(v))) {
		int i;
		for (i = 0; i < NRG; i++)
			if (!strcmp(v, rg_names[i]))
				rgain = i;
	}
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
		n = inf_set(inf3, n, "REPLAYGAIN", rg_names[rgain], gnote, inf2, INF_MAX);
		if (n < 0)
			return;
		memcpy(inf3, inf2, (size_t)n);
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
		for (i = -4; i < URL_RECENT && n >= 0; i++) {
			char k[12];
			if (i == -4)
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

/* ---- the dialog, built in code (as common's Font dialog): each group of
   radio buttons in a box of its own, so that the AES keeps one chosen in
   each ---- */

#define NOBJ 24
static OBJECT tree[NOBJ];
static short nobj;

/* an object, the last child of parent (its ob_next back to the parent, as
   a tree's last child's is) */
static short add(short parent, short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &tree[i];
	o->ob_next = parent;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (tree[parent].ob_tail >= 0)
		tree[tree[parent].ob_tail].ob_next = i;
	else
		tree[parent].ob_head = i;
	tree[parent].ob_tail = i;
	return i;
}

void settings_dialog(void)
{
	static const char oline[] = "Output:";
	static const char gline[] = "Through the Gateway:", aword[] = "address", pword[] = "port";
	static TEDINFO t_addr, t_port, t_band;
	static char a_text[sizeof(gate)], a_tmpl[25], a_valid[25], p_text[6], p_tmpl[6], p_valid[6];
	static LFIELD lf;
	static const char rgline[] = "ReplayGain:";
	short radio[NOUT], rgr[NRG], ok, cancel, r, g, fa, fp, band;
	int i, eff = settings_output(), chose = eff;
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = 62;
	tree[0].ob_height = 12;
	/* the title band (D22): hidden in a dialog window, whose title names it */
	memset(&t_band, 0, sizeof(t_band));
	t_band.te_ptext = (char *)"Settings";
	t_band.te_ptmplt = (char *)"";
	t_band.te_pvalid = (char *)"";
	t_band.te_font = 3;
	t_band.te_just = 2;
	t_band.te_color = 0x1071;	/* (white text, transparent, on black) */
	band = add(0, G_BOXTEXT, 0, 0, (long)&t_band, 0, 0, 62, 1);
	add(0, G_STRING, 0, 0, (long)oline, 2, 2, (short)strlen(oline), 1);
	g = add(0, G_IBOX, 0, 0, 0L, 14, 2, 28, 1);
	for (i = 0; i < NOUT; i++)
		radio[i] = add(g, G_BUTTON, SELECTABLE | RBUTTON, (short)((i == eff ? SELECTED : 0) |
									(has(i) ? 0 : DISABLED)),
			       (long)labels[i], (short)(10 * i), 0, 8, 1);
	/* stations through the Gateway (phase 7): its address and port, as the
	   family's Settings have them (7.3); the address a long field (5.4) */
	add(0, G_STRING, 0, 0, (long)gline, 2, 4, (short)strlen(gline), 1);
	add(0, G_STRING, 0, 0, (long)aword, 2, 5, (short)strlen(aword), 1);
	memset(a_tmpl, '_', 24);
	a_tmpl[24] = 0;
	memset(a_valid, 'X', 24);
	a_valid[24] = 0;
	snprintf(a_text, sizeof(a_text), "%s", gate);
	memset(&t_addr, 0, sizeof(t_addr));
	t_addr.te_ptext = a_text;
	t_addr.te_ptmplt = a_tmpl;
	t_addr.te_pvalid = a_valid;
	t_addr.te_font = 3;
	t_addr.te_color = 0x1180;
	t_addr.te_thickness = -1;
	t_addr.te_txtlen = 25;
	t_addr.te_tmplen = 25;
	fa = add(0, G_FBOXTEXT, EDITABLE, 0, (long)&t_addr, 10, 5, 24, 1);
	add(0, G_STRING, 0, 0, (long)pword, 36, 5, (short)strlen(pword), 1);
	strcpy(p_tmpl, "_____");
	strcpy(p_valid, "99999");
	snprintf(p_text, sizeof(p_text), "%ld", gateport > 0 ? gateport : 8580L);
	memset(&t_port, 0, sizeof(t_port));
	t_port.te_ptext = p_text;
	t_port.te_ptmplt = p_tmpl;
	t_port.te_pvalid = p_valid;
	t_port.te_font = 3;
	t_port.te_color = 0x1180;
	t_port.te_thickness = -1;
	t_port.te_txtlen = 6;
	t_port.te_tmplen = 6;
	fp = add(0, G_FBOXTEXT, EDITABLE, 0, (long)&t_port, 41, 5, 5, 1);
	/* ReplayGain (phase 8b): its four as radio buttons, in a box of their own */
	add(0, G_STRING, 0, 0, (long)rgline, 2, 7, (short)strlen(rgline), 1);
	g = add(0, G_IBOX, 0, 0, 0L, 14, 7, 38, 1);
	for (i = 0; i < NRG; i++)
		rgr[i] = add(g, G_BUTTON, SELECTABLE | RBUTTON, (short)(i == rgain ? SELECTED : 0), (long)rg_labels[i],
			     (short)(10 * i), 0, 8, 1);
	/* Cancel then OK, 10 wide (C1, D26) */
	cancel = add(0, G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", 39, 10, 10, 1);
	ok = add(0, G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"OK", 50, 10, 10, 1);
	tree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(tree, nobj);
	lf_init(&lf, tree, fa, a_text, (short)(sizeof(a_text) - 1), 0);
	dlg_name("Settings");
	for (;;) {
		long pv;
		dlg_band(band);
		r = dlg_modal(tree, fa, cancel);
		if (r != ok)
			break;
		/* an out-of-range port refused, saying what to type (D30, C21);
		   left empty: the radio service's port */
		pv = atol(p_text);
		if (!p_text[0] || (pv > 0 && pv <= 65535))
			break;
		alert(1, "[1][The port is a number from 1 to 65535.|The Gateway's radio service listens|on 8580.][ OK ]");
	}
	lf_forget(tree);
	if (r != ok)
		return;
	{
		long pv = p_text[0] ? atol(p_text) : 8580;
		if (strcmp(a_text, gate) || pv != gateport) {
			snprintf(gate, sizeof(gate), "%s", a_text);
			gateport = pv;
			gate_dirty = 1;
			save();
			elog("Settings: the Gateway at %s, port %ld", gate[0] ? gate : "(none)", gateport);
		}
	}
	(void)fp;
	for (i = 0; i < NRG; i++)
		if ((tree[rgr[i]].ob_state & SELECTED) && i != rgain) {
			rgain = i;
			save();
			elog("Settings: ReplayGain %s", rg_labels[rgain]);
			break;
		}
	for (i = 0; i < NOUT; i++)
		if (tree[radio[i]].ob_state & SELECTED)
			chose = i;
	/* (as it was, nothing saved: an FDI chosen and its driver not loaded
	   this time stays chosen) */
	if (chose != eff) {
		output = chose;
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
	if (volume != volume_saved || mini != mini_saved || recent_dirty || shuffle != shuffle_saved ||
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
