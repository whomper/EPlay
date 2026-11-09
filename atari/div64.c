/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * div64.c - libgcc's 64-bit multiply, divide and remainder, made only of
 * what every CPU from the 68020 to the 68060 has (32 by 32 bits). libgcc
 * here is built for the 68020 and uses mulu.l and divu.l with 64-bit
 * operands, which the 68060 doesn't have: on a CT60 they trap (exception
 * 61), and with no 060 support package loaded (EmuTOS; some set-ups) the
 * machine stops. EPLAY.PRG is built with -m68020-60, so gcc calls these
 * instead of using those instructions inline (a tester's CT60, 9 Nov 2026).
 * Linked before libgcc, they take its place. One program for every CPU
 * (Erez, 9 Nov 2026): a 68020 or 68030 takes the one instruction, as
 * libgcc did (the plain 32-bit way cost a stock Falcon 250 ms at each
 * start); div64_cpu60 (set first thing at the start, from _CPU) sends a
 * 68060 the 32-bit way.
 */

typedef unsigned long u32;
typedef unsigned long long u64;
typedef long long s64;

int div64_cpu60;		/* a 68060: no 64-bit mulu.l or divu.l */

u64 __muldi3(u64 a, u64 b);
u64 __udivdi3(u64 n, u64 d);
u64 __umoddi3(u64 n, u64 d);
s64 __divdi3(s64 n, s64 d);
s64 __moddi3(s64 n, s64 d);

/* a 32 by 32 multiply to 64 bits: mulu.l, or 16-bit halves on a 68060 */
static u64 mul32(u32 a, u32 b)
{
#ifndef EPLAY_ST
	if (!div64_cpu60) {
		u32 hi, lo = a;
		__asm__("mulu.l %2,%0:%1" : "=d"(hi), "+d"(lo) : "d"(b) : "cc");
		return (u64)hi << 32 | lo;
	}
#endif
	{
	u32 al = a & 0xffff, ah = a >> 16, bl = b & 0xffff, bh = b >> 16;
	u32 ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
	u32 mid = (ll >> 16) + (lh & 0xffff) + (hl & 0xffff);
	u32 lo = (ll & 0xffff) | (mid << 16);
	u32 hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
	return (u64)hi << 32 | lo;
	}
}

u64 __muldi3(u64 a, u64 b)
{
	u32 al = (u32)a, ah = (u32)(a >> 32), bl = (u32)b, bh = (u32)(b >> 32);
	u64 r = mul32(al, bl);
	return r + ((u64)(al * bh + ah * bl) << 32);
}

/* n / d and its remainder: 32-bit division where both fit, else shift and
   subtract over only the bits the quotient can have */
static u64 udivmod(u64 n, u64 d, u64 *rem)
{
	u64 q = 0, bit = 1;
	if (!d) {
		*rem = n;
		return ~(u64)0;
	}
	if (!(n >> 32) && !(d >> 32)) {
		u32 a = (u32)n, b = (u32)d;
		*rem = a % b;
		return a / b;
	}
#ifndef EPLAY_ST
	if (!div64_cpu60 && !(d >> 32)) {
		/* a 32-bit divisor: two divu.l of 64 by 32 bits */
		u32 b = (u32)d, nh = (u32)(n >> 32), qh = nh / b, r = nh % b, ql = (u32)n;
		__asm__("divu.l %2,%0:%1" : "+d"(r), "+d"(ql) : "d"(b) : "cc");
		*rem = r;
		return (u64)qh << 32 | ql;
	}
#endif
	if (d > n) {
		*rem = n;
		return 0;
	}
	while (!(d >> 63) && d < n) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	*rem = n;
	return q;
}

u64 __udivdi3(u64 n, u64 d)
{
	u64 r;
	return udivmod(n, d, &r);
}

u64 __umoddi3(u64 n, u64 d)
{
	u64 r;
	udivmod(n, d, &r);
	return r;
}

s64 __divdi3(s64 n, s64 d)
{
	int neg = (n < 0) != (d < 0);
	u64 r, q = udivmod(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &r);
	return neg ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 n, s64 d)
{
	u64 r;
	udivmod(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &r);
	return n < 0 ? -(s64)r : (s64)r;
}
