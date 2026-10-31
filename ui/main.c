/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * main.c - EPlay 0.1: a small window to try the player with. The file's
 * name, what it is, where it plays (the FDI's clock or the Falcon's own),
 * the time; Open, Play and Stop. Files come from the file selector, the
 * command line, or the desktop (VA_START). The player's own window, its
 * playlist and keys come in later phases (docs/DESIGN.md).
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "mbar.h"
#include "alert.h"
#include "av.h"
#include "crash.h"
#include "aboutbox.h"
#include "play.h"
#include "elog.h"
#include "snd.h"

extern const char app_version[];

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id;

enum { C_ABOUT = 1, C_OPEN, C_QUIT, C_PLAY, C_STOP };

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
	{ "Play", 0, C_PLAY },
	{ "Stop", 0, C_STOP },
	{ 0, 0, 0 }
};
static const MBAR_TITLE bar[] = {
	{ " EPlay ", m_desk },
	{ " File ", m_file },
	{ " Play ", m_play },
	{ 0, 0 }
};

#define COLS 50
#define ROWS 7
#define KIND (NAME | CLOSER | MOVER | INFO)

static MBAR mb;
static short wh = -1;
static GRECT work;
static int quit;
static char path[256], name[64], file[320];	/* the file selector's, and the file */
static GRECT b_open, b_play, b_stop;

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

static void fmt_time(char *out, int n, long ms)
{
	long s = ms / 1000;
	snprintf(out, n, "%ld:%02ld", s / 60, s % 60);
}

static void layout(void)
{
	short y = work.y + 5 * ch + ch / 2, w = 12 * cw;
	b_open.x = work.x + 8;
	b_play.x = b_open.x + w + 2 * cw;
	b_stop.x = b_play.x + w + 2 * cw;
	b_open.y = b_play.y = b_stop.y = y;
	b_open.w = b_play.w = b_stop.w = w;
	b_open.h = b_play.h = b_stop.h = ch + 4;
}

static void button(GRECT *b, const char *s, int on)
{
	short n = (short)strlen(s);
	fill(b, 0);
	frame(b, 1);
	text_at(b->x + (b->w - n * cw) / 2, b->y + 2, s, n, n, on ? 0 : TX_DIM);
}

/* the rows 0..3 and the buttons, within area */
static void draw_rows(GRECT *area, int from, int to)
{
	char t[80], a[20], b[20];
	int r;
	GRECT box;
	box.x = work.x;
	box.w = work.w;
	box.y = work.y + from * ch + 4;
	box.h = (to - from + 1) * ch;
	if (to >= ROWS - 1) {
		box.y = work.y;
		box.h = work.h;
	}
	if (!rc_intersect(area, &box))
		return;
	clip_on(&box);
	fill(&box, 0);
	for (r = from; r <= to && r < 4; r++) {
		short y = work.y + 4 + r * ch;
		const char *s = "";
		short fl = 0;
		switch (r) {
		case 0:
			s = play_name[0] ? play_name : "No file: File > Open... (^O)";
			fl = play_name[0] ? TX_BOLD : 0;
			break;
		case 1:
			s = play_info;
			break;
		case 2:
			s = play_out;
			break;
		case 3:
			if (!play_name[0])
				break;
			fmt_time(a, sizeof(a), play_ms());
			fmt_time(b, sizeof(b), play_len_ms());
			snprintf(t, sizeof(t), "%s / %s%s", a, b,
				 play_state() == PS_PLAYING ? "" : "  (stopped)");
			s = t;
			break;
		}
		text_at(work.x + 8, y, s, (long)strlen(s), COLS - 2, fl);
	}
	if (to >= ROWS - 1) {
		button(&b_open, "Open...", 1);
		button(&b_play, "Play", play_name[0] && play_state() != PS_PLAYING);
		button(&b_stop, "Stop", play_state() == PS_PLAYING);
	}
	clip_off();
}

