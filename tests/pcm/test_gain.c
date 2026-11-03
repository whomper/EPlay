/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_gain.c - src/pcm.c's pcm_gain, the volume on samples (the FDI's):
 * as they are at full volume, a change gliding over PCM_RAMP frames with
 * no jump, the end exact, silence silent, and the ring's other words
 * (four tracks) left alone.
 */
#include <stdio.h>
#include <stdlib.h>
#include "pcm.h"

static int bad;
static void check(int ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	bad += !ok;
}

int main(void)
{
	static short a[2 * 4000], b[8 * 1000];
	PCMGAIN g;
	long i, worst = 0;
	int same = 1, ends = 1, quiet = 1, alone = 1;
	for (i = 0; i < 4000; i++)
		a[2 * i] = a[2 * i + 1] = 30000;	/* (a loud level: the worst for a step) */
	/* full volume: bit for bit, and nothing to do */
	pcm_gain_set(&g, 32768);
	check(pcm_gain_idle(&g, 32768), "full volume: nothing to do");
	pcm_gain(&g, 32768, a, 4000, 2);
	for (i = 0; i < 8000; i++)
		same &= a[i] == 30000;
	check(same, "full volume: the samples as they are");
	/* 0 dB to -6 dB (32768 to 16384): a glide, no step over 1/128 of full */
	pcm_gain(&g, 16384, a, 100, 2);
	pcm_gain(&g, 16384, a + 200, 3900, 2);	/* (across two calls, as the tick has it) */
	for (i = 1; i < 4000; i++) {
		long d = labs((long)a[2 * i] - a[2 * i - 2]);
		if (d > worst)
			worst = d;
	}
	check(worst <= 32768 / 128, "a change glides: no jump between two frames");
	for (i = PCM_RAMP; i < 4000; i++)
		ends &= a[2 * i] == 15000 && a[2 * i + 1] == 15000;
	check(ends, "after PCM_RAMP frames exactly the new volume");
	/* to silence */
	for (i = 0; i < 4000; i++)
		a[2 * i] = a[2 * i + 1] = 30000;
	pcm_gain(&g, 0, a, 4000, 2);
	for (i = PCM_RAMP; i < 8000 / 2; i++)
		quiet &= a[2 * i] == 0 && a[2 * i + 1] == 0;
	check(quiet, "silent: silence after the glide");
	/* four tracks: words 2 to 7 of each frame untouched */
	for (i = 0; i < 8000; i++)
		b[i] = (short)(i * 7);
	pcm_gain_set(&g, 16384);
	pcm_gain(&g, 16384, b, 1000, 8);
	for (i = 0; i < 8000; i++)
		alone &= (i & 7) < 2 ? b[i] == (short)(((long)(short)(i * 7) * 16384L) >> 15) : b[i] == (short)(i * 7);
	check(alone, "a step of 8: the frame's other words left alone");
	return bad != 0;
}
