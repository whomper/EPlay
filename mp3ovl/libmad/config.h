/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * config.h - libmad's configuration for MP3INT.OVL (third_party/libmad, no
 * configure run): the 68060's sizes; the fixed-point multiply is fixed.h's
 * FPM_68060 (the Makefile's -D, with OPT_SPEED).
 */
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define HAVE_LIMITS_H 1
