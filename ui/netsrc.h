/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * netsrc.h - a web radio station or audio stream as the source of the
 * queue (phase 7): connected with common's conn (STinG or MiNTnet), asked
 * for with "Icy-MetaData: 1", its answer read (redirects followed, a
 * station list's first station taken), then its audio read without
 * waiting, the ICY metadata taken out (src/stream.c).
 */
#ifndef EPLAY_NETSRC_H
#define EPLAY_NETSRC_H

#include "stream.h"

/* connect to url and read the answer's head (waits, Esc stops it: up to
   the connection's 60 s). 1 when audio follows, else 0 with err ("The
   station doesn't answer.", "... sends AAC: ...") */
int ns_open(const char *url, char *err, int n);

/* the audio come so far, up to max bytes, at buf: > 0 the bytes; 0 none
   yet (wait 1: until some come, or the connection's time runs out); -1
   the connection ended (closed, timed out, stopped) */
long ns_read(void *buf, long max, int wait);

/* the same station again after the connection ended: 1 when its audio
   follows (it starts where the station is now, not where it stopped) */
int ns_reopen(char *err, int n);

void ns_close(void);
int ns_is_open(void);

/* the station's name (icy-name, else the host), its kbit/s (0: not given),
   the stream's title now ("" before the first) and whether it changed
   since the last ns_title */
const char *ns_name(void);
long ns_kbps(void);
const char *ns_title(int *changed);
const char *ns_title_peek(void);	/* the title now, its change left for ns_title */
const char *ns_url(void);	/* the station's URL, after any redirect */

#endif
