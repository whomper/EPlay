/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * lib000.c - MIDI.OVL's 68000 build's multiply and divide: the cross
 * compiler's libgcc has them with bsr.l, a 68020 instruction, which a
 * 68000 takes for a short branch to an odd address (the STE's address
 * error, 26 Nov 2026). The program has its own (common's tos.c); the
 * overlay links neither, so these are its, before -lgcc.
 */
unsigned long __mulsi3(unsigned long a, unsigned long b);
unsigned long __udivsi3(unsigned long a, unsigned long b);
unsigned long __umodsi3(unsigned long a, unsigned long b);
long __divsi3(long a, long b);
long __modsi3(long a, long b);

static inline unsigned long mulu16(unsigned long a, unsigned long b)
{
	__asm__("mulu.w %1,%0" : "+d"(a) : "d"(b));
	return a;
}

unsigned long __mulsi3(unsigned long a, unsigned long b)
{
	unsigned long al = a & 0xffff, bl = b & 0xffff, ah = a >> 16, bh = b >> 16;
	return mulu16(al, bl) + ((mulu16(ah, bl) + mulu16(al, bh)) << 16);
}

/* a / b and a % b by shifts (b 0: 0 and a) */
static unsigned long udivmod(unsigned long a, unsigned long b, unsigned long *r)
{
	unsigned long q = 0, bit = 1;
	if (!b) {
		*r = a;
		return 0;
	}
	while (b < a && !(b & 0x80000000UL)) {
		b <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (a >= b) {
			a -= b;
			q |= bit;
		}
		b >>= 1;
		bit >>= 1;
	}
	*r = a;
	return q;
}

unsigned long __udivsi3(unsigned long a, unsigned long b)
{
	unsigned long r;
	return udivmod(a, b, &r);
}

unsigned long __umodsi3(unsigned long a, unsigned long b)
{
	unsigned long r;
	udivmod(a, b, &r);
	return r;
}

long __divsi3(long a, long b)
{
	unsigned long r, q = udivmod(a < 0 ? -(unsigned long)a : (unsigned long)a,
				     b < 0 ? -(unsigned long)b : (unsigned long)b, &r);
	return (a < 0) ^ (b < 0) ? -(long)q : (long)q;
}

long __modsi3(long a, long b)
{
	unsigned long r;
	udivmod(a < 0 ? -(unsigned long)a : (unsigned long)a, b < 0 ? -(unsigned long)b : (unsigned long)b, &r);
	return a < 0 ? -(long)r : (long)r;
}
