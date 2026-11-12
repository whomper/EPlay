# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# dsp/opl.a56, the YM3812 (OPL2) on the DSP (phase 10, step 7), on Hatari's
# DSP core: against its model (src/oplfast.c) bit for bit on tests/opl's
# files, and its worst cases' time (sim.c)
H := ../../common/tests/ec/build/hatari
B := build
TOP := ../..
FILES := SINE FM ENV WAVE LFO RHYTHM CHORD KEYS

run: $(B)/sim $(B)/files/.made
	./$(B)/sim $(addprefix $(B)/files/,$(addsuffix .VGM,$(FILES)))
$(B)/opl_prog.h: $(TOP)/dsp/opl.a56 | $(B)
	cd $(B) && a56 ../$(TOP)/dsp/opl.a56 > a56.lst && ! grep -q 'warning:\|error:' a56.lst && \
		python3 ../$(TOP)/common/dsp/mkh.py a56.out opl_prog.h dsp_opl_prog opl.a56
$(B)/sim: sim.c ../dspsim.h $(B)/opl_prog.h $(H)/dsp_core.c $(TOP)/src/opld.c $(TOP)/src/opld.h $(TOP)/src/egmath.c \
		$(TOP)/src/oplfast.c $(TOP)/src/oplfast.h
	cc -O2 -w -I$(B) -I$(H) -I$(H)/stub -I$(TOP)/src -o $@ sim.c $(H)/dsp_core.c $(H)/dsp_cpu.c \
		$(TOP)/src/opld.c $(TOP)/src/egmath.c $(TOP)/src/oplfast.c -lm
$(B)/files/.made: ../opl/mkvgm.py | $(B)
	python3 ../opl/mkvgm.py $(B)/files && touch $@
$(H)/dsp_core.c:
	sh $(TOP)/common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: run clean
