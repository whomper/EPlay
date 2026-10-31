#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A DSP test's SIMPROF file against the program's a56 listing: the time
by routine (the label before each address) and the costliest instructions.

    tests/profile.py PROFILE LISTING [N]
"""
import re
import sys
from collections import Counter

prof = {int(a, 16): int(b) for a, b in (l.split() for l in open(sys.argv[1]))}
tot = sum(prof.values())
lab, text, cur = {}, {}, "?"
for line in open(sys.argv[2]):
    m = re.match(r"P:([0-9A-F]{4}) [0-9A-F]{6}(.*)", line.rstrip("\n"))
    if not m:
        continue
    a, rest = int(m.group(1), 16), m.group(2)
    if rest[:1] == " " and rest[1:2] not in (" ", "\t", "") and len(rest.split()) > 1:
        cur = rest.split()[0]
    lab[a], text[a] = cur, rest.strip()
by = Counter()
for a, v in prof.items():
    by[lab.get(a, "?")] += v
n = int(sys.argv[3]) if len(sys.argv) > 3 else 25
print("by routine:", "  ".join("%s %.1f%%" % (k, 100 * v / tot) for k, v in by.most_common(n)))
for a, v in sorted(prof.items(), key=lambda x: -x[1])[:n]:
    print("%04X %5.1f%%  %s" % (a, 100 * v / tot, text.get(a, "")))
