/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * inf.h - EPLAY.INF's text (the guidelines' 6.1): NAME=value lines, ';'
 * notes, an empty line after each setting, CR/LF. A value is read, or set
 * keeping every other line as it was (notes and lines EPlay doesn't know).
 */
#ifndef EPLAY_INF_H
#define EPLAY_INF_H

/* NAME's value into val (at most vmax - 1 characters): 1, or 0 if the text
   (t, n bytes) has no NAME= line */
int inf_get(const char *t, long n, const char *name, char *val, int vmax);

/* the text with NAME=value: its line's value replaced where it has one,
   else added at the end with its note (';' lines, CR/LF ends, or 0) and an
   empty line after it. Into out (omax bytes): the new length, or -1 if it
   doesn't fit */
long inf_set(const char *t, long n, const char *name, const char *val, const char *note, char *out, long omax);

#endif
