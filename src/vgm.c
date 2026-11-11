/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgm.c - VGM files (vgm.h). The format as vgmrips.net's specification
 * 1.71 gives it: the header's fields past its data offset are 0; a
 * command's length by its first byte.
 */
#include <string.h>
#include "vgm.h"
#include "charset.h"

static unsigned long le32(const unsigned char *p)
{
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

/* a header field at o, 0 where the header (up to the data) hasn't it */
static unsigned long field(const unsigned char *b, long n, long data, long o)
{
	return o + 4 <= data && o + 4 <= n ? le32(b + o) : 0;
}

int vgm_head(const unsigned char *b, long n, VGMHEAD *h)
{
	return vgm_head_of(b, n, n, h);
}

int vgm_head_of(const unsigned char *b, long n, long size, VGMHEAD *h)
{
	long data, o;
	memset(h, 0, sizeof(*h));
	if (n < 0x40 || memcmp(b, "Vgm ", 4))
		return 0;
	h->version = le32(b + 8);
	data = 0x40;
	if (h->version >= 0x150 && le32(b + 0x34))
		data = 0x34 + (long)le32(b + 0x34);
	if (data < 0x40 || data >= size)
		return 0;
	h->data_at = data;
	h->total = (long)le32(b + 0x18);
	o = (long)le32(b + 0x1c);
	if (o && 0x1c + o >= data && 0x1c + o < size) {
		h->loop_at = 0x1c + o;
		h->loop_samples = (long)le32(b + 0x20);
	}
	o = (long)le32(b + 0x14);
	if (o && 0x14 + o + 12 <= size)
		h->gd3_at = 0x14 + o;
	h->sn_clock = le32(b + 0x0c) & 0x3fffffffUL;
	if (h->sn_clock && h->version >= 0x110 && 0x2b < data && 0x2b < n) {
		h->sn_fb = b[0x28] | b[0x29] << 8;
		h->sn_w = b[0x2a];
	}
	if (h->version >= 0x110)
		h->ym2612_clock = field(b, n, data, 0x2c) & 0x3fffffffUL;
	if (h->version >= 0x151) {
		h->ay_clock = field(b, n, data, 0x74) & 0x3fffffffUL;
		if (h->ay_clock && 0x79 < data && 0x79 < n) {
			h->ay_type = b[0x78];
			if (b[0x79] & 0x10)	/* (a YM2149 with its clock halved: pin 26 low) */
				h->ay_clock /= 2;
		}
	}
	/* the other chips: their clocks (YM2413, YM2151, the 1.51 ones but
	   the AY, and on) */
	if (le32(b + 0x10) & 0x3fffffffUL)
		h->other = 1;
	if (h->version >= 0x110 && (field(b, n, data, 0x30) & 0x3fffffffUL))
		h->other = 1;
	for (o = 0x38; o < 0x74 && h->version >= 0x151; o += 4)
		if (o != 0x3c && (field(b, n, data, o) & 0x3fffffffUL))	/* (0x3c: SegaPCM's interface) */
			h->other = 1;
	for (o = 0x80; o < 0xe0 && h->version >= 0x161; o += 4)
		if (field(b, n, data, o) & 0x3fffffffUL)
			h->other = 1;
	return 1;
}

long vgm_length(const VGMHEAD *h)
{
	return h->total + (h->loop_at ? h->loop_samples : 0);
}

/* a GD3 string: UTF-16LE to its end (0), from *p; the Atari's text into
   out (len bytes) when out isn't 0 */
static void gd3_str(const unsigned char *b, long n, long *p, char *out, int len)
{
	int k = 0;
	while (*p + 1 < n) {
		unsigned long u = (unsigned long)b[*p] | (unsigned long)b[*p + 1] << 8;
		char c[4];
		*p += 2;
		if (!u)
			break;
		if (u >= 0xd800 && u < 0xe000)
			u = '?';	/* (a pair: none of its characters is the Atari's) */
		if (out && k + 1 < len) {
			int m = uni_to_atari(u, c);
			if (m < 1) {
				c[0] = '?';
				m = 1;
			}
			if (k + m < len) {
				memcpy(out + k, c, (size_t)m);
				k += m;
			}
		}
	}
	if (out)
		out[k] = 0;
}

void vgm_gd3(const unsigned char *b, long n, const VGMHEAD *h, char *title, char *album, char *artist, int len)
{
	char other[3][64];
	long p, end;
	title[0] = album[0] = artist[0] = 0;
	if (!h->gd3_at || memcmp(b + h->gd3_at, "Gd3 ", 4))
		return;
	end = h->gd3_at + 12 + (long)le32(b + h->gd3_at + 8);
	if (end > n)
		end = n;
	p = h->gd3_at + 12;
	gd3_str(b, end, &p, title, len);
	gd3_str(b, end, &p, other[0], sizeof(other[0]));
	gd3_str(b, end, &p, album, len);
	gd3_str(b, end, &p, other[1], sizeof(other[1]));
	gd3_str(b, end, &p, 0, 0);		/* (the system's names) */
	gd3_str(b, end, &p, 0, 0);
	gd3_str(b, end, &p, artist, len);
	gd3_str(b, end, &p, other[2], sizeof(other[2]));
	if (!title[0] && other[0][0])
		strncpy(title, other[0], (size_t)len - 1), title[len - 1] = 0;
	if (!album[0] && other[1][0])
		strncpy(album, other[1], (size_t)len - 1), album[len - 1] = 0;
	if (!artist[0] && other[2][0])
		strncpy(artist, other[2], (size_t)len - 1), artist[len - 1] = 0;
}

/* a command's bytes by its first (0: one EPlay doesn't know: the end) */
static int cmd_len(const unsigned char *b, long n, long p)
{
	unsigned c = b[p];
	if (c >= 0x30 && c <= 0x3f)
		return 2;
	if (c >= 0x40 && c <= 0x4e)
		return 3;
	if (c == 0x4f || c == 0x50)
		return 2;
	if (c >= 0x51 && c <= 0x5f)
		return 3;
	if (c == 0x61)
		return 3;
	if (c == 0x62 || c == 0x63)
		return 1;
	if (c == 0x64)
		return 4;
	if (c == 0x67)
		return p + 7 <= n ? 7 + (int)(le32(b + p + 3) & 0x7fffffffUL) : 0;
	if (c == 0x68)
		return 12;
	if (c >= 0x70 && c <= 0x8f)
		return 1;
	switch (c) {
	case 0x90: case 0x91: case 0x95:
		return 5;
	case 0x92:
		return 6;
	case 0x93:
		return 11;
	case 0x94:
		return 2;
	}
	if (c >= 0xa0 && c <= 0xbf)
		return 3;
	if (c >= 0xc0 && c <= 0xdf)
		return 4;
	if (c >= 0xe0)
		return 5;
	return 0;
}

/* the AY's period of registers lo, hi (bits of hi: mask) turned to 2 MHz,
   both written */
static void ay_period(VGMRUN *r, int lo, int hi, unsigned mask, unsigned long max)
{
	unsigned long v = (unsigned long)r->ay[lo] | (unsigned long)(r->ay[hi] & mask) << 8;
	if (r->ratio != 65536UL) {
		v = (v * (r->ratio >> 4) + 2048) >> 12;
		if (v > max)
			v = max;
	}
	r->ay_fn(r->u, lo, (int)(v & 0xff));
	r->ay_fn(r->u, hi, (int)(v >> 8));
}

static int faded(const VGMRUN *r, int v)
{
	if (v & 0x10)
		return r->att >= 15 ? 0 : v;	/* (the envelope's: as it is, but silent) */
	v -= 16 - r->fade + r->att;
	return v < 0 ? 0 : v;
}

static void ay_write(VGMRUN *r, int reg, int val)
{
	if (reg > 13)
		return;			/* (the Atari's ports) */
	r->ay[reg] = (unsigned char)val;
	if (!r->ay_fn)
		return;
	if (r->ratio != 65536UL && reg <= 5)
		ay_period(r, reg & ~1, reg | 1, 0x0f, 4095);
	else if (r->ratio != 65536UL && reg == 6) {
		unsigned long v = ((unsigned long)(val & 31) * (r->ratio >> 4) + 2048) >> 12;
		r->ay_fn(r->u, 6, (int)(v > 31 ? 31 : v));
	} else if (r->ratio != 65536UL && (reg == 11 || reg == 12))
		ay_period(r, 11, 12, 0xff, 65535);
	else if (reg >= 8 && reg <= 10 && (r->fade < 16 || r->att))
		r->ay_fn(r->u, reg, faded(r, val));
	else
		r->ay_fn(r->u, reg, val);
}

void vgm_begin(VGMRUN *r, const unsigned char *b, long n, const VGMHEAD *h, VGM_AY ay_fn, VGM_SN sn_fn, void *u)
{
	memset(r, 0, sizeof(*r));
	r->sn_vol[0] = r->sn_vol[1] = r->sn_vol[2] = r->sn_vol[3] = 15;
	r->b = b;
	r->n = n;
	r->h = *h;
	r->pos = h->data_at;
	r->end = vgm_length(h);
	r->fade = 16;
	r->ratio = 65536UL;
	if (h->ay_clock && h->ay_clock != VGM_AY_CLOCK)
		r->ratio = ((unsigned long)VGM_AY_CLOCK << 9) / (h->ay_clock >> 7);	/* (in 32 bits) */
	r->ay_fn = ay_fn;
	r->sn_fn = sn_fn;
	r->u = u;
}

/* the SN76489's registers as the file sets them (vgm_seek's) */
static void sn_shadow(VGMRUN *r, int v)
{
	int ch;
	if (v & 0x80)
		r->sn_latch = v & 0x70;
	ch = (r->sn_latch >> 5) & 3;
	if (r->sn_latch & 0x10)
		r->sn_vol[ch] = v & 15;
	else if (ch == 3)
		r->sn_noise = v & 7;
	else if (v & 0x80)
		r->sn_period[ch] = (r->sn_period[ch] & 0x3f0) | (v & 15);
	else
		r->sn_period[ch] = (r->sn_period[ch] & 15) | ((v & 0x3f) << 4);
}

/* a YM2612 register: kept (a seek writes them again), and passed on */
static void opn_write(VGMRUN *r, int port, int reg, int val)
{
	r->opn[port][reg & 0xff] = (unsigned char)val;
	if (port == 0 && reg == 0x28)
		r->opn_key[val & 7] = (unsigned char)val;
	if (r->opn_fn)
		r->opn_fn(r->u, port, reg, val);
}

/* the commands until one waits; 0 at the end */
static int commands(VGMRUN *r)
{
	const unsigned char *b = r->b;
	while (r->wait <= 0) {
		long p = r->pos;
		int k;
		unsigned c;
		if (p >= r->n) {
			r->done = 1;
			return 0;
		}
		c = b[p];
		/* (first the commands a Mega Drive's drums are made of, some 44 a
		   tick: the DAC's next sample and its wait, then a short wait) */
		if ((c & 0xf0) == 0x80) {
			r->pos = p + 1;
			if (r->pcm_pos >= 0 && r->pcm_pos < r->pcm_len) {
				int v = b[r->pcm_at + r->pcm_pos++];
				VGMDAC *q = r->dac;
				if (q) {
					vgmdac_put(q, r->at - q->base, v);
					r->opn[0][0x2a] = (unsigned char)v;
				} else
					opn_write(r, 0, 0x2a, v);
			}
			r->wait += c & 15;
			continue;
		}
		if ((c & 0xf0) == 0x70) {
			r->pos = p + 1;
			r->wait += (c & 15) + 1;
			continue;
		}
		if (c == 0x66) {	/* the end: the loop once more, else done */
			if (r->h.loop_at && !r->looped) {
				r->looped = 1;
				r->pos = r->h.loop_at;
				continue;
			}
			r->done = 1;
			return 0;
		}
		k = cmd_len(b, r->n, p);
		if (!k || p + k > r->n) {
			r->done = 1;
			return 0;
		}
		r->pos = p + k;
		if (c == 0xa0) {
			if (!(b[p + 1] & 0x80))		/* (the second AY: not played) */
				ay_write(r, b[p + 1] & 0x0f, b[p + 2]);
		} else if (c == 0x50) {
			sn_shadow(r, b[p + 1]);
			if (r->sn_fn)
				r->sn_fn(r->u, b[p + 1]);
		} else if (c == 0x52 || c == 0x53) {
			opn_write(r, c & 1, b[p + 1], b[p + 2]);
		} else if (c == 0x67 && k >= 7) {
			/* a data block: the YM2612's samples (type 0), the first kept */
			if (b[p + 2] == 0 && !r->pcm_len) {
				r->pcm_at = p + 7;
				r->pcm_len = k - 7;
			}
		} else if (c == 0xe0) {
			r->pcm_pos = (long)((unsigned long)b[p + 1] | (unsigned long)b[p + 2] << 8 |
					    (unsigned long)b[p + 3] << 16 | (unsigned long)b[p + 4] << 24);
		} else if (c == 0x61)
			r->wait += (long)b[p + 1] | (long)b[p + 2] << 8;
		else if (c == 0x62)
			r->wait += 735;
		else if (c == 0x63)
			r->wait += 882;
	}
	return 1;
}

/* the fade's level for where it is: the AY's volumes again when it moved */
static void fade_to(VGMRUN *r)
{
	long left = r->end - r->at;
	int f = 16, reg;
	if (!r->h.loop_at || left >= VGM_FADE)
		return;
	if (left > 0)
		f = (int)(left * 16 / VGM_FADE);
	else
		f = 0;
	if (f >= r->fade)
		return;
	r->fade = f;
	if (r->ay_fn)
		for (reg = 8; reg <= 10; reg++)
			r->ay_fn(r->u, reg, faded(r, r->ay[reg]));
}

/* the drums' commands (the DAC's next sample and its wait; a short wait)
   from where the file is, until another command or the sample lim, with
   no call, in registers: a stock Falcon's 68030 has thousands a second.
   Each write into the tick's DAC (r->dac), or with none (a seek) only
   kept as the register's */
static void drums(VGMRUN *r, long lim)
{
	const unsigned char *b = r->b, *pcm = b + r->pcm_at;
	VGMDAC *q = r->dac;
	long p = r->pos, n = r->n, at = r->at, wait = 0;
	long pp = r->pcm_pos, plen = r->pcm_len, base = q ? q->base : 0;
	int v = -1;
	while (p < n) {
		unsigned c = b[p];
		if ((c & 0xf0) == 0x80) {
			if (pp >= 0 && pp < plen) {
				v = pcm[pp++];
				if (q)
					vgmdac_put(q, at - base, v);
			}
			wait = c & 15;
		} else if ((c & 0xf0) == 0x70)
			wait = (c & 15) + 1;
		else
			break;
		p++;
		if (wait >= lim - at) {
			wait -= lim - at;
			at = lim;
			break;
		}
		at += wait;
		wait = 0;
	}
	r->pos = p;
	r->at = at;
	r->wait = wait;
	r->pcm_pos = pp;
	if (v >= 0)
		r->opn[0][0x2a] = (unsigned char)v;
}

int vgm_run(VGMRUN *r, long samples)
{
	if (r->done)
		return 0;
	while (samples > 0) {
		long step;
		if (r->dac && r->wait <= 0 && (r->end <= 0 || r->at + samples < r->end)) {
			long lim = r->at + samples;
			drums(r, lim);
			samples = lim - r->at;
			if (samples <= 0)
				break;
		}
		if (!commands(r))
			return 0;
		step = r->wait < samples ? r->wait : samples;
		r->wait -= step;
		r->at += step;
		samples -= step;
		if (r->at >= r->end && r->end > 0) {
			r->done = 1;
			return 0;
		}
	}
	fade_to(r);
	return 1;
}

void vgm_seek(VGMRUN *r, long to)
{
	vgm_seek_from(r, 0, to);
}

void vgm_seek_from(VGMRUN *r, const VGMRUN *from, long to)
{
	VGM_AY ay = r->ay_fn;
	VGM_SN sn = r->sn_fn;
	VGM_OPN opn = r->opn_fn;
	VGMHEAD h = r->h;
	int att = r->att;
	void *u = r->u;
	if (from && from->at <= to) {
		*r = *from;	/* (a place it was at: the file from there) */
		r->ay_fn = 0;
		r->sn_fn = 0;
		r->opn_fn = 0;
		r->dac = 0;
		r->u = u;
	} else
		vgm_begin(r, r->b, r->n, &h, 0, 0, u);
	r->att = att;
	r->ay_fn = 0;
	r->sn_fn = 0;
	while (r->at < to && !r->done) {
		long step;
		/* (the drums run through without a call each: a minute of Mega
		   Drive music is some 400,000 of them; with no YM2612 to write,
		   their bytes only kept) */
		if (r->wait <= 0 && !r->opn_fn) {
			drums(r, to);
			if (r->at >= to)
				break;
		}
		if (!commands(r))
			break;
		step = r->wait < to - r->at ? r->wait : to - r->at;
		r->wait -= step;
		r->at += step;
	}
	r->ay_fn = ay;
	r->sn_fn = sn;
	r->opn_fn = opn;
	fade_to(r);
	vgm_resend(r);
}

void vgm_set_att(VGMRUN *r, int att)
{
	int reg;
	r->att = att < 0 ? 0 : att > 15 ? 15 : att;
	if (r->ay_fn)
		for (reg = 8; reg <= 10; reg++)
			r->ay_fn(r->u, reg, faded(r, r->ay[reg]));
}

void vgm_resend(VGMRUN *r)
{
	int reg;
	if (r->ay_fn)
		for (reg = 0; reg <= 13; reg++)
			ay_write(r, reg, r->ay[reg]);
	if (r->opn_fn && r->h.ym2612_clock) {
		/* the YM2612: the channels' and operators' registers, the
		   frequencies' high byte before the low, then the keys */
		int port, reg, ch;
		for (port = 0; port < 2; port++) {
			for (reg = 0x30; reg < 0xa0; reg++)
				r->opn_fn(r->u, port, reg, r->opn[port][reg]);
			for (reg = 0xa4; reg < 0xa8; reg++) {
				r->opn_fn(r->u, port, reg, r->opn[port][reg]);
				r->opn_fn(r->u, port, reg - 4, r->opn[port][reg - 4]);
			}
			for (reg = 0xac; reg < 0xb0; reg++) {
				r->opn_fn(r->u, port, reg, r->opn[port][reg]);
				r->opn_fn(r->u, port, reg - 4, r->opn[port][reg - 4]);
			}
			for (reg = 0xb0; reg < 0xb8; reg++)
				r->opn_fn(r->u, port, reg, r->opn[port][reg]);
		}
		r->opn_fn(r->u, 0, 0x22, r->opn[0][0x22]);
		r->opn_fn(r->u, 0, 0x27, r->opn[0][0x27]);
		r->opn_fn(r->u, 0, 0x2b, r->opn[0][0x2b]);
		for (ch = 0; ch < 8; ch++)
			if (ch != 3 && ch != 7)
				r->opn_fn(r->u, 0, 0x28, r->opn_key[ch] ? r->opn_key[ch] : ch);
	}
	if (r->sn_fn && r->h.sn_clock) {
		int ch;
		for (ch = 0; ch < 3; ch++) {
			r->sn_fn(r->u, 0x80 | ch << 5 | (r->sn_period[ch] & 15));
			r->sn_fn(r->u, (r->sn_period[ch] >> 4) & 0x3f);
		}
		r->sn_fn(r->u, 0xe0 | r->sn_noise);
		for (ch = 0; ch < 4; ch++)
			r->sn_fn(r->u, 0x90 | ch << 5 | r->sn_vol[ch]);
	}
}
