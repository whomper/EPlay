/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3frame.c - frames, side information and the bit reservoir, made into
 * packets for the DSP (mp3frame.h). Runs on the 68030 as it is on the
 * build machine in the tests.
 */
#include <string.h>
#include "mp3frame.h"

static const short br_tab[2][16] = {
	{ 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 },
	{ 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 }
};
static const long rate_tab[9] = { 44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000 };

/* where each long scalefactor band starts, for regions 1 and 2 */
static const short sfb_long[9][23] = {
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 52, 62, 74, 90, 110, 134, 162, 196, 238, 288, 342, 418, 576 },
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 42, 50, 60, 72, 88, 106, 128, 156, 190, 230, 276, 330, 384, 576 },
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 54, 66, 82, 102, 126, 156, 194, 240, 296, 364, 448, 550, 576 },
	{ 0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576 },
	{ 0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 114, 136, 162, 194, 232, 278, 332, 394, 464, 540, 576 },
	{ 0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576 },
	{ 0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576 },
	{ 0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576 },
	{ 0, 12, 24, 36, 48, 60, 72, 88, 108, 132, 160, 192, 232, 280, 336, 400, 476, 566, 568, 570, 572, 574, 576 }
};

int mp3_header(const unsigned char *p, MP3HDR *h)
{
	int bi, ri, v;

	if (p[0] != 0xff || (p[1] & 0xe0) != 0xe0)
		return 0;
	v = (p[1] >> 3) & 3;		/* 3: MPEG-1, 2: MPEG-2, 0: MPEG-2.5 */
	if (v == 1 || ((p[1] >> 1) & 3) != 1)
		return 0;		/* layer III only */
	bi = p[2] >> 4;
	ri = (p[2] >> 2) & 3;
	if (bi == 0 || bi == 15 || ri == 3)
		return 0;
	h->version = v == 3 ? 1 : v == 2 ? 2 : 25;
	h->layer = 3;
	h->crc = !(p[1] & 1);
	h->bitrate = br_tab[v != 3][bi];
	h->rate_index = ri + (v == 3 ? 0 : v == 2 ? 3 : 6);
	h->rate = (int)rate_tab[h->rate_index];
	h->padding = (p[2] >> 1) & 1;
	h->mode = p[3] >> 6;
	h->mode_ext = (p[3] >> 4) & 3;
	h->channels = h->mode == 3 ? 1 : 2;
	h->samples = v == 3 ? 1152 : 576;
	h->length = (int)((v == 3 ? 144000L : 72000L) * h->bitrate / h->rate) + h->padding;
	return 1;
}

void mp3_reset(MP3PARSE *ps)
{
	ps->nres = 0;
}

/* side information, MSB first */
typedef struct {
	const unsigned char *p;
	long bit;
} BITS;

static unsigned get(BITS *b, int n)
{
	unsigned v = 0;
	while (n--) {
		v = v << 1 | ((b->p[b->bit >> 3] >> (7 - (b->bit & 7))) & 1);
		b->bit++;
	}
	return v;
}

/* MPEG-2's scalefac_compress: the four slen and the row of nr_of_sfb
 * (ISO/IEC 13818-3, 2.4.3.2); the right channel of intensity stereo has its
 * own rows (3-5) and an intensity scale; rows 2 set preflag */
