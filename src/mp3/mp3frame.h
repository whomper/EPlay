/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3frame.h - the 68030's part of an MP3 decode: it finds the frames,
 * reads their headers and side information, keeps the bit reservoir, and
 * makes each frame a packet of 24-bit words for the DSP (mp3dsp.c is the
 * DSP's part). No decoding happens here: a packet is the frame's side
 * information, laid out for the DSP, and its main data from where
 * main_data_begin points.
 *
 * A packet:
 *   word 0      flags: PK_STEREO, PK_MS, PK_IS, PK_MUTE, the rate (PK_RATE)
 *   word 1      N, the main data words at the end
 *   then for each granule, each channel, PK_GC words:
 *     0  part2_3_length (bits)
 *     1  big_values * 2 (lines)
 *     2  global_gain
 *     3  slen1 | slen2 << 4
 *     4  block_type | mixed << 2 | scalefac_scale << 3 | preflag << 4
 *        | count1table_select << 5 | scfsi << 8 (granule 1 only)
 *     5  table_select[0] | [1] << 5 | [2] << 10
 *     6  region 1's first line
 *     7  region 2's first line
 *     8  subblock_gain[0] | [1] << 3 | [2] << 6
 *   then the N words of main data, three bytes each, the first in the
 *   high byte: from main_data_begin bytes back, the bits of every granule.
 * A PK_MUTE packet (no reservoir yet, after a seek) has no granules: the
 * DSP plays its 1152 samples as silence.
 */
#ifndef MP3FRAME_H
#define MP3FRAME_H

#define PK_STEREO	0x01
#define PK_MS		0x02
#define PK_IS		0x04
#define PK_MUTE		0x08
#define PK_RATE(f)	(((f) >> 4) & 3)	/* 0: 44.1, 1: 48, 2: 32 kHz */
#define PK_GC		9
#define PK_MAXDATA	700	/* main data words: 511 + 1441 bytes at most */
#define PK_MAX		(2 + 4 * PK_GC + PK_MAXDATA)

typedef struct {
	int version;		/* 1: MPEG-1 (the only one played today) */
	int layer;		/* 3 */
	int crc;		/* 1: a CRC follows the header */
	int bitrate;		/* kbit/s */
	int rate;		/* Hz */
	int rate_index;		/* 0..2 */
	int padding;
	int mode;		/* 0 stereo, 1 joint, 2 dual, 3 mono */
	int mode_ext;
	int channels;
	int length;		/* the frame's bytes */
} MP3HDR;

typedef struct {
	unsigned char res[2048];	/* main data of the last frames */
	int nres;
} MP3PARSE;

/* the header at p (4 bytes): 1 and h filled, or 0 if it isn't one EPlay
 * plays (MPEG-1 layer III, not free format) */
int mp3_header(const unsigned char *p, MP3HDR *h);

void mp3_reset(MP3PARSE *ps);

/* the frame at f, of h->length bytes, as a packet: its words, or -1 if the
 * frame is bad (the reservoir is kept) */
int mp3_packet(MP3PARSE *ps, const MP3HDR *h, const unsigned char *f, unsigned long *pk);

#endif
