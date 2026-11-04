/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pcmtest_c.c - src/pcm.c with its C alone (no 68030 loops) and its names
 * prefixed c_, for pcmtest.c to hold the loops against
 */
#define PCM_C_ONLY
#define pcm_probe c_pcm_probe
#define pcm_convert c_pcm_convert
#define pcm_convert_to c_pcm_convert_to
#define pcm_gain_set c_pcm_gain_set
#define pcm_gain_idle c_pcm_gain_idle
#define pcm_gain c_pcm_gain
#include "../../src/pcm.c"
