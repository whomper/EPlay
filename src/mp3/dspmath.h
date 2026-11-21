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
#ifdef __mcoldfire__
/* (the FireBee's ColdFire has no 32 x 32 -> 64 multiply: GCC's would be
   ll64.c's __muldi3. The words are 24 bits: four muls.w on their 12-bit
   halves, the high signed, the low 0 to 4095, exact) */
static inline da d_mpy(dw x, dw y)
{
	short xh = (short)(x >> 12), xl = (short)(x & 0xfff), yh = (short)(y >> 12), yl = (short)(y & 0xfff);
	long hh = (long)xh * yh, mid = (long)xh * yl + (long)xl * yh, ll = (long)xl * yl;
	return 2 * (((da)hh << 24) + ((da)mid << 12) + ll);
}
#else
static inline da d_mpy(dw x, dw y)
{
	return (da)x * y * 2;
}
#endif

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

#ifdef EPLAY_EMAC
/* The FireBee's EMAC (its ColdFire V4e; Erez, 21 Nov 2026): a sum of
 * products, each d_mpy's, made exactly on it. Each word is split into
 * 12-bit halves, its high half signed, its low half 0 to 4095, packed as
 * the two 16-bit halves of a register (em_pack); mac.w multiplies the
 * halves, and the four products' sums go into the four accumulators:
 * high by high, high by low, low by high, low by low. Each product is 24
 * bits at most and a sum of 16 of them 28, so every accumulator stays
 * within its low 32 bits, which the EMAC holds exactly however its
 * extension bits are laid out (QEMU's and the manual's differ): no
 * extension read, no saturation, nothing rounded. em_sum puts them
 * together as d_mpy's sum: x * y * 2 = 2 * (hh << 24 + (hl + lh) << 12 +
 * ll). The EMAC in signed integer mode (em_begin): MACSR's S/U bit is
 * read one way by QEMU (signed with it set) and the other by the manual, so
 * em_probe tries both on a known product at the start and keeps the one
 * that multiplies signed (em_macsr; -1: neither, the EMAC not used); its MACSR and
 * accumulators kept as they were for whoever had them (em_end): FreeMiNT
 * doesn't save them across a switch of process (sys/arch/context_cf.S). At
 * most 16 products a sum. */
static inline unsigned long em_pack(dw x)
{
	return ((unsigned long)(x >> 12) << 16) | ((unsigned long)x & 0xfff);
}

static inline void em_clear(void)
{
	__asm__ volatile("move.l #0,%%acc0\n\tmove.l #0,%%acc1\n\tmove.l #0,%%acc2\n\tmove.l #0,%%acc3" ::: "memory");
}

static inline void em_mac(unsigned long px, unsigned long py)
{
	__asm__ volatile("mac.w %0u,%1u,%%acc0\n\tmac.w %0u,%1l,%%acc1\n\t"
			 "mac.w %0l,%1u,%%acc2\n\tmac.w %0l,%1l,%%acc3" :: "d"(px), "d"(py));
}

static inline da em_sum(void)
{
	long hh, hl, lh, ll;
	__asm__ volatile("movclr.l %%acc0,%0\n\tmovclr.l %%acc1,%1\n\tmovclr.l %%acc2,%2\n\tmovclr.l %%acc3,%3"
			 : "=d"(hh), "=d"(hl), "=d"(lh), "=d"(ll));
	return 2 * (((da)hh << 24) + ((da)(hl + lh) << 12) + ll);
}

typedef struct {
	unsigned long macsr, acc[4], ext01, ext23;
} EMSAVE;

extern long em_macsr;		/* mp3dsp.c's: em_probe's choice */

static inline void em_begin(EMSAVE *s)
{
	unsigned long a0, a1, a2, a3, m, e1, e2;
	__asm__ volatile("move.l %%macsr,%0\n\tmove.l %%acc0,%1\n\tmove.l %%acc1,%2\n\tmove.l %%acc2,%3\n\t"
			 "move.l %%acc3,%4\n\tmove.l %%accext01,%5\n\tmove.l %%accext23,%6"
			 : "=d"(m), "=d"(a0), "=d"(a1), "=d"(a2), "=d"(a3), "=d"(e1), "=d"(e2));
	s->macsr = m;
	s->acc[0] = a0;
	s->acc[1] = a1;
	s->acc[2] = a2;
	s->acc[3] = a3;
	s->ext01 = e1;
	s->ext23 = e2;
	__asm__ volatile("move.l %0,%%macsr" :: "d"(em_macsr) : "memory");
	em_clear();
}

static inline void em_end(const EMSAVE *s)
{
	__asm__ volatile("move.l %0,%%acc0\n\tmove.l %1,%%acc1\n\tmove.l %2,%%acc2\n\tmove.l %3,%%acc3\n\t"
			 "move.l %4,%%accext01\n\tmove.l %5,%%accext23\n\tmove.l %6,%%macsr"
			 :: "d"(s->acc[0]), "d"(s->acc[1]), "d"(s->acc[2]), "d"(s->acc[3]),
			    "d"(s->ext01), "d"(s->ext23), "d"(s->macsr) : "memory");
}
#endif

#endif
