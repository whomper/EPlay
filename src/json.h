/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * json.h - a small JSON reader that walks an answer in place: no copy, no
 * tree, never past its end. The library's platforms (lib.h) take what they
 * need from each object and pass over the rest. Any fault sets bad and
 * every call after it does nothing.
 */
#ifndef JSON_H
#define JSON_H

typedef struct {
	const char *p, *e;
	int bad, first;
} JS;

void js_init(JS *j, const char *text, long len);
int js_peek(JS *j);			/* the next character after white space, 0 at the end */

/* an object: js_obj(j) takes its '{'; then while js_key(j, key, n) gives a
   key (its ':' taken), read or js_skip its value; js_key gives 0 at '}' */
int js_obj(JS *j);
int js_key(JS *j, char *key, int n);
/* an array: js_arr(j) takes its '['; while js_item(j) read each value */
int js_arr(JS *j);
int js_item(JS *j);

void js_skip(JS *j);			/* any value */
int js_str(JS *j, char *out, int n);	/* a string as raw UTF-8 (null: any value passed over) */
void js_text(JS *j, char *out, int n);	/* a string as Atari text; "" for null or another value */
/* a number, its last drop digits dropped (RunTimeTicks / 10^4 = ms); 0 for
   null or another value, at most 2^31 - 1 */
long js_num(JS *j, int drop);
int js_bool(JS *j);			/* true, else 0 */

/* s (Atari text) as a JSON string, quoted, onto out at k: the new length, -1 if it won't fit */
int js_put(char *out, int k, int n, const char *s);

/* UTF-8 p..e as Atari text into out (n bytes) */
void js_utf8_atari(const char *p, const char *e, char *out, int n);

#endif
