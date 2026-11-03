/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * main.c - EPlay's window (design D, Erez 3 Nov 2026: bar.c draws it),
 * its menus and keys. Files come from the file selector, the command
 * line, or the desktop (VA_START). The playlist comes in phase 4
 * (docs/DESIGN.md).
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "mbar.h"
#include "alert.h"
#include "av.h"
#include "crash.h"
#include "aboutbox.h"
#include "text.h"
#include "offdraw.h"
#include "click.h"
#include "play.h"
#include "settings.h"
#include "list.h"
#include "dd.h"
#include "popup.h"
#include "elog.h"
#include "snd.h"
#include "bar.h"
#include "art.h"

extern const char app_version[];

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id;

enum { C_ABOUT = 1, C_OPEN, C_QUIT, C_PLAY, C_STOP, C_SETTINGS, C_SHUFFLE, C_REPEAT, C_CLEAR, C_PREV, C_NEXT,
       C_REMOVE };

static const MBAR_ITEM m_desk[] = {
	{ "About EPlay...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "Open...", "^O", C_OPEN },
	{ "-", 0, 0 },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Remove from the list", "Delete", C_REMOVE },
	{ "Clear the list", 0, C_CLEAR },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_play[] = {
	{ "Play/Pause", "Space", C_PLAY },
	{ "Stop", 0, C_STOP },
	{ "Previous", 0, C_PREV },
	{ "Next", 0, C_NEXT },
	{ "-", 0, 0 },
	{ "Shuffle", 0, C_SHUFFLE },	/* (the playlist's: out of the bar, 56 columns) */
	{ "Repeat", 0, C_REPEAT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_options[] = {
	{ "Settings...", "^,", C_SETTINGS },
	{ 0, 0, 0 }
};
static const MBAR_TITLE bar[] = {
	{ " EPlay ", m_desk },
	{ " File ", m_file },
	{ " Edit ", m_edit },
	{ " Play ", m_play },
	{ " Options ", m_options },
	{ 0, 0 }
};

#define KIND (NAME | CLOSER | MOVER | INFO | SIZER | VSLIDE | UPARROW | DNARROW)

static MBAR mb;
static short wh = -1;
static GRECT work;
static int quit;
static char path[256], name[64], file[320];	/* the file selector's, and the file */
static int press_draw;		/* drawn while the button is held: press_update_begin's lock */

static void status(const char *s)
{
	static char info[90];
	char line[100];
	snprintf(info, sizeof(info), " %s", s);
	snprintf(line, sizeof(line), "EPLAY status: %s\n", s);
	nf_debug(line);			/* the rigs read it (no-op on hardware) */
	if (wh > 0)
		wind_set_str(wh, WF_INFO, info);
}

/* the window's parts again (bar.h's BP_...), through the AES's
   rectangles, within area (0: the parts' own). A small part (the times,
   each second; Play/Pause) is drawn off the screen and shown in one step
   (common's offdraw, where it can be: no flicker), else on it */
static void redraw(GRECT *area, int parts)
{
	GRECT r, a;
	int small = area == 0 && !(parts & (BP_TEXT | BP_LIST)), took;
	if (wh <= 0)
		return;
	if (!area) {
		bar_area(parts, &a);
		area = &a;
	}
	draw_forget();			/* (the About box draws with this handle too) */
	/* (while a button is held, TOS's AES holds BEG_UPDATE back until it
	   is let go: click.c's lock, which skips it there) */
	took = press_draw ? press_update_begin() : (wind_update(BEG_UPDATE), 1);
	graf_mouse(M_OFF, 0);
	wind_get(wh, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w && r.h) {
		if (rc_intersect(area, &r)) {
			if (small && off_begin(vdi_h, r.x, r.y, r.w, r.h)) {
				bar_draw(&r, parts);
				off_end();
			} else
				bar_draw(&r, parts);
		}
		wind_get(wh, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	graf_mouse(M_ON, 0);
	if (press_draw)
		press_update_end(took);
	else
		wind_update(END_UPDATE);
}

static void open_window(void)
{
	short x, y, w, h, ww, wh_;
	bar_size(&ww, &wh_);
	wind_calc(WC_BORDER, KIND, desk_x, desk_y, ww, wh_, &x, &y, &w, &h);
	if (w > desk_w)
		w = desk_w;
	x = desk_x + (desk_w - w) / 2;
	y = desk_y + (desk_h - h) / 3;
	wh = wind_create(KIND, desk_x, desk_y, desk_w, desk_h);
	if (wh <= 0)
		return;
	wind_set_str(wh, WF_NAME, " EPlay ");
	wind_open(wh, x, y, w, h);
	wind_get(wh, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	bar_layout(&work);
	av_window(wh, 1);		/* (an AV desktop drops files on it: VA_DRAGACCWIND) */
}

/* "Playing 2 of 6 · Falcon 49.1 kHz" (one file alone: no count) */
static void playing_status(char *st, int n)
{
	if (list_cur >= 0 && list_count() > 1)
		snprintf(st, (size_t)n, "Playing %d of %d  \xfa  %s", list_cur + 1, list_count(), play_out);
	else
		snprintf(st, (size_t)n, "Playing  \xfa  %s", play_out);
}

static int bar_due;		/* the bar to draw in pieces, a part a turn (2, 1; 0 none) */
static int open_due;		/* a file to open the next turn (file[]): a row's answer first */

static void play_file(const char *f)
{
	char err[160];
	if (!play_open(f, err, sizeof(err))) {
		char line[200];
		snprintf(line, sizeof(line), "EPLAY refused: %s\n", err);
		nf_debug(line);
		alert(1, "[1][%s][ OK ]", err);
		redraw(&work, BP_ALL);
		return;
	}
	play_start();
	art_begin(f);			/* (its picture made in the steps from now) */
	{
		char line[200];
		snprintf(line, sizeof(line), "EPLAY playing: %s | %s | %s\n", play_name, play_info, play_out);
		nf_debug(line);
	}
	{
		char st[100];
		playing_status(st, sizeof(st));
		status(st);
	}
	{
		/* the whole window for another file (or the same one elsewhere);
		   for the one shown, Play/Pause and the time (the speed rig's
		   "Play: let go": 150 ms drawn whole on a stock Falcon) */
		static char shown[200];
		char now[200];
		snprintf(now, sizeof(now), "%s|%s|%s|%ld|%ld", play_name, play_tags.title, play_via, play_kbps, play_hz);
		if (strcmp(now, shown)) {
			/* (the bar for another file: in the turns after this one, a
			   part a turn, as a job's slices: the 50 ms between event
			   calls) */
			strcpy(shown, now);
			bar_due = 2;
		} else
			redraw(0, BP_PLAY | BP_TIME);
	}
}

/* the bar drawn again in pieces after another file began (bar_due): its
   text and art, then Play/Pause with the times; one a turn */
static void bar_turn(void)
{
	if (bar_due == 2)
		redraw(0, BP_TEXT);
	else
		redraw(0, BP_PLAY | BP_TIME);
	bar_due--;
}

/* ---- the list (Erez, 3 Nov 2026: files added in bulk) ---- */

static int list_unread;		/* files added whose tags and length aren't read yet */
static void message(short *msg);
static void step(int playing);

/* the list's slider as the list and its top are */
static void sliders(void)
{
	int n = list_count(), rows = bar_rows();
	short size = n > rows ? (short)((long)rows * 1000 / n) : 1000;
	short pos = n > rows ? (short)((long)list_top * 1000 / (n - rows)) : 0;
	if (wh <= 0)
		return;
	wind_set(wh, WF_VSLSIZE, size, 0, 0, 0);
	wind_set(wh, WF_VSLIDE, pos, 0, 0, 0);
}

static void scroll_to(int top)
{
	int most = list_count() - bar_rows();
	if (top > most)
		top = most;
	if (top < 0)
		top = 0;
	if (top == list_top)
		return;
	list_top = top;
	sliders();
	redraw(0, BP_LIST);
}

static void show_row(int i)
{
	if (i < 0)
		return;
	if (i < list_top)
		scroll_to(i);
	else if (i >= list_top + bar_rows())
		scroll_to(i - bar_rows() + 1);
}

/* one row drawn again, through the AES's rectangles */
static void row_redraw(int i)
{
	GRECT a, r;
	if (wh <= 0 || i < list_top || i >= list_top + bar_rows())
		return;
	bar_row_area(i, &a);
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(wh, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w && r.h) {
		if (rc_intersect(&a, &r))
			bar_draw_row(&r, i);
		wind_get(wh, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

static void select_row(int i)
{
	int old = list_sel;
	if (i == old)
		return;
	list_sel = i;
	row_redraw(old);
	show_row(i);
	row_redraw(i);
}

static void play_index(int i)
{
	ENTRY *e = list_at(i);
	int old = list_cur;
	if (!e)
		return;
	list_cur = i;
	list_sel = i;
	snprintf(file, sizeof(file), "%s", e->path);
	show_row(i);
	row_redraw(old);
	row_redraw(i);
	open_due = 1;			/* (opened next turn: the press's answer is the rows) */
	redraw(0, BP_NAV);
}

/* MP3, WAV and AIFF by their names' ends (a folder's files: what to add) */
static int audio_name(const char *n)
{
	const char *d = strrchr(n, '.');
	char e[6];
	int i;
	if (!d || strlen(d) > 5)
		return 0;
	for (i = 0; d[i + 1] && i < 5; i++)
		e[i] = (char)(d[i + 1] | 0x20);
	e[i] = 0;
	return !strcmp(e, "mp3") || !strcmp(e, "wav") || !strcmp(e, "aif") || !strcmp(e, "aiff") ||
	       !strcmp(e, "aifc");
}

/* every MP3, WAV and AIFF in folder dir (ending in '\'), in name order:
   the first added's index, -1 if none */
static int add_folder(const char *dir)
{
	char mask[200], (*names)[14] = 0, one[200];
	static char found[200][14];
	DTA *old = Fgetdta(), dta;
	int k = 0, i, j, first = -1;
	names = found;
	snprintf(mask, sizeof(mask), "%s*.*", dir);
	Fsetdta(&dta);
	if (Fsfirst(mask, 0) == 0)
		do {
			if (audio_name(dta.d_fname) && k < 200)
				snprintf(names[k++], 14, "%s", dta.d_fname);
		} while (Fsnext() == 0);
	Fsetdta(old);
	for (i = 1; i < k; i++)		/* (a few hundred at most: an insertion sort) */
		for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
			char t[14];
			memcpy(t, names[j], 14);
			memcpy(names[j], names[j - 1], 14);
			memcpy(names[j - 1], t, 14);
		}
	for (i = 0; i < k; i++) {
		int at;
		snprintf(one, sizeof(one), "%s%s", dir, names[i]);
		at = list_add(one);
		if (at >= 0 && first < 0)
			first = at;
	}
	return first;
}

/* one name: a folder's files, or the file; its (first) index */
static int add_one(const char *f)
{
	long a = Fattrib(f, 0, 0);
	size_t n = strlen(f);
	if ((a >= 0 && (a & 0x10)) || (n && f[n - 1] == '\\')) {
		char dir[200];
		snprintf(dir, sizeof(dir), "%s%s", f, n && f[n - 1] == '\\' ? "" : "\\");
		return add_folder(dir);
	}
	return list_add(f);
}

/* names as VA_START has them (quoted where they have spaces): added; now 1
   plays the first of them at once, else it plays only if nothing plays */
static void add_names(const char *s, int now)
{
	char f[256];
	int first = -1, before = list_count();
	while ((s = av_arg(s, f, sizeof(f))) != 0) {
		int i = add_one(f);
		if (first < 0)
			first = i;
	}
	if (list_count() > before)
		list_unread = 1;
	sliders();
	if (list_count() != before)
		redraw(0, BP_LIST);
	if (first >= 0 && (now || (play_state() != PS_PLAYING && !play_paused())))
		play_index(first);
	else if (list_count() > before)
		redraw(0, BP_TEXT);	/* (Next may light) */
}

/* File > Add... (^O): a file, or with none chosen the whole folder; played
   if nothing plays */
static void open_file(void)
{
	short button = 0;
	char *e;
	long n;
	int i, before = list_count();
	if (!path[0])
		snprintf(path, sizeof(path), "%s*.*", app_dir());
	name[0] = 0;
	if (!fsel_exinput(path, name, &button, "Open a file (none: the folder)") || !button)
		return;
	e = strrchr(path, '\\');
	n = e ? e - path + 1 : 0;
	if (name[0]) {
		snprintf(file, sizeof(file), "%.*s%s", (int)n, path, name);
		i = list_add(file);
	} else {
		char dir[200];
		snprintf(dir, sizeof(dir), "%.*s", (int)n, path);
		i = add_folder(dir);
	}
	{
		char line[60];
		snprintf(line, sizeof(line), "EPLAY list: %d files\n", list_count());
		nf_debug(line);			/* (the rigs) */
	}
	if (list_count() > before)
		list_unread = 1;
	sliders();
	redraw(0, BP_LIST);
	if (i >= 0 && play_state() != PS_PLAYING && !play_paused())
		play_index(i);
	else
		redraw(0, BP_TEXT);
}

/* File > Clear the list: all but the one playing */
static void list_clear_but_playing(void)
{
	ENTRY keep, *e = list_at(list_cur);
	int playing = e && (play_state() == PS_PLAYING || play_paused());
	if (playing)
		keep = *e;
	list_clear();
	if (playing) {
		list_cur = list_add(keep.path);
		*list_at(list_cur) = keep;
	}
	sliders();
	redraw(0, BP_LIST | BP_TEXT);
}

/* the window made taller or shorter (its width is the bar's): whole
   rows of the list, three at the least */
static void sized(short w, short h)
{
	short x, y, ww, hh, bw, bh, rh = bar_row_h(), base, rows, t0, t1, t2, wk;
	(void)w;
	wind_get(wh, WF_CURRXYWH, &x, &y, &ww, &hh);
	bar_size(&bw, &bh);
	base = bh - 6 * rh;		/* the bar and the list's heading */
	wind_calc(WC_WORK, KIND, x, y, ww, h, &t0, &t1, &t2, &wk);
	rows = (short)((wk - base) / rh);
	if (rows < 3)
		rows = 3;
	wind_calc(WC_BORDER, KIND, work.x, work.y, work.w, base + rows * rh, &t0, &t1, &t2, &hh);
	wind_set(wh, WF_CURRXYWH, x, y, ww, hh);
	wind_get(wh, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	bar_layout(&work);
	scroll_to(list_top);
	sliders();
}

static int rows_from = -1;	/* the list's rows from this one on to draw again, two a turn */

static void rows_turn(void)
{
	int k;
	for (k = 0; k < 2 && rows_from >= 0; k++) {
		row_redraw(rows_from);
		if (++rows_from >= list_top + bar_rows())
			rows_from = -1;
	}
}

/* the selected file out of the list (the one playing stopped first) */
static void list_remove_sel(void)
{
	int i = list_sel;
	if (i < 0)
		return;
	if (i == list_cur) {
		play_log_stop();
		play_stop();
		status("Stopped.");
	}
	list_remove(i);
	list_sel = i < list_count() ? i : list_count() - 1;
	if (list_top > 0 && list_top + bar_rows() > list_count())
		scroll_to(list_count() - bar_rows());	/* (the whole list: one draw) */
	else
		rows_from = i;		/* the rows from it on, two a turn */
	sliders();
	redraw(0, BP_NAV);
}

static void command(int c);

/* a row's menu (the guidelines' 3.3): its double click's action first, then
   what the menu bar has for it, with the keys */
static void row_menu(short x, short y)
{
	static const char *const lab[] = { "  Play                  Return ", "-",
					   "  Remove from the list  Delete ", "  Clear the list " };
	switch (popup(x, y, lab, 4)) {
	case 0:
		play_index(list_sel);
		break;
	case 2:
		command(C_REMOVE);
		break;
	case 3:
		command(C_CLEAR);
		break;
	}
}

/* the list's keys: Up and Down move the selection, Return plays it, Delete
   takes it out of the list. 1 when the key was one of them */
static int list_keys(short kr)
{
	short scan = (kr >> 8) & 0xff;
	int n = list_count();
	if (!n)
		return 0;
	switch (scan) {
	case 0x48:			/* Up */
		select_row(list_sel > 0 ? list_sel - 1 : 0);
		return 1;
	case 0x50:			/* Down */
		select_row(list_sel + 1 < n ? list_sel + 1 : n - 1);
		return 1;
	case 0x1c:			/* Return */
	case 0x72:			/* Enter */
		if (list_sel >= 0)
			play_index(list_sel);
		return 1;
	case 0x53:			/* Delete */
		list_remove_sel();
		return 1;
	case 0x52:			/* Insert: the selected row's menu (3.3) */
		if (list_sel >= 0) {
			GRECT r;
			show_row(list_sel);
			bar_row_area(list_sel, &r);
			row_menu(r.x + 4 * bar_row_h(), r.y + r.h);
		}
		return 1;
	}
	return 0;
}

static void idle_step(void)
{
	step(play_state() == PS_PLAYING);
}

/* a press on the list: the row selected at once; a double click plays it
   (guidelines 5.8: the second press's release) */
static void list_press(short mx, short my, int right)
{
	int i = bar_row_at(my), dbl;
	GRECT r;
	short dx, dy;
	if (i < 0)
		return;
	dbl = press_double((short)i, mx, my);
	select_row(i);
	if (right) {			/* (a right press: the row's menu) */
		row_menu(mx, my);
		return;
	}
	bar_row_area(i, &r);
	if (press_track(&r, mx, my, idle_step, message, &dx, &dy) == PRESS_IN && dbl)
		play_index(i);
}

static void command(int c)
{
	switch (c) {
	case C_ABOUT:
		dlg_about();
		break;
	case C_OPEN:
		open_file();
		break;
	case C_QUIT:
		quit = 1;
		break;
	case C_SETTINGS:
		settings_dialog();
		break;
	case C_CLEAR:
		list_clear_but_playing();
		break;
	case C_REMOVE:
		list_remove_sel();
		break;
	case C_PREV:
		if (list_cur > 0)
			play_index(list_cur - 1);
		break;
	case C_NEXT:
		if (list_cur + 1 < list_count())
			play_index(list_cur + 1);
		break;
	case C_PLAY:
		if (play_state() == PS_PLAYING) {
			/* Play/Pause: the sound stopped where it is, the DSP given back */
			char line[60];
			play_pause();
			elog_save();
			snprintf(line, sizeof(line), "EPLAY paused at %ld ms\n", play_ms());
			nf_debug(line);			/* (the rigs) */
			status("Paused.");
			redraw(0, BP_PLAY | BP_TIME);
		} else if (play_paused()) {
			char err[160], st[100];
			if (!play_resume(err, sizeof(err))) {
				alert(1, "[1][%s][ OK ]", err);
				status("Stopped.");
			} else {
				playing_status(st, sizeof(st));
				status(st);
				snprintf(st, sizeof(st), "EPLAY resumed at %ld ms\n", play_ms());
				nf_debug(st);		/* (the rigs) */
			}
			redraw(0, BP_PLAY | BP_TIME);
		} else if (play_state() != PS_PLAYING && play_is_open()) {
			/* (opened at a place the position bar went to, stopped) */
			char st[100];
			play_start();
			playing_status(st, sizeof(st));
			status(st);
			redraw(0, BP_PLAY | BP_TIME);
		} else if (play_state() != PS_PLAYING && list_sel >= 0)
			play_index(list_sel);
		else if (play_state() != PS_PLAYING && list_count())
			play_index(list_cur >= 0 ? list_cur : 0);
		break;
	case C_STOP:
		if (play_state() == PS_PLAYING) {
			play_log_stop();
			play_stop();
			elog_save();
			status("Stopped.");
			redraw(0, BP_PLAY | BP_TIME);
		} else if (play_paused()) {
			play_stop();		/* (paused: the place forgotten, Play from the start) */
			status("Stopped.");
			redraw(0, BP_PLAY | BP_TIME);
		}
		break;
	}
}

static int inside(const GRECT *b, short x, short y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static void message(short *msg);

/* the music fed between events; the window's time or end shown */
/* the turn's start (after its event): the music's queue is filled with
   what is left of 20 ms after the turn's action, so that an action and the
   reading don't add up in one stretch (the position bar's release on a
   stock Falcon: 25 ms of seek and 20 of reading) */
static unsigned long turn_t0;
static int next_due;		/* a file played to its end: the list's next, next turn */

static void step(int playing)
{
	unsigned long used = tos_ms() - turn_t0;
	if (playing && play_step(used >= 20 ? 0 : (int)(20 - used))) {
		if (play_state() != PS_PLAYING) {
			status(play_out[0] == 'T' ? play_out : "Played to the end.");
			redraw(0, BP_PLAY | BP_TIME);
			if (play_out[0] != 'T' && list_cur >= 0 && list_cur + 1 < list_count())
				next_due = 1;	/* (the next file: at the loop's turn, not inside a press) */
		} else if (bar_drag < 0) {
			redraw(0, BP_TIME);
		}
	}
}

/* a button of the bar pressed (guidelines 5.8): drawn pressed at once, its
   action on the release over it, nothing if let go elsewhere; drawn
   pressed again as the pointer comes back, as GEM's dialog buttons are.
   The music is fed all the while (a loop of its own, not evnt_button's
   wait). 1: let go over it */
static int track(int hit, int part)
{
	short msg[8];
	AEVENT ev;
	GRECT b;
	int in = 1;
	bar_area(part, &b);
	bar_held = hit;
	press_draw = 1;
	redraw(0, part);
	press_draw = 0;
	while (!quit) {
		short which;
		int playing = play_state() == PS_PLAYING;
		evnt_set_m1(in, b.x, b.y, b.w, b.h);
		which = evnt_multi_(MU_MESAG | MU_BUTTON | MU_M1 | MU_TIMER, 1, 1, 0, play_wait_ms(), msg, &ev);
		turn_t0 = tos_ms();
		if (which & MU_MESAG)
			message(msg);
		if (which & MU_BUTTON) {
			in = inside(&b, ev.mx, ev.my);
			break;
		}
		if (which & MU_M1) {
			in = !in;
			bar_held = in ? hit : BH_NONE;
			press_draw = 1;
			redraw(0, part);
			press_draw = 0;
		}
		step(playing);
	}
	bar_held = BH_NONE;
	/* (Play and Stop draw the button again as they act) */
	if (!in || quit)
		redraw(0, part);
	return in && !quit;
}

static long seek_to = -1;		/* the position bar let go: where, for the next turn */

/* the position bar pressed (or dragged): the time shown follows the
   pointer, and on the release the music goes there */
static void drag_pos(short mx, short my)
{
	short msg[8];
	AEVENT ev;
	long len = play_len_ms();
	int at;
	bar_drag = bar_pos_at(mx);
	press_draw = 1;
	redraw(0, BP_TIME);
	press_draw = 0;
	while (!quit) {
		short which;
		int playing = play_state() == PS_PLAYING;
		evnt_set_m1(1, mx, my, 1, 1);
		which = evnt_multi_(MU_MESAG | MU_BUTTON | MU_M1 | MU_TIMER, 1, 1, 0, play_wait_ms(), msg, &ev);
		turn_t0 = tos_ms();
		if (which & MU_MESAG)
			message(msg);
		if (which & MU_BUTTON)
			break;
		if (which & MU_M1) {
			mx = ev.mx;
			my = ev.my;
			if (bar_pos_at(mx) != bar_drag) {
				bar_drag = bar_pos_at(mx);
				press_draw = 1;
				redraw(0, BP_TIME);
				press_draw = 0;
			}
		}
		step(playing);
	}
	at = bar_drag;
	if (quit)
		return;
	/* the release shows where it goes; the going there is the next turn's
	   (5.8: the action after the release in a stretch of its own) */
	seek_to = len / 1000 * at + len % 1000 * at / 1000;
	redraw(0, BP_TIME);
}

/* the volume pressed (or dragged): it changes as the pointer moves, as a
   slider does (Erez, 3 Nov 2026: "like the progress bar"), and is kept */
static void volume_to(int v)
{
	if (v == play_volume())
		return;
	play_set_volume(v);
	settings_set_volume(play_volume());
	redraw(0, BP_VOL);
}

static void drag_vol(short mx, short my)
{
	short msg[8];
	AEVENT ev;
	volume_to(bar_vol_at(mx));
	while (!quit) {
		short which;
		int playing = play_state() == PS_PLAYING;
		evnt_set_m1(1, mx, my, 1, 1);
		which = evnt_multi_(MU_MESAG | MU_BUTTON | MU_M1 | MU_TIMER, 1, 1, 0, play_wait_ms(), msg, &ev);
		turn_t0 = tos_ms();
		if (which & MU_MESAG)
			message(msg);
		if (which & MU_BUTTON)
			break;
		if (which & MU_M1) {
			mx = ev.mx;
			my = ev.my;
			volume_to(bar_vol_at(mx));
		}
		step(playing);
	}
	{
		char line[40];
		snprintf(line, sizeof(line), "EPLAY volume %d\n", play_volume());
		nf_debug(line);			/* (the speed rig's) */
	}
}

/* the place the position bar was let go at, gone to (the turn after) */
static void do_seek(void)
{
	char err[160];
	long to = seek_to;
	seek_to = -1;
	bar_drag = -1;
	if (!play_seek(to, err, sizeof(err))) {
		alert(1, "[1][%s][ OK ]", err);
		status("Stopped.");
		redraw(0, BP_PLAY | BP_TIME);
		return;
	} else {
		char line[60];
		snprintf(line, sizeof(line), "EPLAY went to %ld ms\n", play_ms());
		nf_debug(line);			/* (the speed rig's) */
	}
	/* (no drawing: the time shown since the release is where it went, and
	   the next second's draws it on; with it the turn was 55 ms on a stock
	   Falcon) */
}

static void turn(int nested);

/* a turn for GEM inside the album art's decode (art.c, every 40 ms): the
   whole of the loop's, but for the art's own step; 0 when EPlay quits */
static int art_turn(void)
{
	turn(1);
	return !quit;
}

static void message(short *msg)
{
	const char *s;
	switch (av_message(msg, &s)) {
	case AV_START:
		/* the desktop's "open": the first played now, the rest added */
		add_names(s, 1);
		return;
	case AV_NONE:
		break;
	default:
		return;
	}
	{
		DROP d;
		switch (dd_message(msg, DD_WANT_FILES, &d)) {
		case DD_FILES:
			add_names(d.data, 0);	/* (dropped: added; played if nothing plays) */
			dd_done(&d);
			return;
		case DD_NONE:
			break;
		default:
			dd_done(&d);
			return;
		}
	}
	switch (msg[0]) {
	case MN_SELECTED:
		command(mbar_command(&mb, msg[4]));
		menu_tnormal(mb.tree, msg[3], 1);
		break;
	case WM_REDRAW: {
		GRECT r;
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		redraw(&r, BP_ALL);
		break;
	}
	case WM_TOPPED:
		wind_set(msg[3], WF_TOP, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
		quit = 1;
		break;
	case WM_MOVED:
		wind_set(msg[3], WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
		wind_get(wh, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
		bar_layout(&work);
		break;
	case WM_SIZED:
		sized(msg[6], msg[7]);
		break;
	case WM_ARROWED:
		switch (msg[4]) {
		case WA_UPPAGE:
			scroll_to(list_top - bar_rows());
			break;
		case WA_DNPAGE:
			scroll_to(list_top + bar_rows());
			break;
		case WA_UPLINE:
			scroll_to(list_top - 1);
			break;
		case WA_DNLINE:
			scroll_to(list_top + 1);
			break;
		}
		break;
	case WM_VSLID:
		scroll_to((int)((long)msg[4] * (list_count() - bar_rows()) / 1000));
		break;
	case AP_TERM:
		quit = 1;
		break;
	}
}

/* a crash: the sound stopped and the matrix put back before EPlay goes */
static void crash_quit(const char *what)
{
	(void)what;
	play_stop();
	snd_exit();
	bar_colours_back();
}

/* one turn of the event loop: an event, or none within the timer (none at
   all inside the album art's decode: nested), and the music fed */
static void turn(int nested)
{
	AEVENT ev;
	short msg[8];
	short which;
	int playing = play_state() == PS_PLAYING;
	/* (either button: 0x101, 3, 0 as popup.c asks; the right one opens a
	   row's menu) */
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 0x101, 3, 0,
			    nested || art_pending() || seek_to >= 0 || next_due || open_due || bar_due ||
			    rows_from >= 0 ? 0 :
			    list_unread ? 20 : play_wait_ms(), msg, &ev);
	turn_t0 = tos_ms();
	if (next_due && !nested) {
		next_due = 0;
		play_index(list_cur + 1);
	} else if (open_due && !nested) {
		open_due = 0;
		play_file(file);
	} else if (bar_due && !nested)
		bar_turn();
	else if (rows_from >= 0 && !nested)
		rows_turn();
	if (seek_to >= 0 && !nested) {
		do_seek();
		step(play_state() == PS_PLAYING);
	}
	if (which & MU_MESAG)
		message(msg);
	if (which & MU_KEYBD) {
		short a = ev.kreturn & 0xff;
		if (key_ctrl(ev.kstate, ev.kreturn)) {
			if (a == 15 || (a | 0x20) == 'o')
				command(C_OPEN);
			else if (a == 17 || a == 21 || (a | 0x20) == 'q' || (a | 0x20) == 'u')
				command(C_QUIT);
			else if (((ev.kreturn >> 8) & 0xff) == 0x33)	/* ^, (the comma's key) */
				command(C_SETTINGS);
		} else if (a == ' ')
			command(C_PLAY);	/* Play/Pause */
		else if (!key_ctrl(ev.kstate, ev.kreturn) && list_keys(ev.kreturn))
			;
		else if (a == '+' || a == '-') {
			/* the volume a step of 3 dB (two of the bar's) */
			char line[40];
			volume_to(play_volume() + (a == '+' ? 2 : -2));
			snprintf(line, sizeof(line), "EPLAY volume %d\n", play_volume());
			nf_debug(line);		/* (the speed rig's) */
		}
	}
	if ((which & MU_BUTTON) && ev.mbutton == 2 && bar_hit(ev.mx, ev.my) == BH_LIST)
		list_press(ev.mx, ev.my, 1);
	if ((which & MU_BUTTON) && ev.mbutton == 1) {
		/* (Play/Pause only where it would act: with a file; the
		   playlist's controls come with phase 4) */
		int hit = bar_hit(ev.mx, ev.my);
		if (hit == BH_PLAY && (play_name[0] || file[0]) && track(BH_PLAY, BP_PLAY))
			command(C_PLAY);	/* Play/Pause */
		else if (hit == BH_POS && play_name[0] && play_len_ms() > 0)
			drag_pos(ev.mx, ev.my);
		else if (hit == BH_VOL)
			drag_vol(ev.mx, ev.my);
		else if (hit == BH_PREV && list_cur > 0 && track(BH_PREV, BP_TEXT))
			command(C_PREV);
		else if (hit == BH_NEXT && list_cur + 1 < list_count() && track(BH_NEXT, BP_TEXT))
			command(C_NEXT);
		else if (hit == BH_LIST)
			list_press(ev.mx, ev.my, 0);
	}
	step(playing);
}

const long cpu_need = 30;
const char cpu_alert[] = "[3][EPlay needs a Falcon.][ Quit ]";
int app_main(void);
int app_main(void)
{
	short work_out[57], d;
	const char *why;

	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	vdi_h = graf_handle(&gl_wchar, &gl_hchar, &d, &d);
	cw = gl_wchar;
	ch = gl_hchar;
	{
		short aes_h = vdi_h;
		vdi_h = v_opnvwk_(aes_h, work_out);
		if (vdi_h <= 0) {
			form_alert(1, "[3][EPlay can't open its screen:|the VDI has no memory for it.][ Quit ]");
			appl_exit();
			return 1;
		}
	}
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	vq_extnd(vdi_h, 1, work_out);
	planes = work_out[4];
	if (!screen_check("EPlay", scr_w)) {
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	vst_alignment(vdi_h, 0, 5);
	text_font(vdi_h, 1, cw, ch);		/* (the system font: common's fast text) */
	bar_colours();
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	crash_install(app_dir(), "EPlay", app_version);
	crash_cleanup = crash_quit;
	av_init("EPLAY");
	/* the VDI's ST RAM block taken now, not at the first draw that wants
	   it: Erez's 0.12, DFB1X off, took it while the first file played,
	   just past that file's queue, and the free ST RAM stayed split in two
	   (the queues after it 1.28 MB, the first 1.94) */
	st_ram(1);
	if (!snd_init(&why)) {
		nf_debug("EPLAY no sound: ");
		nf_debug(why);
		nf_debug("\n");
		alert(1, "[3][%s][ Quit ]", why);
		av_exit();
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	settings_load();		/* (after snd_init: what the machine has) */
	play_set_volume(settings_volume());
	if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h)) {
		form_alert(1, "[3][EPlay: not enough memory.][ Quit ]");
		snd_exit();
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	/* Shuffle and Repeat: the playlist's (phase 4), grey until then */
	menu_ienable(mb.tree, mbar_object(&mb, C_SHUFFLE), 0);
	menu_ienable(mb.tree, mbar_object(&mb, C_REPEAT), 0);
	menu_bar(mb.tree, 1);
	graf_mouse(ARROW, 0);
	open_window();
	art_init(bar_art_side());
	if (!list_init()) {
		alert(1, "[3][EPlay: not enough memory.][ Quit ]");
		quit = 1;
	}
	{
		char line[60];
		snprintf(line, sizeof(line), "EPLAY external clock: %ld Hz\n", snd_ext_rate);
		nf_debug(line);
	}
	elog("EPlay %s; _MCH $%08lX, _CPU %ld, _SND $%02lX; the FDI's driver %s", app_version,
	     get_cookie(0x5F4D4348L), get_cookie(0x5F435055L), get_cookie(0x5F534E44L),
	     snd_fdi ? "answers ($4711)" : "doesn't answer");
	/* (TT RAM is the DFB1's: Erez's logs of 0.11 had a queue of 1.9 MB,
	   8 MB with it, and nothing else to say whether it was on) */
	{
		long st = (long)Mxalloc(-1L, 0), tt = (long)Mxalloc(-1L, 1);
		elog("free memory, the largest blocks: ST RAM %ld KB, TT RAM %ld KB", st > 0 ? st / 1024 : 0,
		     tt > 0 ? tt / 1024 : 0);
	}
	elog("the DSP port's clock: %ld Hz at prescale 1 (a clock of %ld Hz, timed by %s); taken as %ld Hz",
	     snd_ext_raw, snd_ext_clock, snd_ta_busy ? "the 200 Hz clock: Timer A in use" : "Timer A", snd_ext_rate);
	elog("the DSP port's GPIO: direction %d, data %d%s%s", snd_gpio_dir, snd_gpio_data,
	     snd_ext_note[0] ? "; tried " : "", snd_ext_note);
	elog("Settings: output %s, quality %s", settings_output_name(settings_output()),
	     settings_quality_name(settings_quality()));
	if (snd_ext_rate)
		status(snd_ext_rate == 44100 ? "An external clock at 44.1 kHz (the FDI)." :
					       "An external clock at 48 kHz (the FDI).");
	else if (snd_fdi)
		status("The FDI's driver is there, but no clock came from it: see EPLAY.LOG.");
	else
		status("No external clock: the Falcon's own rates.");
	crash_offer("EPlay", "in EPlay's folder");
	{
		const char *cmd = av_command();	/* started with files */
		if (*cmd)
			add_names(cmd, 1);
	}

	while (!quit) {
		turn(0);
		if (art_pending() && art_step(art_turn))
			redraw(0, BP_ART);	/* (the album art made: shown) */
		else if (list_unread) {
			/* one file's tags and length, then its row */
			int row;
			if (list_step(&row))
				row_redraw(row);
			else
				list_unread = 0;
		}
	}

	play_log_stop();
	play_stop();
	settings_flush();		/* (the volume, if it changed) */
	snd_exit();
	elog("quit");
	elog_save();
	menu_bar(mb.tree, 0);
	bar_colours_back();
	if (wh > 0) {
		av_window(wh, 0);
		wind_close(wh);
		wind_delete(wh);
	}
	av_exit();
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
