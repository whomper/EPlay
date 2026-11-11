/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mul060.c - __muldi3 for the 68060: libgcc's (built for the 68020) uses
 * the 64-bit mulu.l the 68060 doesn't have (exception 61). The low 64
 * bits of a product from 16-bit pieces, each a mulu.w. For MP3CPU.PRG's
 * measurement.
 */
#include <stdint.h>

static inline uint64_t mul32x32(uint32_t a, uint32_t b)
{
	uint32_t al = a & 0xffff, ah = a >> 16, bl = b & 0xffff, bh = b >> 16;
	uint32_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
	uint32_t mid = (ll >> 16) + (lh & 0xffff) + (hl & 0xffff);
	uint32_t lo = (ll & 0xffff) | (mid << 16);
	uint32_t hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
	return (uint64_t)hi << 32 | lo;
}

long long __muldi3(long long a, long long b);
long long __muldi3(long long a, long long b)
{
	uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
	uint32_t al = (uint32_t)ua, ah = (uint32_t)(ua >> 32), bl = (uint32_t)ub, bh = (uint32_t)(ub >> 32);
	uint64_t r = mul32x32(al, bl);
	r += (uint64_t)(ah * bl + al * bh) << 32;
	return (long long)r;
}
