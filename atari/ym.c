/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ym.c - the Atari's YM2149 for a VGM file (ym.h). The chip at $FF8800
 * (the register chosen, and read) and $FF8802 (written); each write with
 * the processor's interrupts off, so that nothing else's choice of
 * register comes between.
 */
#include "tos.h"
#include "tickcode.h"
#include "ym.h"
#include "snd.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define PSG_SEL (*(volatile unsigned char *)0xffff8800L)
#define PSG_WR (*(volatile unsigned char *)0xffff8802L)
#define CONTERM (*(volatile unsigned char *)0x484L)

#define Soundcmd(m, d) trap14_w6(130, (m), (d), 0, 0, 0, 0)
#define ADDERIN 4
#define ADCINPUT 5

static unsigned char ports;		/* register 7's bits 6 and 7, as found */
static unsigned char conterm_was;
static short adder_was = -1, adc_was = -1;
static int on;

static unsigned short sr_off(void)
{
	unsigned short sr;
	__asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
	return sr;
}

static void sr_back(unsigned short sr)
{
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
}

TICK_CODE("60ym_write")
void ym_write(int reg, int val)
{
	unsigned short sr;
	if (reg < 0 || reg > 13)
		return;
	if (reg == 7)
		val = (val & 0x3f) | ports;
	sr = sr_off();
	PSG_SEL = (unsigned char)reg;
	PSG_WR = (unsigned char)val;
	sr_back(sr);
}

static long quiet(void)
{
	int r;
	for (r = 8; r <= 10; r++)
		ym_write(r, 0);
	ym_write(7, 0x3f);
	return 0;
}

static long begin(void)
{
	unsigned short sr = sr_off();
	PSG_SEL = 7;
	ports = PSG_SEL & 0xc0;
	sr_back(sr);
	conterm_was = CONTERM;
	CONTERM = conterm_was & ~5;	/* (the key click and the bell off) */
	quiet();
	on = 1;
	return 0;
}

static long end(void)
{
	quiet();
	if (on)
		CONTERM = (CONTERM & ~5) | (conterm_was & 5);
	on = 0;
	return 0;
}

static int falcon(void)
{
	long m = get_cookie(0x5F4D4348L);	/* _MCH */
	return (m >> 16) == 3;
}

void ym_begin(void)
{
	Supexec(begin);
	if (falcon()) {
		/* the PSG heard through the codec: its ADC input both sides,
		   added to the DAC's (the matrix's sound kept) */
		adc_was = (short)(Soundcmd(ADCINPUT, -1) & 3);
		adder_was = (short)(Soundcmd(ADDERIN, -1) & 3);
		Soundcmd(ADCINPUT, 3);
		Soundcmd(ADDERIN, 1 | (adder_was & 2));
		snd_adder_adc = 1;
	}
}

void ym_silence(void)
{
	Supexec(quiet);
}

void ym_end(void)
{
	Supexec(end);
	if (adc_was >= 0) {
		Soundcmd(ADCINPUT, adc_was);
		Soundcmd(ADDERIN, adder_was);
		adc_was = adder_was = -1;
	}
	snd_adder_adc = 0;
}

void ym_term(void)
{
	if (on)
		end();
}
