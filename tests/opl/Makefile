# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# phase 10 step 7: the YM3812's (OPL2's) test files (mkvgm.py) and the
# reference's WAVs (ref.c over Nuked-OPL3, fetched by fetch-ref.sh: never
# committed); EPlay's model against them follows
B := build
N := $(B)/Nuked-OPL3
FILES := SINE FM ENV WAVE LFO RHYTHM CHORD KEYS

run: $(B)/ref $(B)/files/.made
	for f in $(FILES); do ./$(B)/ref $(B)/files/$$f.VGM $(B)/files/$$f.ref.wav || exit 1; done
$(N)/opl3.c:
	sh fetch-ref.sh
$(B)/ref: ref.c $(N)/opl3.c
	cc -O2 -w -I$(N) -o $@ ref.c $(N)/opl3.c
$(B)/files/.made: mkvgm.py
	python3 mkvgm.py $(B)/files && touch $@
clean:
	rm -rf $(B)
.PHONY: run clean
