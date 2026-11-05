/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bar.c - the window's bar and list (bar.h). Rectangles and lines are the
 * VDI's; a disc or a triangle is a 1-plane mask made once (bar_layout)
 * in a small block of ST RAM taken at the start, and blitted in its pen
 * (vrt_cpyfm, transparent), cut to the redraw's rectangle by hand as a
 * raster copy isn't clipped. Drawn as its rows, one fill each, the window
 * took a stock Falcon 165 ms. Sizes follow the system font's cell (ch 16
 * on VGA: the bar 68 pixels high, the art 56).
 *
 * Colours: in 256 colours and more, pens 16 to 22 are set to the bar's
 * (the guidelines' 5.2: put back at the end); in 16 colours the bar is
 * black with white text and the others are the nearest of pens 0 to 15;
 * in mono black and white, the position's fill thicker than its track,
 * the controls that don't work yet in a grey pattern.
 */
#include <string.h>
#include <stdio.h>
#include "bar.h"
#include "play.h"
#include "text.h"
#include "dialog.h"
#include "art.h"
#include "list.h"

int bar_held = BH_NONE, bar_drag = -1, bar_vdrag = -1;

#define POOL_WORDS 1536
static unsigned short *pool;		/* ST RAM: the masks (bar_colours takes it) */

/* the pens: the bar, its text, the dim text, the accent, the track, the
   list's other rows, the list's header text */
static short p_bar, p_txt, p_dim, p_acc, p_trk, p_alt, p_hdr;
static int mono, themed;
#define TPENS 7
static short saved[TPENS][3];
static const short theme[TPENS][3] = {
	{ 114, 133, 188 },		/* #1d2230 the bar */
	{ 1000, 1000, 1000 },		/* its text */
	{ 643, 671, 722 },		/* #a4abb8 dim */
	{ 247, 714, 1000 },		/* #3fb6ff the accent */
	{ 290, 318, 380 },		/* #4a5161 the track */
	{ 945, 953, 965 },		/* #f1f3f6 the list's other rows */
	{ 290, 322, 384 },		/* #4a5262 the header */
};

void bar_colours(void)
{
	int i;
	mono = planes < 2;
	themed = planes >= 8;
	if (!pool) {
		/* (ST RAM at the start, before any file's queue: 0.13's lesson) */
		void *p = Mxalloc((long)POOL_WORDS * 2, 0);
		pool = (long)p > 0 ? p : 0;
	}
	if (themed) {
		for (i = 0; i < TPENS; i++) {
			vq_color(vdi_h, 16 + i, saved[i]);
			vs_color(vdi_h, 16 + i, theme[i]);
		}
		p_bar = 16;
		p_txt = 17;
		p_dim = 18;
		p_acc = 19;
		p_trk = 20;
		p_alt = 21;
		p_hdr = 22;
	} else if (mono) {
		p_bar = 1;
		p_txt = p_dim = p_acc = p_trk = 0;
		p_alt = 0;
		p_hdr = 1;
	} else {
		short n = (short)(1 << planes);
		p_bar = 1;
		p_txt = 0;
		p_dim = pen_nearest(vdi_h, n, 643, 671, 722, 0);
		p_acc = pen_nearest(vdi_h, n, 247, 714, 1000, 0);
		p_trk = pen_nearest(vdi_h, n, 290, 318, 380, 0);
		p_alt = 0;
		p_hdr = pen_nearest(vdi_h, n, 290, 322, 384, 1);
		if (p_dim == p_bar)
			p_dim = 0;
		if (p_acc == p_bar)
			p_acc = 0;
		if (p_trk == p_bar || p_trk == p_acc)
			p_trk = p_dim;
	}
}

void bar_colours_back(void)
{
	int i;
	if (themed)
		for (i = 0; i < TPENS; i++)
			vs_color(vdi_h, 16 + i, saved[i]);
}

/* ---- the layout ---- */

#define LIST_ROWS 6

static GRECT w_all, w_bar, w_art, w_txt, w_ctl, w_pos, w_out, w_list;
static GRECT b_btn[6];		/* by BH_SHUFFLE .. BH_REPEAT */
static GRECT w_track, w_vol;
static short bar_h, row_h, rad;

static void make_masks(void);

int bar_mini;			/* the mini player: the bar alone */

void bar_size(short *w, short *h)
{
	short bh = 4 * ch + 4, rh = ch + 4;
	*w = 56 * cw;		/* (Erez, 3 Nov 2026: 56 columns, one row) */
	*h = bar_mini ? bh : bh + (1 + LIST_ROWS) * rh;
}

static void rect(GRECT *r, short x, short y, short w, short h)
{
	r->x = x;
	r->y = y;
	r->w = w;
	r->h = h;
}

static short scw, sch;		/* the badges' small font: its cell */

void bar_layout(const GRECT *work)
{
	short x, y, art, mid, bw, gap;
	w_all = *work;
	bar_h = 4 * ch + 4;
	row_h = ch + 4;
	rect(&w_bar, work->x, work->y, work->w, bar_h);
	art = bar_h - 12;
	rect(&w_art, work->x + 6, work->y + 6, art, art);
	/* the title, the artist, and under them the labels: room for both
	   ("320 KBPS", "44.1 KHZ") */
	rect(&w_txt, w_art.x + art + 10, work->y + 6, 19 * cw, bar_h - 12);
	/* the right: the output over the speaker and the volume */
	rect(&w_out, work->x + work->w - 6 - 7 * cw, work->y, 7 * cw, bar_h);
	rect(&w_vol, w_out.x + 18, work->y + bar_h - 6 - ch / 2 - 5, w_out.w - 18, 10);
	/* the middle: Previous, Play/Pause, Next, the position under them
	   (Shuffle and Repeat: the Play menu's) */
	x = w_txt.x + w_txt.w + 6;
	rect(&w_ctl, x, work->y, w_out.x - 8 - x, bar_h);
	rad = (ch * 13 + 8) / 16;		/* the round button's radius: 13 on VGA */
	bw = (ch * 22 + 8) / 16;
	gap = 6;
	mid = w_ctl.x + w_ctl.w / 2;
	y = work->y + 6;
	rect(&b_btn[BH_PLAY - 1], mid - rad, y, 2 * rad, 2 * rad);
	rect(&b_btn[BH_PREV - 1], mid - rad - gap - bw, y + rad - bw / 2, bw, bw);
	rect(&b_btn[BH_NEXT - 1], mid + rad + gap, y + rad - bw / 2, bw, bw);
	rect(&b_btn[BH_SHUFFLE - 1], 0, 0, 0, 0);
	rect(&b_btn[BH_REPEAT - 1], 0, 0, 0, 0);
	/* the times, 5 characters and a gap each side of the track */
	y = w_vol.y + 5 - ch / 2;
	rect(&w_pos, w_ctl.x, y, w_ctl.w, ch);
	rect(&w_track, w_ctl.x + 5 * cw + 4, y + ch / 2 - 2, w_ctl.w - 10 * cw - 8, 4);
	rect(&w_list, work->x, work->y + bar_h, work->w, work->h - bar_h);
	{
		short a, b;
		vst_height(vdi_h, 4, &scw, &sch);	/* (the badges' small font: its cell) */
		vst_height(vdi_h, ch >= 16 ? 13 : 6, &a, &b);
		if (scw <= 0 || sch <= 0) {
			scw = cw;
			sch = ch;
		}
	}
	make_masks();
}

void bar_area(int parts, GRECT *r)
{
	if (parts == BP_TIME)
		*r = w_pos;
	else if (parts == BP_PLAY)
		*r = b_btn[BH_PLAY - 1];
	else if (parts == BP_LIST)
		*r = w_list;
	else if (parts == BP_ART)
		*r = w_art;
	else if (parts == BP_LEFT) {
		*r = w_bar;
		r->w = w_ctl.x - w_bar.x;
		r->h -= 1;
	} else if (parts == BP_RIGHT) {
		*r = w_bar;
		r->x = w_out.x;
		r->w = w_bar.x + w_bar.w - w_out.x;
		r->h -= 1;
	} else if (parts == BP_NAV) {
		*r = b_btn[BH_PREV - 1];
		r->w = b_btn[BH_NEXT - 1].x + b_btn[BH_NEXT - 1].w - r->x;
	} else if (parts == BP_VOL) {
		*r = w_vol;
		r->x -= 18;
		r->w += 18;
	}
	else if (parts == (BP_TIME | BP_PLAY)) {
		*r = w_ctl;
	} else
		*r = w_all;
}

static int in(const GRECT *b, short x, short y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

int bar_hit(short x, short y)
{
	int i;
	GRECT pos;
	for (i = 0; i < 5; i++)
		if (in(&b_btn[i], x, y))
			return BH_SHUFFLE + i;
	pos = w_track;
	pos.y = w_pos.y - 2;
	pos.h = w_pos.h + 4;
	pos.x -= 4;
	pos.w += 8;
	if (in(&pos, x, y))
		return BH_POS;
	pos = w_vol;			/* (a little taller and wider than it looks) */
	pos.x -= 4;
	pos.w += 8;
	pos.y -= 3;
	pos.h += 6;
	if (in(&pos, x, y))
		return BH_VOL;
	if (in(&w_list, x, y))
		return BH_LIST;
	return BH_NONE;
}

int bar_pos_at(short x)
{
	long p = (long)(x - w_track.x) * 1000 / (w_track.w > 0 ? w_track.w : 1);
	return p < 0 ? 0 : p > 1000 ? 1000 : (int)p;
}

/* ---- drawing ---- */

static short cur_fill = -1, cur_pat = -1, cur_line = -1, cur_tcol = -1, cur_fx = -1;

static void box(short x, short y, short w, short h, short pen, int pat)
{
	short p[4];
	if (w <= 0 || h <= 0)
		return;
	if (cur_pat != pat) {
		vsf_interior(vdi_h, pat ? 2 : 1);
		if (pat)
			vsf_style(vdi_h, 4);
		vsf_perimeter(vdi_h, 0);
		cur_pat = pat;
	}
	if (cur_fill != pen) {
		vsf_color(vdi_h, pen);
		cur_fill = pen;
	}
	p[0] = x;
	p[1] = y;
	p[2] = x + w - 1;
	p[3] = y + h - 1;
	vr_recfl(vdi_h, p);
}

static void gbox(const GRECT *r, short pen)
{
	box(r->x, r->y, r->w, r->h, pen, 0);
}

static void line(short x1, short y1, short x2, short y2, short pen)
{
	short p[4];
	if (cur_line != pen) {
		vsl_color(vdi_h, pen);
		cur_line = pen;
	}
	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	v_pline(vdi_h, 2, p);
}

/* text in a pen, transparent, cut to cols characters */
static void text(short x, short y, const char *s, short cols, short pen)
{
	long n = (long)strlen(s);
	if (n > cols)
		n = cols;
	if (n <= 0)
		return;
	if (!(cw & 7))
		x = (x + 3) & ~7;
	if (cur_tcol != pen) {
		vst_color(vdi_h, pen);
		cur_tcol = pen;
	}
	if (cur_fx != 0) {
		vst_effects(vdi_h, 0);
		cur_fx = 0;
	}
	text_n(vdi_h, x, y, s, n);
}

/* ---- the masks ---- */

typedef struct {
	short w, h, wpl;
	unsigned short *bits;
} MASK;

static long pool_used;
static GRECT clip_r;			/* the redraw's rectangle */

static short isqrt(long v)
{
	short r = 0;
	while ((long)(r + 1) * (r + 1) <= v)
		r++;
	return r;
}

static int mask_new(MASK *m, short w, short h)
{
	long n;
	m->w = w;
	m->h = h;
	m->wpl = (short)((w + 15) / 16);
	n = (long)m->wpl * h;
	m->bits = 0;
	if (!pool || pool_used + n > POOL_WORDS)
		return 0;
	m->bits = pool + pool_used;
	pool_used += n;
	memset(m->bits, 0, (size_t)n * 2);
	return 1;
}

static void mask_run(MASK *m, short y, short x0, short x1)
{
	short x;
	for (x = x0; x < x1; x++)
		m->bits[y * m->wpl + (x >> 4)] |= 0x8000 >> (x & 15);
}

static void mask_disc(MASK *m, short r)
{
	short dy;
	if (!mask_new(m, 2 * r, 2 * r))
		return;
	for (dy = -r; dy < r; dy++) {
		long yy = 2 * dy + 1;
		short dx = isqrt(((long)4 * r * r - yy * yy) / 4);
		mask_run(m, dy + r, r - dx, r + dx);
	}
}

/* pointing right (dir 1) or left (-1), w by h */
static void mask_tri(MASK *m, short w, short h, int dir)
{
	short i;
	if (!mask_new(m, w, h))
		return;
	for (i = 0; i < h; i++) {
		short d = i < h - 1 - i ? i : h - 1 - i;
		short len = (short)(((long)w * (2 * d + 1) + h - 1) / h);
		if (len > w)
			len = w;
		mask_run(m, i, dir > 0 ? 0 : w - len, dir > 0 ? len : w);
	}
}

static void mask_draw(const MASK *m, short x, short y, short pen)
{
	GRECT r;
	MFDB src, dst;
	short pxy[8], colors[2];
	if (!m->bits)
		return;
	rect(&r, x, y, m->w, m->h);
	if (!rc_intersect(&clip_r, &r))
		return;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = m->bits;
	src.fd_w = (short)(m->wpl * 16);
	src.fd_h = m->h;
	src.fd_wdwidth = m->wpl;
	src.fd_nplanes = 1;
	pxy[0] = r.x - x;
	pxy[1] = r.y - y;
	pxy[2] = pxy[0] + r.w - 1;
	pxy[3] = pxy[1] + r.h - 1;
	pxy[4] = r.x;
	pxy[5] = r.y;
	pxy[6] = r.x + r.w - 1;
	pxy[7] = r.y + r.h - 1;
	colors[0] = pen;
	colors[1] = 0;
	vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);	/* transparent */
}

/* the shapes, made for the layout's sizes */
static MASK k_art, k_hole, k_spot, k_play, k_ptri, k_prev, k_next, k_spk;

static void make_masks(void)
{
	short s = (short)(rad * 10 / 13), h = b_btn[BH_PREV - 1].h / 2;
	pool_used = 0;
	mask_disc(&k_art, w_art.w / 2 - 3);
	mask_disc(&k_hole, 6);
	mask_disc(&k_spot, 2);
	mask_disc(&k_play, rad);
	mask_tri(&k_ptri, s * 3 / 4 + 1, s, 1);
	mask_tri(&k_prev, h * 3 / 4 + 1, h, -1);
	mask_tri(&k_next, h * 3 / 4 + 1, h, 1);
	mask_tri(&k_spk, 5, 13, -1);
}

static void fmt_time(char *out, int n, long ms)
{
	long s = ms / 1000;
	snprintf(out, (size_t)n, "%ld:%02ld", s / 60, s % 60);
}

short bar_art_side(void)
{
	return w_art.w;
}

static void draw_art(void)
{
	/* the file's own picture; else EPlay's, a record */
	short cx = w_art.x + w_art.w / 2, cy = w_art.y + w_art.h / 2;
	if (art_ready()) {
		art_draw(w_art.x, w_art.y, &clip_r);
		return;
	}
	gbox(&w_art, p_trk);
	mask_draw(&k_art, cx - k_art.w / 2, cy - k_art.h / 2, p_bar);
	mask_draw(&k_hole, cx - 6, cy - 6, p_acc);
	mask_draw(&k_spot, cx - 2, cy - 2, p_bar);
}

/* a badge (the bit rate, the rate, where it plays): its text in the
   system font's small size (6 x 6), a quarter narrower than the window's
   and half as tall (Erez, 5 Nov 2026: "too big"), in a box a pixel
   clear of it all round, centred in the row of height rh at y. Its
   width; draw 0: only measured */
static short badge(short x, short y, short rh, const char *s, short pen, int draw)
{
	short n = (short)strlen(s), w = n * scw + 5, h = sch + 4, by = y + (rh - h) / 2;
	if (!draw || n <= 0)
		return w;
	box(x, by, w, 1, pen, 0);
	box(x, by + h - 1, w, 1, pen, 0);
	box(x, by, 1, h, pen, 0);
	box(x + w - 1, by, 1, h, pen, 0);
	{
		short a, b;
		vst_height(vdi_h, 4, &a, &b);
		text_font(vdi_h, 1, scw, sch);
		if (cur_tcol != pen) {
			vst_color(vdi_h, pen);
			cur_tcol = pen;
		}
		if (cur_fx != 0) {
			vst_effects(vdi_h, 0);
			cur_fx = 0;
		}
		/* (the glyphs' blank column is their right one: 3 left, 2 right) */
		text_n(vdi_h, x + 3, by + 2, s, n);
		vst_height(vdi_h, ch >= 16 ? 13 : 6, &a, &b);
		text_font(vdi_h, 1, cw, ch);
	}
	return w;
}

static void draw_text(void)
{
	char t[24];
	const char *title = play_tags.title[0] ? play_tags.title : play_name[0] ? play_name : "No file";
	const char *artist = play_tags.artist[0] ? play_tags.artist : play_name[0] ? "" : "File > Open... (^O)";
	short x = w_txt.x, cols = (short)(w_txt.w / cw);
	text(x, w_txt.y, title, cols, p_txt);
	text(x, w_txt.y + ch + 2, artist, cols, p_dim);
	if (play_name[0]) {
		short y = w_txt.y + 2 * ch + 6;
		if (play_kbps > 0) {
			snprintf(t, sizeof(t), "%ld KBPS", play_kbps);
			x += badge(x, y, ch + 2, t, p_dim, 1) + 4;
		}
		if (play_hz > 0) {
			if (play_hz % 1000)
				snprintf(t, sizeof(t), "%ld.%ld KHZ", play_hz / 1000, play_hz % 1000 / 100);
			else
				snprintf(t, sizeof(t), "%ld KHZ", play_hz / 1000);
			badge(x, y, ch + 2, t, p_dim, 1);
		}
	}
}

/* the speaker (its waves as loud as it is) and the volume, drawn as the
   position bar is (Erez, 3 Nov 2026) */
static void draw_vol(void)
{
	short x = w_vol.x - 18, y = w_vol.y + 5;
	int v = bar_vdrag >= 0 ? bar_vdrag : play_volume();
	short fillw = (short)((long)w_vol.w * v / VOL_MAX);
	box(x, w_vol.y, w_vol.x - x, w_vol.h, p_bar, 0);
	box(w_vol.x, w_vol.y, w_vol.w, w_vol.h, p_bar, 0);
	box(x, y - 2, 4, 5, p_txt, 0);
	mask_draw(&k_spk, x + 3, y - 6, p_txt);
	if (v > 0)
		line(x + 10, y - 2, x + 10, y + 2, p_txt);
	if (v > VOL_MAX / 2)
		line(x + 13, y - 4, x + 13, y + 4, p_txt);
	box(w_vol.x, w_vol.y + 4, w_vol.w, 2, p_trk, 0);
	box(w_vol.x, w_vol.y + 3, fillw, 4, p_acc, 0);
}

int bar_vol_at(short x)
{
	long v = ((long)(x - w_vol.x) * VOL_MAX + w_vol.w / 2) / (w_vol.w > 0 ? w_vol.w : 1);
	return v < 0 ? 0 : v > VOL_MAX ? VOL_MAX : (int)v;
}

static void draw_out(void)
{
	short x = w_out.x, y = w_out.y + 8;
	if (play_via && play_via[0]) {
		/* (over the speaker and the volume, in the middle of them) */
		short w = badge(0, 0, 0, play_via, p_acc, 0);
		badge(x + (w_out.w - w) / 2, y, ch + 2, play_via, p_acc, 1);
	}
	draw_vol();
}

static void draw_buttons(int parts)
{
	int pressed = bar_held == BH_PLAY;
	GRECT *b = &b_btn[BH_PLAY - 1];
	short cx = b->x + rad, cy = b->y + rad, s = (short)(rad * 10 / 13);
	if (parts & BP_PLAY) {
		gbox(b, p_bar);
		mask_draw(&k_play, cx - rad, cy - rad, pressed ? p_acc : p_txt);
		if (play_state() == PS_PLAYING) {
			/* Pause's two bars */
			box(cx - s / 2 - 1, cy - s / 2, s / 3 + 1, s, p_bar, 0);
			box(cx + s / 2 - s / 3, cy - s / 2, s / 3 + 1, s, p_bar, 0);
		} else
			mask_draw(&k_ptri, cx - s / 3, cy - s / 2, play_name[0] ? p_bar : p_trk);
	}
	if (parts & (BP_TEXT | BP_NAV)) {
		/* Previous, Next: the list's, dim where there is none */
		int i;
		short pen;
		for (i = 0; i < 5; i++) {
			GRECT *c = &b_btn[i];
			short x = c->x + c->w / 2, y = c->y + c->h / 2, h = c->h / 2, ww = h * 3 / 4;
			if (i == BH_PLAY - 1 || !c->w)
				continue;
			gbox(c, p_bar);
			switch (i + 1) {
			case BH_PREV:
				pen = bar_held == BH_PREV ? p_acc : order_has_prev(&list_order, list_cur) ? p_txt : p_dim;
				box(x - ww / 2 - 2, y - h / 2, 2, h, pen, 0);
				mask_draw(&k_prev, x - ww / 2, y - h / 2, pen);
				break;
			case BH_NEXT:
				pen = bar_held == BH_NEXT ? p_acc : order_has_next(&list_order, list_cur) ? p_txt : p_dim;
				mask_draw(&k_next, x - ww / 2, y - h / 2, pen);
				box(x + ww / 2 + 1, y - h / 2, 2, h, pen, 0);
				break;
			case BH_SHUFFLE:
				line(x - 6, y - 3, x - 3, y - 3, p_dim);
				line(x - 3, y - 3, x + 2, y + 3, p_dim);
				line(x + 2, y + 3, x + 6, y + 3, p_dim);
				line(x - 6, y + 3, x - 3, y + 3, p_dim);
				line(x - 3, y + 3, x + 2, y - 3, p_dim);
				line(x + 2, y - 3, x + 6, y - 3, p_dim);
				break;
			case BH_REPEAT:
				line(x - 6, y + 1, x - 6, y - 3, p_dim);
				line(x - 6, y - 3, x + 4, y - 3, p_dim);
				line(x + 2, y - 5, x + 4, y - 3, p_dim);
				line(x + 6, y - 1, x + 6, y + 3, p_dim);
				line(x + 6, y + 3, x - 4, y + 3, p_dim);
				line(x - 2, y + 5, x - 4, y + 3, p_dim);
				break;
			}
		}
	}
}

static void draw_time(void)
{
	char a[12], b[12];
	long len = play_len_ms(), at = play_name[0] ? play_ms() : 0, fillw;
	gbox(&w_pos, p_bar);
	if (!play_name[0])
		len = 0;
	if (bar_drag >= 0)
		at = len * bar_drag / 1000;
	if (at > len)
		at = len;
	fmt_time(a, sizeof(a), at);
	fmt_time(b, sizeof(b), len);
	text(w_track.x - 4 - (short)strlen(a) * cw, w_pos.y, a, 6, p_dim);
	text(w_track.x + w_track.w + 4, w_pos.y, b, 6, p_dim);
	box(w_track.x, w_track.y + 1, w_track.w, 2, p_trk, 0);
	fillw = len > 0 ? w_track.w * at / len : 0;
	box(w_track.x, w_track.y, (short)fillw, 4, p_acc, 0);
}

/* one row of the list: the file i, its ground the playing row's (the
   bar's colours), the selected one's, or every other one's */
static void draw_row(int i)
{
	short x = w_list.x + cw, ry = w_list.y + (short)(i - list_top + 1) * row_h;
	short c_title = x + 3 * cw, c_time = w_list.x + w_list.w - 6 * cw, c_album = c_time - 13 * cw,
	      c_artist = c_album - 12 * cw, tw = (short)((c_artist - c_title) / cw - 1);
	short fg = 1;
	ENTRY *e = list_at(i);
	char t[12];
	if (i < list_top || ry + row_h > w_list.y + w_list.h)
		return;
	if (e && i == list_cur) {
		box(w_list.x, ry, w_list.w, row_h, p_bar, 0);
		fg = p_acc;
	} else if (e && i == list_sel && !mono) {
		box(w_list.x, ry, w_list.w, row_h, p_dim, 0);
		fg = p_bar;
	} else
		box(w_list.x, ry, w_list.w, row_h, (i - list_top) & 1 ? p_alt : 0, 0);
	if (!e)
		return;
	if (i == list_sel && mono && i != list_cur) {	/* (mono: lines over and under) */
		box(w_list.x, ry, w_list.w, 1, 1, 0);
		box(w_list.x, ry + row_h - 1, w_list.w, 1, 1, 0);
	}
	snprintf(t, sizeof(t), "%d", i + 1);
	text(x + (i + 1 < 10 ? cw : 0), ry + 2, t, 3, fg);
	text(c_title, ry + 2, list_name(e), tw, e->bad && !mono && i != list_cur ? p_dim : fg);
	text(c_artist, ry + 2, e->artist, 11, fg);
	text(c_album, ry + 2, e->album, 12, fg);
	if (e->len_ms > 0) {
		fmt_time(t, sizeof(t), e->len_ms);
		text(w_list.x + w_list.w - cw - (short)strlen(t) * cw, ry + 2, t, 6, fg);
	}
}

static void draw_list(void)
{
	short y = w_list.y, x = w_list.x + cw;
	short c_title = x + 3 * cw, c_time = w_list.x + w_list.w - 6 * cw, c_album = c_time - 13 * cw,
	      c_artist = c_album - 12 * cw;
	int r;
	gbox(&w_list, 0);
	text(x, y + 2, "#", 2, p_hdr);
	text(c_title, y + 2, "Title", 5, p_hdr);
	text(c_artist, y + 2, "Artist", 6, p_hdr);
	text(c_album, y + 2, "Album", 5, p_hdr);
	text(c_time + cw, y + 2, "Time", 4, p_hdr);
	box(w_list.x, y + row_h - 1, w_list.w, 1, mono ? 1 : p_dim, 0);
	if (!list_count()) {
		text(c_title, y + row_h + 2, "File > Add... (^O), or drag files here", 40, mono ? 1 : p_dim);
		return;
	}
	for (r = 0; r < bar_rows(); r++)
		draw_row(list_top + r);
}

int bar_rows(void)
{
	short n = (short)((w_list.h - row_h) / row_h);
	return n > 0 ? n : 0;
}

void bar_row_area(int i, GRECT *r)
{
	r->x = w_list.x;
	r->y = w_list.y + (short)(i - list_top + 1) * row_h;
	r->w = w_list.w;
	r->h = row_h;
}

/* one row drawn again, within clip (the redraw's rectangle) */
void bar_draw_row(const GRECT *clip, int i)
{
	GRECT r = *clip, a;
	short pxy[4];
	bar_row_area(i, &a);
	if (i < list_top || i >= list_top + bar_rows() || !rc_intersect(&a, &r))
		return;
	draw_forget();
	cur_fill = cur_pat = cur_line = cur_tcol = cur_fx = -1;
	clip_r = r;
	pxy[0] = r.x;
	pxy[1] = r.y;
	pxy[2] = r.x + r.w - 1;
	pxy[3] = r.y + r.h - 1;
	text_clip(vdi_h, pxy);
	vswr_mode(vdi_h, 2);
	draw_row(i);
	text_noclip(vdi_h);
	draw_forget();
}

int bar_row_at(short y)
{
	int r;
	if (y < w_list.y + row_h)
		return -1;
	r = (y - w_list.y) / row_h - 1;
	if (r >= bar_rows())
		return -1;
	r += list_top;
	return r < list_count() ? r : -1;
}

short bar_row_h(void)
{
	return row_h;
}

void bar_draw(const GRECT *clip, int parts)
{
	GRECT r = *clip;
	short pxy[4];
	draw_forget();
	cur_fill = cur_pat = cur_line = cur_tcol = cur_fx = -1;
	if (!rc_intersect(&w_all, &r))
		return;
	clip_r = r;
	pxy[0] = r.x;
	pxy[1] = r.y;
	pxy[2] = r.x + r.w - 1;
	pxy[3] = r.y + r.h - 1;
	text_clip(vdi_h, pxy);
	vswr_mode(vdi_h, 2);		/* (text over its ground: transparent) */
	vsl_type(vdi_h, 1);
	if (parts == BP_ART)
		draw_art();
	if (parts == BP_VOL)
		draw_vol();
	if (parts == BP_LEFT) {			/* (another file: a piece a turn) */
		box(w_bar.x, w_bar.y, w_ctl.x - w_bar.x, w_bar.h - 1, p_bar, 0);
		draw_art();
		draw_text();
	}
	if (parts == BP_RIGHT) {
		box(w_out.x, w_bar.y, w_bar.x + w_bar.w - w_out.x, w_bar.h - 1, p_bar, 0);
		draw_out();
	}
	if (parts & BP_TEXT) {
		box(w_bar.x, w_bar.y, w_bar.w, w_bar.h - 1, p_bar, 0);
		box(w_bar.x, w_bar.y + w_bar.h - 1, w_bar.w, 1, 1, 0);
		draw_art();
		draw_text();
		draw_out();
	}
	if (parts & (BP_PLAY | BP_TEXT | BP_NAV))
		draw_buttons(parts);
	if (parts & (BP_TIME | BP_TEXT))
		draw_time();
	if (parts & BP_LIST)
		draw_list();
	text_noclip(vdi_h);
	draw_forget();
}
