/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gmsynth.c - EPlay's General MIDI synth (gmsynth.h). The arithmetic
 * follows Sonivox EAS's wavetable engine (its eas_wtsynth.c, Apache
 * License 2.0, read for its units, not copied): the same envelope steps,
 * pitch in cents, gain law and pan law, so that its sound set sounds as
 * it was made to. Left out at first: its filter, its reverb and chorus,
 * the LFO's tremolo, channel pressure.
 */
#include <string.h>
#include "gmsynth.h"

enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE, EG_OFF };

#define FULL 32767
#define MODWHEEL_CENTS 50	/* the mod wheel's vibrato at its top (EAS's default) */

/* sin(pi/2 i/128), Q15: the pan's law (EAS_CalcPanControl's) */
static const unsigned short pan_sin[129] = {
	0, 402, 804, 1206, 1608, 2009, 2410, 2811, 3212, 3612, 4011, 4410,
	4808, 5205, 5602, 5998, 6393, 6786, 7179, 7571, 7962, 8351, 8739, 9126,
	9512, 9896, 10278, 10659, 11039, 11417, 11793, 12167, 12539, 12910, 13279, 13645,
	14010, 14372, 14732, 15090, 15446, 15800, 16151, 16499, 16846, 17189, 17530, 17869,
	18204, 18537, 18868, 19195, 19519, 19841, 20159, 20475, 20787, 21096, 21403, 21705,
	22005, 22301, 22594, 22884, 23170, 23452, 23731, 24007, 24279, 24547, 24811, 25072,
	25329, 25582, 25832, 26077, 26319, 26556, 26790, 27019, 27245, 27466, 27683, 27896,
	28105, 28310, 28510, 28706, 28898, 29085, 29268, 29447, 29621, 29791, 29956, 30117,
	30273, 30424, 30571, 30714, 30852, 30985, 31113, 31237, 31356, 31470, 31580, 31685,
	31785, 31880, 31971, 32057, 32137, 32213, 32285, 32351, 32412, 32469, 32521, 32567,
	32609, 32646, 32678, 32705, 32728, 32745, 32757, 32765, 32767,
};

/* 2^(i/12) and 2^(i/1200), Q15: a pitch in cents as two products */
static const unsigned short semi_tab[12] = {
	32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858
};
static const unsigned short cent_tab[100] = {
	32768, 32787, 32806, 32825, 32844, 32863, 32882, 32901, 32920, 32939, 32958, 32977,
	32996, 33015, 33034, 33053, 33072, 33091, 33110, 33130, 33149, 33168, 33187, 33206,
	33225, 33245, 33264, 33283, 33302, 33322, 33341, 33360, 33379, 33399, 33418, 33437,
	33457, 33476, 33495, 33515, 33534, 33553, 33573, 33592, 33611, 33631, 33650, 33670,
	33689, 33709, 33728, 33748, 33767, 33787, 33806, 33826, 33845, 33865, 33884, 33904,
	33924, 33943, 33963, 33982, 34002, 34022, 34041, 34061, 34081, 34100, 34120, 34140,
	34160, 34179, 34199, 34219, 34239, 34258, 34278, 34298, 34318, 34338, 34357, 34377,
	34397, 34417, 34437, 34457, 34477, 34497, 34517, 34536, 34556, 34576, 34596, 34616,
	34636, 34656, 34676, 34696,
};

/* (a * b) >> 15 for Q15 values: EAS's MULT_EG1_EG1 */
static long q15(long a, long b)
{
	return (a * b) >> 15;
}

/* the points a sample (Q16) for a pitch in cents from the set's rate (0:
   the sample as recorded, at 22,050 Hz), at the output's rate */
static unsigned long step_of(const GMSYN *s, long cents)
{
	long oct = 0, rem;
	unsigned long m;
	if (cents < -12000)
		cents = -12000;
	if (cents > 4800)
		cents = 4800;
	while (cents < 0) {
		cents += 1200;
		oct--;
	}
	oct += cents / 1200;
	rem = cents % 1200;
	m = (unsigned long)semi_tab[rem / 100] * cent_tab[rem % 100];	/* Q30, 1 to 2 */
	m = (m >> 14) * (s->ratio >> 2) >> 14;	/* Q16 times the rate's ratio (Q14) */
	return oct >= 0 ? m << oct : m >> -oct;
}

