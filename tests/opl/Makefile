# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# phase 10 step 7: the YM3812's (OPL2's) test files (mkvgm.py), the
# reference's WAVs (ref.c over Nuked-OPL3, fetched by fetch-ref.sh: never
# committed), the DSP's arithmetic's (fast.c over src/opld.c and
# src/oplfast.c) at the chip's rate and the Falcon's, and the two compared
# (compare.py); and the CPU's (cpu: src/oplcpu.c) the same way
TOP := ../..
B := build
N := $(B)/Nuked-OPL3
FILES2 := SINE FM ENV WAVE LFO RHYTHM CHORD KEYS
FILES3 := O3PAN O3BANK O3WAVE O3FOUR0 O3FOUR1 O3FOUR2 O3FOUR3 O3CHORD O3COMPAT
FILES := $(FILES2) $(FILES3)
CF := -O2 -Wall -Wextra -Wno-unused-parameter -Werror -I$(TOP)/src

run: $(B)/ref $(B)/fast $(B)/cpu $(B)/files/.made
	for f in $(FILES); do ./$(B)/ref $(B)/files/$$f.VGM $(B)/files/$$f.ref.wav && \
		./$(B)/fast $(B)/files/$$f.VGM $(B)/files/$$f.f49.wav 49716 && \
		./$(B)/fast $(B)/files/$$f.VGM $(B)/files/$$f.f24.wav 24585 && \
		./$(B)/fast $(B)/files/$$f.VGM $(B)/files/$$f.f16.wav 16390 && \
		./$(B)/cpu $(B)/files/$$f.VGM $(B)/files/$$f.c24.wav 24585 && \
		./$(B)/cpu $(B)/files/$$f.VGM $(B)/files/$$f.c16.wav 16390 || exit 1; done
	python3 compare.py $(B)/files 49716 $(FILES2)
	python3 compare.py $(B)/files 24585 $(FILES2)
	python3 compare.py $(B)/files 49716 $(FILES3)
	python3 compare.py $(B)/files 24585 $(FILES3)
	python3 compare.py $(B)/files 16390 $(FILES3)
	python3 compare.py $(B)/files c24585 $(FILES)
	python3 compare.py $(B)/files c16390 $(FILES3)
$(N)/opl3.c:
	sh fetch-ref.sh
$(B)/ref: ref.c $(N)/opl3.c
	cc -O2 -w -I$(N) -o $@ ref.c $(N)/opl3.c
$(B)/fast: fast.c $(TOP)/src/opld.c $(TOP)/src/opld.h $(TOP)/src/oplfast.c $(TOP)/src/oplfast.h \
		$(TOP)/src/egmath.c $(TOP)/src/egmath.h
	@mkdir -p $(B)
	cc $(CF) -o $@ fast.c $(TOP)/src/opld.c $(TOP)/src/oplfast.c $(TOP)/src/egmath.c -lm
# the CPU's (src/oplcpu.c, phase 11: a 68060 without the DSP), its tables
# the Atari's (tools/mkopltab.c's)
$(B)/opltab.h: $(TOP)/tools/mkopltab.c $(TOP)/src/oplfast.c $(TOP)/src/oplfast.h $(TOP)/src/oplexp.h
	@mkdir -p $(B)
	cc $(CF) -o $(B)/mkopltab $(TOP)/tools/mkopltab.c $(TOP)/src/oplfast.c -lm
	$(B)/mkopltab $@
$(B)/cpu: fast.c $(B)/opltab.h $(TOP)/src/opld.c $(TOP)/src/opld.h $(TOP)/src/oplcpu.c $(TOP)/src/oplcpu.h \
		$(TOP)/src/egmath.c $(TOP)/src/egmath.h
	cc $(CF) -DOPL_CPU -I$(B) -o $@ fast.c $(TOP)/src/opld.c $(TOP)/src/oplcpu.c $(TOP)/src/egmath.c -lm
$(B)/files/.made: mkvgm.py
	python3 mkvgm.py $(B)/files && touch $@
clean:
	rm -rf $(B)
.PHONY: run clean
