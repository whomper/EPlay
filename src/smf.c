/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * smf.c - a Standard MIDI File made into the MIDI port's timed bytes
 * (smf.h). Built into MIDI.OVL (no libc there: nothing of it used here)
 * and into the host's test. The tracks are merged by their ticks, the
 * tempo map taken as it comes (any track's tempo changes, at its tick);
 * the times are kept in microseconds with only additions and multiplies,
 * as a 68000 has no fast divide: a tempo's microseconds a tick is
 * q + f/65536, its parts found once at each tempo change.
 *
 * A record's time is in milliseconds. A type 2 file's tracks are songs
 * one after another: each starts at the end of the one before.
 */
#include "smf.h"

#define MAXTRK 128
#define SMF_TEMPOS 512	/* the tempo changes smf_length keeps */

typedef struct {
	const unsigned char *p, *end, *start;
	unsigned long tick;	/* the next event's */
	int status;		/* running status */
	int done, started;
} TRK;

static void put_s(char *d, int n, const char *s)
{
	int i = 0;
	if (n <= 0)
		return;
	while (s && *s && i < n - 1)
		d[i++] = *s++;
	d[i] = 0;
}

static unsigned long be32(const unsigned char *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* a variable-length number at *pp (4 bytes at most); -1 past end */
static long vlq(const unsigned char **pp, const unsigned char *end)
{
	unsigned long v = 0;
	int i;
	for (i = 0; i < 4; i++) {
		int c;
		if (*pp >= end)
			return -1;
		c = *(*pp)++;
		v = v << 7 | (c & 0x7f);
		if (!(c & 0x80))
			return (long)v;
	}
	return -1;
}

int smf_magic(const unsigned char *b, long n)
{
	return n >= 4 && b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd';
}

/* a meta event's text kept, printable ASCII only (the bar's font) */
static void text(char *d, int n, const unsigned char *s, long len)
{
	int i = 0;
	long k;
	for (k = 0; k < len && i < n - 1; k++) {
		int c = s[k];
		if (c >= 32 && c < 127)
			d[i++] = (char)c;
	}
	while (i > 0 && d[i - 1] == ' ')
		i--;
	d[i] = 0;
}

/* the track's next delta read: its tick, or done */
static void next_delta(TRK *t)
{
	long d;
	if (t->done)
		return;
	if (t->p >= t->end || (d = vlq(&t->p, t->end)) < 0) {
		t->done = 1;
		return;
	}
	t->tick += (unsigned long)d;
}

/* a 16 x 16 -> 32 bit product: one mulu.w on a 68000 (C would call
   __mulsi3, a long multiply) */
#if defined(__m68k__)
static inline unsigned long mul16(unsigned long a, unsigned long b)
{
	__asm__("mulu.w %1,%0" : "+d"(a) : "d"(b));
	return a;
}
#else
#define mul16(a, b) ((unsigned long)(unsigned short)(a) * (unsigned short)(b))
#endif

typedef struct {
	unsigned char *out;
	long max, at, events;
	long last;		/* the record channel messages are being added to (-1: none) */
	unsigned long last_ms;
	int over;
} OUT;

/* a new record of n bytes at ms (a system exclusive message: split at 255) */
static void put(OUT *o, unsigned long ms, const unsigned char *b, long n)
{
	o->last = -1;
	while (n > 0) {
		long k = n > 255 ? 255 : n, i;
		if (o->out) {
			if (o->at + SMF_REC_HEAD + k > o->max) {
				o->over = 1;
				return;
			}
			o->out[o->at] = (unsigned char)(ms >> 24);
			o->out[o->at + 1] = (unsigned char)(ms >> 16);
			o->out[o->at + 2] = (unsigned char)(ms >> 8);
			o->out[o->at + 3] = (unsigned char)ms;
			o->out[o->at + 4] = (unsigned char)k;
			for (i = 0; i < k; i++)
				o->out[o->at + SMF_REC_HEAD + i] = b[i];
		}
		o->at += SMF_REC_HEAD + k;
		b += k;
		n -= k;
	}
	o->events++;
}

/* a channel message (n 2 or 3 bytes): added to the record before when it
   is one of channel messages at the same ms with room, else a record */
static void put_chan(OUT *o, unsigned long ms, const unsigned char *b, int n)
{
	if (o->last >= 0 && o->last_ms == ms && o->at - o->last - SMF_REC_HEAD + n <= 255) {
		if (o->out) {
			int i;
			if (o->at + n > o->max) {
				o->over = 1;
				return;
			}
			for (i = 0; i < n; i++)
				o->out[o->at + i] = b[i];
			o->out[o->last + 4] = (unsigned char)(o->at - o->last - SMF_REC_HEAD + n);
		}
		o->at += n;
		o->events++;
		return;
	}
	{
		long at = o->at;
		put(o, ms, b, n);
		o->last = at;
		o->last_ms = ms;
	}
}

/* the time: whole ms and a 32-bit fraction of one, the ms a tick as a
   whole part and 32 bits of fraction, found once at each tempo change;
   a tick's time then three 16 x 16 products (the 68000's mulu.w) */
typedef struct {
	unsigned long tick;	/* the tick the time is at */
	unsigned long ms;
	unsigned long ah, al;	/* the fraction's sum: its high and low 16 bits (carried) */
	unsigned long qm;	/* whole ms a tick (< 65536) */
	unsigned long fh, fl;	/* the ms a tick's fraction, its high and low 16 bits */
	int smpte;
	unsigned long div;
} CLK;

/* ms a tick = num / den (den under 2^25) */
static void clk_rate(CLK *c, unsigned long num, unsigned long den)
{
	unsigned long q = num / den, r = num - q * den, f = 0;
	int i;
	for (i = 0; i < 32; i++) {
		r <<= 1;
		f <<= 1;
		if (r >= den) {
			r -= den;
			f |= 1;
		}
	}
	c->qm = q > 65535 ? 65535 : q;
	c->fh = f >> 16;
	c->fl = f & 0xffff;
}

static void clk_tempo(CLK *c, unsigned long tempo)
{
	if (!c->smpte)
		clk_rate(c, tempo, c->div * 1000);	/* (us a quarter, ticks a quarter) */
}

static void clk_to(CLK *c, unsigned long tick)
{
	unsigned long dt = tick - c->tick;
	c->tick = tick;
	while (dt) {
		unsigned long k = dt > 65535 ? 65535 : dt;
		dt -= k;
		c->al += mul16(k, c->fl);
		c->ah += mul16(k, c->fh) + (c->al >> 16);
		c->al &= 0xffff;
		c->ms += mul16(k, c->qm) + (c->ah >> 16);
		c->ah &= 0xffff;
	}
}

/* a file being made: smf_begin, then smf_more (the caller's memory, so
   that the list can read one file while another plays) */
struct SMF_ST {
	TRK trk[MAXTRK];
	CLK c;
	OUT o;
	int type, nt, cur, got_name, got_text, ended_;
	unsigned long o_tick, end_ms;
	SMF_INFO *info;
	char *err;
	int errlen;
	unsigned char tmp[256];
	unsigned long tt_tick[SMF_TEMPOS], tt_tempo[SMF_TEMPOS];	/* (smf_length's tempo map) */
};

long smf_state_size(void)
{
	return (long)sizeof(SMF_ST);
}

int smf_begin(SMF_ST *r, const unsigned char *b, long n, unsigned char *out, long max, SMF_INFO *info_, char *err_, int errlen_)
{
	const unsigned char *p = b, *end = b + n;
	unsigned long hlen;
	int ntrk, div, i;
	r->info = info_;
	r->err = err_;
	r->errlen = errlen_;
	r->nt = r->got_name = r->got_text = r->ended_ = 0;
	r->end_ms = 0;
	if (!smf_magic(b, n) || n < 14) {
		put_s(r->err, r->errlen, "This isn't a MIDI file.");
		return -1;
	}
	hlen = be32(b + 4);
	r->type = b[8] << 8 | b[9];
	ntrk = b[10] << 8 | b[11];
	div = (short)(b[12] << 8 | b[13]);
	if (hlen < 6 || hlen > (unsigned long)n - 8 || r->type > 2 || !ntrk || !div) {
		put_s(r->err, r->errlen, "The MIDI file is damaged.");
		return -1;
	}
	p = b + 8 + hlen;
	/* the tracks' chunks ("MTrk"; others passed over) */
	while (p + 8 <= end && r->nt < ntrk && r->nt < MAXTRK) {
		unsigned long len = be32(p + 4);
		const unsigned char *d = p + 8;
		if (len > (unsigned long)(end - d))
			len = (unsigned long)(end - d);	/* (cut short: played to where it ends) */
		if (p[0] == 'M' && p[1] == 'T' && p[2] == 'r' && p[3] == 'k') {
			r->trk[r->nt].p = r->trk[r->nt].start = d;
			r->trk[r->nt].end = d + len;
			r->trk[r->nt].tick = 0;
			r->trk[r->nt].status = 0;
			r->trk[r->nt].done = 0;
			r->trk[r->nt].started = 0;
			r->nt++;
		}
		p = d + len;
	}
	if (!r->nt) {
		put_s(r->err, r->errlen, "The MIDI file has no tracks.");
		return -1;
	}
	if (r->info) {
		r->info->type = r->type;
		r->info->tracks = ntrk;
		r->info->division = div;
		r->info->channels = 0;
		r->info->title[0] = r->info->copyright[0] = 0;
	}
	r->c.tick = r->c.ms = r->c.ah = r->c.al = 0;
	r->c.smpte = div < 0;
	r->c.div = (unsigned long)(div < 0 ? 1 : div);
	if (r->c.smpte) {
		/* (SMPTE: frames a second times ticks a frame; 29 is 29.97) */
		int fps = -(signed char)(div >> 8), tpf = div & 0xff;
		unsigned long f100 = (unsigned long)(fps == 29 ? 2997 : fps * 100);
		clk_rate(&r->c, 100000UL, f100 * (unsigned long)(tpf ? tpf : 1));
	} else
		clk_tempo(&r->c, 500000UL);	/* (120 a minute) */
	r->o.out = out;
	r->o.max = max;
	r->o.at = r->o.events = 0;
	r->o.last = -1;
	r->o.last_ms = 0;
	r->o.over = 0;
	for (i = 0; i < r->nt; i++)
		if (r->type != 2) {
			r->trk[i].started = 1;
			next_delta(&r->trk[i]);
		}
	r->cur = -1;
	r->o_tick = 0;
	if (r->info) {
		r->info->len_ms = 0;
		r->info->events = r->info->bytes = 0;
	}
	return 0;
}

int smf_more(SMF_ST *r, long most, long *made, unsigned long *made_ms)
{
	long k = 0;
	if (r->ended_)
		goto done;
	for (;;) {
		TRK *t = 0;
		int ti = 0, i;
		const unsigned char *s;
		int st;
		unsigned char msg[3];
		if (k++ >= most) {
			/* (to the record still taking this ms's channel messages,
			   which the tick mustn't send while it may grow) */
			*made = r->o.last >= 0 ? r->o.last : r->o.at;
			*made_ms = r->c.ms;
			return 1;
		}
		/* the track whose next event comes first (r->type 2: the one playing) */
		if (r->type == 2) {
			while (ti < r->nt && r->trk[ti].done)
				ti++;
			if (ti >= r->nt)
				break;
			t = &r->trk[ti];
			if (!t->started) {
				/* the next song, from the end of the one before */
				t->started = 1;
				t->tick = r->c.tick;
				next_delta(t);
				continue;
			}
		} else {
			/* every track's events at the earliest tick, the tracks in
			   their order (a track's own events in theirs): the earliest
			   found once a tick, not once a message */
			if (r->cur < 0 || r->cur >= r->nt) {
				unsigned long m = 0xffffffffUL;
				int any = 0;
				for (i = 0; i < r->nt; i++)
					if (!r->trk[i].done && (!any || r->trk[i].tick < m)) {
						m = r->trk[i].tick;
						any = 1;
					}
				if (!any)
					break;
				r->o_tick = m;
				r->cur = 0;
			}
			while (r->cur < r->nt && (r->trk[r->cur].done || r->trk[r->cur].tick != r->o_tick))
				r->cur++;
			if (r->cur >= r->nt)
				continue;	/* (all of this tick's: the next tick's) */
			ti = r->cur;
			t = &r->trk[ti];
		}
		if (t->tick > r->c.tick)
			clk_to(&r->c, t->tick);
		s = t->p;
		if (s >= t->end) {
			t->done = 1;
			continue;
		}
		st = *s;
		if (st & 0x80) {
			s++;
		} else {
			st = t->status;		/* running status */
			if (!(st & 0x80) || st >= 0xf0) {
				t->done = 1;	/* (damaged: no status to run on) */
				continue;
			}
		}
		if (st < 0xf0) {
			int len = (st & 0xe0) == 0xc0 ? 1 : 2;
			t->status = st;
			if (t->end - s < len) {
				t->done = 1;
				continue;
			}
			msg[0] = (unsigned char)st;
			msg[1] = s[0] & 0x7f;
			if (len == 2)
				msg[2] = s[1] & 0x7f;
			s += len;
			put_chan(&r->o, r->c.ms, msg, len + 1);
			if (r->info)
				r->info->channels |= 1 << (st & 15);
		} else if (st == 0xf0 || st == 0xf7) {
			long len;
			t->status = 0;		/* (a system message ends the running status) */
			len = vlq(&s, t->end);
			if (len < 0 || len > t->end - s) {
				t->done = 1;
				continue;
			}
			if (st == 0xf0) {
				/* F0 and its bytes, together where they fit a record */
								unsigned char f0 = 0xf0;
				if (len + 1 <= 255) {
					long k;
					r->tmp[0] = f0;
					for (k = 0; k < len; k++)
						r->tmp[k + 1] = s[k];
					put(&r->o, r->c.ms, r->tmp, len + 1);
				} else {
					put(&r->o, r->c.ms, &f0, 1);
					put(&r->o, r->c.ms, s, len);
				}
			} else if (len)
				put(&r->o, r->c.ms, s, len);
			s += len;
		} else if (st == 0xff) {
			int type_ = s < t->end ? *s++ : 0;
			long len = vlq(&s, t->end);
			t->status = 0;
			if (len < 0 || len > t->end - s) {
				t->done = 1;
				continue;
			}
			if (type_ == 0x51 && len >= 3 && !r->c.smpte)
				clk_tempo(&r->c, (unsigned long)s[0] << 16 | (unsigned long)s[1] << 8 | s[2]);
			else if (type_ == 0x2f) {
				s = t->end;	/* the track's end */
			} else if (r->info && ti == 0 && type_ == 0x03 && !r->got_name) {
				text(r->info->title, sizeof(r->info->title), s, len);
				r->got_name = r->info->title[0] != 0;
			} else if (r->info && type_ == 0x01 && !r->got_name && !r->got_text) {
				text(r->info->title, sizeof(r->info->title), s, len);
				r->got_text = r->info->title[0] != 0;
			} else if (r->info && type_ == 0x02 && !r->info->copyright[0])
				text(r->info->copyright, sizeof(r->info->copyright), s, len);
			s += len;
		} else {
			/* (F1 to FE in a file: not MIDI file events; the track stops) */
			t->done = 1;
			continue;
		}
		t->p = s;
		if (s >= t->end) {
			t->done = 1;
			if (r->c.ms > r->end_ms)
				r->end_ms = r->c.ms;
		} else
			next_delta(t);
		if (r->o.over) {
			put_s(r->err, r->errlen, "There isn't the memory to play.");
			*made = r->o.at;
			*made_ms = r->c.ms;
			return -2;
		}
		if (t->done && r->c.ms > r->end_ms)
			r->end_ms = r->c.ms;
	}
	if (r->c.ms > r->end_ms)
		r->end_ms = r->c.ms;
	r->ended_ = 1;
	if (r->info) {
		r->info->len_ms = (long)r->end_ms;
		r->info->events = r->o.events;
		r->info->bytes = r->o.at;
	}
done:
	*made = r->o.at;
	*made_ms = r->c.ms;
	return 0;
}

long smf_render(const unsigned char *b, long n, unsigned char *out, long max, SMF_INFO *info_, char *err_, int errlen_)
{
	long made;
	unsigned long ms;
	int r;
	static SMF_ST st;
	if (smf_begin(&st, b, n, out, max, info_, err_, errlen_) < 0)
		return -1;
	r = smf_more(&st, 0x7fffffffL, &made, &ms);
	return r < 0 ? r : made;
}

/* the file's length with no stream made: each track read on its own for
   where it ends and its tempo changes (no merging, no bytes written), the
   ends' ticks then timed through the tempo map */
long smf_length(SMF_ST *r, const unsigned char *b, long n, SMF_INFO *info_, char *err_, int errlen_)
{
	unsigned long end_tick = 0, off = 0;
	int i, ntt = 0, got_name = 0, got_text = 0;
	if (smf_begin(r, b, n, 0, 0, info_, err_, errlen_) < 0)
		return -1;
	for (i = 0; i < r->nt; i++) {
		const unsigned char *s = r->trk[i].start, *end = r->trk[i].end;
		unsigned long tick = 0;
		int rs = 0;
		while (s < end) {
			long d = vlq(&s, end), len;
			int st;
			if (d < 0)
				break;
			tick += (unsigned long)d;	/* (a delta at the very end counts: the stream's way) */
			if (s >= end)
				break;
			st = *s;
			if (st & 0x80)
				s++;
			else if (!(rs & 0x80) || rs >= 0xf0)
				break;
			else
				st = rs;
			if (st < 0xf0) {
				len = (st & 0xe0) == 0xc0 ? 1 : 2;
				if (end - s < len)
					break;
				s += len;
				rs = st;
				if (info_) {
					info_->channels |= 1 << (st & 15);
					info_->events++;
				}
			} else if (st == 0xf0 || st == 0xf7) {
				rs = 0;
				len = vlq(&s, end);
				if (len < 0 || len > end - s)
					break;
				s += len;
			} else if (st == 0xff) {
				int t = s < end ? *s++ : 0;
				rs = 0;
				len = vlq(&s, end);
				if (len < 0 || len > end - s)
					break;
				if (t == 0x51 && len >= 3 && !r->c.smpte) {
					if (ntt >= SMF_TEMPOS)
						return -2;	/* (more than it keeps: smf_render's count) */
					r->tt_tick[ntt] = off + tick;
					r->tt_tempo[ntt++] = (unsigned long)s[0] << 16 | (unsigned long)s[1] << 8 | s[2];
				} else if (info_ && i == 0 && t == 0x03 && !got_name) {
					text(info_->title, sizeof(info_->title), s, len);
					got_name = info_->title[0] != 0;
				} else if (info_ && t == 0x01 && !got_name && !got_text) {
					text(info_->title, sizeof(info_->title), s, len);
					got_text = info_->title[0] != 0;
				} else if (info_ && t == 0x02 && !info_->copyright[0])
					text(info_->copyright, sizeof(info_->copyright), s, len);
				s += len;
				if (t == 0x2f)
					break;
			} else
				break;
		}
		if (r->type == 2)
			off += tick;	/* (songs one after another) */
		else if (tick > end_tick)
			end_tick = tick;
	}
	if (r->type == 2)
		end_tick = off;
	/* the tempo map in tick order (a stable insertion sort: a few at most,
	   in order already from a type 0 or 1's first track) */
	for (i = 1; i < ntt; i++) {
		unsigned long tk = r->tt_tick[i], tp = r->tt_tempo[i];
		int j = i;
		while (j > 0 && r->tt_tick[j - 1] > tk) {
			r->tt_tick[j] = r->tt_tick[j - 1];
			r->tt_tempo[j] = r->tt_tempo[j - 1];
			j--;
		}
		r->tt_tick[j] = tk;
		r->tt_tempo[j] = tp;
	}
	for (i = 0; i < ntt && r->tt_tick[i] <= end_tick; i++) {
		if (r->tt_tick[i] > r->c.tick)
			clk_to(&r->c, r->tt_tick[i]);
		clk_tempo(&r->c, r->tt_tempo[i]);
	}
	if (end_tick > r->c.tick)
		clk_to(&r->c, end_tick);
	if (info_)
		info_->len_ms = (long)r->c.ms;
	return (long)r->c.ms;
}
