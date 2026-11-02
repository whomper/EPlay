/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * settings.c - Settings and EPLAY.INF (settings.h). One page, so no sidebar:
 * a sentence on what Output is, then its four choices as radio buttons
 * (Erez, 2 Nov 2026: the choices need no notes). A choice the machine
 * hasn't is greyed: the FDI without its driver, the DSP without a DSP, the
 * 68030 until its decoder is in (phase 2's last part). The INF is written
 * as EPLAY.IN$ first and renamed over EPLAY.INF (the guidelines, 2.3), both
 * named in lowercase, as every file of the family's (6.6: MiNT's ext2).
 */
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "gem.h"
#include "dialog.h"
#include "inf.h"
#include "snd.h"
#include "elog.h"
#include "settings.h"

static int output = OUT_AUTO;
static const char *const names[4] = { "AUTO", "FDI", "DSP", "68030" };
static const char *const labels[4] = { "Auto", "FDI", "DSP", "68030" };

#define INF_MAX 4096
static char inf[INF_MAX], inf2[INF_MAX];
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
	case OUT_68030:
		return 0;			/* (its decoder: phase 2's last part) */
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
		for (i = 0; i < 4; i++)
			if (!strcmp(v, names[i]))
				output = i;
}

static void save(void)
{
	static const char note[] =
	    "; Output: how EPlay plays the music. AUTO (the FDI if its driver answers,\r\n"
	    "; else the DSP, else the 68030), FDI, DSP or 68030\r\n";
	char path[200], tmp[200];
	long n = inf_set(inf, inf_n, "OUTPUT", names[output], note, inf2, INF_MAX), h;
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
}

int settings_output(void)
{
	return has(output) ? output : OUT_AUTO;
}

const char *settings_output_name(int out)
{
	return out >= 0 && out < 4 ? labels[out] : "?";
}

/* ---- the dialog, built in code (as common's Font dialog) ---- */

#define NOBJ 9
static OBJECT tree[NOBJ];
static short nobj, last;

static short add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &tree[i];
	o->ob_next = 0;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last)
		tree[last].ob_next = i;
	else
		tree[0].ob_head = i;
	tree[0].ob_tail = i;
	last = i;
	return i;
}

void settings_dialog(void)
{
	static const char sentence[] = "Output: how EPlay plays the music.";
	short radio[4], ok, cancel, r;
	int i, eff = settings_output(), chose = eff;
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	last = 0;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = 42;
	tree[0].ob_height = 8;
	add(G_STRING, 0, 0, (long)sentence, 2, 1, (short)strlen(sentence), 1);
	for (i = 0; i < 4; i++)
		radio[i] = add(G_BUTTON, SELECTABLE | RBUTTON, (short)((i == eff ? SELECTED : 0) |
								     (has(i) ? 0 : DISABLED)),
			       (long)labels[i], (short)(2 + 10 * i), 3, 8, 1);
	ok = add(G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"OK", 20, 6, 9, 1);
	cancel = add(G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", 31, 6, 9, 1);
	tree[last].ob_next = 0;
	tree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(tree, nobj);
	dlg_name("Settings");
	r = dlg_modal(tree, 0, cancel);
	if (r != ok)
		return;
	for (i = 0; i < 4; i++)
		if (tree[radio[i]].ob_state & SELECTED)
			chose = i;
	/* (as it was, nothing saved: an FDI chosen and its driver not loaded
	   this time stays chosen) */
	if (chose != eff) {
		output = chose;
		save();
		elog("Settings: output %s (from the next Play)", labels[output]);
	}
}
