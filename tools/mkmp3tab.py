#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes mp3tab.h: every table the MP3 decoder's DSP program uses, as
24-bit words (Q23 fractions where they are numbers), for the C model
(src/mp3/mp3dsp.c), for dsp/mp3.a56 and for the 68030 that loads them.

    tools/mkmp3tab.py data/ OUT.h

The numbers are the standard's (ISO/IEC 11172-3): the Huffman codes and
the synthesis window from data/, the rest worked out here.
"""
import math
import os
import sys

datadir, out = sys.argv[1:3]
Q = 1 << 23
tabs = []  # (C name, words, comment)


def q23(x):
    v = int(round(x * Q))
    return max(-Q, min(Q - 1, v))


def add(name, words, comment):
    tabs.append((name, [w & 0xffffff for w in words], comment))


# ---- the inverse transforms: rows that differ (the others are them, or
# minus them; mp3dsp.c says which)
r36 = list(range(0, 9)) + list(range(18, 27))
add("t_imdct36", [q23(math.cos(math.pi / 72 * (2 * i + 19) * (2 * k + 1)))
                  for i in r36 for k in range(18)],
    "36-point IMDCT, rows 0-8 and 18-26 of cos(pi/72 (2i+19)(2k+1)), 18 x 18")
r12 = [0, 1, 2, 6, 7, 8]
add("t_imdct12", [q23(math.cos(math.pi / 24 * (2 * i + 7) * (2 * k + 1)))
                  for i in r12 for k in range(6)],
    "12-point IMDCT, rows 0-2 and 6-8 of cos(pi/24 (2i+7)(2k+1)), 6 x 6")


def win(bt):
    w = []
    for i in range(36):
        if bt == 0:
            v = math.sin(math.pi / 36 * (i + 0.5))
        elif bt == 1:
            v = (math.sin(math.pi / 36 * (i + 0.5)) if i < 18 else 1.0 if i < 24 else
                 math.sin(math.pi / 12 * (i - 18 + 0.5)) if i < 30 else 0.0)
        elif bt == 3:
            v = (0.0 if i < 6 else math.sin(math.pi / 12 * (i - 6 + 0.5)) if i < 12 else
                 1.0 if i < 18 else math.sin(math.pi / 36 * (i + 0.5)))
        else:
            v = math.sin(math.pi / 12 * (i + 0.5)) if i < 12 else 0.0
        w.append(q23(v))
    return w


add("t_win", win(0) + win(1) + win(2) + win(3),
    "the windows of block types 0-3, 36 each (type 2: the 12 of a short block)")
c = [-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037]
add("t_alias", [q23(1 / math.sqrt(1 + x * x)) for x in c] + [q23(x / math.sqrt(1 + x * x)) for x in c],
    "alias reduction: cs[0..7], then ca[0..7]")

# ---- the synthesis filter
# the 32-point DCT X[n] = sum x[k] cos(pi (2k+1) n / 64), after one
# butterfly (e[k] = x[k] + x[31-k], o[k] = x[k] - x[31-k]): X[2m] from e,
# X[2m+1] from o, 16 x 16 each, the rows in the order of n
add("t_synth", [q23(math.cos(math.pi * (2 * k + 1) * n / 64)) for n in range(32) for k in range(16)],
    "the synthesis filter's DCT: for n = 0..31, 16 cos(pi (2k+1) n / 64) (k < 16)")
d = []
for line in open(os.path.join(datadir, "mp3window.txt")):
    if not line.startswith("#"):
        d += [int(x) for x in line.split()]
D = [0] * 512
for i, v in enumerate(d):
    D[i] = v
    if i:
        D[512 - i] = v if i % 64 == 0 else -v
add("t_dwin", [q23(v / 65536 / 2) for v in D], "the synthesis window D[0..511], halved")

# ---- dequantising
mant, expo = [], []
for n in range(1024):
    m, e = math.frexp(n ** (4 / 3)) if n else (0.0, 0)
    mv = q23(m)
    if mv == Q - 1 and m >= 1 - 2 ** -24:  # rounded up to 1: keep it in range
        mv = Q - 1
    mant.append(mv)
    expo.append(e)
add("t_pow43m", mant, "n^(4/3) = m 2^e for n < 1024: the mantissas m, in [0.5, 1)")
add("t_pow43e", expo, "and the exponents e")
add("t_pow43b", [q23(a ** (4 / 3) / 2 ** 14) for a in range(128, 1027)],
    "a^(4/3) / 2^14 for a = 128..1026, for n >= 1024 (a = n / 8, interpolated)")
add("t_gain", [q23(2 ** (r / 4) / 2) for r in range(4)], "2^(r/4) / 2, r = 0..3")
add("t_misc", [q23(1 / math.sqrt(2))], "1/sqrt(2), for mid/side stereo")
kl, kr = [], []
for p in range(7):
    t = math.tan(p * math.pi / 12) if p < 6 else None
    kl.append(q23(1.0 if t is None else t / (1 + t)))
    kr.append(q23(0.0 if t is None else 1 / (1 + t)))
add("t_istereo", kl + kr, "intensity stereo, is_pos 0..6: left factors, then right")

# ---- scalefactor bands, as boundaries in lines: MPEG-1 (44.1, 48, 32 kHz),
# MPEG-2 (22.05, 24, 16) and MPEG-2.5 (11.025, 12, 8)
long_w = [[4, 4, 4, 4, 4, 4, 6, 6, 8, 8, 10, 12, 16, 20, 24, 28, 34, 42, 50, 54, 76, 158],
          [4, 4, 4, 4, 4, 4, 6, 6, 6, 8, 10, 12, 16, 18, 22, 28, 34, 40, 46, 54, 54, 192],
          [4, 4, 4, 4, 4, 4, 6, 6, 8, 10, 12, 16, 20, 24, 30, 38, 46, 56, 68, 84, 102, 26],
          [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54],
          [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 18, 22, 26, 32, 38, 46, 54, 62, 70, 76, 36],
          [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54],
          [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54],
          [6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54],
          [12, 12, 12, 12, 12, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 76, 90, 2, 2, 2, 2, 2]]
short_w = [[4, 4, 4, 4, 6, 8, 10, 12, 14, 18, 22, 30, 56],
           [4, 4, 4, 4, 6, 6, 10, 12, 14, 16, 20, 26, 66],
           [4, 4, 4, 4, 6, 8, 12, 16, 20, 26, 34, 42, 12],
           [4, 4, 4, 6, 6, 8, 10, 14, 18, 26, 32, 42, 18],
           [4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 32, 44, 12],
           [4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 30, 40, 18],
           [4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 30, 40, 18],
           [4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 30, 40, 18],
           [8, 8, 8, 12, 16, 20, 24, 28, 36, 2, 2, 2, 26]]
sl, ss = [], []
for lw, sw in zip(long_w, short_w):
    assert sum(lw) == 576 and sum(sw) == 192
    sl += [sum(lw[:i]) for i in range(23)]
    ss += [sum(sw[:i]) for i in range(14)]
add("t_sfbl", sl, "long scalefactor band starts, 23 a rate (PK_RATE's order)")
add("t_sfbs", ss, "short scalefactor band starts (in a window's lines), 14 a rate")
# MPEG-2's nr_of_sfb (ISO/IEC 13818-3, table B.1): row, then long, short, mixed
nsf = [[[6, 5, 5, 5], [9, 9, 9, 9], [6, 9, 9, 9]],
       [[6, 5, 7, 3], [9, 9, 12, 6], [6, 9, 12, 6]],
       [[11, 10, 0, 0], [18, 18, 0, 0], [15, 18, 0, 0]],
       [[7, 7, 7, 0], [12, 12, 12, 0], [6, 15, 12, 0]],
       [[6, 6, 6, 3], [12, 9, 9, 6], [6, 12, 9, 6]],
       [[8, 8, 5, 0], [15, 12, 9, 0], [6, 18, 9, 0]]]
add("t_nsf", [n for row in nsf for blk in row for n in blk],
    "MPEG-2's scalefactors in each of the four parts: row (0-5), block (long, short, mixed)")
add("t_lsfis", [q23(2 ** (-q / 4)) for q in range(32)], "MPEG-2's intensity stereo: 2^(-q/4)")
add("t_pretab", [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0], "pretab")

# ---- the Huffman codes as lookup tables: a node looks at n bits; a word
# is a leaf, bit 23 clear, with the bits it uses (16-20) and x, y (4-7,
# 0-3; v w x y in 3-0 for count1), or bit 23 set, a node: its n (16-19)
# and where it starts (0-15)
codes = {}
for line in open(os.path.join(datadir, "mp3huff.txt")):
    f = line.split()
    if not f or f[0].startswith("#"):
        continue
    if f[0] in "AB":
        v = int(f[1]) << 3 | int(f[2]) << 2 | int(f[3]) << 1 | int(f[4])
        codes.setdefault(f[0], []).append((f[6], v))
    else:
        codes.setdefault(int(f[0]), []).append((f[4], int(f[1]) << 4 | int(f[2])))

ht = []
ROOT, SUB = 6, 4


def build(clist, n):
    """clist: (bits, value) with the bits still to read; returns where it starts"""
    at = len(ht)
    ht.extend([None] * (1 << n))
    for i in range(1 << n):
        pre = format(i, "0%db" % n)
        leaf = [(b, v) for b, v in clist if len(b) <= n and pre.startswith(b)]
        if leaf:
            b, v = leaf[0]
            ht[at + i] = len(b) << 16 | v
        else:
            rest = [(b[n:], v) for b, v in clist if len(b) > n and b.startswith(pre)]
            if not rest:
                ht[at + i] = 0  # no code starts so: a bad stream
                continue
            m = min(SUB, max(len(b) for b, _ in rest))
            ht[at + i] = ("node", m, rest)
    for i in range(1 << n):
        e = ht[at + i]
        if isinstance(e, tuple):
            _, m, rest = e
            ht[at + i] = 1 << 23 | m << 16 | build(rest, m)
    return at


order = [1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24, "A", "B"]
roots = {}
for t in order:
    roots[t] = build(codes[t], min(ROOT, max(len(b) for b, _ in codes[t])))
assert len(ht) < 1 << 16
add("t_huff", ht, "the Huffman lookup tables, %d words" % len(ht))

linbits = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           1, 2, 3, 4, 6, 8, 10, 13, 4, 5, 6, 7, 8, 9, 11, 13]
sel = []
for t in range(32):
    base = t if t < 16 else 16 if t < 24 else 24
    if base in roots:
        n = min(ROOT, max(len(b) for b, _ in codes[base]))
        sel.append(linbits[t] << 20 | n << 16 | roots[base])
    else:
        sel.append(0)  # table 0, and the unused 4 and 14: all zeros
for t in "AB":
    sel.append(min(ROOT, max(len(b) for b, _ in codes[t])) << 16 | roots[t])
add("t_hsel", sel, "tables 0-31 then A, B: linbits (20-23), the root's n (16-19), where it starts")

# ---- the same codes laid out for dsp/mp3.a56, at fixed DSP addresses: a
# node is a word in X and one in Y at the same address, read together.
#
# A pair table's root looks at 8 bits. Where they hold a whole code and its
# sign bits (and no escape to linbits), X is the bits they take (1-8) and Y
# the place of x and y, signed, in the signed pair table: the fast way.
# Otherwise X has bit 23 set: with bit 22, a node (Y its address), from
# which the code goes on (below); without, a code of X's low bits with its
# unsigned x and y at Y in the pair table, for the signs and linbits after.
# Below a root, and in the count1 tables (a 6-bit root), X is a leaf's bits
# at this node, or minus the bits a node looks at; Y the leaf's place in the
# pair table (pairs) or its v w x y (count1), or the node's address.
DSP_SPAIR = 0x0a00  # X and Y: x and y signed, -15..15 each (961)
DSP_PAIR = 0x0e00   # X and Y: x and y of each pair value (256)
DSP_HUFF = 0x1000   # X and Y external: the nodes
dx, dy = [], []


def dbuild(clist, n, quad):
    at = len(dx)
    dx.extend([None] * (1 << n))
    dy.extend([None] * (1 << n))
    subs = []
    for i in range(1 << n):
        pre = format(i, "0%db" % n)
        leaf = [(b, v) for b, v in clist if len(b) <= n and pre.startswith(b)]
        if leaf:
            b, v = leaf[0]
            dx[at + i] = len(b)
            dy[at + i] = v if quad else DSP_PAIR + v
        else:
            subs.append(i)
    for i in subs:
        pre = format(i, "0%db" % n)
        rest = [(b[n:], v) for b, v in clist if len(b) > n and b.startswith(pre)]
        if rest:
            m = min(4, max(len(b) for b, _ in rest))
            dx[at + i] = -m
            dy[at + i] = DSP_HUFF + dbuild(rest, m, quad)
        else:  # no code starts so (a bad stream): a bit used, a zero
            dx[at + i], dy[at + i] = 1, 0 if quad else DSP_PAIR
    return at


def droot8(clist, lin):
    """a pair table's 8-bit root"""
    at = len(dx)
    dx.extend([None] * 256)
    dy.extend([None] * 256)
    subs = []
    for i in range(256):
        pre = format(i, "08b")
        leaf = [(b, v) for b, v in clist if pre.startswith(b)]
        if not leaf:
            subs.append(i)
            continue
        b, v = leaf[0]
        x, y = v >> 4, v & 15
        n = len(b) + (x != 0) + (y != 0)
        if (lin and (x == 15 or y == 15)) or n > 8:
            dx[at + i] = 0x800000 | len(b)
            dy[at + i] = DSP_PAIR + v
            continue
        k = len(b)
        if x:
            if pre[k] == "1":
                x = -x
            k += 1
        if y and pre[k] == "1":
            y = -y
        dx[at + i] = n
        dy[at + i] = DSP_SPAIR + (x + 15) * 31 + (y + 15)
    for i in subs:
        pre = format(i, "08b")
        rest = [(b[8:], v) for b, v in clist if len(b) > 8 and b.startswith(pre)]
        if rest:
            m = min(4, max(len(b) for b, _ in rest))
            dx[at + i] = 0xc00000 | m
            dy[at + i] = DSP_HUFF + dbuild(rest, m, False)
        else:
            dx[at + i], dy[at + i] = 0x800001, DSP_PAIR
    return at


