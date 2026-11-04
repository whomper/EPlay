# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The rig's launcher (launch.c): make, then python3 play_test.py WORK.
# PCMTEST.PRG (pcmtest.c): src/pcm.c's 68030 loops against its C, on
# Hatari's 68030 (python3 pcm_test.py WORK)
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
A       := ../../common/atari
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build

all: $(B)/LAUNCH.PRG $(B)/PCMTEST.PRG
$(B)/%.PRG: $(B)/%.elf ../../common/tools/elf2tos.py
	python3 ../../common/tools/elf2tos.py $< $@
$(B)/LAUNCH.elf: $(B)/crt0.o $(B)/launch.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
# (as EPlay builds it: the 68030, -O2)
P030    := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Wall -Wextra -Werror \
           -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I../../src
$(B)/PCMTEST.elf: $(B)/crt0.o $(B)/pcmtest.o $(B)/pcm.o $(B)/pcmtest_c.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/pcmtest.o: pcmtest.c ../../src/pcm.h | $(B)
	$(CC) $(P030) -c -o $@ $<
$(B)/pcmtest_c.o: pcmtest_c.c ../../src/pcm.c ../../src/pcm.h | $(B)
	$(CC) $(P030) -c -o $@ $<
$(B)/pcm.o: ../../src/pcm.c ../../src/pcm.h | $(B)
	$(CC) $(P030) -c -o $@ $<
$(B)/%.o: %.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -DSTACK_SIZE=4096 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: all clean
