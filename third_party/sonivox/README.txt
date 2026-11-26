Sonivox EAS's General MIDI sound set, as Android's Open Source Project
carries it:

  wt_22khz.c    platform/external/sonivox, arm-wt-22k/lib_src/wt_22khz.c
                (its own name inside: wt_200k_G.c, from wt_200k_G.dls),
                commit be2ad9d2cd96566ce22bfcd405fe21055b2f083d
                (7 Sep 2024), unchanged
  NOTICE        that project's NOTICE, unchanged
  LICENSE-2.0.txt  the Apache License, Version 2.0

Copyright (c) 2004-2009 Sonic Network Inc., under the Apache License,
Version 2.0 (NOTICE, LICENSE-2.0.txt).

The set: 128 General MIDI instruments and the drum kit, 377 key ranges
(regions), 185 envelopes and filters (articulations), 150 samples, 212,050
sample points at 22,050 Hz, both as 8-bit and as 16-bit samples.

EPlay doesn't build this file into its program: tools/mkgmset.py makes it
into a data file of its own (built into binary/, shipped beside EPLAY.PRG),
which stays a separate work under the Apache License (NOTICES.TXT).
