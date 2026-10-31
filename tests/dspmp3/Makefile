# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# dsp/mp3.a56, the whole DSP decoder, on Hatari's DSP core against the
# model, bit for bit, and its cycles: make (after the top-level make test
# has made the files)
H := ../../common/tests/ec/build/hatari
B := build
TOP := ../..
FILES ?= $(wildcard $(TOP)/build/mp3files/*.mp3)

run: $(B)/sim
	./$(B)/sim $(FILES)
$(B)/mp3tab.h: $(TOP)/tools/mkmp3tab.py | $(B)
	python3 $(TOP)/tools/mkmp3tab.py $(TOP)/data $@
$(B)/mp3_prog.h: $(TOP)/dsp/mp3.a56 $(B)/mp3tab.h
	cd $(B) && a56 ../$(TOP)/dsp/mp3.a56 > a56.lst && ! grep -q 'warning:\|error:' a56.lst && \
		python3 ../$(TOP)/common/dsp/mkh.py a56.out mp3_prog.h dsp_mp3_prog mp3.a56
$(B)/sim: sim.c ../dspsim.h $(B)/mp3_prog.h $(B)/mp3tab.h $(TOP)/src/mp3/mp3dsp.c $(TOP)/src/mp3/mp3frame.c $(H)/dsp_core.c
	cc -O2 -w -DMP3DSP_DEBUG -I$(B) -I$(TOP)/src/mp3 -I$(H) -I$(H)/stub -o $@ sim.c \
		$(TOP)/src/mp3/mp3dsp.c $(TOP)/src/mp3/mp3frame.c $(H)/dsp_core.c $(H)/dsp_cpu.c
$(H)/dsp_core.c:
	sh $(TOP)/common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: run clean
