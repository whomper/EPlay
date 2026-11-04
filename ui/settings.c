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
#include "inf.h"
#include "snd.h"
#include "elog.h"
#include "settings.h"

static int output = OUT_AUTO;
static int volume = VOL_FULL, volume_saved = VOL_FULL;	/* the bar's (0 to 16), saved at the end */
static char add_dir[AF_PATH], recent[AF_RECENT][AF_PATH];	/* the Add files dialog's */
static int recent_dirty;
static int mini, mini_saved;		/* the mini player (View > Mini player), saved at the end */
static int qlate = 3;			/* QLATE=: the rigs' (read, never written) */
static int shuffle, shuffle_saved, repeat, repeat_saved;	/* the Play menu's, saved at the end */
static const char *const rnames[3] = { "OFF", "ALL", "ONE" };
#define NOUT 3
static const char *const names[NOUT] = { "AUTO", "FDI", "DSP" };
static const char *const labels[NOUT] = { "Auto", "FDI", "DSP" };

#define INF_MAX 4096
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
	for (i = 0; i < AF_RECENT; i++) {
		char k[12];
		snprintf(k, sizeof(k), "FOLDER%d", i + 1);
		inf_get(inf, inf_n, k, recent[i], AF_PATH);
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
		for (i = -1; i < AF_RECENT && n >= 0; i++) {
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
	short radio[NOUT], ok, cancel, r, g;
	int i, eff = settings_output(), chose = eff;
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = 62;
	tree[0].ob_height = 8;
	add(0, G_STRING, 0, 0, (long)sentence, 2, 1, (short)strlen(sentence), 1);
	g = add(0, G_IBOX, 0, 0, 0L, 2, 3, 28, 1);
	for (i = 0; i < NOUT; i++)
		radio[i] = add(g, G_BUTTON, SELECTABLE | RBUTTON, (short)((i == eff ? SELECTED : 0) |
									(has(i) ? 0 : DISABLED)),
			       (long)labels[i], (short)(10 * i), 0, 8, 1);
	ok = add(0, G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"OK", 40, 6, 9, 1);
	cancel = add(0, G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", 51, 6, 9, 1);
	tree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(tree, nobj);
	dlg_name("Settings");
	r = dlg_modal(tree, 0, cancel);
	if (r != ok)
		return;
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
	    repeat != repeat_saved)
		save();
}

char *settings_add_dir(void)
{
	return add_dir;
}

char (*settings_recent(void))[AF_PATH]
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
