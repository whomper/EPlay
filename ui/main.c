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
#include <stdlib.h>
#include "ui.h"
#include "mbar.h"
#include "alert.h"
#include "av.h"
#include "crash.h"
#include "aboutbox.h"
#include "dialog.h"
#include "help.h"
#include "text.h"
#include "offdraw.h"
#include "click.h"
#include "play.h"
#include "settings.h"
#include "list.h"
#include "dd.h"
#include "popup.h"
#include "filepick.h"
#include "m3u.h"
#include "elog.h"
#include "snd.h"
#include "bar.h"
#include "art.h"

extern const char app_version[];

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id;

enum { C_ABOUT = 1, C_OPEN, C_QUIT, C_PLAY, C_STOP, C_SETTINGS, C_SHUFFLE, C_REPEAT, C_CLEAR, C_PREV, C_NEXT,
       C_REMOVE, C_MINI, C_SAVE, C_GUIDE };

static const MBAR_ITEM m_desk[] = {
	{ "About EPlay...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "Open...", "^O", C_OPEN },
	{ "Save playlist...", "^S", C_SAVE },
	{ "-", 0, 0 },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Remove from the list", "Delete", C_REMOVE },
	{ "Clear the list", 0, C_CLEAR },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_view[] = {
	{ "Mini player", 0, C_MINI },	/* (the bar alone: Erez, 3 Nov 2026) */
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_play[] = {
	{ "Play/Pause", "Space", C_PLAY },
	{ "Stop", "ClrHome", C_STOP },
	{ "Previous", "\x01\x04", C_PREV },
	{ "Next", "\x01\x03", C_NEXT },
	{ "-", 0, 0 },
	{ "Shuffle", 0, C_SHUFFLE },	/* (out of the bar, 56 columns: Erez, 3 Nov 2026) */
	{ "Repeat: off", 0, C_REPEAT },	/* (its text order_repeat_name's: the same length each) */
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_options[] = {
	{ "Settings...", "^,", C_SETTINGS },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_help[] = {
	{ "EPlay guide", "Help", C_GUIDE },	/* (eplay.hyp: the guidelines, 4.3) */
	{ 0, 0, 0 }
};
static const MBAR_TITLE bar[] = {
	{ " EPlay ", m_desk },
	{ " File ", m_file },
	{ " Edit ", m_edit },
	{ " View ", m_view },
	{ " Play ", m_play },
	{ " Options ", m_options },
	{ " Help ", m_help },
	{ 0, 0 }
};

#define KIND_FULL (NAME | CLOSER | MOVER | INFO | SIZER | VSLIDE | UPARROW | DNARROW)
#define KIND_MINI (NAME | CLOSER | MOVER)	/* the mini player: the bar alone */
#define KIND (bar_mini ? KIND_MINI : KIND_FULL)

static MBAR mb;

/* the Play menu as Shuffle and Repeat are: Shuffle ticked, Repeat's text
   ("Repeat: off", "all", "one": the item's own, the same length) */
static void play_menu_shown(void)
{
	short o = mbar_object(&mb, C_REPEAT);
	menu_icheck(mb.tree, mbar_object(&mb, C_SHUFFLE), list_order.shuffle);
	if (o > 0)
		memcpy((char *)mb.tree[o].ob_spec + 2, order_repeat_name(list_order.repeat), 11);
}
static short wh = -1;
static GRECT work;
static int quit;
static char file[320];		/* the file playing */
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

/* the window: centred, or at (at_x, at_y) when one went before it (the
   mini player switched: its corner stays where it was) */
static short at_x = -1, at_y = -1;

static void open_window(void)
{
	short x, y, w, h, ww, wh_;
	bar_size(&ww, &wh_);
	wind_calc(WC_BORDER, KIND, desk_x, desk_y, ww, wh_, &x, &y, &w, &h);
	if (w > desk_w)
		w = desk_w;
	x = desk_x + (desk_w - w) / 2;
	y = desk_y + (desk_h - h) / 3;
	if (at_x >= 0) {
		x = at_x + w > desk_x + desk_w ? desk_x + desk_w - w : at_x;
		y = at_y + h > desk_y + desk_h ? desk_y + desk_h - h : at_y;
	}
	wh = wind_create(KIND, desk_x, desk_y, desk_w, desk_h);
	if (wh <= 0)
		return;
	wind_set_str(wh, WF_NAME, " EPlay ");
	wind_open(wh, x, y, w, h);
	wind_get(wh, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	bar_layout(&work);
	av_window(wh, 1);		/* (an AV desktop drops files on it: VA_DRAGACCWIND) */
}

/* "Playing 2 of 6 · Falcon 49.1 kHz" (one file alone: no count; "of 256,
   the list full" while files were left out for want of memory) */
static void playing_status(char *st, int n)
{
	if (list_cur >= 0 && list_count() > 1)
		snprintf(st, (size_t)n, "Playing %d of %d%s  \xfa  %s", list_cur + 1, list_count(),
			 list_full ? ", the list full" : "", play_out);
	else
		snprintf(st, (size_t)n, "Playing  \xfa  %s", play_out);
}


/* a picture's time, from when it is due to its last piece drawn, to the
   rigs' console ("EPLAY picture start 85 ms"): the guidelines' Speed, "A
   rule never costs speed", drawing whole against drawing in pieces */
static unsigned long pic_t0, pic_in, input_ms;	/* input_ms: the last key or click's turn */
static const char *pic_name;
static void pic_begin(const char *n)
{
	if (!pic_name) {
		pic_name = n;
		pic_t0 = tos_ms();
		pic_in = input_ms && pic_t0 - input_ms < 2000 ? input_ms : pic_t0;
	}
}

static void pic_end(void)
{
	if (pic_name) {
		char b[64];
		unsigned long now = tos_ms();
		snprintf(b, sizeof(b), "EPLAY picture %s %lu ms, %lu from the input\n", pic_name, now - pic_t0,
			 now - pic_in);
		nf_debug(b);
		pic_name = 0;
	}
}
#ifdef AES_PROFILE
/* the profiled build's: a picture's pieces timed to some 26 us, by the
   200 Hz count and Timer C's count down within its 5 ms (TOS's: 192 at
   2457.6 kHz / 64), to the rigs' console as "EPLAY part NAME 12.3 ms"
   (the hub's speed rig, 5 Nov 2026: which piece of a row's picture grew) */
static unsigned long part_us_now;
static long part_read(void)
{
	static volatile unsigned long *volatile hz_at = (volatile unsigned long *)0x4baL;	/* (through a variable: */
	volatile unsigned long *hz = hz_at;				/* GCC's bounds on a low address) */
	volatile unsigned char *tcdr = (volatile unsigned char *)0xfffffa23L;
	unsigned long a, b;
	unsigned char c;
	do {
		a = *hz;
		c = *tcdr;
		b = *hz;
	} while (a != b);
	part_us_now = a * 5000UL + (c && c <= 192 ? (192UL - c) * 5000UL / 192 : 0);
	return 0;
}
static unsigned long part_us(void)
{
	Supexec(part_read);
	return part_us_now;
}
static unsigned long part_from;
#define PART_T0() (part_from = part_us())
#define PART(n) do { \
		char pb_[48]; \
		unsigned long pn_ = part_us(); \
		snprintf(pb_, sizeof(pb_), "EPLAY part %s %lu.%lu ms\n", (n), (pn_ - part_from) / 1000, \
			 (pn_ - part_from) / 100 % 10); \
		nf_debug(pb_); \
		part_from = part_us(); \
	} while (0)
#else
#define PART_T0() ((void)0)
#define PART(n) ((void)0)
#endif

static int open_due;		/* a file to open the next turn (file[]): a row's answer first */
static int shown_due;		/* what plays to draw whole the next turn: 2 the bar, 1 Play/Pause and
				   the times (the open's turn is the file's: drawing and reading in
				   turns of their own, each whole) */

/* what plays shown: the info line, and the bar (whole: 1) or Play/Pause
   with the times; drawn from start to end, no event call between (Erez,
   4 Nov 2026: drawing is not a job to slice) */
static void playing_drawn(int whole)
{
	static char via_shown[8];
	char st[100];
	PART_T0();
	playing_status(st, sizeof(st));
	status(st);
	PART("info line");
	if (whole) {
		redraw(0, BP_LEFT);
		PART("left");
	}
	/* the output's badge and the volume only when where it plays changed
	   (Settings' Output, then Play: Erez, 5 Nov 2026), not for each file:
	   nothing there is the file's (the hub's speed rig, 5 Nov 2026: a
	   row played 10 ms slower on TOS 4.04 than 0.26 with it drawn each
	   time) */
	if (strcmp(via_shown, play_via)) {
		redraw(0, BP_RIGHT);
		PART("right");
	}
	snprintf(via_shown, sizeof(via_shown), "%s", play_via);
	redraw(0, BP_PLAY | BP_TIME);
	PART("play and times");
}

/* the file played: opened (play.c), started; its art the step's (play_file) */
static void play_now(const char *f)
{
	char err[160];
	int opened;
	PART_T0();
	opened = play_open(f, err, sizeof(err));
	PART("the file opened");
	if (!opened) {
		char line[200];
		snprintf(line, sizeof(line), "EPLAY refused: %s\n", err);
		nf_debug(line);
		alert(1, "[1][%s][ OK ]", err);
		redraw(&work, BP_ALL);
		return;
	}
	play_start();
	PART("started");
	{
		char line[200];
		snprintf(line, sizeof(line), "EPLAY playing: %s | %s | %s\n", play_name, play_info, play_out);
		nf_debug(line);
	}
	{
		/* the whole window for another file (or the same one elsewhere);
		   for the one shown, Play/Pause and the time */
		static char shown[200];
		char now[200];
		snprintf(now, sizeof(now), "%s|%s|%s|%ld|%ld", play_name, play_tags.title, play_via, play_kbps, play_hz);
		pic_begin("start");
		if (strcmp(now, shown)) {
			strcpy(shown, now);
			shown_due = 2;
		} else
			shown_due = 1;
	}
}

/* ---- the list (Erez, 3 Nov 2026: files added in bulk) ---- */

static unsigned long turn_t0;		/* (the turn's start: its time, the steps') */
static int list_unread;		/* files added whose tags and length aren't read yet */
static void message(short *msg);
static void step(int playing);

/* the list's slider as the list and its top are */
static void sliders(void)
{
	int n = list_count(), rows = bar_rows();
	if (bar_mini)
		return;
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
	if (!bar_rows())
		return;			/* (the mini player: no list shown) */
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
	if (i < 0 || !bar_rows())
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
	order_played(&list_order, i);
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

static void feed_only(void);

/* every MP3, WAV and AIFF in folder dir (ending in '\\') and its subfolders
   (Erez, 4 Nov 2026: an album collection added at once), eight deep at
   most; the music fed meanwhile */
static void walk(const char *dir, int depth)
{
	char mask[200], one[200];
	DTA *old = Fgetdta(), dta;
	int k = 0;
	snprintf(mask, sizeof(mask), "%s*.*", dir);
	Fsetdta(&dta);
	if (Fsfirst(mask, 0x10) == 0)
		do {
			if (++k % 16 == 0)
				feed_only();
			if (list_full)
				break;
			if (dta.d_attrib & 0x10) {
				if (dta.d_fname[0] != '.' && depth < 8) {
					snprintf(one, sizeof(one), "%s%s\\", dir, dta.d_fname);
					walk(one, depth + 1);
					Fsetdta(&dta);	/* (this folder's search again) */
				}
			} else if (audio_name(dta.d_fname)) {
				snprintf(one, sizeof(one), "%s%s", dir, dta.d_fname);
				list_add(one);
			}
		} while (Fsnext() == 0);
	Fsetdta(old);
}

/* rows from to the end in path order: a shell sort, the records swapped
   (the names stay where they are; a folder's walk comes nearly sorted, a
   tree of them not: an insertion sort's n squared would be seconds) */
static void sort_from(int from)
{
	static const int gaps[] = { 701, 301, 132, 57, 23, 10, 4, 1 };
	int n = list_count(), g, i, j;
	for (g = 0; g < 8; g++) {
		int h = gaps[g];
		for (i = from + h; i < n; i++) {
			ENTRY t = *list_at(i);
			for (j = i; j >= from + h && strcmp(list_at(j - h)->path, t.path) > 0; j -= h)
				*list_at(j) = *list_at(j - h);
			*list_at(j) = t;
		}
	}
}

/* folder dir (ending in '\\') and its subfolders' music, in path order:
   the first added's index, -1 if none */
static int add_folder(const char *dir)
{
	int before = list_count();
	walk(dir, 0);
	if (list_count() > before + 1)
		sort_from(before);
	return list_count() > before ? before : -1;
}

/* .M3U (m3u.h): its files added, in its order; the first's index */
static int m3u_name(const char *f)
{
	const char *d = strrchr(f, '.');
	return d && (d[1] | 0x20) == 'm' && d[2] == '3' && (d[3] | 0x20) == 'u' && !d[4];
}

static void m3u_add(const char *path, void *ctx)
{
	(void)ctx;
	list_add(path);
}

/* (read a piece at a time, a line cut by a piece's end carried into the
   next: a playlist of 1,000 files saved with its #EXTINF lines is about
   80 KB, more than one piece) */
#define PL_PIECE 16384L
static int add_playlist(const char *f)
{
	char dir[200], *e;
	long h = Fopen(f, 0), n, have = 0;
	char *buf;
	int before = list_count();
	if (h < 0)
		return -1;
	buf = malloc(PL_PIECE);
	if (!buf) {
		Fclose((short)h);
		return -1;
	}
	snprintf(dir, sizeof(dir), "%s", f);
	e = strrchr(dir, '\\');
	if (e)
		e[1] = 0;
	else
		dir[0] = 0;
	for (;;) {
		long cut;
		n = Fread((short)h, PL_PIECE - have, buf + have);
		if (n < 0)
			n = 0;
		have += n;
		if (!n || have < PL_PIECE) {
			if (have > 0)
				m3u_read(buf, have, dir, m3u_add, 0);	/* (the end: the last line whole) */
			break;
		}
		for (cut = have; cut > 0 && buf[cut - 1] != '\n'; cut--)
			;
		if (!cut)
			cut = have;		/* (a line longer than a piece: cut) */
		m3u_read(buf, cut, dir, m3u_add, 0);
		memmove(buf, buf + cut, (size_t)(have - cut));
		have -= cut;
		feed_only();
		if (list_full)
			break;
	}
	Fclose((short)h);
	free(buf);
	elog("playlist %s: %d files", f, list_count() - before);
	return list_count() > before ? before : -1;
}

/* one name: a folder's files (its subfolders' too), a playlist's, or the
   file; its (first) index */
static int add_one(const char *f)
{
	if (m3u_name(f))
		return add_playlist(f);
	long a = Fattrib(f, 0, 0);
	size_t n = strlen(f);
	if ((a >= 0 && (a & 0x10)) || (n && f[n - 1] == '\\')) {
		char dir[200];
		snprintf(dir, sizeof(dir), "%s%s", f, n && f[n - 1] == '\\' ? "" : "\\");
		return add_folder(dir);
	}
	return list_add(f);
}

/* files left out for want of memory (the reserve kept for the AES): said
   on the info line and in the log, once a time it happens */
static void full_note(void)
{
	static int said;
	if (list_full && !said) {
		long st = (long)Mxalloc(-1L, 0);
		said = 1;
		status("The list is full: no memory for more files.");
		elog("the list full at %d files: the largest free ST RAM block %ld KB (%ld kept free for the AES)",
		     list_count(), st > 0 ? st / 1024 : 0, LIST_RESERVE / 1024);
	}
	if (!list_full)
		said = 0;
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
	full_note();
	sliders();
	if (list_count() != before)
		redraw(0, BP_LIST);
	if (first >= 0 && (now || (play_state() != PS_PLAYING && !play_paused())))
		play_index(first);
	else if (list_count() > before)
		redraw(0, BP_TEXT);	/* (Next may light) */
}

/* an MP3 whose album art the DSP unpacks first (Erez, 3 Nov 2026): its
   music waits for it, the window answering meanwhile (turn()) */
static char pending_play[320];

static void play_file(const char *f)
{
	play_log_stop();		/* (the one playing gives the DSP back) */
	play_stop();
	if (art_begin(f) == ART_FIRST) {
		snprintf(pending_play, sizeof(pending_play), "%s", f);
		status("Getting the album art...");
		return;
	}
	pending_play[0] = 0;
	play_now(f);
}

/* File > Add... (^O): a file, or with none chosen the whole folder; played
   if nothing plays */
/* File > Open... (^O): the Add files dialog (Erez, 4 Nov 2026: several
   files at once); the files chosen at the list's end, the first played if
   nothing plays. The music goes on meanwhile (its queue filled) */
static int first_added;

static void add_one_file(const char *f)
{
	int i = add_one(f);	/* (a folder: its subfolders too; a playlist: its files) */
	if (i >= 0 && first_added < 0)
		first_added = i;
}

static void feed_only(void)
{
	/* (no drawing: the dialog is over the window) */
	if (play_state() == PS_PLAYING)
		play_step(15);
}

/* File > Open... (^O): common's file dialog with EPlay's types (Add
   files: Erez, 4 Nov 2026) */
static const DL_TYPE add_types[] = { { "Music files", "MP3 WAV AIF AIFF AIFC M3U" }, { "MP3", "MP3" },
				     { "WAV", "WAV" }, { "AIFF", "AIF AIFF AIFC" }, { "Playlists", "M3U" },
				     { "All files", "" } };
static const FP_SPEC add_spec = { "Add files", "Add", "No folder added from yet", "EPLAY", add_types,
				  (int)(sizeof(add_types) / sizeof(add_types[0])) };

static void open_file(void)
{
	int before = list_count();
	first_added = -1;
	filepick_run(&add_spec, settings_add_dir(), settings_recent(), add_one_file, feed_only);
	settings_recent_changed();	/* (its folder kept, and the last folders if any were added) */
	{
		char line[60];
		snprintf(line, sizeof(line), "EPLAY list: %d files\n", list_count());
		nf_debug(line);			/* (the rigs) */
	}
	if (list_count() > before)
		list_unread = 1;
	full_note();
	sliders();
	redraw(0, BP_LIST);
	if (first_added >= 0 && play_state() != PS_PLAYING && !play_paused())
		play_index(first_added);
	else
		redraw(0, BP_TEXT);
}

/* File > Clear the list: all but the one playing */
static void list_clear_but_playing(void)
{
	ENTRY *e = list_at(list_cur);
	if (e && (play_state() == PS_PLAYING || play_paused()))
		list_keep_only(list_cur);
	else
		list_clear();
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

/* the list's rows from row i to its last shown, drawn in one go */
static void rows_redraw(int i)
{
	GRECT a, b;
	int last = list_top + bar_rows() - 1;
	if (i < list_top)
		i = list_top;
	if (wh <= 0 || i > last)
		return;
	bar_row_area(i, &a);
	bar_row_area(last, &b);
	a.h = (short)(b.y + b.h - a.y);
	redraw(&a, BP_LIST);
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
	pic_begin("rows");
	list_sel = i < list_count() ? i : list_count() - 1;
	if (list_top > 0 && list_top + bar_rows() > list_count()) {
		scroll_to(list_count() - bar_rows());	/* (the whole list: one draw) */
		pic_end();
	} else {
		rows_redraw(i);		/* the rows from it on */
		pic_end();
	}
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

/* the music's keys (Erez, 5 Nov 2026), the window's and the mini
   player's alike: Left and Right 10 s back and forward in the file,
   Shift+Left and Shift+Right the previous and next file, Clr Home Stop.
   (TOS gives a shifted arrow '4' and '6': the scan code is taken.) 1 when
   the key was one of them */
#define SEEK_STEP 10000L
static long seek_to = -1;		/* the position bar let go: where, for the next turn */
static int music_keys(short kr, short ks)
{
	short scan = (kr >> 8) & 0xff;
	int shift = (ks & 3) != 0;
	long len, at;
	switch (scan) {
	case 0x62:			/* Help */
		command(C_GUIDE);
		return 1;
	case 0x47:			/* Clr Home */
		command(C_STOP);
		return 1;
	case 0x4b:			/* Left */
	case 0x4d:			/* Right */
		if (shift) {
			command(scan == 0x4b ? C_PREV : C_NEXT);
			return 1;
		}
		len = play_len_ms();
		if (!play_is_open() || len <= 0)
			return 1;
		at = (seek_to >= 0 ? seek_to : play_ms()) + (scan == 0x4b ? -SEEK_STEP : SEEK_STEP);
		if (at < 0)
			at = 0;
		if (at > len - 1)
			at = len - 1;
		seek_to = at;		/* (gone there the next turn, as the position bar's) */
		redraw(0, BP_TIME);
		return 1;
	}
	return 0;
}

static void idle_step(void)
{
	step(play_state() == PS_PLAYING);
}

/* the list reordered by a drag (Erez, 5 Nov 2026): a row pressed and
   moved goes where it is let go, a line between the rows showing where;
   held above or below the rows shown, the list scrolls. The music goes
   on meanwhile. The line is drawn in XOR, so that taking it away leaves
   the rows as they were */
static void drop_line(short y)
{
	GRECT a, r;
	short p[4];
	if (y < 0 || wh <= 0)
		return;
	bar_row_area(list_top, &a);
	p[0] = a.x;
	p[2] = a.x + a.w - 1;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	vswr_mode(vdi_h, 3);		/* XOR */
	vsl_color(vdi_h, 1);
	wind_get(wh, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w && r.h) {
		short c[4];
		GRECT b = a;
		b.y = (short)(y - 1);
		b.h = 2;
		if (rc_intersect(&r, &b)) {
			c[0] = b.x;
			c[1] = b.y;
			c[2] = b.x + b.w - 1;
			c[3] = b.y + b.h - 1;
			vs_clip(vdi_h, 1, c);
			p[1] = p[3] = (short)(y - 1);
			v_pline(vdi_h, 2, p);
			p[1] = p[3] = y;
			v_pline(vdi_h, 2, p);
		}
		wind_get(wh, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	vs_clip(vdi_h, 0, p);
	vswr_mode(vdi_h, 1);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

/* the place between rows y points at (0 to the list's count), and the
   line's height there */
static int drop_at(short y, short *ly)
{
	GRECT a;
	int n = list_count(), rows = bar_rows(), t;
	short h = bar_row_h();
	bar_row_area(list_top, &a);
	t = y < a.y ? list_top : list_top + (y - a.y + h / 2) / h;
	if (t > list_top + rows)
		t = list_top + rows;
	if (t > n)
		t = n;
	*ly = (short)(a.y + (t - list_top) * h);
	return t;
}

static void drag_row(int from, short mx, short my)
{
	short msg[8], ly = -1, nly;
	AEVENT ev;
	GRECT a;
	int t = -1, to, n = list_count();
	unsigned long last_scroll = 0;
	(void)mx;
	graf_mouse(FLAT_HAND, 0);
	while (!quit) {
		short which;
		int playing = play_state() == PS_PLAYING, rows = bar_rows();
		int nt = drop_at(my, &nly);
		if (nt != t || nly != ly) {
			drop_line(ly);
			t = nt;
			ly = nly;
			drop_line(ly);
		}
		bar_row_area(list_top, &a);
		/* held above or below the rows: the list scrolls, a row each 80 ms */
		if ((my < a.y && list_top > 0) || (my >= a.y + rows * a.h && list_top + rows < n)) {
			if (tos_ms() - last_scroll >= 80) {
				drop_line(ly);
				ly = -1;
				scroll_to(list_top + (my < a.y ? -1 : 1));
				last_scroll = tos_ms();
				t = -1;
				continue;
			}
		}
		evnt_set_m1(1, mx = ev.mx, my, 1, 1);
		which = evnt_multi_(MU_MESAG | MU_BUTTON | MU_M1 | MU_TIMER, 1, 1, 0, 20, msg, &ev);
		turn_t0 = tos_ms();
		if (which & MU_MESAG) {
			drop_line(ly);		/* (a redraw would draw over it) */
			ly = -1;
			t = -1;
			message(msg);
		}
		if (which & (MU_M1 | MU_BUTTON | MU_TIMER))
			my = ev.my;
		if (which & MU_BUTTON) {
			input_ms = turn_t0;	/* (the picture's time from the release: the rigs') */
			break;
		}
		step(playing);
	}
	drop_line(ly);
	graf_mouse(ARROW, 0);
	if (quit || t < 0)
		return;
	to = t > from ? t - 1 : t;
	if (to == from)
		return;
	list_move(from, to);
	pic_begin("rows");
	rows_redraw(from < to ? from : to);	/* (the rows between, in one go) */
	pic_end();
	if (play_state() == PS_PLAYING) {	/* ("Playing 3 of 8": its place moved) */
		char st[100];
		playing_status(st, sizeof(st));
		status(st);
	}
	redraw(0, BP_NAV);
	{
		char line[48];
		snprintf(line, sizeof(line), "EPLAY moved %d to %d\n", from + 1, to + 1);
		nf_debug(line);			/* (the rigs) */
	}
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
	switch (press_track(&r, mx, my, idle_step, message, &dx, &dy)) {
	case PRESS_IN:
		if (dbl)
			play_index(i);
		break;
	case PRESS_DRAG:
		drag_row(i, dx, dy);
		break;
	}
}

/* View > Mini player: the window again, the bar alone or with the list,
   its top left corner where it was */
static void mini_switch(void)
{
	short x, y, w, h;
	if (wh > 0) {
		wind_get(wh, WF_CURRXYWH, &x, &y, &w, &h);
		at_x = x;
		at_y = y;
		av_window(wh, 0);
		wind_close(wh);
		wind_delete(wh);
		wh = -1;
	}
	bar_mini = !bar_mini;
	settings_set_mini(bar_mini);
	menu_icheck(mb.tree, mbar_object(&mb, C_MINI), bar_mini);
	open_window();
	if (!bar_mini) {
		scroll_to(list_top);
		show_row(list_cur);
		sliders();
	}
	{
		char line[40];
		snprintf(line, sizeof(line), "EPLAY mini %d\n", bar_mini);
		nf_debug(line);			/* (the rigs) */
	}
}

/* File > Save playlist... (^S; Erez, 4 Nov 2026): the list as an .M3U,
   named in the file selector, each file's path relative to the
   playlist's folder where it is in it (m3u.h) */
static void save_playlist(void)
{
	static char path[200], name[64] = "PLAYLIST.M3U";
	char full[260], dir[200], *e, buf[4096];
	short button = 0;
	long h, n = 0, k;
	int i, ok = 1;
	if (!list_count()) {
		alert(1, "[1][The list is empty: there is|nothing to save.][ OK ]");
		return;
	}
	if (!path[0])
		snprintf(path, sizeof(path), "%s*.M3U", settings_add_dir()[0] ? settings_add_dir() : app_dir());
	if (!fsel_exinput(path, name, &button, "Save the list as a playlist") || !button || !name[0])
		return;
	snprintf(dir, sizeof(dir), "%s", path);
	e = strrchr(dir, '\\');
	if (e)
		e[1] = 0;
	else
		dir[0] = 0;
	if (!strchr(name, '.'))
		strncat(name, ".M3U", sizeof(name) - strlen(name) - 1);
	snprintf(full, sizeof(full), "%s%s", dir, name);
	if (Fattrib(full, 0, 0) >= 0 &&
	    alert(2, "[2][%s is there already.|Replace it?][ Replace | Cancel ]", name) != 1)
		return;
	h = Fcreate(full, 0);
	if (h < 0) {
		alert(1, "[1][The playlist couldn't be saved|(%s).][ OK ]", name);
		return;
	}
	memcpy(buf, M3U_HEAD, sizeof(M3U_HEAD) - 1);
	n = (long)sizeof(M3U_HEAD) - 1;
	for (i = 0; i < list_count() && ok; i++) {
		ENTRY *t = list_at(i);
		k = m3u_entry(buf + n, (long)sizeof(buf) - n, t->path, dir, t->read ? t->len_ms / 1000 : -1,
			      t->artist, t->title);
		if (k < 0) {
			/* (full: written, and the entry again) */
			ok = Fwrite((short)h, n, buf) == n;
			n = 0;
			k = m3u_entry(buf, (long)sizeof(buf), t->path, dir, t->read ? t->len_ms / 1000 : -1, t->artist,
				      t->title);
			if (k < 0)
				continue;
		}
		n += k;
	}
	if (ok && n)
		ok = Fwrite((short)h, n, buf) == n;
	Fclose((short)h);
	if (!ok) {
		Fdelete(full);
		alert(1, "[1][The playlist couldn't be saved|(%s): the disk is full?][ OK ]", name);
		return;
	}
	{
		char st[100];
		snprintf(st, sizeof(st), "Saved %s: %d file%s.", name, list_count(), list_count() == 1 ? "" : "s");
		status(st);
		elog("playlist saved: %s, %d files", full, list_count());
		snprintf(st, sizeof(st), "EPLAY saved %d\n", list_count());
		nf_debug(st);		/* (the rigs) */
	}
}

static void command(int c)
{
	switch (c) {
	case C_ABOUT:
		dlg_about();
		break;
	case C_GUIDE:
		/* eplay.hyp in the viewer that runs, else how to get one, with
		   About for what EPlay shows itself (the guidelines, 4.3) */
		if (help_key(app_dir(), "eplay.hyp", 0, "About"))
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
	case C_SAVE:
		save_playlist();
		break;
	case C_MINI:
		mini_switch();
		break;
	case C_PREV: {
		int i = order_prev(&list_order, list_cur);
		if (i >= 0)
			play_index(i);
		break;
	}
	case C_NEXT: {
		int i = order_next(&list_order, list_cur, 0);
		if (i >= 0)
			play_index(i);
		break;
	}
	case C_SHUFFLE:
		/* a random order from here on: a round begun at the file playing */
		list_order.shuffle = !list_order.shuffle;
		settings_set_shuffle(list_order.shuffle);
		order_clear(&list_order);
		order_played(&list_order, list_cur);
		play_menu_shown();
		redraw(0, BP_NAV);
		break;
	case C_REPEAT:
		list_order.repeat = (list_order.repeat + 1) % 3;	/* off, all, one */
		settings_set_repeat(list_order.repeat);
		play_menu_shown();
		redraw(0, BP_NAV);
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
				pic_begin("resume");
				playing_drawn(0);
				pic_end();
				snprintf(st, sizeof(st), "EPLAY resumed at %ld ms\n", play_ms());
				nf_debug(st);		/* (the rigs) */
				break;
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
static int next_due;		/* a file played to its end: the list's next, next turn */

static void step(int playing)
{
	unsigned long used = tos_ms() - turn_t0;
	if (playing && play_step(used >= 20 ? 0 : (int)(20 - used))) {
		if (play_note_new) {
			/* (Normal by itself: the rate on the bar and the info
			   line) */
			play_note_new = 0;
			playing_drawn(1);
		}
		if (play_state() != PS_PLAYING) {
			status(play_out[0] == 'T' ? play_out : "Played to the end.");
			redraw(0, BP_PLAY | BP_TIME);
			if (play_out[0] != 'T' && list_cur >= 0 && order_has_next(&list_order, list_cur))
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
	int piece = 0;
	AEVENT ev;
	short msg[8];
	short which;
	int playing = play_state() == PS_PLAYING;
	/* a dialog held open (Settings, About) goes on feeding the queue while
	   the music plays, nothing while it doesn't (common's dlg_idle: the
	   hub, 4 Nov 2026, from 0.22's underruns with Settings open) */
	dlg_idle = playing ? feed_only : 0;
	/* (either button: 0x101, 3, 0 as popup.c asks; the right one opens a
	   row's menu) */
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 0x101, 3, 0,
			    nested || art_pending() || seek_to >= 0 || pending_play[0] || next_due || open_due || shown_due ? 0 :
			    list_unread ? 20 : play_wait_ms(), msg, &ev);
	turn_t0 = tos_ms();
	if (which & (MU_KEYBD | MU_BUTTON))
		input_ms = turn_t0;	/* (a picture's time from the input: the rigs') */
	if (pending_play[0] && !nested && !art_needs_dsp()) {
		/* (the album art unpacked: the MP3 it waited for) */
		char f[320];
		snprintf(f, sizeof(f), "%s", pending_play);
		pending_play[0] = 0;
		play_now(f);
		piece = 1;
	} else if (shown_due && !nested) {
		playing_drawn(shown_due == 2);
		shown_due = 0;
		pic_end();
		piece = 1;
	} else if (next_due && !nested) {
		next_due = 0;
		{
			int i = order_next(&list_order, list_cur, 1);	/* (Repeat one: the same) */
			if (i >= 0)
				play_index(i);
		}
	} else if (open_due && !nested) {
		open_due = 0;
		play_file(file);
		piece = 1;
	}
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
			else if (a == 19 || (a | 0x20) == 's')
				command(C_SAVE);
			else if (a == 17 || a == 21 || (a | 0x20) == 'q' || (a | 0x20) == 'u')
				command(C_QUIT);
			else if (((ev.kreturn >> 8) & 0xff) == 0x33)	/* ^, (the comma's key) */
				command(C_SETTINGS);
		} else if (a == ' ')
			command(C_PLAY);	/* Play/Pause */
		else if (music_keys(ev.kreturn, ev.kstate))
			;
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
		else if (hit == BH_PREV && order_has_prev(&list_order, list_cur) && track(BH_PREV, BP_TEXT))
			command(C_PREV);
		else if (hit == BH_NEXT && order_has_next(&list_order, list_cur) && track(BH_NEXT, BP_TEXT))
			command(C_NEXT);
		else if (hit == BH_LIST)
			list_press(ev.mx, ev.my, 0);
	}
	/* (a turn that drew a piece of the window reads no more of the file:
	   a 32 KB read after it went past the 50 ms between event calls; the
	   queue is filled the turn after; nor one after which another file
	   opens: the next turn stops this one, so its frames would be wasted,
	   and Next's rows would wait for them) */
	step(playing && !piece && !open_due && !next_due);
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
	help_init();
	order_init(&list_order, tos_ms());
	list_order.shuffle = settings_shuffle();
	list_order.repeat = settings_repeat();
	play_set_volume(settings_volume());
	if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h)) {
		form_alert(1, "[3][EPlay: not enough memory.][ Quit ]");
		snd_exit();
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	play_menu_shown();
	bar_mini = settings_mini();
	menu_icheck(mb.tree, mbar_object(&mb, C_MINI), bar_mini);
	menu_bar(mb.tree, 1);
	graf_mouse(ARROW, 0);
	open_window();
	art_init(bar_art_side());
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
	elog("Settings: output %s", settings_output_name(settings_output()));
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
			else {
				long st = (long)Mxalloc(-1L, 0);
				list_unread = 0;
				elog("the list: %d files read, %ld KB; the largest free ST RAM block %ld KB", list_count(),
				     (list_memory() + 1023) / 1024, st > 0 ? st / 1024 : 0);
			}
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
