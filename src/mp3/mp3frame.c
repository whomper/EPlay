/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3frame.c - frames, side information and the bit reservoir, made into
 * packets for the DSP (mp3frame.h). Runs on the 68030 as it is on the
 * build machine in the tests.
 */
#include <string.h>
#include "mp3frame.h"

static const short br_tab[16] = {
	0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
};
static const long rate_tab[3] = { 44100, 48000, 32000 };

/* where each long scalefactor band starts, for regions 1 and 2 */
static const short sfb_long[3][23] = {
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 52, 62, 74, 90, 110, 134, 162, 196, 238, 288, 342, 418, 576 },
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 42, 50, 60, 72, 88, 106, 128, 156, 190, 230, 276, 330, 384, 576 },
	{ 0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 54, 66, 82, 102, 126, 156, 194, 240, 296, 364, 448, 550, 576 }
};

int mp3_header(const unsigned char *p, MP3HDR *h)
{
	int bi, ri;

	if (p[0] != 0xff || (p[1] & 0xe0) != 0xe0)
		return 0;
	if (((p[1] >> 3) & 3) != 3 || ((p[1] >> 1) & 3) != 1)
		return 0;	/* MPEG-1, layer III only */
	bi = p[2] >> 4;
	ri = (p[2] >> 2) & 3;
	if (bi == 0 || bi == 15 || ri == 3)
		return 0;
	h->version = 1;
	h->layer = 3;
	h->crc = !(p[1] & 1);
	h->bitrate = br_tab[bi];
	h->rate_index = ri;
	h->rate = (int)rate_tab[ri];
	h->padding = (p[2] >> 1) & 1;
	h->mode = p[3] >> 6;
	h->mode_ext = (p[3] >> 4) & 3;
	h->channels = h->mode == 3 ? 1 : 2;
	h->length = (int)(144000L * h->bitrate / rate_tab[ri]) + h->padding;
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

int mp3_packet(MP3PARSE *ps, const MP3HDR *h, const unsigned char *f, unsigned long *pk)
{
	static const unsigned char slen[16][2] = {
		{0,0},{0,1},{0,2},{0,3},{3,0},{1,1},{1,2},{1,3},
		{2,1},{2,2},{2,3},{3,1},{3,2},{3,3},{4,2},{4,3}
	};
	BITS b;
	int nch = h->channels, side = nch == 2 ? 32 : 17;
	int hdr = 4 + (h->crc ? 2 : 0), main_n = h->length - hdr - side;
	int begin, scfsi[2], gr, ch, i, nw;
	long bits = 0, nbytes;
	unsigned long *g = pk + 2;
	const unsigned char *src;
	unsigned char *d;

	if (main_n < 0)
		return -1;
	b.p = f + hdr;
	b.bit = 0;
	begin = (int)get(&b, 9);
	get(&b, nch == 2 ? 3 : 5);
	for (ch = 0; ch < nch; ch++)
		scfsi[ch] = (int)get(&b, 4);
	pk[0] = (nch == 2 ? PK_STEREO : 0) | (unsigned long)h->rate_index << 4;
	if (h->mode == 1) {
		if (h->mode_ext & 2)
			pk[0] |= PK_MS;
		if (h->mode_ext & 1)
			pk[0] |= PK_IS;
	}
	for (gr = 0; gr < 2; gr++)
		for (ch = 0; ch < nch; ch++, g += PK_GC) {
			unsigned p23 = get(&b, 12), bv = get(&b, 9), gg = get(&b, 8);
			unsigned sc = get(&b, 4), bt = 0, mixed = 0, ts[3] = { 0, 0, 0 };
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
				r1 = 36;
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
			g[3] = slen[sc][0] | slen[sc][1] << 4;
			g[4] = bt | mixed << 2;
			g[4] |= get(&b, 1) << 4;	/* preflag */
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

	/* the reservoir: this frame's main data goes after what is kept */
	if (ps->nres + main_n > (int)sizeof(ps->res)) {
		int drop = ps->nres + main_n - (int)sizeof(ps->res);
		memmove(ps->res, ps->res + drop, (size_t)(ps->nres - drop));
		ps->nres -= drop;
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
	nw = (int)((nbytes + 2) / 3);
	if (nw > PK_MAXDATA)
		return -1;
	pk[1] = (unsigned long)nw;
	g = pk + 2 + 2 * nch * PK_GC;
	d = (unsigned char *)src;
	for (i = 0; i < nw; i++) {
		unsigned long w = 0;
		int k;
		for (k = 0; k < 3; k++) {
			long at = (long)i * 3 + k;
			w = w << 8 | (at < nbytes ? d[at] : 0);
		}
		g[i] = w;
	}
	return 2 + 2 * nch * PK_GC + nw;
}
