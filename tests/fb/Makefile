# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EPLAYFB.PRG's MP3 decoder (ui/cmp3fb.c: minimp3 for the FireBee's ColdFire
# and its FPU), built as EPlay builds it, run under qemu-m68k -cpu cfv4e
# (common/tests/coldfire's runtime: a Linux program, no TOS) on make test's
# MP3 files, its samples against the host's minimp3 (compare.py)
TOP    := ../..
CROSS  ?= m68k-linux-gnu-
CC     := $(CROSS)gcc
GCCINC := $(shell $(CC) -print-file-name=include 2>/dev/null)
RT     := $(TOP)/common/tests/coldfire
B      := build
QEMU   ?= qemu-m68k -cpu cfv4e
CF     := -mcpu=5475 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -nostdinc -isystem $(GCCINC) -I$(RT) -I$(RT)/inc -I$(TOP)/common/src -I$(TOP)/src -I$(TOP)/ui \
          -I$(TOP)/third_party/minimp3 -Wall
FILES  := tones mix128 mix320 mono lsf22 mix48 mixvbr
# (the VGM chips on the ColdFire: Erez, 21 Nov 2026) one file each for the
# AY, the SN76489, the YM2612, the Mega Drive's pair, the YM3812, the YMF262
VGMS   := ay sn opn md opl opl3
VGMSRC := $(addprefix $(TOP)/src/,vgm.c vgmcpu.c sncmd.c sncpu.c opnd.c egmath.c opncpu.c opld.c oplcpu.c aycpu.c)

test: $(B)/fbmp3 $(B)/fbmp3_host $(B)/fbvgm $(B)/fbvgm_host
	for f in $(FILES); do $(QEMU) $(B)/fbmp3 $(TOP)/build/mp3files/$$f.mp3 > $(B)/$$f.cf.raw && \
		./$(B)/fbmp3_host $(TOP)/build/mp3files/$$f.mp3 > $(B)/$$f.host.raw && \
		python3 compare.py $(B)/$$f.cf.raw $(B)/$$f.host.raw $$f || exit 1; done
	python3 -I mkvgms.py $(B) $(VGMS)
	for f in $(VGMS); do $(QEMU) $(B)/fbvgm $(B)/$$f.vgm > $(B)/$$f.vcf.raw && \
		./$(B)/fbvgm_host $(B)/$$f.vgm > $(B)/$$f.vhost.raw && \
		python3 compare.py --exact $(B)/$$f.vcf.raw $(B)/$$f.vhost.raw $$f.vgm || exit 1; done
# (the models as EPlay's Makefile builds them for the FireBee; the tables
# build/opntab.h's and opltab.h's, as dspstub.c takes them there)
$(B)/fbvgm: fbvgm.c $(VGMSRC) $(TOP)/common/atari/ll64.c $(RT)/rt.S $(RT)/libc.c Makefile | $(B)
	$(CC) $(CF) -I$(TOP)/build -ffunction-sections -fdata-sections -o $(B)/fbvgm.elf -nostdlib -static -no-pie \
		-Wl,-z,noexecstack -Wl,--gc-sections -Wno-implicit-function-declaration \
		fbvgm.c $(VGMSRC) $(TOP)/common/atari/ll64.c $(RT)/libc.c $(RT)/rt.S
	cp $(B)/fbvgm.elf $@
$(B)/fbvgm_host: fbvgm.c $(VGMSRC) | $(B)
	cc -O2 -Wall -DHOST -I$(TOP)/src -I$(TOP)/build -I$(TOP)/common/src -ffunction-sections -Wl,--gc-sections -o $@ fbvgm.c $(VGMSRC)
# (cmp3fb.c as EPlay's Makefile builds it: -ffast-math)
$(B)/fbmp3: fbmp3.c $(TOP)/ui/cmp3fb.c $(TOP)/ui/cmp3.h $(RT)/rt.S $(RT)/libc.c | $(B)
	$(CC) $(CF) -c -o $(B)/fbmp3.o fbmp3.c
	$(CC) $(CF) -ffast-math -c -o $(B)/cmp3fb.o $(TOP)/ui/cmp3fb.c
	$(CC) $(CF) -c -o $(B)/libc.o $(RT)/libc.c
	$(CC) -mcpu=5475 -c -o $(B)/rt.o $(RT)/rt.S
	$(CC) -mcpu=5475 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $@ $(B)/fbmp3.o $(B)/cmp3fb.o \
		$(B)/libc.o $(B)/rt.o
$(B)/fbmp3_host: fbmp3.c $(TOP)/ui/cmp3fb.c | $(B)
	cc -O2 -Wall -DHOST -I$(TOP)/src -I$(TOP)/ui -I$(TOP)/third_party/minimp3 -o $@ fbmp3.c $(TOP)/ui/cmp3fb.c -lm
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: test clean
