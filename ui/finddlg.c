/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * finddlg.c - Find station (finddlg.h): a dialog built in code, as Open
 * URL's, kept open (dlg_open, dlg_do, dlg_close) while it is searched,
 * paged and added from. The pop-ups open over their buttons with the
 * choice ticked (D27). Its last search is kept in EPLAY.INF (FIND=).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "gem.h"
#include "ui.h"
#include "dialog.h"
#include "popup.h"
#include "busy.h"
#include "conn.h"
#include "elog.h"
#include "prefs.h"
#include "netsrc.h"
#include "sart.h"
#include "stsearch.h"
#include "finddlg.h"

extern short gl_hchar;

/* the choices (radio-browser's tags, country codes and language names) */
static const char *const genre_lab[] = { "Any", "Pop", "Rock", "Jazz", "Classical", "Electronic", "Dance",
					 "Chillout", "Ambient", "Hip hop", "Country", "Blues", "Oldies", "Metal",
					 "Reggae", "Latin", "News", "Talk" };
static const char *const genre_tag[] = { "", "pop", "rock", "jazz", "classical", "electronic", "dance",
					 "chillout", "ambient", "hip hop", "country", "blues", "oldies", "metal",
					 "reggae", "latin", "news", "talk" };
static const char *const country_lab[] = { "Any", "Australia", "Austria", "Belgium", "Brazil", "Canada",
					   "Denmark", "Finland", "France", "Germany", "Israel", "Italy", "Japan",
					   "Netherlands", "Norway", "Poland", "Spain", "Sweden", "Switzerland",
					   "United Kingdom", "United States" };
static const char *const country_cc[] = { "", "AU", "AT", "BE", "BR", "CA", "DK", "FI", "FR", "DE", "IL",
					  "IT", "JP", "NL", "NO", "PL", "ES", "SE", "CH", "GB", "US" };
static const char *const lang_lab[] = { "Any", "Dutch", "English", "French", "German", "Hebrew", "Italian",
					"Japanese", "Polish", "Portuguese", "Spanish", "Swedish" };
static const char *const lang_rb[] = { "", "dutch", "english", "french", "german", "hebrew", "italian",
				       "japanese", "polish", "portuguese", "spanish", "swedish" };
static const char *const sort_lab[] = { "Most votes", "Most listened", "Name" };
#define N(a) ((short)(sizeof(a) / sizeof(a[0])))

/* radio-browser's servers, asked in turn (one often refuses: radiogate's) */
static const char *const api_host[] = { "de1.api.radio-browser.info", "fi1.api.radio-browser.info",
					"nl1.api.radio-browser.info", "de2.api.radio-browser.info" };

#define ROWS 10
#define W 64
#define NOBJ 40
static OBJECT tree[NOBJ];
static short nobj;

static short add_obj(short parent, short type, short flags, short state, long spec, short x, short y, short w,
		     short h)
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

static SSTATION *found;			/* the search's stations (SS_MAX), malloc'ed */
static int nfound, top, sel = -1;
static int c_genre, c_country, c_lang, c_sort;
static char q_text[32];
static char b_genre[24], b_country[24], b_lang[24], b_sort[24], note[64];
static char rowtext[ROWS][W];
static short o_rows[ROWS], o_note, o_up, o_dn;

/* "g,c,l,s,words": the four numbers, and where the words begin (k) */
static int sscanf_four(const char *f, int *g, int *c, int *l, int *s, int *k)
{
	int *v[4], i, n;
	const char *p = f;
	v[0] = g;
	v[1] = c;
	v[2] = l;
	v[3] = s;
	for (i = 0; i < 4; i++) {
		if (*p < '0' || *p > '9')
			return 0;
		for (n = 0; *p >= '0' && *p <= '9' && n < 1000; p++)
			n = n * 10 + (*p - '0');
		*v[i] = n;
		if (*p != ',')
			return 0;
		p++;
	}
	*k = (int)(p - f);
	return 1;
}

static void button_text(char *b, int n, const char *lab)
{
	snprintf(b, (size_t)n, "%s \x02", lab);
}

