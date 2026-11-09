/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * urldlg.c - File > Open URL... (urldlg.h; Erez, 8 Nov 2026): one field for
 * a station's address (common's long field: it holds 255 characters and
 * shows 50, the guidelines' 5.4), and the last ten stations opened, each a
 * line to choose: chosen, it opens at once. Built in code, as Settings.
 */
#include <string.h>
#include <stdio.h>
#include "gem.h"
#include "dialog.h"
#include "field.h"
#include "settings.h"
#include "stream.h"
#include "urldlg.h"

#define NOBJ 20
#define SHOW 50			/* the field's and the lines' width */

static OBJECT tree[NOBJ];
static short nobj;

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

int url_dialog(char *out, int omax)
{
	static const char aword[] = "Address:", lword[] = "The last stations:", none[] = "None yet";
	static TEDINFO t;
	static char text[URL_MAX], tmpl[SHOW + 1], valid[SHOW + 1], lines[URL_RECENT][SHOW + 1];
	static LFIELD lf;
	char (*urls)[URL_MAX] = settings_urls();
	static TEDINFO t_band;
	short field, open, cancel, row[URL_RECENT], r, band;
	int i, nurl = 0, h;
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = SHOW + 13;
	for (i = 0; i < URL_RECENT; i++)
		nurl += urls[i][0] != 0;
	h = 9 + (nurl ? nurl : 1);	/* (as tall as the stations it has) */
	tree[0].ob_height = (short)h;
	nurl = 0;
	/* the title band (D22): hidden in a dialog window, whose title names it */
	memset(&t_band, 0, sizeof(t_band));
	t_band.te_ptext = (char *)"Open URL";
	t_band.te_ptmplt = (char *)"";
	t_band.te_pvalid = (char *)"";
	t_band.te_font = 3;
	t_band.te_just = 2;
	t_band.te_color = 0x1071;	/* (white text, transparent, on black) */
	band = add(0, G_BOXTEXT, 0, 0, (long)&t_band, 0, 0, SHOW + 13, 1);
	add(0, G_STRING, 0, 0, (long)aword, 2, 2, (short)strlen(aword), 1);
	memset(tmpl, '_', SHOW);
	tmpl[SHOW] = 0;
	memset(valid, 'X', SHOW);
	valid[SHOW] = 0;
	snprintf(text, sizeof(text), "%s", settings_url_typed());	/* (the last typed, again) */
	memset(&t, 0, sizeof(t));
	t.te_ptext = text;
	t.te_ptmplt = tmpl;
	t.te_pvalid = valid;
	t.te_font = 3;
	t.te_color = 0x1180;
	t.te_thickness = -1;
	t.te_txtlen = SHOW + 1;
	t.te_tmplen = SHOW + 1;
	field = add(0, G_FBOXTEXT, EDITABLE, 0, (long)&t, 11, 2, SHOW, 1);
	add(0, G_STRING, 0, 0, (long)lword, 2, 4, (short)strlen(lword), 1);
	for (i = 0; i < URL_RECENT; i++) {
		row[i] = -1;
		if (!urls[i][0])
			continue;
		snprintf(lines[i], sizeof(lines[i]), "%s", urls[i]);
		if (strlen(urls[i]) > SHOW)
			memcpy(lines[i] + SHOW - 3, "...", 3);	/* (cut: the whole address is kept) */
		row[i] = add(0, G_STRING, SELECTABLE | EXIT, 0, (long)lines[i], 4, (short)(5 + nurl), SHOW + 4, 1);
		nurl++;
	}
	if (!nurl)
		add(0, G_STRING, 0, DISABLED, (long)none, 4, 5, (short)strlen(none), 1);
	/* Cancel then Open, 10 wide (C1, D26) */
	cancel = add(0, G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", (short)(SHOW + 13 - 23), (short)(h - 2), 10, 1);
	open = add(0, G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"Open", (short)(SHOW + 13 - 12), (short)(h - 2), 10, 1);
	tree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(tree, nobj);
	lf_init(&lf, tree, field, text, URL_MAX - 1, 0);
	dlg_name("Open URL");
	dlg_band(band);
	r = dlg_modal(tree, field, cancel);
	lf_forget(tree);
	for (i = 0; i < URL_RECENT; i++)
		if (row[i] >= 0 && r == row[i]) {
			snprintf(out, (size_t)omax, "%s", urls[i]);
			return 1;
		}
	if (r != open)
		return 0;
	{
		/* the address as typed, spaces around it cut; "radio.example/x"
		   taken as http:// */
		char *s = text, *e;
		while (*s == ' ')
			s++;
		e = s + strlen(s);
		while (e > s && e[-1] == ' ')
			*--e = 0;
		settings_set_url_typed(s);
		if (!*s)
			return 0;
		if (surl_is(s))
			snprintf(out, (size_t)omax, "%s", s);
		else
			snprintf(out, (size_t)omax, "http://%s", s);
	}
	return 1;
}
