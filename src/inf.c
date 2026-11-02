/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * inf.c - EPLAY.INF's text (inf.h). A line is NAME=value when it starts
 * with NAME and '=' (the case as written: the family's INFs are upper
 * case); its value runs to the line's end, CR and LF left out.
 */
#include <string.h>
#include "inf.h"

/* where NAME='s line starts in t, or -1; *vat, *vend its value */
static long find(const char *t, long n, const char *name, long *vat, long *vend)
{
	long i = 0, k = (long)strlen(name);
	while (i < n) {
		long e = i;
		while (e < n && t[e] != '\n' && t[e] != '\r')
			e++;
		if (e - i > k && !memcmp(t + i, name, (size_t)k) && t[i + k] == '=') {
			*vat = i + k + 1;
			*vend = e;
			return i;
		}
		while (e < n && (t[e] == '\n' || t[e] == '\r'))
			e++;
		i = e;
	}
	return -1;
}

int inf_get(const char *t, long n, const char *name, char *val, int vmax)
{
	long a, e, len;
	if (vmax < 1 || find(t, n, name, &a, &e) < 0)
		return 0;
	len = e - a;
	if (len > vmax - 1)
		len = vmax - 1;
	memcpy(val, t + a, (size_t)len);
	val[len] = 0;
	return 1;
}

static int put(char *out, long omax, long *at, const char *s, long len)
{
	if (*at + len > omax)
		return 0;
	memcpy(out + *at, s, (size_t)len);
	*at += len;
	return 1;
}

long inf_set(const char *t, long n, const char *name, const char *val, const char *note, char *out, long omax)
{
	long a, e, at = 0, vl = (long)strlen(val);
	if (find(t, n, name, &a, &e) >= 0) {
		if (!put(out, omax, &at, t, a) || !put(out, omax, &at, val, vl) || !put(out, omax, &at, t + e, n - e))
			return -1;
		return at;
	}
	if (!put(out, omax, &at, t, n))
		return -1;
	if (at > 0) {
		long p;
		if (out[at - 1] != '\n' && !put(out, omax, &at, "\r\n", 2))
			return -1;
		/* (a setting's paragraph: an empty line before it, where the
		   text's last line isn't one) */
		p = at - 1;			/* its last line's end */
		if (p > 0 && out[p - 1] == '\r')
			p--;
		if (p > 0 && out[p - 1] != '\n' && !put(out, omax, &at, "\r\n", 2))
			return -1;
	}
	if (note && !put(out, omax, &at, note, (long)strlen(note)))
		return -1;
	if (!put(out, omax, &at, name, (long)strlen(name)) || !put(out, omax, &at, "=", 1) ||
	    !put(out, omax, &at, val, vl) || !put(out, omax, &at, "\r\n\r\n", 4))
		return -1;
	return at;
}
