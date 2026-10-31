#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes data/mp3window.txt: the first 257 coefficients of the synthesis
window D (ISO/IEC 11172-3, Annex B, table B.3), each as a multiple of
2^-16, which is how the standard gives them (D[1] = -0.000015259 =
-1/65536), from FFmpeg's ff_mpa_enwindow (libavcodec/mpegaudiodsp_data.c,
LGPL-2.1-or-later). The rest follow: D[512 - i] = -D[i], but D[i] itself
where i is a multiple of 64. Run once; the output is committed.

    tools/mkwin.py mpegaudiodsp_data.c > data/mp3window.txt
"""
import re
import sys

m = re.search(r"ff_mpa_enwindow\[257\]\s*=\s*\{(.*?)\};", open(sys.argv[1]).read(), re.S)
v = [int(x) for x in m.group(1).replace("\n", " ").split(",") if x.strip()]
assert len(v) == 257
print("# SPDX-License-Identifier: GPL-2.0-or-later")
print("# The synthesis window D[0..256] of MPEG audio (ISO/IEC 11172-3, Annex B,")
print("# table B.3) in units of 2^-16, from FFmpeg's transcription (LGPL-2.1-or-later),")
print("# by tools/mkwin.py. D[512 - i] = -D[i], but D[i] where i is a multiple of 64.")
for i in range(0, 257, 8):
    print(" ".join(str(x) for x in v[i:i + 8]))