/* a pop-up over its button, the choice ticked (D27): the new choice */
static int pop_over(short o, const char *const *lab, short n, int cur)
{
	static char t[24][24];
	const char *p[24];
	short x, y, r, i;
	for (i = 0; i < n && i < 24; i++) {
		snprintf(t[i], sizeof(t[i]), "%s%s", i == cur ? "\x08" : "", lab[i]);
		p[i] = t[i];
	}
	objc_offset(tree, o, &x, &y);
	r = popup_m(x, (short)(y - 3 - cur * (gl_hchar + 2)), p, n);
	tree[o].ob_state &= ~SELECTED;
	dlg_draw(tree, o);
	return r >= 0 ? r : cur;
}

/* the result rows as the list and its place are */
static void rows_text(void)
{
	int i;
	for (i = 0; i < ROWS; i++) {
		SSTATION *s = top + i < nfound ? &found[top + i] : 0;
		if (s) {
			char kb[8];
			snprintf(kb, sizeof(kb), s->kbps ? "%d" : "", s->kbps);
			snprintf(rowtext[i], sizeof(rowtext[i]), "%-34.34s %-2s  %-4.4s %4s", s->name, s->cc, s->codec,
				 kb);
		} else
			rowtext[i][0] = 0;
		tree[o_rows[i]].ob_flags = s ? (SELECTABLE | TOUCHEXIT) : 0;
		tree[o_rows[i]].ob_state = s && top + i == sel ? SELECTED : 0;
	}
	tree[o_up].ob_state = top > 0 ? 0 : DISABLED;
	tree[o_dn].ob_state = top + ROWS < nfound ? 0 : DISABLED;
}

static void rows_draw(void)
{
	int i;
	rows_text();
	for (i = 0; i < ROWS; i++)
		dlg_draw(tree, o_rows[i]);
	dlg_draw(tree, o_up);
	dlg_draw(tree, o_dn);
}

static void say(const char *s)
{
	snprintf(note, sizeof(note), "%s", s);
	dlg_draw(tree, o_note);
}

/* the search, as the Options menu has stations reached */
static void search(void)
{
	char u[700], why[120], err[120];
	unsigned char *d = 0;
	long len = 0;
	SSQUERY q;
	int ok = 0, gw = settings_gateway(), i;
	q.q = q_text;
	q.tag = genre_tag[c_genre];
	q.cc = country_cc[c_country];
	q.lang = lang_rb[c_lang];
	q.sort = c_sort;
	if (!ns_net(err, sizeof(err))) {
		say(err);
		return;
	}
	if (!found)
		found = malloc(SS_MAX * sizeof(SSTATION));
	if (!found) {
		say("There isn't the memory to search.");
		return;
	}
	say("Searching...   (Esc stops)");
	busy_bee(1);
	conn_stop = 0;
	if (gw) {
		char g[96];
		if (!settings_gate()[0] || settings_gateport() <= 0) {
			busy_bee(0);
			say("Give the Gateway's address and port in Settings.");
			return;
		}
		snprintf(g, sizeof(g), "http://%s:%ld", settings_gate(), settings_gateport());
		if (ss_url_gateway(u, sizeof(u), g, &q) > 0)
			ok = sart_fetch(u, "text/plain", "", 512L * 1024, &d, &len, why, sizeof(why));
		if (ok)
			nfound = ss_parse_lines((const char *)d, len, found, SS_MAX);
	} else {
		for (i = 0; i < N(api_host) && !ok && !conn_stop; i++) {
			char api[64];
			snprintf(api, sizeof(api), "%s://%s", ns_tls() ? "https" : "http", api_host[i]);
			if (ss_url_direct(u, sizeof(u), api, &q) > 0)
				ok = sart_fetch(u, "application/json", "", 2048L * 1024, &d, &len, why, sizeof(why));
		}
		if (ok)
			nfound = ss_parse_json((const char *)d, len, found, SS_MAX);
	}
	busy_bee(0);
	free(d);
	if (!ok) {
		elog("Find station: %s (%s)", gw ? "the Gateway" : "radio-browser.info", why);
		nfound = 0;
		say(conn_stop ? "Searching was stopped." :
			gw ? "The Gateway doesn't answer: is its radio service on?" :
			     "The station directory doesn't answer.");
	} else {
		char s[64];
		elog("Find station: %d stations (%s)", nfound, gw ? "through the Gateway" : "directly");
		snprintf(s, sizeof(s), nfound ? "%d station%s" : "No station found.", nfound, nfound == 1 ? "" : "s");
		say(s);
	}
	conn_stop = 0;
	top = 0;
	sel = nfound ? 0 : -1;
	rows_draw();
}

