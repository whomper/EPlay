/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * huff68.h - a granule's Huffman codes decoded by the 68030, the values
 * the DSP's model (mp3dsp.c's huffman()) gives, from the main data as
 * bytes: measured (bench.c) as the 68030's possible share of the decode,
 * so that a stock Falcon's DSP had the time for the rest at 49,170 Hz
 * (Erez, 7 Nov 2026: "Many users will not have an FDI"). A stock 68030
 * decodes 63 granules a second of a 320 kbit/s file, where playing needs
 * 153: 243 % of it. Not in the program (docs/DESIGN.md).
 */
#ifndef HUFF68_H
#define HUFF68_H

/* the granule's values into is[576] (the rest zeros): g its side
 * information as a packet has it (mp3frame.h: words 1, 4, 5, 6, 7), d the
 * main data's bytes, its codes from bit start to bit end (d readable 4
 * bytes past end) */
void huff68_granule(const unsigned long *g, const unsigned char *d, long start, long end, short *is);

#endif
