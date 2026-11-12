/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * egmath.h - the envelopes' arithmetic the FM chips' 68030 halves share
 * (opnd.c, opld.c)
 */
#ifndef EPLAY_EGMATH_H
#define EPLAY_EGMATH_H

/* log2(1 - inc / 16), Q24, inc 0-15 */
extern const long eg_log2_att[16];
/* 2^x, Q23, for x (Q24) at most 0 */
long eg_exp2(long long x);

#endif