static void eg_step(short *val, unsigned char *st, short *inc, const short *env)
{
	long t = *val;
	switch (*st) {
	case EG_ATTACK:
		t += *inc;
		if (t >= FULL) {
			t = FULL;
			*st = EG_DECAY;
			*inc = env[1];
		}
		break;
	case EG_DECAY:
		t = q15(t, *inc);
		if (t <= env[2]) {
			t = env[2];
			*st = t > 0 ? EG_SUSTAIN : EG_OFF;
		}
		break;
	case EG_SUSTAIN:
		return;
	case EG_RELEASE:
		t = q15(t, *inc);
		if (t <= 0) {
			t = 0;
			*st = EG_OFF;
		}
		break;
	default:
		t = 0;
	}
	*val = (short)t;
}

/* a voice's update: its envelopes and vibrato a step on, its pitch and
   its gains' targets, ramped to across the samples to the next update */
static void v_update(GMSYN *s, GMS_VOICE *v, long over)
{
	GMS_CHAN *c = &s->c[v->ch];
	long g, cents, lfo, pan, gl, gr;
	eg_step(&v->eg1, &v->eg1st, &v->eg1inc, v->a.eg1);
	eg_step(&v->eg2, &v->eg2st, &v->eg2inc, v->a.eg2);
	if (v->lfoph < 0)
		v->lfoph++;
	else {
		lfo = (short)(v->lfoph << 2);
		if (v->lfoph > 0x1fff && v->lfoph < 0x6000)
			lfo = ~lfo;
		v->lfo = (short)lfo;
		v->lfoph = (short)((v->lfoph + v->a.lfo_freq) & 0x7fff);
	}
	/* the pitch: the bend, the range's tuning, the note, EG2, the vibrato */
	cents = ((long)c->bend * 4 * c->range) >> 15;
	cents += v->r.tuning + (long)v->note * 100;
	cents += q15(v->eg2, v->a.eg2_to_pitch);
	cents += q15(v->lfo, v->a.lfo_to_pitch + q15((long)c->mod << 8, MODWHEEL_CENTS));
	v->inc = step_of(s, cents);
	/* the gain: velocity squared, the range's, (volume x expression)
	   squared, the master volume, EG1 */
	g = (long)v->vel << 8;
	g = q15(g, g);
	g = q15(g, v->r.gain);
	{
		long cg = q15((long)c->vol << 8, (long)c->expr << 8);
		cg = q15(cg, cg);
		g = q15(g, q15(cg, s->master));
	}
	g = q15(g, v->eg1);
	pan = (long)c->pan - 64 + v->a.pan;
	if (pan < -64)
		pan = -64;
	if (pan > 63)
		pan = 63;
	gl = q15(g, pan_sin[64 - pan]);
	gr = q15(g, pan_sin[pan + 64]);
	if (over < 1)
		over = 1;
	v->dgl = (gl * 32768L - v->gl) / over;
	v->dgr = (gr * 32768L - v->gr) / over;
}

static void v_off(GMSYN *s, GMS_VOICE *v)
{
	if (v->on)
		s->live--;
	v->on = 0;
	v->eg1st = EG_OFF;
	v->gl = v->gr = v->dgl = v->dgr = 0;
}

static void v_release(GMS_VOICE *v)
{
	if (!v->on || v->eg1st == EG_OFF || v->eg1st == EG_RELEASE)
		return;
	v->eg1st = EG_RELEASE;
	v->eg1inc = v->a.eg1[3];
	v->eg2st = EG_RELEASE;
	v->eg2inc = v->a.eg2[3];
	v->held = 0;
}

/* (a voice ended at once, its gain ramped to 0 by the next update) */
static void v_quiet(GMS_VOICE *v)
{
	if (!v->on || v->eg1st == EG_OFF)
		return;
	v->eg1st = EG_RELEASE;
	v->eg1inc = 0;
	v->eg2st = EG_RELEASE;
	v->eg2inc = 0;
	v->held = 0;
}

static void chan_reset(GMS_CHAN *c, int all)
{
	c->expr = 127;
	c->sus = 0;
	c->mod = 0;
	c->bend = 0;
	c->rpn_lo = c->rpn_hi = 127;
	if (all) {
		c->prog = 0;
		c->vol = 100;
		c->pan = 64;
		c->range = 200;
	}
}