static unsigned long lsf_slen(unsigned sfc, int ist, unsigned long *g4)
{
	unsigned s[4] = { 0, 0, 0, 0 }, row;

	if (!ist) {
		if (sfc < 400) {
			s[0] = (sfc >> 4) / 5;
			s[1] = (sfc >> 4) % 5;
			s[2] = (sfc & 15) >> 2;
			s[3] = sfc & 3;
			row = 0;
		} else if (sfc < 500) {
			sfc -= 400;
			s[0] = (sfc >> 2) / 5;
			s[1] = (sfc >> 2) % 5;
			s[2] = sfc & 3;
			row = 1;
		} else {
			sfc -= 500;
			s[0] = sfc / 3;
			s[1] = sfc % 3;
			row = 2;
			*g4 |= 1 << 4;		/* preflag */
		}
	} else {
		unsigned isc = sfc >> 1;
		*g4 |= (sfc & 1) << 6;		/* intensity_scale */
		if (isc < 180) {
			s[0] = isc / 36;
			s[1] = (isc % 36) / 6;
			s[2] = (isc % 36) % 6;
			row = 3;
		} else if (isc < 244) {
			isc -= 180;
			s[0] = (isc & 63) >> 4;
			s[1] = (isc & 15) >> 2;
			s[2] = isc & 3;
			row = 4;
		} else {
			isc -= 244;
			s[0] = isc / 3;
			s[1] = isc % 3;
			row = 5;
		}
	}
	return s[0] | s[1] << 4 | s[2] << 8 | s[3] << 12 | (unsigned long)row << 16;
}

