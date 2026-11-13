/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tickcode.h - the ticks' code at a place of its own. A function the
 * sound's tick runs (an interrupt, a thousand times a second at most) is
 * marked TICK_CODE("NNname"): its section .text.0hot.NNname, which the
 * link (the Makefile's --sort-section=name) puts before all other code,
 * in the order of NN. A stock Falcon's 68030 caches 256 bytes: with the
 * tick's code wherever the code before it left it, the VGM tick took 235
 * to 324 us and the MP3 tick 18 to 20 % of the 68030 as unrelated code
 * grew (Erez, 13 Nov 2026: pinned). Nothing on the host (its tests).
 */
#ifndef TICKCODE_H
#define TICKCODE_H
#if defined(__m68k__)
#define TICK_CODE(n) __attribute__((section(".text.0hot." n)))
#else
#define TICK_CODE(n)
#endif
#endif