droot = {}
for t in order:
    if t in ("A", "B"):
        droot[t] = DSP_HUFF + dbuild(codes[t], 6, True)
    else:
        droot[t] = DSP_HUFF + droot8(codes[t], t in (16, 24))
assert DSP_HUFF + len(dx) <= 0x3000, len(dx)
add("t_dhx", dx, "dsp/mp3.a56's Huffman nodes in X at $%04x" % DSP_HUFF)
add("t_dhy", dy, "and in Y")
add("t_dpx", [v >> 4 for v in range(256)], "the pair table at X:$%04x: x of each value" % DSP_PAIR)
add("t_dpy", [v & 15 for v in range(256)], "and Y: y")
add("t_dsx", [x for x in range(-15, 16) for y in range(-15, 16)],
    "the signed pair table at X:$%04x: x" % DSP_SPAIR)
add("t_dsy", [y for x in range(-15, 16) for y in range(-15, 16)], "and Y: y")
dsel_x, dsel_y = [], []
for t in range(32):
    base = t if t < 16 else 16 if t < 24 else 24
    dsel_x.append(droot[base] if base in droot else 0)
    dsel_y.append(linbits[t])
dsel_x += [droot["A"], droot["B"]]
dsel_y += [0, 0]
add("t_dselx", dsel_x, "tables 0-31, A, B: the root's address (0: all zeros)")
add("t_dsely", dsel_y, "and the linbits")

with open(out, "w") as f:
    f.write("/* SPDX-License-Identifier: GPL-2.0-or-later\n * Copyright (C) 2026 AERA | Erez Yaary\n"
            " * generated by tools/mkmp3tab.py - do not edit */\n")
    total = 0
    for name, words, comment in tabs:
        f.write("\n/* %s */\n#define %s_N %d\nstatic const unsigned long %s[%d] = {\n" %
                (comment, name.upper(), len(words), name, len(words)))
        for i in range(0, len(words), 8):
            f.write("\t" + ", ".join("0x%06lx" % w for w in words[i:i + 8]) + ",\n")
        f.write("};\n")
        total += len(words)
    f.write("\n#define MP3TAB_WORDS %d\n" % total)
print("%s: %d words" % (out, total))