void gms_reset(GMSYN *s)
{
	int k;
	for (k = 0; k < s->voices; k++)
		v_off(s, &s->v[k]);
	s->live = 0;
	for (k = 0; k < 16; k++)
		chan_reset(&s->c[k], 1);
	s->master = FULL;
}

void gms_cap(GMSYN *s, int n)
{
	int k;
	if (n < 1)
		n = 1;
	if (n > s->voices)
		n = s->voices;
	for (k = n; k < s->voices; k++)
		v_quiet(&s->v[k]);
	s->cap = n;
}

void gms_init(GMSYN *s, const GMSET *g, long rate, int voices)
{
	int k;
	s->g = g;
	s->rate = rate;
	s->voices = voices < 1 ? 1 : voices > GMS_VOICES ? GMS_VOICES : voices;
	s->cap = s->voices;
	s->upd = (128L * rate + 11025) / 22050;
	s->to_upd = s->upd;
	s->ratio = (unsigned long)((g->rate * 65536L + rate / 2) / rate);
	s->age = 0;
	s->notes = s->stolen = 0;
	for (k = 0; k < GMS_VOICES; k++) {
		s->v[k].eg1st = EG_OFF;
		s->v[k].on = 0;
		s->v[k].gl = s->v[k].gr = s->v[k].dgl = s->v[k].dgr = 0;
	}
	s->live = 0;
	gms_reset(s);
}

static void note_on(GMSYN *s, int ch, int note, int vel)
{
	GMS_CHAN *c = &s->c[ch];
	GMS_VOICE *v = 0;
	GM_REGION r;
	int idx, k, drums = ch == 9;
	long len;
	if (!gm_find(s->g, drums, c->prog, note, &r, &idx))
		return;
	if (r.flags & 0x02)
		return;		/* (a wave the noise generator makes: none in this set) */
	/* a key group (an open and a closed hi-hat): its other notes end */
	if (GM_KEYGROUP(r.flags))
		for (k = 0; k < s->voices; k++)
			if (s->v[k].on && s->v[k].ch == ch && s->v[k].kgroup == GM_KEYGROUP(r.flags))
				v_quiet(&s->v[k]);
	/* a voice: a free one, else the quietest let go, else the oldest */
	for (k = 0; k < s->cap; k++)
		if (!s->v[k].on) {
			v = &s->v[k];
			break;
		}
	if (!v) {
		long least = 0x7fffffffL;
		for (k = 0; k < s->cap; k++)
			if (s->v[k].eg1st == EG_RELEASE && s->v[k].eg1 < least) {
				least = s->v[k].eg1;
				v = &s->v[k];
			}
		if (!v) {
			unsigned long oldest = 0;
			for (k = 0; k < s->cap; k++)
				if (s->age - s->v[k].age >= oldest) {
					oldest = s->age - s->v[k].age;
					v = &s->v[k];
				}
		}
		s->stolen++;
		v_off(s, v);
	}
	s->notes++;
	s->live++;
	v->on = 1;
	v->ch = (unsigned char)ch;
	v->note = (unsigned char)note;
	v->vel = (unsigned char)vel;
	v->held = 0;
	v->drums = (unsigned char)drums;
	v->kgroup = (unsigned char)GM_KEYGROUP(r.flags);
	v->region = idx;
	v->r = r;
	gm_art(s->g, r.art, &v->a);
	v->w = gm_wave(s->g, r.wave, &len);
	v->len = len;
	if ((r.flags & GM_LOOPED) && r.loop_end > r.loop_start) {
		v->ls = (long)r.loop_start;
		v->le = (long)r.loop_end;
	} else
		v->ls = v->le = len - 1;
	v->pos = 0;
	v->frac = 0;
	v->eg1 = 0;
	v->eg1st = EG_ATTACK;
	v->eg1inc = v->a.eg1[0];
	v->eg2 = 0;
	v->eg2st = EG_ATTACK;
	v->eg2inc = v->a.eg2[0];
	v->lfo = 0;
	v->lfoph = (short)-v->a.lfo_delay;
	v->gl = v->gr = 0;
	v->age = ++s->age;
	v_update(s, v, s->to_upd);
}