/* rows from..to again, through the AES's rectangles */
static void redraw(GRECT *area, int from, int to)
{
	GRECT r, a;
	if (wh <= 0)
		return;
	if (!area) {
		a = work;
		area = &a;
	}
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(wh, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w && r.h) {
		if (rc_intersect(area, &r))
			draw_rows(&r, from, to);
		wind_get(wh, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

static void open_window(void)
{
	short x, y, w, h;
	wind_calc(WC_BORDER, KIND, desk_x, desk_y, COLS * cw, ROWS * ch + 8, &x, &y, &w, &h);
	x = desk_x + (desk_w - w) / 2;
	y = desk_y + (desk_h - h) / 3;
	wh = wind_create(KIND, desk_x, desk_y, desk_w, desk_h);
	if (wh <= 0)
		return;
	wind_set_str(wh, WF_NAME, " EPlay ");
	wind_open(wh, x, y, w, h);
	wind_get(wh, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	layout();
}

static void play_file(const char *f)
{
	char err[160];
	if (!play_open(f, err, sizeof(err))) {
		char line[200];
		snprintf(line, sizeof(line), "EPLAY refused: %s\n", err);
		nf_debug(line);
		alert(1, "[1][%s][ OK ]", err);
		redraw(0, 0, ROWS - 1);
		return;
	}
	play_start();
	{
		char line[200];
		snprintf(line, sizeof(line), "EPLAY playing: %s | %s | %s\n", play_name, play_info, play_out);
		nf_debug(line);
	}
	status("Playing.");
	redraw(0, 0, ROWS - 1);
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
	case C_PLAY:
		if (play_state() != PS_PLAYING && file[0])
			play_file(file);
		break;
	case C_STOP:
		if (play_state() == PS_PLAYING) {
			elog("stopped at %ld ms; %ld underruns", play_ms(), snd_underruns);
			play_stop();
			elog_save();
			status("Stopped.");
			redraw(0, 0, ROWS - 1);
		}
		break;
	}
}

static int inside(const GRECT *b, short x, short y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
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
		redraw(&r, 0, ROWS - 1);
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
		layout();
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
}

const long cpu_need = 30;
const char cpu_alert[] = "[3][EPlay needs a Falcon.][ Quit ]";
int app_main(void);
int app_main(void)
{
	short work_out[57], msg[8], d;
	const char *why;
	AEVENT ev;

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
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	crash_install(app_dir(), "EPlay", app_version);
	crash_cleanup = crash_quit;
	av_init("EPLAY");
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
	{
		char line[60];
		snprintf(line, sizeof(line), "EPLAY external clock: %ld Hz\n", snd_ext_rate);
		nf_debug(line);
	}
	elog("EPlay %s; _MCH $%08lX, _CPU %ld, _SND $%02lX; the FDI's driver %s", app_version,
	     get_cookie(0x5F4D4348L), get_cookie(0x5F435055L), get_cookie(0x5F534E44L),
	     snd_fdi ? "answers ($4711)" : "doesn't answer");
	elog("the DSP port's clock: %ld Hz at prescale 1 (a clock of %ld Hz); taken as %ld Hz",
	     snd_ext_raw, snd_ext_clock, snd_ext_rate);
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
		short which;
		int playing = play_state() == PS_PLAYING;
		which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1,
				    play_wait_ms(), msg, &ev);
		if (which & MU_MESAG)
			message(msg);
		if (which & MU_KEYBD) {
			short a = ev.kreturn & 0xff;
			if (key_ctrl(ev.kstate, ev.kreturn)) {
				if (a == 15 || (a | 0x20) == 'o')
					command(C_OPEN);
				else if (a == 17 || a == 21 || (a | 0x20) == 'q' || (a | 0x20) == 'u')
					command(C_QUIT);
			}
		}
		if ((which & MU_BUTTON) && ev.mbutton == 1) {
			if (inside(&b_open, ev.mx, ev.my))
				command(C_OPEN);
			else if (inside(&b_play, ev.mx, ev.my))
				command(C_PLAY);
			else if (inside(&b_stop, ev.mx, ev.my))
				command(C_STOP);
		}
		if (playing && play_step(20)) {
			if (play_state() != PS_PLAYING) {
				status(play_out[0] == 'T' ? play_out : "Played to the end.");
				redraw(0, 0, ROWS - 1);
			} else {
				redraw(0, 3, 3);
			}
		}
	}

	play_stop();
	snd_exit();
	elog("quit");
	elog_save();
	menu_bar(mb.tree, 0);
	if (wh > 0) {
		wind_close(wh);
		wind_delete(wh);
	}
	av_exit();
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
