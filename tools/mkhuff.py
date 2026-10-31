#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes data/mp3huff.txt, the Huffman codes of MPEG audio layer III
(ISO/IEC 11172-3, Annex B, tables B.7), one code a line, from FFmpeg's
transcription of them (libavcodec/mpegaudiodec_common.c, LGPL-2.1-or-later):
its code lengths and symbols in code order, from which the codes follow
(each the last plus one, at its own length). Run once; the output is
committed, and tests/mp3 checks it decodes real files.

    tools/mkhuff.py mpegaudiodec_common.c > data/mp3huff.txt
"""
import re
import sys

src = open(sys.argv[1]).read()


def array(name):
    m = re.search(r"%s\[\]\s*=\s*\{(.*?)\};" % name, src, re.S)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [int(v, 0) for v in body.replace("\n", " ").split(",") if v.strip()]


def array2(name):
    m = re.search(r"%s\[2\]\[16\]\s*=\s*\{(.*?)\};" % name, src, re.S)
    rows = re.findall(r"\{([^}]*)\}", m.group(1))
    return [[int(v, 0) for v in r.split(",") if v.strip()] for r in rows]


lens = array("mpa_hufflens")
syms = array("mpa_huffsymbols")
sizes = [n + 1 for n in array("mpa_huff_sizes_minus_one")]
numbers = [1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24]
assert sum(sizes) == len(lens) == len(syms)

print("# SPDX-License-Identifier: GPL-2.0-or-later")
print("# The Huffman codes of MPEG audio layer III (ISO/IEC 11172-3, Annex B),")
print("# from FFmpeg's transcription (LGPL-2.1-or-later), by tools/mkhuff.py.")
print("# table x y length code (the code in binary); tables 16 and 24 also stand")
print("# for 17-23 and 25-31, which differ only in their linbits; tables A and B")
print("# are the count1 tables, with v w x y.")
at = 0
for num, n in zip(numbers, sizes):
    code = 0  # left-aligned in 32 bits
    for i in range(at, at + n):
        ln, s = lens[i], syms[i]
        c = code >> (32 - ln)
        code += 1 << (32 - ln)
        print("%d %d %d %d %s" % (num, s >> 4, s & 15, ln, format(c, "0%db" % ln)))
    assert code == 1 << 32, "table %d is not complete" % num
    at += n
qc, qb = array2("mpa_quad_codes"), array2("mpa_quad_bits")
for t, name in enumerate("AB"):
    for i in range(16):
        print("%s %d %d %d %d %d %s" % (name, i >> 3 & 1, i >> 2 & 1, i >> 1 & 1, i & 1,
                                         qb[t][i], format(qc[t][i], "0%db" % qb[t][i])))
