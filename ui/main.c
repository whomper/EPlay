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
#include "plat.h"
#include "conn.h"
#include "busy.h"
#include "stream.h"
#include "urldlg.h"
#include "ui.h"
#include "iconify.h"
#include "term.h"
#include "finddlg.h"
#include "stsearch.h"
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
#include "tooltip.h"
#include "prefs.h"
#include "list.h"
#include "dd.h"
#include "popup.h"
#include "filepick.h"
#include "m3u.h"
#include "charset.h"
#include "elog.h"
#include "snd.h"
#include "bar.h"
#include "art.h"
#include "sart.h"

extern const char app_version[];

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id;

enum { C_ABOUT = 1, C_OPEN, C_QUIT, C_PLAY, C_STOP, C_SETTINGS, C_SHUFFLE, C_REPEAT, C_CLEAR, C_PREV, C_NEXT,
       C_REMOVE, C_MINI, C_SAVE, C_GUIDE, C_URL, C_DIRECT, C_GATEWAY, C_KEYS, C_FIND, C_SELALL };

static const MBAR_ITEM m_desk[] = {
	{ "About EPlay...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "Open...", "^O", C_OPEN },
	{ "Open URL...", "\x01^O", C_URL },	/* (a station: phase 7; ^U is the family's Close) */
	{ "Find station...", 0, C_FIND },	/* (radio-browser.info's: Erez, 9 Nov 2026) */
	{ "Save playlist...", "^S", C_SAVE },
	{ "-", 0, 0 },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Remove from the list", "Del", C_REMOVE },	/* (MENUS 1.2: Del; MD14) */
	{ "Clear the list", 0, C_CLEAR },
	{ "-", 0, 0 },
	{ "Select all", "^A", C_SELALL },	/* (D21: several rows) */
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
/* Settings first, a line, then the ticks (MENUS 1.5); the connection's two
   ticks a pair (D9) */
static const MBAR_ITEM m_options[] = {
	{ "Settings...", "^,", C_SETTINGS },
	{ "-", 0, 0 },
	{ "Direct connection", 0, C_DIRECT },	/* (how a station is reached: Erez, 8 Nov 2026) */
	{ "Through the Gateway", 0, C_GATEWAY },
	{ 0, 0, 0 }
};
/* NAME help and Keys... (MENUS 1.5, D10): eplay.hyp at its first page */
static const MBAR_ITEM m_help[] = {
	{ "EPlay help", "Help", C_GUIDE },
	{ "Keys...", 0, C_KEYS },
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

/* the full window has the fuller and, where the AES can, the iconifier
   (D47, W13); the mini player stays without (Erez, 9 Nov 2026) */
#define KIND_FULL (NAME | CLOSER | FULLER | MOVER | INFO | SIZER | VSLIDE | UPARROW | DNARROW)
#define KIND_MINI (NAME | CLOSER | MOVER)	/* the mini player: the bar alone */
static short kind_full;			/* KIND_FULL with SMALLER where the AES has it */
#define KIND (bar_mini ? KIND_MINI : kind_full)
static int iconified;			/* the window shown as EPlay's icon (WM_ICONIFY) */

static MBAR mb;

/* the Play menu as Shuffle and Repeat are: Shuffle ticked, Repeat's text
   ("Repeat: off", "all", "one": the item's own, the same length) */
/* the Options menu as the connection is: one of the two ticked */
static void conn_menu_shown(void)
{
	menu_icheck(mb.tree, mbar_object(&mb, C_DIRECT), !settings_gateway());
	menu_icheck(mb.tree, mbar_object(&mb, C_GATEWAY), settings_gateway());
}

static void play_menu_shown(void)
{
	short o = mbar_object(&mb, C_REPEAT);
	menu_icheck(mb.tree, mbar_object(&mb, C_SHUFFLE), list_order.shuffle);
	if (o > 0)
		memcpy((char *)mb.tree[o].ob_spec + 2, order_repeat_name(list_order.repeat), 11);
}
static short wh = -1;
static GRECT work;
static int list_has_sel(void)
{
	return list_nsel > 0;
}
static char file[320];		/* the file playing */

/* what can apply now (MENUS 1.3): greyed in the menu, and its key does
   nothing (command() asks the same) */
static int can(int c)
{
	switch (c) {
	case C_SAVE:
	case C_CLEAR:
		return list_count() > 0;
	case C_REMOVE:
		return list_has_sel();
	case C_SELALL:
		return list_count() > 0 && !bar_mini;
	case C_PLAY:
		return play_is_open() || play_paused() || play_name[0] || list_count() > 0;
	case C_STOP:
		return play_state() == PS_PLAYING || play_paused();
	case C_PREV:
		return order_has_prev(&list_order, list_cur);
	case C_NEXT:
		return order_has_next(&list_order, list_cur);
	case C_MINI:
		return !iconified;	/* (uniconified first: the icon is the full window's) */
	}
	return 1;
}

/* the bar's greyed items as can() says, the AES asked only for a change
   (MENUS 1.3: EMail measured 40 calls at 40 ms on an 8 MHz ST) */
static void menu_update(void)
{
	static const int grey[] = { C_SAVE, C_CLEAR, C_REMOVE, C_SELALL, C_PLAY, C_STOP, C_PREV, C_NEXT, C_MINI };
	static signed char was[sizeof(grey) / sizeof(grey[0])];
	unsigned i;
	for (i = 0; i < sizeof(grey) / sizeof(grey[0]); i++) {
		int on = can(grey[i]);
		if (was[i] != on + 1) {
			short o = mbar_object(&mb, grey[i]);
			was[i] = (signed char)(on + 1);
			if (o > 0)
				menu_ienable(mb.tree, o, on);
		}
	}
}

/* Help > Keys... (D10): EPlay's keys, as the menus and the guide have them */
static void keys_box(void)
{
	text_box("Keys",
		 "Space          Play or pause\n"
		 "ClrHome        Stop\n"
		 "Left, Right    10 seconds back, on\n"
		 "Shift+Left     The previous file\n"
		 "Shift+Right    The next file\n"
		 "+  -           Louder, softer\n"
		 "\n"
		 "Up, Down       Select a row\n"
		 "Shift+click    Add a row, or take it out\n"
		 "^click         Add the rows up to it\n"
		 "^A             Select all\n"
		 "Return         Play the selected row\n"
		 "Del            Remove the selected rows\n"
		 "Insert         The row's menu\n"
		 "\n"
		 "^O             Open...\n"
		 "Shift+^O       Open URL...\n"
		 "^S             Save playlist...\n"
		 "^,             Settings...\n"
		 "Help           EPlay help\n"
		 "Esc, Undo      Stop connecting to a station\n"
		 "^Q             Quit");
}
static int quit;
static int press_draw;		/* drawn while the button is held: press_update_begin's lock */

/* the info line: set only when it changed, and not in the mini player,
   which has none (WINDOWS 1.1); status_again() after a window is made */
static char info[90], info_set[90];
static void status(const char *s)
{
	char line[100];
	snprintf(info, sizeof(info), " %s", s);
	snprintf(line, sizeof(line), "EPLAY status: %s\n", s);
	nf_debug(line);			/* the rigs read it (no-op on hardware) */
	if (wh > 0 && !bar_mini && !iconified && strcmp(info, info_set)) {
		strcpy(info_set, info);
		wind_set_str(wh, WF_INFO, info_set);
	}
}

static void status_again(void)
{
	info_set[0] = 0;
	if (!info[0])
		strcpy(info, " ");	/* (set at once: XaAES drew garbage in an info line never set) */
	if (wh > 0 && !bar_mini) {
		strcpy(info_set, info);
		wind_set_str(wh, WF_INFO, info_set);
	}
}

/* r less the tip's box where one is up (tooltip.h's tip_area): up to 4
   pieces into out, their number */
static int around_tip(const GRECT *r, GRECT out[4])
{
	GRECT t;
	int n = 0;
	short x2, y2, tx2, ty2;
	if (!tip_area(&t) || !rc_intersect(r, &t)) {
		out[0] = *r;
		return 1;
	}
	x2 = r->x + r->w;
	y2 = r->y + r->h;
	tx2 = t.x + t.w;
	ty2 = t.y + t.h;
	if (t.y > r->y) {			/* above it */
		out[n].x = r->x, out[n].y = r->y, out[n].w = r->w, out[n].h = t.y - r->y;
		n++;
	}
	if (ty2 < y2) {				/* below it */
		out[n].x = r->x, out[n].y = ty2, out[n].w = r->w, out[n].h = y2 - ty2;
		n++;
	}
	if (t.x > r->x) {			/* beside it, left */
		out[n].x = r->x, out[n].y = t.y, out[n].w = t.x - r->x, out[n].h = t.h;
		n++;
	}
	if (tx2 < x2) {				/* and right */
		out[n].x = tx2, out[n].y = t.y, out[n].w = x2 - tx2, out[n].h = t.h;
		n++;
	}
	return n;
}

/* the window's parts again (bar.h's BP_...), through the AES's
   rectangles, within area (0: the parts' own). A small part (the times,
   each second; Play/Pause) is drawn off the screen and shown in one step
   (common's offdraw, where it can be: no flicker), else on it */
/* the event's wait, no later than the title's next scroll step */
static unsigned long scroll_wait(unsigned long ms)
{
	long w = bar_scroll_wait();
	return w >= 0 && (unsigned long)w < ms ? (unsigned long)w : ms;
}

static void redraw(GRECT *area, int parts)
{
	GRECT r, a;
	int small = area == 0 && !(parts & (BP_TEXT | BP_LIST)), took;
	if (wh <= 0)
		return;
	if (iconified) {
		/* the icon, and under it Playing or Paused: again when that
		   changes (BP_PLAY) or the AES asks; nothing else is drawn */
		if (area || (parts & BP_PLAY))
			about_icon_redraw(wh, area, play_state() == PS_PLAYING ? "Playing" :
						      play_paused() ? "Paused" : "");
		return;
	}
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
			/* (a tip up: what it covers left out, D18's tip_area; the
			   clock and the position bar draw by themselves each second) */
			GRECT piece[4];
			int k, n = around_tip(&r, piece);
			for (k = 0; k < n; k++) {
				if (small && off_begin(vdi_h, piece[k].x, piece[k].y, piece[k].w, piece[k].h)) {
					bar_draw(&piece[k], parts);
					off_end();
				} else
					bar_draw(&piece[k], parts);
			}
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

static void sized(short w, short h);

static void open_window(void)
{
	short x, y, w, h, ww, wh_, sx, sy, sw, sh;
	int saved = 0;
	if (!kind_full)
		kind_full = icon_kind(KIND_FULL);
	bar_size(&ww, &wh_);
	wind_calc(WC_BORDER, KIND, desk_x, desk_y, ww, wh_, &x, &y, &w, &h);
	if (w > desk_w)
		w = desk_w;
	x = desk_x + (desk_w - w) / 2;
	y = desk_y + (desk_h - h) / 3;
	if (at_x >= 0) {
		x = at_x + w > desk_x + desk_w ? desk_x + desk_w - w : at_x;
		y = at_y + h > desk_y + desk_h ? desk_y + desk_h - h : at_y;
	} else if (!bar_mini && !settings_window(&sx, &sy, &sw, &sh)) {
		/* the first start (no place kept): the desk's whole height, its
		   width design D's, centred across (D39, WINDOWS 1.1) */
		y = desk_y;
		saved = desk_h != h ? desk_h : 0;
	} else if (!bar_mini) {
		/* the place it was left at (WINDOW=: D43), kept on the desk; its
		   width is the bar's, its height whole rows (sized, below) */
		if (sh > desk_h)
			sh = desk_h;
		x = sx + w > desk_x + desk_w ? desk_x + desk_w - w : sx < desk_x ? desk_x : sx;
		y = sy + sh > desk_y + desk_h ? desk_y + desk_h - sh : sy < desk_y ? desk_y : sy;
		saved = sh != h ? sh : 0;
	}
	x = wind_x8(KIND, x, y, w, h);	/* (the work area on a multiple of 8: text on a byte) */
	wh = wind_create(KIND, desk_x, desk_y, desk_w, desk_h);
	if (wh <= 0)
		return;
	wind_set_str(wh, WF_NAME, " EPlay ");
	wind_open(wh, x, y, w, h);
	iconified = 0;
	status_again();			/* (the info line, the full window's only) */
	wind_get(wh, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	bar_layout(&work);
	if (saved)
		sized(w, saved);	/* (the saved height, in whole rows) */
	{
		/* (the rigs' clicks go by it: an STE's screen puts it elsewhere) */
		char l[60];
		snprintf(l, sizeof(l), "EPLAY window %d %d %d %d\n", work.x, work.y, work.w, work.h);
		nf_debug(l);
	}
	av_window(wh, 1);		/* (an AV desktop drops files on it: VA_DRAGACCWIND) */
}

/* "Playing 2 of 6 · Falcon 49.1 kHz" (one file alone: no count; "of 256,
   the list full" while files were left out for want of memory) */
static void playing_status(char *st, int n)
{
	if (play_nosig)			/* (alone: it takes the line's width) */
		snprintf(st, (size_t)n, "%s", play_out);
	else if (list_cur >= 0 && list_count() > 1)
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
static int chain_due;		/* gapless: the next file to open, at the next turn */
static int chain_row = -1;	/* gapless: the next row, chosen as the file's last bytes went
				   into the queue (followed on, or opened at its end) */
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
	sart_reset();		/* (a station's picture: the new one's from now) */
	if (surl_is(f)) {
		/* a station: the bee while it connects, Esc or Undo stops it
		   (the guidelines, 7.1) */
		conn_stop = 0;
		status("Connecting to the station...   (Esc stops)");
		busy_bee(1);
		opened = play_open(f, err, sizeof(err));
		busy_bee(0);
		if (!opened && conn_stop)
			snprintf(err, sizeof(err), "Connecting to the station was stopped.");
		conn_stop = 0;
	} else
		opened = play_open(f, err, sizeof(err));
	PART("the file opened");
	if (opened && surl_is(f)) {
		int i = list_find(f);
		settings_url_used(f);	/* (Open URL's last stations) */
		sart_station(f, play_name);	/* (its logo, through a Gateway, if it names no picture) */
		if (i >= 0) {
			list_set_title(i, play_name);	/* (the row: the station's name) */
			redraw(0, BP_LIST);
		}
	}
	if (!opened) {
		char line[200];
		snprintf(line, sizeof(line), "EPLAY refused: %s\n", err);
		nf_debug(line);
		alert(1, "[3][%s][ OK ]", err);
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

static void rows_redraw(int i);

/* row i selected alone (a click, Up and Down): the rows that change drawn */
static void select_row(int i)
{
	int old = list_sel, many = list_nsel > 1;
	ENTRY *e = list_at(i);
	if (i == old && e && e->sel && !many)
		return;
	list_select_none();
	list_select(i, 1);
	list_sel = i;
	show_row(i);
	if (many)
		rows_redraw(list_top);	/* (the rows shown, in one go) */
	else {
		row_redraw(old);
		row_redraw(i);
	}
}

/* Shift+click: row i in or out of the selection (5.8); the anchor there */
static void select_toggle(int i)
{
	ENTRY *e = list_at(i);
	if (!e)
		return;
	list_select(i, !e->sel);
	list_sel = i;
	row_redraw(i);
}

/* Control+click: the rows from the anchor to i added */
static void select_range(int i)
{
	int a = list_sel >= 0 ? list_sel : i, k, lo = a < i ? a : i, hi = a < i ? i : a;
	for (k = lo; k <= hi; k++)
		if (!list_at(k)->sel) {
			list_select(k, 1);
			row_redraw(k);
		}
}

/* the row playing selected, as it was when one row at most is: a
   selection of several is left as it is */
static void select_follow(int i, int drawn)
{
	int old = list_sel;
	if (list_nsel > 1)
		return;
	list_select_none();
	list_select(i, 1);
	list_sel = i;
	if (old != i && old != drawn)
		row_redraw(old);	/* (drawn: a row the caller draws again anyway) */
}

/* the next (or the one before) from cur that EPlay can play here: rows
   marked bad (an MP3 without a DSP, a file it couldn't read) passed over,
   once round the list at most */
static int playable_from(int cur, int ended, int back)
{
	int i = cur, k;
	for (k = 0; k <= list_count(); k++) {
		ENTRY *e;
		i = back ? order_prev(&list_order, i) : order_next(&list_order, i, ended);
		if (i < 0 || !(e = list_at(i)) || !e->bad)
			return i;
		order_played(&list_order, i);	/* (Shuffle: counted as played this round) */
		if (i == cur)
			return -1;
	}
	return -1;
}

static void play_index(int i)
{
	ENTRY *e = list_at(i);
	int old = list_cur;
	order_played(&list_order, i);
	if (!e)
		return;
	list_cur = i;
	select_follow(i, old);
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
	       !strcmp(e, "aifc") || !strcmp(e, "vgm") || !strcmp(e, "vgz");
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
	int i;
	(void)ctx;
	i = list_add(path);
	if (i >= 0 && surl_is(path) && m3u_extinf[0]) {
		/* a station: its row named as the playlist names it (UTF-8
		   where it is, as stations' lists are written) */
		long ol;
		char *a = has_8bit(m3u_extinf, (long)strlen(m3u_extinf)) ?
			  cs_to_atari(m3u_extinf, (long)strlen(m3u_extinf), cs_id("utf-8"), &ol) : 0;
		list_set_title(i, a ? a : m3u_extinf);
		free(a);
	}
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

/* files came in and none can play here (MP3s without a DSP): said so,
   when nothing plays (Erez, 7 Nov 2026: "MP3 needs a Falcon's DSP") */
static void none_playable(int before)
{
	if (list_count() > before && play_state() != PS_PLAYING && !play_paused() &&
	    !(get_cookie(0x5F534E44L) & 8)) {
		nf_debug("EPLAY refused: MP3 needs a Falcon's DSP.\n");
		status("MP3 needs a Falcon's DSP.");
	}
}

/* names as VA_START has them (quoted where they have spaces): added; now 1
   plays the first of them at once, else it plays only if nothing plays */
static void add_names(const char *s, int now)
{
	char f[256];
	int first = -1, before = list_count();
	while ((s = av_arg(s, f, sizeof(f))) != 0) {
		int i = add_one(f);
		if (first < 0 && i >= 0 && !list_at(i)->bad)	/* (the first it can play here) */
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
	else if (list_count() > before) {
		if (first < 0)
			none_playable(before);
		redraw(0, BP_TEXT);	/* (Next may light) */
	}
}

/* an MP3 whose album art the DSP unpacks first (Erez, 3 Nov 2026): its
   music waits for it, the window answering meanwhile (turn()) */
static char pending_play[320];

static void play_file(const char *f)
{
	chain_row = -1;			/* (a file chosen: gapless's next forgotten) */
	chain_due = 0;
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

/* File > Open... (^O): the Add files dialog (Erez, 4 Nov 2026: several
   files at once); the files chosen at the list's end, the first of them
   played at once and the list on it, as Open URL plays its station (Erez,
   13 Nov 2026). The music goes on meanwhile (its queue filled) */
static int first_added;

static void add_one_file(const char *f)
{
	int i = add_one(f);	/* (a folder: its subfolders too; a playlist: its files) */
	if (i >= 0 && first_added < 0 && !list_at(i)->bad)
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
static const DL_TYPE add_types[] = { { "Music files", "MP3 WAV AIF AIFF AIFC VGM VGZ M3U" }, { "MP3", "MP3" },
				     { "WAV", "WAV" }, { "AIFF", "AIF AIFF AIFC" }, { "VGM", "VGM VGZ" }, { "Playlists", "M3U" },
				     { "All files", "" } };
static const FP_SPEC add_spec = { "Add files", "Add", "No folder added from yet", "EPLAY", add_types,
				  (int)(sizeof(add_types) / sizeof(add_types[0])) };

static void open_file(void)
{
	int before = list_count();
	first_added = -1;
	draw_forget();			/* (the text's height set again at its first) */
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
	if (first_added >= 0)
		play_index(first_added);
	else {
		if (first_added < 0)
			none_playable(before);
		redraw(0, BP_TEXT);
	}
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

/* the selected files out of the list (the one playing stopped first);
   the row after the first of them selected */
static void list_remove_sel(void)
{
	int i;
	ENTRY *e = list_at(list_cur);
	if (!list_nsel)
		return;
	if (e && e->sel) {
		play_log_stop();
		play_stop();
		status("Stopped.");
	}
	{
		int was = list_count();
		char line[60];
		i = list_remove_selected();
		snprintf(line, sizeof(line), "EPLAY removed %d, list: %d files\n", was - list_count(), list_count());
		nf_debug(line);			/* (the rigs) */
	}
	pic_begin("rows");
	list_sel = i < list_count() ? i : list_count() - 1;
	list_select(list_sel, 1);
	if (list_top > 0 && list_top + bar_rows() > list_count()) {
		scroll_to(list_count() - bar_rows());	/* (the whole list: one draw) */
		pic_end();
	} else {
		rows_redraw(i);		/* the rows from it on */
		pic_end();
	}
	sliders();
	if (play_state() == PS_PLAYING) {	/* ("Playing 1 of 2": fewer now) */
		char st[100];
		playing_status(st, sizeof(st));
		status(st);
	}
	redraw(0, BP_NAV);
}

static void command(int c);

/* a row's menu (the guidelines' 3.3): its double click's action first, then
   what the menu bar has for it, with the keys */
static void row_menu(short x, short y)
{
	/* (popup_m: the words alone, the box adds the margin: D15) */
	static const char *const lab[] = { "Play                  Return", "-",
					   "Remove from the list  Del   ", "Clear the list" };
	switch (popup_m(x, y, lab, 4)) {
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
	if (!n || bar_mini)
		return 0;		/* (the mini player: no list shown, no list keys) */
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
		command(C_REMOVE);
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
	int shift = key_shift(ks, kr);	/* (common's: a Shift held while EPlay was busy, from the key itself) */
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
static void list_press(short mx, short my, short ks, int right)
{
	int i = bar_row_at(my), dbl;
	ENTRY *e = list_at(i);
	GRECT r;
	short dx, dy;
	if (i < 0 || !e)
		return;
	if (!right && (ks & 3)) {	/* Shift+click: the row in or out (WINDOWS 2.2, D21) */
		select_toggle(i);
		return;
	}
	if (!right && (ks & 4)) {	/* Control+click: the rows from the anchor */
		select_range(i);
		return;
	}
	dbl = press_double((short)i, mx, my);
	/* (a row in a selection of several keeps it: the right button's menu
	   acts on them all) */
	if (!e->sel || list_nsel < 2)
		select_row(i);
	else
		list_sel = i;
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
		select_row(i);		/* (a row moves by itself) */
		drag_row(i, dx, dy);
		break;
	}
}

/* the full window's place, for EPLAY.INF (WINDOW=: D43): not the mini
   player's, nor an icon's */
static void remember_place(void)
{
	short x, y, w, h;
	if (wh <= 0 || bar_mini || iconified)
		return;
	wind_get(wh, WF_CURRXYWH, &x, &y, &w, &h);
	settings_set_window(x, y, w, h);
}

/* View > Mini player: the window again, the bar alone or with the list,
   its top left corner where it was */
static void mini_switch(void)
{
	short x, y, w, h;
	remember_place();
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
	if (!list_count())
		return;			/* (greyed: menu_update) */
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
	    alert(2, "[2][%s is there already.|Replace it?][Replace|Cancel]", name) != 1)
		return;
	h = Fcreate(full, 0);
	if (h < 0) {
		alert(1, "[3][The playlist couldn't be saved|(%s).][ OK ]", name);
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
		alert(1, "[3][The playlist couldn't be saved|(%s): the disk is full?][ OK ]", name);
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

/* Find station's Add: the station in the list, under its name */
static void find_add(const char *url, const char *name)
{
	int i = list_add(url);
	if (i >= 0)
		list_set_title(i, name);
}

static void command(int c)
{
	if (!can(c))
		return;			/* (greyed: its key does nothing too) */
	switch (c) {
	case C_ABOUT:
		dlg_about();
		break;
	case C_GUIDE:
		/* eplay.hyp in the viewer that runs, else how to get one, with
		   the keys for what EPlay shows itself (the guidelines, 4.3; D10) */
		if (help_key(app_dir(), "eplay.hyp", 0, "Keys"))
			keys_box();
		break;
	case C_KEYS:
		keys_box();
		break;
	case C_OPEN:
		open_file();
		break;
	case C_QUIT:
		quit = 1;
		break;
	case C_URL: {
		/* File > Open URL...: a station into the list, played */
		static char u[URL_MAX];
		if (url_dialog(u, sizeof(u))) {
			int i = list_add(u);
			sliders();
			redraw(0, BP_LIST);
			if (i >= 0)
				play_index(i);
		}
		break;
	}
	case C_FIND: {
		/* File > Find station...: one played, others added meanwhile */
		static char u[SS_URL], name[64];
		if (find_dialog(u, sizeof(u), name, sizeof(name), find_add)) {
			int i = list_add(u);
			if (i >= 0)
				list_set_title(i, name);
			sliders();
			redraw(0, BP_LIST);
			if (i >= 0)
				play_index(i);
		} else {
			sliders();
			redraw(0, BP_LIST);
		}
		break;
	}
	case C_DIRECT:
	case C_GATEWAY:
		settings_set_gateway(c == C_GATEWAY);
		conn_menu_shown();
		elog("stations: %s", c == C_GATEWAY ? "through the Gateway" : "a Direct connection");
		break;
	case C_SETTINGS:
		prefs_dialog();
		play_rgain_changed();	/* (ReplayGain: at once, gliding) */
		break;
	case C_CLEAR:
		list_clear_but_playing();
		break;
	case C_REMOVE:
		list_remove_sel();
		break;
	case C_SELALL:
		list_select_all();
		rows_redraw(list_top);
		break;
	case C_SAVE:
		save_playlist();
		break;
	case C_MINI:
		mini_switch();
		break;
	case C_PREV: {
		int i = playable_from(list_cur, 0, 1);
		if (i >= 0)
			play_index(i);
		break;
	}
	case C_NEXT: {
		int i = playable_from(list_cur, 0, 0);
		if (i >= 0)
			play_index(i);
		break;
	}
	case C_SHUFFLE:
		/* a random order from here on: a round begun at the file playing */
		list_order.shuffle = !list_order.shuffle;
		settings_set_shuffle(list_order.shuffle);
		play_rgain_changed();	/* (Auto: the album's in order, the track's shuffled) */
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
			int ok;
			if (play_is_net()) {
				/* a station: connected again, live (Erez, 8 Nov 2026) */
				conn_stop = 0;
				status("Connecting to the station...   (Esc stops)");
				busy_bee(1);
				ok = play_resume(err, sizeof(err));
				busy_bee(0);
				if (!ok && conn_stop)
					snprintf(err, sizeof(err), "Connecting to the station was stopped.");
				conn_stop = 0;
			} else
				ok = play_resume(err, sizeof(err));
			if (!ok) {
				alert(1, "[3][%s][ OK ]", err);
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
	if (playing && play_chained()) {
		/* the file that followed on is heard: the list's current row,
		   the bar and its album art (unpacked by the 68030: the DSP
		   plays) */
		int old = list_cur, i = chain_row;
		ENTRY *e = list_at(i);
		chain_row = -1;
		if (e) {
			order_played(&list_order, i);
			list_cur = i;
			select_follow(i, old);
			snprintf(file, sizeof(file), "%s", e->path);
			show_row(i);
			row_redraw(old);
			row_redraw(i);
			art_begin_cpu(e->path);
		}
		playing_drawn(1);
		{
			char line[240];		/* (the rigs' console, as play_now's) */
			snprintf(line, sizeof(line), "EPLAY playing: %s | %s | %s (followed on)\n", play_name, play_info,
				 play_out);
			nf_debug(line);
		}
	}
	if (playing && !chain_due && play_wants_next() && list_cur >= 0 && order_has_next(&list_order, list_cur))
		chain_due = 1;		/* (opened in a turn of its own: not a Return's) */
	else if (playing && chain_due) {
		/* gapless: the next row chosen now, as at the end (Shuffle,
		   Repeat), and followed on in the queue where it can be */
		ENTRY *e;
		chain_due = 0;
		if (chain_row < 0)	/* (chosen once: Pause or a seek asks again) */
			chain_row = playable_from(list_cur, 1, 0);
		if (chain_row >= 0 && (e = list_at(chain_row)) != 0 && !play_chain(e->path))
			elog("gapless: the next file opens at this one's end");
	}
	if (bar_scroll_step())
		redraw(0, BP_TITLE);	/* (a step of the title's scroll: its two lines) */
	if (playing && play_step(used >= 20 ? 0 : (int)(20 - used))) {
		if (play_tags_late) {
			/* (a big VGZ's GD3, at its end: its row read again, now from
			   memory) */
			int i = list_find(play_path());
			play_tags_late = 0;
			if (i >= 0) {
				list_at(i)->read = 0;
				list_unread = 1;
			}
		}
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
	/* (Previous and Next: drawn alone, BP_NAV, not the whole bar as they
	   were, the album art, the title and the times again for a button's
	   colour: 100 ms of an STE's press, 13 Nov 2026; let go anywhere on
	   the bar, as before) */
	bar_area(part == BP_NAV ? BP_TEXT : part, &b);
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
	if (play_is_net())
		return;			/* (a station: nowhere to go) */
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
		alert(1, "[3][%s][ OK ]", err);
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

/* the window's messages while a station's picture is fetched (sart.c):
   redraws; a menu title let go (its command not taken then) */
static void fetch_message(const short *msg)
{
	if (msg[0] == WM_REDRAW)
		message((short *)msg);
	else if (msg[0] == MN_SELECTED)
		menu_tnormal(mb.tree, msg[3], 1);
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
	case WM_BOTTOMED:
		wind_set(msg[3], WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		/* EPlay's icon, the music going on (D47; the guidelines, 5.1) */
		if (iconified || bar_mini)
			break;
		remember_place();
		iconified = 1;
		wind_set(wh, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
		wind_set_str(wh, WF_NAME, "EPlay");	/* (the icon's title: no room for its spaces) */
		nf_debug("EPLAY iconified\n");		/* (the rigs) */
		break;
	case WM_UNICONIFY:
		if (!iconified)
			break;
		wind_set(wh, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]);
		wind_set_str(wh, WF_NAME, " EPlay ");
		iconified = 0;
		wind_get(wh, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
		bar_layout(&work);
		status_again();
		redraw(0, BP_ALL);
		nf_debug("EPLAY uniconified\n");
		break;
	case WM_FULLED: {
		/* as tall as the desk, and back: the width is the bar's */
		static short uy = -1, uh;	/* (the place before it was made full) */
		short x, y, w, h;
		if (iconified || bar_mini)
			break;
		wind_get(wh, WF_CURRXYWH, &x, &y, &w, &h);
		if (y == desk_y && h >= desk_h - 2 * bar_row_h() && uy >= 0 && uh < h) {
			wind_set(wh, WF_CURRXYWH, x, uy, w, h);
			sized(w, uh);
			uy = -1;
		} else {
			uy = y;
			uh = h;
			wind_set(wh, WF_CURRXYWH, x, desk_y, w, h);
			sized(w, desk_h);
		}
		redraw(0, BP_ALL);
		remember_place();
		break;
	}
	case WM_MOVED:
		if (iconified) {	/* (the icon goes where it is put, as it is) */
			wind_set(msg[3], WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			break;
		}
		msg[4] = wind_x8(KIND, msg[4], msg[5], msg[6], msg[7]);
		wind_set(msg[3], WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
		wind_get(wh, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
		bar_layout(&work);
		remember_place();
		break;
	case WM_SIZED:
		if (iconified)
			break;
		sized(msg[6], msg[7]);
		remember_place();
		break;
	case WM_ARROWED:
		switch (msg[4]) {
		case WA_UPPAGE:
			scroll_to(list_top - (bar_rows() > 1 ? bar_rows() - 1 : 1));	/* (W5) */
			break;
		case WA_DNPAGE:
			scroll_to(list_top + (bar_rows() > 1 ? bar_rows() - 1 : 1));
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

/* the window drawn again under a dialog window (Settings, About, Open
   URL in XaAES or MagiC): without it the AES's requests waited for the
   dialog to close (a tester, 9 Nov 2026) */
static void redraw_under(const short *m)
{
	GRECT r;
	if (m[0] != WM_REDRAW || m[3] != wh)
		return;
	r.x = m[4];
	r.y = m[5];
	r.w = m[6];
	r.h = m[7];
	redraw(&r, BP_ALL);
}

/* one step of quitting in EPLAY.LOG, on the disk at once: if the machine
   bombs after the window has gone (the hub, 10 Nov 2026: EFTP's and
   EMail's quit bombs), the last line says where */
static void quit_step(const char *what)
{
	elog("quit: %s", what);
	elog_save();
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
/* the bar's tips (D18, the guidelines' 3.4): what a control does and its
   key, where the pointer rests on it in EPlay's window on top */
static short tip_x = -1, tip_y = -1;

static int bar_tip(short x, short y, char *out, int size)
{
	const char *t;
	short top, k;
	if (wind_find(x, y) != wh)
		return 0;
	wind_get(0, WF_TOP, &top, &k, &k, &k);
	if (top != wh)
		return 0;
	switch (bar_hit(x, y)) {
	case BH_PREV:
		t = "Previous file (Shift+Left)";
		break;
	case BH_PLAY:
		t = play_state() == PS_PLAYING ? "Pause (Space)" : "Play (Space)";
		break;
	case BH_NEXT:
		t = "Next file (Shift+Right)";
		break;
	case BH_SHUFFLE:
		t = "Shuffle";
		break;
	case BH_REPEAT:
		t = "Repeat";
		break;
	case BH_POS:
		t = "Position: click or drag (Left and Right: 10 s)";
		break;
	case BH_VOL:
		t = "Volume: click or drag (+ and -)";
		break;
	default:
		return 0;
	}
	snprintf(out, (size_t)size, "%s", t);
	return 1;
}

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
	/* (MU_M1: the pointer left its place, for the tips: D18) */
	evnt_set_m1(1, tip_x, tip_y, 1, 1);
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER | MU_M1, 0x101, 3, 0,
			    nested || art_pending() || seek_to >= 0 || pending_play[0] || next_due || open_due || shown_due ? 0 :
			    tip_wait(list_unread ? 20 : scroll_wait((unsigned long)play_wait_ms())), msg, &ev);
	turn_t0 = tos_ms();
	if (which & MU_M1) {
		tip_x = ev.mx;
		tip_y = ev.my;
	}
	if (!nested)
		tip_tick(which, iconified || wh <= 0 ? -1 : tip_x, tip_y, bar_tip);
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
			/* (Repeat one: the same; gapless's choice where it was made) */
			int i = chain_row >= 0 ? chain_row : playable_from(list_cur, 1, 0);
			chain_row = -1;
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
	if (iconified)
		which &= ~(MU_KEYBD | MU_BUTTON);	/* (an icon takes no keys or clicks: taken, not kept) */
	if (which & MU_KEYBD) {
		short a = ev.kreturn & 0xff;
		if (key_ctrl(ev.kstate, ev.kreturn)) {
			if ((a == 15 || (a | 0x20) == 'o') && key_shift(ev.kstate, ev.kreturn))
				command(C_URL);		/* (Shift+Control+O: Open URL) */
			else if (a == 15 || (a | 0x20) == 'o')
				command(C_OPEN);
			else if (a == 19 || (a | 0x20) == 's')
				command(C_SAVE);
			else if (a == 1 || (a | 0x20) == 'a')
				command(C_SELALL);
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
		list_press(ev.mx, ev.my, ev.kstate, 1);
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
		else if (hit == BH_PREV && order_has_prev(&list_order, list_cur) && track(BH_PREV, BP_NAV))
			command(C_PREV);
		else if (hit == BH_NEXT && order_has_next(&list_order, list_cur) && track(BH_NEXT, BP_NAV))
			command(C_NEXT);
		else if (hit == BH_LIST)
			list_press(ev.mx, ev.my, ev.kstate, 0);
	}
	/* (a turn that drew a piece of the window reads no more of the file:
	   a 32 KB read after it went past the 50 ms between event calls; the
	   queue is filled the turn after; nor one after which another file
	   opens: the next turn stops this one, so its frames would be wasted,
	   and Next's rows would wait for them) */
	step(playing && !piece && !open_due && !next_due);
	menu_update();			/* (greyed as things are now: MENUS 1.3) */
	play_idle_give();		/* (the sound system for others while nothing plays) */
}

#if defined(EPLAY_FB)
/* EPLAYFB.PRG: common's cpucheck_cf.S asks for _CF_ */
const char cpu_alert[] = "[3][This EPlay is for the FireBee.|On a Falcon or a TT: EPLAY.PRG;|on an STE: EPLAYST.PRG.][ Quit ]";
#elif defined(EPLAY_ST)
const long cpu_need = 0;	/* EPLAYST.PRG: the STE's (and any 68000 with DMA sound) */
const char cpu_alert[] = "[3][EPlay can't run here.][ Quit ]";
#else
const long cpu_need = 30;
const char cpu_alert[] = "[3][This EPlay is for the Falcon and|the TT. On an STE: EPLAYST.PRG.][ Quit ]";
#endif
int app_main(void);
int app_main(void)
{
	short work_out[57], d;
	const char *why;

	{
		/* first: a 68060 takes div64.c's 32-bit way (no 64-bit mulu.l
		   or divu.l there), anything before it the one instruction */
		extern int div64_cpu60;
		div64_cpu60 = get_cookie(0x5F435055L) >= 60;	/* '_CPU' */
	}
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
	term_install();			/* (the sound's interrupts taken away however EPlay ends) */
	tip_init(0);			/* (the AES redraws what a tip covered) */
	pf_idle = busy_idle;		/* (Esc or Undo while a station connects: busy.h) */
	sart_init(fetch_message);
	crash_cleanup = crash_quit;
	dlg_redraw_hook = redraw_under;
	av_init("EPLAY|EPLAYST");	/* (the name the AES knows: EPLAYST.PRG on an STE) */
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
	conn_menu_shown();
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
#ifdef EPLAY_FB
	if (1) {
		/* the FireBee: FireTOS's XBIOS sound at 49,170 Hz, files resampled
		   to it by the ColdFire (sndfb.c) */
		elog("the sound: FireTOS's XBIOS (the FireBee's AC'97), 49170 Hz, files resampled by the ColdFire; "
		     "MP3 decoded by it (minimp3)");
		status("The FireBee's sound: MP3, WAV and AIFF.");
	} else
#endif
	if (snd_dma8) {
		/* a TT or an STE (phase 6): its 8-bit DMA, WAV and AIFF */
		elog("the sound: 8-bit DMA (a TT's or an STE's), the music %s",
		     snd_dma8_interp ? "interpolated and dithered by the 68030" : "at the nearest sample");
		status("8-bit DMA sound: WAV and AIFF. MP3 needs a Falcon's DSP.");
	} else {
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
	}
	crash_offer("EPlay", "in EPlay's folder");
	{
		const char *cmd = av_command();	/* started with files */
		if (*cmd)
			add_names(cmd, 1);
	}

	while (!quit) {
		turn(0);
		if (play_is_net()) {
			/* a station's picture: its address from its titles */
			int ch;
			const char *pic = play_net_picture(&ch);
			if (ch)
				sart_want(pic);
		}
		if (art_pending() && art_step(art_turn))
			redraw(0, BP_ART);	/* (the album art made: shown) */
		else if (play_is_net() && sart_step())
			redraw(0, BP_ART);	/* (the disc again) */
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
	snd_exit();
	quit_step("the sound stopped, the DSP and the matrix given back");
	remember_place();
	settings_flush();		/* (the volume, if it changed) */
	quit_step("settings saved");
	if (wh > 0) {
		av_window(wh, 0);
		wind_close(wh);
		wind_delete(wh);
	}
	quit_step("the window closed");
	menu_bar(mb.tree, 0);
	bar_colours_back();
	av_exit();
	v_clsvwk(vdi_h);
	quit_step("colours, menu bar and VDI given back");
	crash_remove();
	{
		const char *o = crash_over();
		char t[220];
		snprintf(t, sizeof(t), "crash catcher off%s%s", *o ? "; hooked over it: " : "", o);
		quit_step(t);
	}
	appl_exit();
	quit_step("out of the AES");
	return 0;
}
