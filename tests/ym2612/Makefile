# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# phase 10: the YM2612's test files (mkvgm.py) and the reference's WAVs
# (ref.c over Nuked-OPN2, fetched by fetch-ref.sh: never committed)
TOP := ../..
B := build
N := $(B)/Nuked-OPN2

run: $(B)/ref $(B)/files/.made
	for f in SINE FM CHAIN ENV DAC CHORD; do ./$(B)/ref $(B)/files/$$f.VGM $(B)/files/$$f.ref.wav || exit 1; done
$(N)/ym3438.c:
	sh fetch-ref.sh
$(B)/ref: ref.c $(TOP)/src/vgm.c $(TOP)/src/vgm.h $(N)/ym3438.c
	cc -O2 -w -I$(TOP)/src -I$(TOP)/common/src -I$(N) -o $@ ref.c $(TOP)/src/vgm.c \
		$(TOP)/common/src/charset.c $(TOP)/common/src/bidi.c $(N)/ym3438.c
$(B)/files/.made: mkvgm.py
	python3 mkvgm.py $(B)/files && touch $@
.PHONY: run
