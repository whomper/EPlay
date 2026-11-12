# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# phase 10: the YM2612's test files (mkvgm.py), the reference's WAVs (ref.c
# over Nuked-OPN2, fetched by fetch-ref.sh: never committed), EPlay's model's
# (model.c over src/opn.c) and the two compared (compare.py)
TOP := ../..
B := build
N := $(B)/Nuked-OPN2

# the model (src/opn.c) against the reference, at the chip's rate and the Falcon's
run: $(B)/ref $(B)/model $(B)/fast $(B)/files/.made
	for f in SINE FM CHAIN ENV DAC CHORD LFO CH3 DETUNE; do ./$(B)/ref $(B)/files/$$f.VGM $(B)/files/$$f.ref.wav && \
		./$(B)/model $(B)/files/$$f.VGM $(B)/files/$$f.m53.wav 53267 && \
		./$(B)/model $(B)/files/$$f.VGM $(B)/files/$$f.m24.wav 24585 && \
		./$(B)/fast $(B)/files/$$f.VGM $(B)/files/$$f.f24.wav 24585 || exit 1; done
	python3 compare.py $(B)/files 53267
	python3 compare.py $(B)/files 24585
	python3 compare.py $(B)/files f24585
$(B)/model: model.c $(TOP)/src/opn.c $(TOP)/src/opn.h $(TOP)/src/vgm.c $(TOP)/src/vgm.h
	cc -O2 -Wall -Wextra -Wno-unused-parameter -Werror -I$(TOP)/src -I$(TOP)/common/src -o $@ model.c \
		$(TOP)/src/opn.c $(TOP)/src/vgm.c $(TOP)/common/src/charset.c $(TOP)/common/src/bidi.c -lm
# the DSP's arithmetic (src/opnd.c, src/opnfast.c), at the Falcon's rate
$(B)/fast: fast.c $(TOP)/src/opnd.c $(TOP)/src/egmath.c $(TOP)/src/opnd.h $(TOP)/src/opnfast.c $(TOP)/src/opnfast.h $(TOP)/src/vgm.c $(TOP)/src/vgm.h
	cc -O2 -Wall -Wextra -Wno-unused-parameter -Werror -I$(TOP)/src -I$(TOP)/common/src -o $@ fast.c \
		$(TOP)/src/opnd.c $(TOP)/src/egmath.c $(TOP)/src/opnfast.c $(TOP)/src/vgm.c $(TOP)/common/src/charset.c $(TOP)/common/src/bidi.c -lm
$(N)/ym3438.c:
	sh fetch-ref.sh
$(B)/ref: ref.c $(TOP)/src/vgm.c $(TOP)/src/vgm.h $(N)/ym3438.c
	cc -O2 -w -I$(TOP)/src -I$(TOP)/common/src -I$(N) -o $@ ref.c $(TOP)/src/vgm.c \
		$(TOP)/common/src/charset.c $(TOP)/common/src/bidi.c $(N)/ym3438.c
$(B)/files/.made: mkvgm.py
	python3 mkvgm.py $(B)/files && touch $@
.PHONY: run
