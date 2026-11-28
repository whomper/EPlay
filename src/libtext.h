/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * libtext.h - building the library's requests without the C library's
 * printf (LIBRARY.OVL has none): each puts onto out at k and gives the new
 * length, or -1 once anything didn't fit (and every call after it does
 * nothing).
 */
#ifndef LIBTEXT_H
#define LIBTEXT_H

int lt_s(char *out, int k, int n, const char *s);	/* as it is */
int lt_n(char *out, int k, int n, long v);		/* a number, v >= 0 */

#endif
