/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * settings.c - Settings and EPLAY.INF (settings.h). One page, so no sidebar:
 * a sentence on what Output is, then its three choices as radio buttons
 * (Erez, 2 Nov 2026: the choices need no notes); a sentence on Quality,
 * then Normal and High (High the default). A choice the machine
 * hasn't is greyed: the FDI without its driver, the DSP without a DSP.
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
#include "inf.h"
#include "snd.h"
#include "elog.h"
#include "settings.h"

static int output = OUT_AUTO, quality = Q_HIGH;
static int volume = VOL_FULL, volume_saved = VOL_FULL;	/* the bar's (0 to 16), saved at the end */
#define NOUT 3
static const char *const names[NOUT] = { "AUTO", "FDI", "DSP" };
static const char *const labels[NOUT] = { "Auto", "FDI", "DSP" };
static const char *const qnames[2] = { "NORMAL", "HIGH" };
static const char *const qlabels[2] = { "Normal", "High" };

#define INF_MAX 4096
static char inf[INF_MAX], inf2[INF_MAX], inf3[INF_MAX];
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
	if (inf_get(inf, inf_n, "QUALITY", v, sizeof(v)))
		for (i = 0; i < 2; i++)
			if (!strcmp(v, qnames[i]))
				quality = i;
	if (inf_get(inf, inf_n, "VOLUME", v, sizeof(v)) && v[0] >= '0' && v[0] <= '9') {
		volume = atoi(v);
		if (volume > VOL_FULL)
			volume = VOL_FULL;
		volume_saved = volume;
	}
}

static void save(void)
{
	static const char note[] =
	    "; Output: how EPlay plays the music. AUTO (the FDI if its driver answers,\r\n"
	    "; else the DSP), FDI or DSP\r\n";
	static const char qnote[] =
	    "; Quality: HIGH, or NORMAL: up to about 15 kHz, for a slower machine (the\r\n"
	    "; DSP on the Falcon's own clock); through the FDI always whole\r\n";
	static const char vnote[] =
	    "; Volume: 0 (silent) to 16 (full), steps of 1.5 dB; as the bar was left\r\n";
	char path[200], tmp[200], vs[8];
	long n = inf_set(inf, inf_n, "OUTPUT", names[output], note, inf2, INF_MAX), h;
	if (n < 0)
		return;
	n = inf_set(inf2, n, "QUALITY", qnames[quality], qnote, inf3, INF_MAX);
	if (n < 0)
		return;
	snprintf(vs, sizeof(vs), "%d", volume);
	n = inf_set(inf3, n, "VOLUME", vs, vnote, inf2, INF_MAX);
	if (n < 0)
		return;
	inf_path(path, sizeof(path), "eplay.inf");
	inf_path(tmp, sizeof(tmp), "eplay.in$");
	h = Fcreate(tmp, 0);
	if (h < 0)
		return;
	if (Fwrite((short)h, n, inf2) != n) {
		Fclose((short)h);
		Fdelete(tmp);
		return;
	}
	Fclose((short)h);
	Fdelete(path);			/* (TOS doesn't rename over a file) */
	if (Frename(tmp, path) < 0)
		return;
	memcpy(inf, inf2, (size_t)n);
	inf_n = n;
	volume_saved = volume;
}

int settings_output(void)
{
	return has(output) ? output : OUT_AUTO;
}

const char *settings_output_name(int out)
{
	return out >= 0 && out < NOUT ? labels[out] : "?";
}

int settings_quality(void)
{
	return quality;
}

const char *settings_quality_name(int q)
{
	return q >= 0 && q < 2 ? qlabels[q] : "?";
}

/* ---- the dialog, built in code (as common's Font dialog): each group of
   radio buttons in a box of its own, so that the AES keeps one chosen in
   each ---- */

#define NOBJ 16
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
	static const char sentence[] = "Output: how EPlay plays the music.";
	static const char qsentence[] = "Quality: Normal plays up to 15 kHz, for a slower machine.";
	short radio[NOUT], qradio[2], ok, cancel, r, g;
	int i, eff = settings_output(), chose = eff, qchose = quality;
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = 62;
	tree[0].ob_height = 12;
	add(0, G_STRING, 0, 0, (long)sentence, 2, 1, (short)strlen(sentence), 1);
	g = add(0, G_IBOX, 0, 0, 0L, 2, 3, 28, 1);
	for (i = 0; i < NOUT; i++)
		radio[i] = add(g, G_BUTTON, SELECTABLE | RBUTTON, (short)((i == eff ? SELECTED : 0) |
									(has(i) ? 0 : DISABLED)),
			       (long)labels[i], (short)(10 * i), 0, 8, 1);
	add(0, G_STRING, 0, 0, (long)qsentence, 2, 5, (short)strlen(qsentence), 1);
	g = add(0, G_IBOX, 0, 0, 0L, 2, 7, 18, 1);
	for (i = 0; i < 2; i++)
		qradio[i] = add(g, G_BUTTON, SELECTABLE | RBUTTON, (short)(i == quality ? SELECTED : 0),
				(long)qlabels[i], (short)(10 * i), 0, 8, 1);
	ok = add(0, G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"OK", 40, 10, 9, 1);
	cancel = add(0, G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", 51, 10, 9, 1);
	tree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(tree, nobj);
	dlg_name("Settings");
	r = dlg_modal(tree, 0, cancel);
	if (r != ok)
		return;
	for (i = 0; i < NOUT; i++)
		if (tree[radio[i]].ob_state & SELECTED)
			chose = i;
	for (i = 0; i < 2; i++)
		if (tree[qradio[i]].ob_state & SELECTED)
			qchose = i;
	/* (as it was, nothing saved: an FDI chosen and its driver not loaded
	   this time stays chosen) */
	if (chose != eff || qchose != quality) {
		if (chose != eff)
			output = chose;
		quality = qchose;
		save();
		elog("Settings: output %s, quality %s (from the next Play)", labels[settings_output()],
		     qlabels[quality]);
	}
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
	if (volume != volume_saved)
		save();
}