static void note_off(GMSYN *s, int ch, int note)
{
	int k;
	for (k = 0; k < s->voices; k++) {
		GMS_VOICE *v = &s->v[k];
		if (v->on && v->eg1st != EG_OFF && v->eg1st != EG_RELEASE && v->ch == ch && v->note == note) {
			if (s->c[ch].sus)
				v->held = 1;
			else
				v_release(v);
		}
	}
}

static void control(GMSYN *s, int ch, int cc, int val)
{
	GMS_CHAN *c = &s->c[ch];
	int k;
	switch (cc) {
	case 1:
		c->mod = (unsigned char)val;
		break;
	case 6:		/* data entry: RPN 0, the bend's range in semitones */
		if (c->rpn_hi == 0 && c->rpn_lo == 0)
			c->range = (short)(val * 100 + c->range % 100);
		break;
	case 38:
		if (c->rpn_hi == 0 && c->rpn_lo == 0)
			c->range = (short)(c->range / 100 * 100 + (val < 100 ? val : 99));
		break;
	case 7:
		c->vol = (unsigned char)val;
		break;
	case 10:
		c->pan = (unsigned char)val;
		break;
	case 11:
		c->expr = (unsigned char)val;
		break;
	case 64:
		c->sus = val >= 64;
		if (!c->sus)
			for (k = 0; k < s->voices; k++)
				if (s->v[k].held && s->v[k].ch == ch)
					v_release(&s->v[k]);
		break;
	case 100:
		c->rpn_lo = (unsigned char)val;
		break;
	case 101:
		c->rpn_hi = (unsigned char)val;
		break;
	case 120:		/* all sound off */
		for (k = 0; k < s->voices; k++)
			if (s->v[k].ch == ch)
				v_quiet(&s->v[k]);
		break;
	case 121:
		chan_reset(c, 0);
		break;
	case 123:		/* all notes off */
		for (k = 0; k < s->voices; k++)
			if (s->v[k].on && s->v[k].ch == ch && s->v[k].eg1st != EG_OFF) {
				if (c->sus)
					s->v[k].held = 1;
				else
					v_release(&s->v[k]);
			}
		break;
	}
}

void gms_midi(GMSYN *s, const unsigned char *m, int n)
{
	int st, ch;
	if (n < 1)
		return;
	st = m[0];
	if (st == 0xf0) {
		/* GM System On; the master volume */
		if (n >= 6 && m[1] == 0x7e && m[3] == 0x09 && m[4] == 0x01)
			gms_reset(s);
		else if (n >= 8 && m[1] == 0x7f && m[3] == 0x04 && m[4] == 0x01)
			s->master = (long)((m[6] & 127) << 7 | (m[5] & 127)) * FULL / 16383;
		return;
	}
	if (st < 0x80 || st >= 0xf0)
		return;
	ch = st & 15;
	switch (st & 0xf0) {
	case 0x90:
		if (n >= 3 && m[2])
			note_on(s, ch, m[1] & 127, m[2] & 127);
		else if (n >= 3)
			note_off(s, ch, m[1] & 127);
		break;
	case 0x80:
		if (n >= 2)
			note_off(s, ch, m[1] & 127);
		break;
	case 0xb0:
		if (n >= 3)
			control(s, ch, m[1] & 127, m[2] & 127);
		break;
	case 0xc0:
		if (n >= 2)
			s->c[ch].prog = m[1] & 127;
		break;
	case 0xe0:
		if (n >= 3)
			s->c[ch].bend = (short)(((m[2] & 127) << 7 | (m[1] & 127)) - 8192);
		break;
	}
}

/* the set's points made the engine's, once, in place (gms_prepare): each
   16-bit point halved, in the host's order, so that two points' difference
   fits 16 bits and the interpolation is one 16 x 16 multiply */
void gms_prepare(GMSET *g, unsigned char *pool)
{
	long k, n = g->points;
	short *p = (short *)(void *)(pool + g->o_smp);
	const unsigned char *b = pool + g->o_smp;
	if (g->bits != 16 || g->prepared)
		return;
	for (k = 0; k < n; k++)
		p[k] = (short)((short)(b[2 * k] << 8 | b[2 * k + 1]) >> 1);
	g->prepared = 1;
}