int mp3_packet(MP3PARSE *ps, const MP3HDR *h, const unsigned char *f, unsigned long *pk)
{
	static const unsigned char slen[16][2] = {
		{0,0},{0,1},{0,2},{0,3},{3,0},{1,1},{1,2},{1,3},
		{2,1},{2,2},{2,3},{3,1},{3,2},{3,3},{4,2},{4,3}
	};
	BITS b;
	int nch = h->channels, lsf = h->version != 1, ngr = lsf ? 1 : 2;
	int side = lsf ? (nch == 2 ? 17 : 9) : (nch == 2 ? 32 : 17);
	int hdr = 4 + (h->crc ? 2 : 0), main_n = h->length - hdr - side;
	int begin, scfsi[2] = { 0, 0 }, gr, ch, i, nw;
	int ist = h->mode == 1 && (h->mode_ext & 1);
	long bits = 0, nbytes;
	unsigned long *g = pk + 2;
	const unsigned char *src;
	unsigned char *d;

	if (main_n < 0)
		return -1;
	b.p = f + hdr;
	b.bit = 0;
	if (lsf) {
		begin = (int)get(&b, 8);
		get(&b, nch == 2 ? 2 : 1);
	} else {
		begin = (int)get(&b, 9);
		get(&b, nch == 2 ? 3 : 5);
		for (ch = 0; ch < nch; ch++)
			scfsi[ch] = (int)get(&b, 4);
	}
	pk[0] = (nch == 2 ? PK_STEREO : 0) | (unsigned long)h->rate_index << 4 | (lsf ? PK_LSF : 0);
	if (h->mode == 1) {
		if (h->mode_ext & 2)
			pk[0] |= PK_MS;
		if (h->mode_ext & 1)
			pk[0] |= PK_IS;
	}
	for (gr = 0; gr < ngr; gr++)
		for (ch = 0; ch < nch; ch++, g += PK_GC) {
			unsigned p23 = get(&b, 12), bv = get(&b, 9), gg = get(&b, 8);
			unsigned sc = get(&b, lsf ? 9 : 4), bt = 0, mixed = 0, ts[3] = { 0, 0, 0 };
			unsigned sbg[3] = { 0, 0, 0 }, r1, r2;
			if (bv > 288)
				return -1;
			if (get(&b, 1)) {
				bt = get(&b, 2);
				mixed = get(&b, 1);
				ts[0] = get(&b, 5);
				ts[1] = get(&b, 5);
				for (i = 0; i < 3; i++)
					sbg[i] = get(&b, 3);
				if (bt == 0)
					return -1;	/* reserved */
				/* as FFmpeg has it (minimp3 differs for MPEG-2's mixed blocks) */
				if (bt == 2)
					r1 = h->rate_index == 8 ? 72 : 36;
				else
					r1 = (unsigned)sfb_long[h->rate_index][8];
				r2 = 576;
			} else {
				unsigned c0, c1;
				for (i = 0; i < 3; i++)
					ts[i] = get(&b, 5);
				c0 = get(&b, 4);
				c1 = get(&b, 3);
				r1 = (unsigned)sfb_long[h->rate_index][c0 + 1];
				r2 = c0 + c1 + 2 > 22 ? 576 : (unsigned)sfb_long[h->rate_index][c0 + c1 + 2];
			}
			if (r1 > bv * 2)
				r1 = bv * 2;
			if (r2 > bv * 2)
				r2 = bv * 2;
			g[0] = p23;
			g[1] = bv * 2;
			g[2] = gg;
			g[4] = bt | mixed << 2;
			if (lsf) {
				g[3] = lsf_slen(sc, ist && ch == 1, &g[4]);
			} else {
				g[3] = slen[sc][0] | slen[sc][1] << 4;
				g[4] |= get(&b, 1) << 4;	/* preflag */
			}
			g[4] |= get(&b, 1) << 3;	/* scalefac_scale */
			g[4] |= get(&b, 1) << 5;	/* count1table_select */
			if (gr == 1)
				g[4] |= (unsigned long)scfsi[ch] << 8;
			g[5] = ts[0] | ts[1] << 5 | ts[2] << 10;
			g[6] = r1;
			g[7] = r2;
			g[8] = sbg[0] | sbg[1] << 3 | sbg[2] << 6;
			bits += p23;
		}

	/* the reservoir: this frame's main data goes after what is kept. A
	   frame reaches back 511 bytes at most (main_data_begin's 9 bits), so
	   when it is full only the last 511 are kept: moved every few frames,
	   not each one */
	if (ps->nres + main_n > (int)sizeof(ps->res) - 4) {
		int keep = ps->nres < 511 ? ps->nres : 511;
		memmove(ps->res, ps->res + ps->nres - keep, (size_t)keep);
		ps->nres = keep;
	}
	if (begin > ps->nres) {
		/* the data starts in a frame we haven't seen: play silence */
		memcpy(ps->res + ps->nres, f + hdr + side, (size_t)main_n);
		ps->nres += main_n;
		pk[0] |= PK_MUTE;
		pk[1] = 0;
		return 2;
	}
	src = ps->res + ps->nres - begin;
	memcpy(ps->res + ps->nres, f + hdr + side, (size_t)main_n);
	ps->nres += main_n;
	nbytes = (bits + 7) >> 3;
	if (nbytes > begin + main_n)
		return -1;
	nw = (int)((nbytes * 8 + 22) / 23);
	if (nw > PK_MAXDATA)
		return -1;
	pk[1] = (unsigned long)nw;
	g = pk + 2 + ngr * nch * PK_GC;
	d = (unsigned char *)src;
	{
		/* 23 bits a word, each from the 4 bytes it starts in (the
		   reservoir has 4 bytes to spare past its end); the last word's
		   bits past the data made 0 */
		long bit = 0;
		int extra = (int)((long)nw * 23 - nbytes * 8);
		for (i = 0; i < nw; i++, bit += 23) {
			const unsigned char *p = d + (bit >> 3);
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
			/* (a 68020 and later read a long at any address, in the
			   stream's own order: one read, not four) */
			unsigned long v = *(const unsigned long *)(const void *)p;
#else
			unsigned long v = (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
					  (unsigned long)p[2] << 8 | p[3];
#endif
			g[i] = (v << (bit & 7)) >> 9 & 0x7fffffUL;
		}
		if (nw)
			g[nw - 1] &= ~((1UL << extra) - 1);
	}
	return 2 + ngr * nch * PK_GC + nw;
}

static unsigned long be32(const unsigned char *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

int mp3_info_toc(const unsigned char *f, const MP3HDR *h, unsigned char *toc)
{
	int side = h->version == 1 ? (h->channels == 2 ? 32 : 17) : (h->channels == 2 ? 17 : 9);
	int k;
	for (k = 0; k < 2; k++) {
		const unsigned char *x = f + 4 + side + (k && h->crc ? 2 : 0);
		unsigned long fl;
		long at;
		if (k && !h->crc)
			break;
		if (h->length < (x - f) + 8 || memcmp(x, "Xing", 4))
			continue;
		fl = be32(x + 4);
		at = 8 + ((fl & 1) ? 4 : 0) + ((fl & 2) ? 4 : 0);
		if (!(fl & 4) || h->length < (x - f) + at + 100)
			return 0;
		memcpy(toc, x + at, 100);
		return 1;
	}
	return 0;
}

long mp3_info_frames(const unsigned char *f, const MP3HDR *h, int *vbr)
{
	int side = h->version == 1 ? (h->channels == 2 ? 32 : 17) : (h->channels == 2 ? 17 : 9);
	int k;
	*vbr = 0;
	/* after the side information; LAME puts it there with a CRC too,
	   where the CRC's 2 bytes would move it */
	for (k = 0; k < 2; k++) {
		const unsigned char *x = f + 4 + side + (k && h->crc ? 2 : 0);
		if (k && !h->crc)
			break;
		if (h->length >= (x - f) + 12 && (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4))) {
			*vbr = x[0] == 'X';
			return (be32(x + 4) & 1) ? (long)be32(x + 8) : 0;
		}
	}
	if (h->length >= 36 + 18 && !memcmp(f + 36, "VBRI", 4)) {
		*vbr = 1;
		return (long)be32(f + 36 + 14);
	}
	return 0;
}

int mp3_info_lame(const unsigned char *f, const MP3HDR *h, MP3LAME *l)
{
	int side = h->version == 1 ? (h->channels == 2 ? 32 : 17) : (h->channels == 2 ? 17 : 9);
	int k;
	memset(l, 0, sizeof(*l));
	for (k = 0; k < 2; k++) {
		const unsigned char *x = f + 4 + side + (k && h->crc ? 2 : 0), *t;
		unsigned long fl;
		long at;
		int i;
		if (k && !h->crc)
			break;
		if (h->length < (x - f) + 8 || (memcmp(x, "Xing", 4) && memcmp(x, "Info", 4)))
			continue;
		fl = be32(x + 4);
		at = 8 + ((fl & 1) ? 4 : 0) + ((fl & 2) ? 4 : 0) + ((fl & 4) ? 100 : 0) + ((fl & 8) ? 4 : 0);
		if (h->length < (x - f) + at + 24)
			return 0;
		t = x + at;
		/* the encoder's name: letters and digits (LAME3.100, Lavc61.3) */
		for (i = 0; i < 4; i++)
			if (!((t[i] >= 'A' && t[i] <= 'Z') || (t[i] >= 'a' && t[i] <= 'z')))
				return 0;
		memcpy(l->encoder, t, 9);
		l->encoder[9] = 0;
		for (i = 0; i < 9; i++)
			if ((unsigned char)l->encoder[i] < 32 || (unsigned char)l->encoder[i] > 126)
				l->encoder[i] = 0;
		l->delay = (int)(t[21] << 4 | t[22] >> 4);
		l->padding = (int)((t[22] & 15) << 8 | t[23]);
		l->radio_gain = (int)(t[15] << 8 | t[16]);	/* (ReplayGain's track gain, as LAME writes it) */
		l->audiophile_gain = (int)(t[17] << 8 | t[18]);
		l->peak = (long)be32(t + 11);
		return 1;
	}
	return 0;
}

void mp3_packet_gain(unsigned long *pk, int up)
{
	int g, n;
	unsigned long *gc = pk + 2 + 2;
	if (!up || (pk[0] & PK_MUTE))
		return;
	n = ((pk[0] & PK_LSF) ? 1 : 2) * ((pk[0] & PK_STEREO) ? 2 : 1);
	for (g = 0; g < n; g++, gc += PK_GC) {
		long v = (long)*gc + up;
		*gc = (unsigned long)(v < 0 ? 0 : v > 255 ? 255 : v);
	}
}