int find_dialog(char *url, int un, char *name, int nn, void (*add)(const char *url, const char *name))
{
	static TEDINFO t_band, t_q;
	static char tmpl[sizeof(q_text)], valid[sizeof(q_text)];
	static int loaded;
	short band, field, b_search, o_genre, o_country, o_lang, o_sort, o_close, o_add, o_play, r, x, y, w, h;
	int i, play = 0;
	if (!loaded) {
		/* the last search (FIND=genre,country,language,sort,words) */
		const char *f = settings_find();
		int g = 0, c = 0, l = 0, s = 0, k = 0;
		if (f[0] && sscanf_four(f, &g, &c, &l, &s, &k)) {
			c_genre = g < N(genre_lab) ? g : 0;
			c_country = c < N(country_lab) ? c : 0;
			c_lang = l < N(lang_lab) ? l : 0;
			c_sort = s < N(sort_lab) ? s : 0;
			snprintf(q_text, sizeof(q_text), "%s", f + k);
		}
		loaded = 1;
	}
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = W;
	tree[0].ob_height = 22;
	memset(&t_band, 0, sizeof(t_band));
	t_band.te_ptext = (char *)"Find station";
	t_band.te_ptmplt = (char *)"";
	t_band.te_pvalid = (char *)"";
	t_band.te_font = 3;
	t_band.te_just = 2;
	t_band.te_color = 0x1071;
	band = add_obj(0, G_BOXTEXT, 0, 0, (long)&t_band, 0, 0, W, 1);
	add_obj(0, G_STRING, 0, 0, (long)"Name:", 2, 2, 5, 1);
	memset(tmpl, '_', sizeof(tmpl) - 1);
	memset(valid, 'X', sizeof(valid) - 1);
	memset(&t_q, 0, sizeof(t_q));
	t_q.te_ptext = q_text;
	t_q.te_ptmplt = tmpl;
	t_q.te_pvalid = valid;
	t_q.te_font = 3;
	t_q.te_color = 0x1180;
	t_q.te_thickness = -1;
	t_q.te_txtlen = sizeof(q_text);
	t_q.te_tmplen = sizeof(q_text);
	field = add_obj(0, G_FBOXTEXT, EDITABLE, 0, (long)&t_q, 13, 2, sizeof(q_text) - 1, 1);
	b_search = add_obj(0, G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"Search", 51, 2, 10, 1);
	button_text(b_genre, sizeof(b_genre), genre_lab[c_genre]);
	button_text(b_country, sizeof(b_country), country_lab[c_country]);
	button_text(b_lang, sizeof(b_lang), lang_lab[c_lang]);
	button_text(b_sort, sizeof(b_sort), sort_lab[c_sort]);
	add_obj(0, G_STRING, 0, 0, (long)"Genre:", 2, 4, 6, 1);
	o_genre = add_obj(0, G_BUTTON, SELECTABLE | TOUCHEXIT, 0, (long)b_genre, 13, 4, 16, 1);
	add_obj(0, G_STRING, 0, 0, (long)"Country:", 33, 4, 8, 1);
	o_country = add_obj(0, G_BUTTON, SELECTABLE | TOUCHEXIT, 0, (long)b_country, 43, 4, 18, 1);
	add_obj(0, G_STRING, 0, 0, (long)"Language:", 2, 6, 9, 1);
	o_lang = add_obj(0, G_BUTTON, SELECTABLE | TOUCHEXIT, 0, (long)b_lang, 13, 6, 16, 1);
	add_obj(0, G_STRING, 0, 0, (long)"Sort:", 33, 6, 5, 1);
	o_sort = add_obj(0, G_BUTTON, SELECTABLE | TOUCHEXIT, 0, (long)b_sort, 43, 6, 18, 1);
	snprintf(note, sizeof(note), "%s", nfound ? "" : "Choose, then Search.");
	o_note = add_obj(0, G_STRING, 0, 0, (long)note, 2, 8, W - 4, 1);
	for (i = 0; i < ROWS; i++)
		o_rows[i] = add_obj(0, G_STRING, 0, 0, (long)rowtext[i], 2, (short)(9 + i), W - 8, 1);
	o_up = add_obj(0, G_BOXCHAR, TOUCHEXIT, 0, 0x01FF1100L, W - 5, 9, 3, 1);
	o_dn = add_obj(0, G_BOXCHAR, TOUCHEXIT, 0, 0x02FF1100L, W - 5, 9 + ROWS - 1, 3, 1);
	/* Close, then what it does, 10 wide (C1, D26) */
	o_close = add_obj(0, G_BUTTON, SELECTABLE | EXIT, 0, (long)"Close", 29, 20, 10, 1);
	o_add = add_obj(0, G_BUTTON, SELECTABLE | EXIT, 0, (long)"Add", 40, 20, 10, 1);
	o_play = add_obj(0, G_BUTTON, SELECTABLE | EXIT, 0, (long)"Play", 51, 20, 10, 1);
	tree[nobj - 1].ob_flags |= LASTOB;
	rows_text();
	rsrc_obfix_all(tree, nobj);
	dlg_name("Find station");
	dlg_band(band);
	dlg_open(tree, &x, &y, &w, &h);
	for (;;) {
		int dbl;
		r = dlg_do(tree, field, o_close);
		dbl = r >= 0 && (r & 0x8000);
		r &= 0x7fff;
		if (r == o_close || r < 0)
			break;
		if (r == b_search) {
			tree[r].ob_state &= ~SELECTED;
			dlg_draw(tree, r);
			search();
			continue;
		}
		if (r == o_genre || r == o_country || r == o_lang || r == o_sort) {
			if (r == o_genre)
				button_text(b_genre, sizeof(b_genre), genre_lab[c_genre = pop_over(r, genre_lab, N(genre_lab), c_genre)]);
			else if (r == o_country)
				button_text(b_country, sizeof(b_country),
					    country_lab[c_country = pop_over(r, country_lab, N(country_lab), c_country)]);
			else if (r == o_lang)
				button_text(b_lang, sizeof(b_lang), lang_lab[c_lang = pop_over(r, lang_lab, N(lang_lab), c_lang)]);
			else
				button_text(b_sort, sizeof(b_sort), sort_lab[c_sort = pop_over(r, sort_lab, N(sort_lab), c_sort)]);
			dlg_draw(tree, r);
			continue;
		}
		if (r == o_up || r == o_dn) {
			int t = top + (r == o_up ? -ROWS : ROWS);
			if (t > nfound - ROWS)
				t = nfound - ROWS;
			top = t < 0 ? 0 : t;
			rows_draw();
			continue;
		}
		for (i = 0; i < ROWS; i++)
			if (r == o_rows[i])
				break;
		if (i < ROWS) {
			sel = top + i;
			rows_draw();
			if (!dbl)
				continue;
			r = o_play;	/* (a double click plays it) */
		}
		if ((r == o_add || r == o_play) && sel >= 0 && sel < nfound) {
			tree[r].ob_state &= ~SELECTED;
			dlg_draw(tree, r);
			if (r == o_add) {
				char s[64];
				add(found[sel].url, found[sel].name);
				snprintf(s, sizeof(s), "Added: %.40s", found[sel].name);
				say(s);
				continue;
			}
			snprintf(url, (size_t)un, "%s", found[sel].url);
			snprintf(name, (size_t)nn, "%s", found[sel].name);
			play = 1;
			break;
		}
		if (r == o_add || r == o_play) {
			tree[r].ob_state &= ~SELECTED;
			dlg_draw(tree, r);
			say("Choose a station first.");
		}
	}
	dlg_close();
	{
		char f[96];
		snprintf(f, sizeof(f), "%d,%d,%d,%d,%s", c_genre, c_country, c_lang, c_sort, q_text);
		settings_set_find(f);
	}
	return play;
}