#define CHUNK 16		/* the samples a gain holds for (its ramp's step) */

/* a voice's n samples added into acc (stereo, Q0 a side): 1 when it
   ended (a sample without a loop played to its end) */
static int v_run(GMS_VOICE *v, long *acc, int n)
{
	const short *w = (const short *)(const void *)v->w;
	long pos = v->pos, le = v->le, span = v->le - v->ls, gl = v->gl, gr = v->gr;
	long dgl = v->dgl, dgr = v->dgr;
	unsigned long frac = v->frac, inc = v->inc;
	unsigned short fr = (unsigned short)frac, incf = (unsigned short)inc;
	long inci = (long)(inc >> 16);
	int looped = v->le > v->ls;
	while (n > 0) {
		/* the samples before the loop's end (or the wave's): no test a
		   sample */
		int m = n > CHUNK ? CHUNK : n, k;
		short g0 = (short)(gl >> 15), g1 = (short)(gr >> 15);
		if (le - pos <= (long)(m * (inci + 1)) + 1) {
			unsigned long left = ((unsigned long)(le - pos) << 16) - fr;
			long most = inc ? (long)((left + inc - 1) / inc) : m;
			if (most < m)
				m = (int)most;
		}
		for (k = 0; k < m; k++) {
			const short *q = w + pos;
			short s0 = q[0], d = (short)(q[1] - s0), x;
			unsigned short nf;
			x = (short)(s0 + (((long)d * (short)(fr >> 1)) >> 15));
			acc[0] += ((long)x * g0) >> 14;
			acc[1] += ((long)x * g1) >> 14;
			acc += 2;
			nf = (unsigned short)(fr + incf);
			pos += inci + (nf < fr);
			fr = nf;
		}
		n -= m;
		gl += dgl * m;
		gr += dgr * m;
		if (pos >= le) {
			if (!looped) {
				v->pos = pos;
				return 1;
			}
			while (pos >= le)
				pos -= span;
		}
	}
	v->pos = pos;
	v->frac = fr;
	v->gl = gl;
	v->gr = gr;
	return 0;
}

void gms_render(GMSYN *s, short *out, int n)
{
	static long acc[2 * 256];
	while (n > 0) {
		int m = n, k;
		long *a;
		if (m > 256)
			m = 256;
		if (m > s->to_upd)
			m = (int)s->to_upd;
		for (k = 0; k < 2 * m; k++)
			acc[k] = 0;
		for (k = 0; k < s->voices; k++) {
			GMS_VOICE *v = &s->v[k];
			if (!v->on)
				continue;
			if (v_run(v, acc, m))
				v_off(s, v);
		}
		for (k = 0, a = acc; k < 2 * m; k++, a++) {
			long x = *a;
			if (x > 32767)
				x = 32767;
			if (x < -32768)
				x = -32768;
			out[k] = (short)x;
		}
		out += 2 * m;
		n -= m;
		s->to_upd -= m;
		if (s->to_upd <= 0) {
			s->to_upd = s->upd;
			for (k = 0; k < s->voices; k++) {
				GMS_VOICE *v = &s->v[k];
				if (!v->on)
					continue;
				if (v->eg1st == EG_OFF)
					v_off(s, v);	/* (its last ramp to 0 played) */
				else
					v_update(s, v, s->upd);
			}
		}
	}
}

/* (the CPU's speed: n samples of the voice loop with v voices sounding, on
   a wave of silence, for the caller to time; nothing of a GMSYN touched) */
long gms_bench(int v, int n)
{
	static short wave[1024];
	static long acc[2 * 256];
	GMS_VOICE t;
	long sum = 0;
	int k, m;
	memset(&t, 0, sizeof(t));
	t.w = (const unsigned char *)wave;
	t.ls = 0;
	t.le = 1000;
	t.inc = 0x12345UL;
	t.gl = t.gr = 1L << 29;
	while (n > 0) {
		m = n > 256 ? 256 : n;
		for (k = 0; k < 2 * m; k++)
			acc[k] = 0;
		for (k = 0; k < v; k++)
			v_run(&t, acc, m);
		for (k = 0; k < 2 * m; k++)
			sum += acc[k];
		n -= m;
	}
	return sum;
}
