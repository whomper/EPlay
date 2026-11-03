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
#include "elog.h"
#include "snd.h"
#include "bar.h"
#include "art.h"

extern const char app_version[];

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id;

enum { C_ABOUT = 1, C_OPEN, C_QUIT, C_PLAY, C_STOP, C_SETTINGS };

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
static const MBAR_ITEM m_play[] = {
	{ "Play/Pause", "Space", C_PLAY },
	{ "Stop", 0, C_STOP },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_options[] = {
	{ "Settings...", "^,", C_SETTINGS },
	{ 0, 0, 0 }
};
static const MBAR_TITLE bar[] = {
	{ " EPlay ", m_desk },
	{ " File ", m_file },
	{ " Play ", m_play },
	{ " Options ", m_options },
	{ 0, 0 }
};

#define KIND (NAME | CLOSER | MOVER | INFO)

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
}

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
		snprintf(st, sizeof(st), "Playing  \xfa  %s", play_out);
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
			strcpy(shown, now);
			redraw(&work, BP_ALL);
		} else
			redraw(0, BP_PLAY | BP_TIME);
	}
}

static void open_file(void)
{
	short button = 0;
	if (!path[0])
		snprintf(path, sizeof(path), "%s*.*", app_dir());
	if (!fsel_exinput(path, name, &button, "Play which file?") || !button || !name[0])
		return;
	{
		char *e = strrchr(path, '\\');
		long n = e ? e - path + 1 : 0;
		snprintf(file, sizeof(file), "%.*s%s", (int)n, path, name);
	}
	play_file(file);
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
				snprintf(st, sizeof(st), "Playing  \xfa  %s", play_out);
				status(st);
				snprintf(st, sizeof(st), "EPLAY resumed at %ld ms\n", play_ms());
				nf_debug(st);		/* (the rigs) */
			}
			redraw(0, BP_PLAY | BP_TIME);
		} else if (play_state() != PS_PLAYING && play_is_open()) {
			/* (opened at a place the position bar went to, stopped) */
			char st[100];
			play_start();
			snprintf(st, sizeof(st), "Playing  \xfa  %s", play_out);
			status(st);
			redraw(0, BP_PLAY | BP_TIME);
		} else if (play_state() != PS_PLAYING && file[0])
			play_file(file);
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

static void step(int playing)
{
	unsigned long used = tos_ms() - turn_t0;
	if (playing && play_step(used >= 20 ? 0 : (int)(20 - used))) {
		if (play_state() != PS_PLAYING) {
			status(play_out[0] == 'T' ? play_out : "Played to the end.");
			redraw(0, BP_PLAY | BP_TIME);
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
	case AV_START: {
		char f[256];
		if (av_arg(s, f, sizeof(f))) {
			snprintf(file, sizeof(file), "%s", f);
			play_file(file);
		}
		return;
	}
	case AV_NONE:
		break;
	default:
		return;
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
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1,
			    nested || art_pending() || seek_to >= 0 ? 0 : play_wait_ms(), msg, &ev);
	turn_t0 = tos_ms();
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
	}
	if ((which & MU_BUTTON) && ev.mbutton == 1) {
		/* (Play/Pause only where it would act: with a file; the
		   playlist's controls come with phase 4) */
		int hit = bar_hit(ev.mx, ev.my);
		if (hit == BH_PLAY && (play_name[0] || file[0]) && track(BH_PLAY, BP_PLAY))
			command(C_PLAY);	/* Play/Pause */
		else if (hit == BH_POS && play_name[0] && play_len_ms() > 0)
			drag_pos(ev.mx, ev.my);
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
	if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h)) {
		form_alert(1, "[3][EPlay: not enough memory.][ Quit ]");
		snd_exit();
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
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
		const char *cmd = av_command();	/* started with a file */
		char f[256];
		if (*cmd && av_arg(cmd, f, sizeof(f))) {
			snprintf(file, sizeof(file), "%s", f);
			play_file(file);
		}
	}

	while (!quit) {
		turn(0);
		if (art_pending() && art_step(art_turn))
			redraw(0, BP_ART);	/* (the album art made: shown) */
	}

	play_log_stop();
	play_stop();
	snd_exit();
	elog("quit");
	elog_save();
	menu_bar(mb.tree, 0);
	bar_colours_back();
	if (wh > 0) {
		wind_close(wh);
		wind_delete(wh);
	}
	av_exit();
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
