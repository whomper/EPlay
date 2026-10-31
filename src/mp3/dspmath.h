/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspmath.h - the DSP56001's arithmetic, for the C model of the MP3
 * decoder (mp3dsp.c), so that dsp/mp3.a56 can be checked against it bit
 * for bit.
 *
 * A word is 24 bits, a fraction in [-1, 1): held here as an int32_t from
 * -2^23 to 2^23 - 1 (Q23). An accumulator is 56 bits: 8 extension bits, a
 * 24-bit high word and a 24-bit low word, a fraction with 47 bits after
 * the point: held as an int64_t (Q47). A product of two words is exact in
 * an accumulator (mpy: x * y * 2 in Q46 terms), and so is a sum of them,
 * so the order the DSP adds in doesn't matter; only where a value is
 * rounded or cut to a word does, and that is what these name.
 */
#ifndef DSPMATH_H
#define DSPMATH_H

#include <stdint.h>

typedef int32_t dw;		/* a DSP word, Q23 */
typedef int64_t da;		/* a DSP accumulator, Q47 */

#define DW_MAX 0x7fffff
#define DW_MIN (-0x800000)
#define DA_EXT (((da)1 << 55) - 1)	/* an accumulator's range */

/* mpy x0,y0,a */
static inline da d_mpy(dw x, dw y)
{
	return (da)x * y * 2;
}

/* a word into an accumulator (move x0,a: into A1, A0 cleared) */
static inline da d_w2a(dw x)
{
	return (da)x << 24;
}

#ifdef DSP_COUNT_LIMIT
extern long dsp_limited;	/* the tests count the values limited */
#define DSP_LIMITED() (dsp_limited++)
#else
#define DSP_LIMITED() ((void)0)
#endif

/* move a,x0: the high word, limited if the extension is in use */
static inline dw d_a2w(da a)
{
	a >>= 24;
	if (a > DW_MAX) {
		DSP_LIMITED();
		return DW_MAX;
	}
	if (a < DW_MIN) {
		DSP_LIMITED();
		return DW_MIN;
	}
	return (dw)a;
}

/* the same for a sample going out, where limiting is clipping, as it should be */
static inline dw d_a2w_out(da a)
{
	a >>= 24;
	return a > DW_MAX ? DW_MAX : a < DW_MIN ? DW_MIN : (dw)a;
}

/* rnd a (as macr, mpyr): convergent rounding at the low word, the tie to
 * even, as the 56001 does without scaling */
static inline da d_rnd(da a)
{
	da low = a & 0xffffff;
	a &= ~(da)0xffffff;
	if (low > 0x800000 || (low == 0x800000 && (a & 0x1000000)))
		a += 0x1000000;
	return a;
}

/* rnd then move: a word, rounded and limited */
static inline dw d_rw(da a)
{
	return d_a2w(d_rnd(a));
}

/* asl / asr by n (the extension keeps the bits that go over) */
static inline da d_asl(da a, int n)
{
	return a * ((da)1 << n);
}

static inline da d_asr(da a, int n)
{
	return a >> n;
}

#endif
