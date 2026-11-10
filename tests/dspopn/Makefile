# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# dsp/opn.a56, the YM2612 on the DSP (phase 10), on Hatari's DSP core:
# against its model (src/opnfast.c) bit for bit on tests/ym2612's files,
# and its worst case's time (sim.c)
H := ../../common/tests/ec/build/hatari
B := build
TOP := ../..
FILES := SINE FM CHAIN ENV DAC CHORD LFO CH3 DETUNE PSG

run: $(B)/sim $(B)/files/.made
	./$(B)/sim $(addprefix $(B)/files/,$(addsuffix .VGM,$(FILES)))
$(B)/opn_prog.h: $(TOP)/dsp/opn.a56 | $(B)
	cd $(B) && a56 ../$(TOP)/dsp/opn.a56 > a56.lst && ! grep -q 'warning:\|error:' a56.lst && \
		python3 ../$(TOP)/common/dsp/mkh.py a56.out opn_prog.h dsp_opn_prog opn.a56
$(B)/sim: sim.c ../dspsim.h $(B)/opn_prog.h $(H)/dsp_core.c $(TOP)/src/opnd.c $(TOP)/src/opnd.h \
		$(TOP)/src/opnfast.c $(TOP)/src/opnfast.h $(TOP)/src/vgm.c $(TOP)/src/sncmd.c $(TOP)/src/sncmd.h
	cc -O2 -w -I$(B) -I$(H) -I$(H)/stub -I$(TOP)/src -I$(TOP)/common/src -o $@ sim.c $(H)/dsp_core.c $(H)/dsp_cpu.c \
		$(TOP)/src/opnd.c $(TOP)/src/opnfast.c $(TOP)/src/vgm.c $(TOP)/src/sncmd.c $(TOP)/common/src/charset.c \
		$(TOP)/common/src/bidi.c -lm
$(B)/files/.made: ../ym2612/mkvgm.py | $(B)
	python3 ../ym2612/mkvgm.py $(B)/files && touch $@
$(H)/dsp_core.c:
	sh $(TOP)/common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: run clean
