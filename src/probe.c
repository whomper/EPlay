/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * probe.c - how long a file plays, for the list (probe.h).
 */
#include <string.h>
#include "probe.h"
#include "pcm.h"
#include "mp3frame.h"

static long ms_of(long frames, long rate)
{
	if (rate <= 0)
		return 0;
	return frames / rate * 1000 + frames % rate * 1000 / rate;	/* (no overflow) */
}

long probe_len_ms(TAG_READ rd, void *u, long size, long id3_len, unsigned char *buf)
{
	PCMFMT f;
	MP3HDR h, h2;
	const char *why;
	long n = rd(u, 0, buf, PROBE_BUF), i;
	if (n >= 12 && pcm_probe(buf, n, &f, &why))
		return ms_of(f.frames, f.rate);
	if (id3_len < 0 || id3_len >= size)
		id3_len = 0;
	n = rd(u, id3_len, buf, PROBE_BUF);
	/* the first header whose next frame starts where it says (or the
	   piece ends first), as play.c's find_mp3 */
	for (i = 0; i + 4 <= n; i++) {
		long bytes, nf;
		int vbr;
		if (buf[i] != 0xff || !mp3_header(buf + i, &h))
			continue;
		if (i + h.length + 4 <= n && !(mp3_header(buf + i + h.length, &h2) && h2.rate == h.rate))
			continue;
		bytes = size - (id3_len + i);
		nf = i + h.length <= n ? mp3_info_frames(buf + i, &h, &vbr) : 0;
		if (nf > 0)
			return ms_of(nf * h.samples, h.rate);
		return h.bitrate ? bytes * 8 / h.bitrate : 0;
	}
	return -1;
}
