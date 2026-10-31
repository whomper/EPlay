# EPlay: the design

EPlay plays MP3, WAV and AIFF files. It is a GEM program (no desk accessory) in the AERA family,
and follows the family's guidelines (AERA-Atari, `standards/ATARI-APP-GUIDELINES.md`).

Erez's decisions, 31 Oct 2026:
- the name **EPlay**: `EPLAY.PRG`, `EPLAY.INF`, `EPLAY.HYP`, Desk menu entry `"  EPlay"`;
- **a program only**, no accessory (the hub's advice: under plain TOS an accessory runs only while
  the program in front calls the AES, so any program holding the machine longer than the buffer
  would break the sound);
- **MP3 on the Falcon only** (its DSP decodes it); **WAV and AIFF on the Falcon, TT and STE**;
- **the DSP rule** (below) for the family;
- Erez plays through a **SoundPool FDI** (S/PDIF, with its own 44.1 and 48 kHz clocks on the DSP
  port), set up at boot by its FDI program.

## Why other players break up, and what EPlay does instead

Players written for a stock Falcon break up on an accelerated one (Erez's DFB1: a 68030 at 50 MHz,
128 MB TT RAM) for reasons that have nothing to do with speed. EPlay avoids each one by design:

| Cause | EPlay |
|---|---|
| Words written to the DSP's host port without waiting for it, which a 16 MHz 68030 could never outrun and a 50 MHz one can | every word waits for the host port's flag (`HISR` TXDE / RXDF), as common's `dspec.c` does |
| Sound buffers in TT RAM, which the sound DMA can't read | the buffers the hardware plays are taken from ST RAM (`Mxalloc` mode 0) and checked; everything else is in TT RAM |
| The sound fed from the main loop, which stalls when GEM, a disk driver or another program holds the machine | the sound is fed by interrupts (the DMA's end-of-frame interrupt, or the DSP's own clocked output), from a buffer of seconds held in memory; GEM only refills that buffer |
| Small reads from the disk | the file is read in big pieces (32 KB, guidelines 6.5) well ahead of the music |
| The DSP left no headroom | MP3 decoding is measured on a stock Falcon's DSP before it is promised, with headroom kept for resampling |

## The sound path

### Falcon: WAV and AIFF (no DSP)

```
disk --32 KB reads--> file buffer (TT RAM) --convert, in GEM steps--> ring (ST RAM) --DMA--> matrix --> DAC / FDI
```

- The ring is about 2 s of 16-bit stereo (at 49170 Hz: 384 KB), taken once at start from ST RAM.
- The DMA plays the ring in a loop (`Setbuffer`, `Buffoper` with repeat). Its end-of-frame
  interrupt (Timer A, `Setinterrupt`) only counts the halves played; the program refills the half
  just played in its next steps (bgwork slices). The audio never waits for GEM: it waits only if
  GEM stalls longer than the ring (2 s).
- Conversion: 8-bit to 16-bit, mono to stereo, AIFF is big-endian as the 68030 is, WAV is
  little-endian (bytes swapped), 24-bit is cut to 16 with dither. Done in the 68030's steps.
- The WAV and AIFF path never needs the DSP, so it never takes it from another program.

### Falcon: MP3 (on the DSP)

```
disk --32 KB reads--> MP3 data (TT RAM) --host interrupt, as the DSP asks--> DSP: decode --> SSI --> matrix --> DAC / FDI
```

- The DSP decodes the whole frame: the bitstream and its Huffman codes, dequantising, stereo, the
  transform and the synthesis filter, as common's `jpeg.a56` decodes a JPEG's Huffman codes on the
  DSP. The 68030 does no decoding, so a stock Falcon plays as a DFB1 does.
- The decoded samples leave the DSP through its SSI port, clocked by the codec's clock, and the
  matrix takes them to the DAC, or to the FDI (`Devconnect`): no sound buffer in RAM, no DMA, the
  bus left alone. The DSP holds its own output queue (a frame or two) in its memory.
- The 68030 feeds the compressed bytes from memory by the DSP's host interrupt: at 320 kbit/s
  that is 40 KB/s, about 14,000 host-port words a second.
- Pause and stop unlock the DSP (the DSP rule).

### The clocks: 44.1 kHz through the FDI, else resampled

The Falcon's own clock gives 49170, 32780 and 24585 Hz (and lower). Music is 44.1 or 48 kHz.
- **With an external clock on the DSP port (the FDI):** the matrix runs from it (`Devconnect`
  with the external clock, `Soundcmd` prescale): 44.1 or 48 kHz exactly, bit for bit to the S/PDIF
  output. EPlay finds the clock by starting the DMA on it with silence and timing it against the
  200 Hz timer: no clock, no movement. It leaves the FDI's own settings (made by its program at
  boot) as they are, and puts the matrix back as it found it when it quits.
- **Without one:** EPlay resamples to 49170 Hz (MP3: on the DSP; WAV and AIFF: on the 68030 in
  its steps). Settings can name the output explicitly (`OUTPUT=auto|codec|external`).

### TT and STE: WAV and AIFF in 8 bits

Their DMA sound is 8-bit stereo at 50066, 25033, 12517 Hz (and 6258 on the STE).
- 44.1 kHz plays at 50066 Hz by a step through the source (nearest sample: no multiply), the
  high byte of each 16-bit sample, in a 68000 loop in assembly. 22.05 kHz plays at 25033 Hz.
- A separate 68000 build, `EPLAYST.PRG`, with the family's 68000 rules (`-fdisable-tree-bswap`,
  `-fno-store-merging`, `oddaccess.py`); `EPLAY.PRG` (68030) runs on the Falcon and TT, and
  `cpucheck.c` stops it on a 68000 with an alert that names `EPLAYST.PRG`.
- What the machine can play comes from the `_SND` cookie, not the CPU (a TT has a 68030 too):
  bit 3 the DSP (MP3), bit 2 the Falcon's 16-bit DMA and matrix; bit 1 8-bit DMA alone (TT, STE).
  Without the DSP, MP3 files show greyed in the playlist: "MP3 needs a Falcon's DSP."
- To decide when this is built (the guidelines' Speed): what 44.1 kHz stereo costs an 8 MHz STE
  from its disk, and so which rates it plays from a file and which from memory.

## The DSP rule (the hub writes it into the guidelines)

- A program holds the DSP only while it uses it: EPlay locks it (`Dsp_Lock`) while an MP3 plays
  and unlocks it on pause and stop.
- Nobody waits for the DSP or asks another program to let go of it: `Dsp_Lock` refused means the
  68030's code (TLS, EWeb's pictures).
- EPlay says when it can't have the DSP ("The DSP is in use by another program.") and doesn't try
  again by itself. The other way round, a TLS or JPEG job holds the DSP for a second or more:
  Play pressed then gets that alert, and Play again later works.

## The program

- **One window**, with the info line as the status: the track, its time, its rate and where it
  plays ("44.1 kHz  •  FDI"). The transport in the window's top row: previous, play/pause, stop,
  next, and the position as a bar that can be clicked or dragged. Under it, the playlist, with
  common's `panebar.c` beside it.
- **The small player** (View > Small player, saved in the INF): the window shrinks to the
  transport row and the title. Under MiNT, XaAES or MagiC it plays beside everything else. Iconified
  (`iconify.c`) it shows the icon and the time played.
- **Files**: File > Open… (^O) adds files, a folder or a playlist (`.M3U`); VA_START from the
  desktop (double-click a `.MP3`, `.WAV`, `.AIF`), and files dragged onto the window
  (`AP_DRAGDROP`). File > Save playlist… (^S).
- **Menus**: Desk · File · Edit · View · Play · Options · Help. Play: Play/Pause (Space),
  Stop, Next, Previous, Repeat, Shuffle. The family's shortcuts as the guidelines have them.
- **Settings** (^,): output (auto, codec, external clock), the ring in seconds, the DAC's volume.
- **About**: line 2 is what plays ("Playing 3 of 12  •  44.1 kHz  •  FDI"); line 3 the memory.
- **BGEM.NG**: "Next: title" as a track starts, where the user has it.
- **Help**: `docs/GUIDE.md` into `EPLAY.HYP`.

## Testing

- **Host**: the file parsers (WAV, AIFF, ID3, MP3 frame headers) and the converters, by
  `make test`.
- **The MP3 decoder**, as common's DSP programs are tested:
  1. a C model of the DSP's arithmetic (24-bit words, 56-bit accumulators) on the host, checked
     against a reference decoder frame by frame within ISO 11172-4's limits for full accuracy;
  2. the `.a56` program run on Hatari's DSP core against the model, bit for bit, which also
     counts its instructions per frame.
- **Hatari rigs** (`common/tools/machines.py`): `Falcon`, `Falcon-4`, `TT`, and an STE, with the
  sound written to a WAV by Hatari and compared with the host's decode. The speed rig as every app
  has it.
- **Erez's Falcon** is the referee: the DFB1, the BlueSCSI's SD card, the FDI.

## Measured: the DSP's budget for MP3 (31 Oct 2026)

`tests/dspbench` (`make -C tests/dspbench`) runs the two heaviest parts of the decode on Hatari's
DSP56001 core, laid out in the Falcon's DSP memory as the decoder will be, and counts the cycles
(Hatari adds the cycles of the Falcon's external memory). 44.1 kHz stereo is 4 granule-channels
of 576 samples, 38.3 times a second; the DSP has 16 million instructions a second.

| Part, in its plain form (straight matrices) | Instructions a sample | Share of a 32 MHz DSP |
|---|---|---|
| synthesis filter, window taps in external Y | 72.7 | 40.1 % |
| synthesis filter, window taps in internal Y | 56.7 | 31.3 % |
| inverse transform, window, overlap | 30.4 | 16.7 % |

- The two together, as they would run: **48 %** of the DSP.
- The rest of the decode (bitstream and Huffman codes, dequantising, stereo, alias reduction, the
  output) is estimated, not yet measured, at 25 to 35 instructions a sample: **15 to 20 %**.
- So a plain decoder takes about **65 to 70 %** of a stock Falcon's DSP at 44.1 kHz stereo, at
  any bit rate (the bit rate changes only the Huffman part). Resampling to 49170 Hz, without an
  external clock, adds about 5 %.
- Headroom, if needed: a fast 32-point transform in the synthesis (about 10 instructions a sample
  in place of the matrix's 36), a fast 18-point transform, and no transform for subbands that are
  all zeros (most files above 16 kHz). Together they would bring the decode near 45 %.
- Memory: the decoder's data, about 16K words, fits the Falcon's 16K external X and 16K external
  Y with the program; its loops stay in internal P (512 words), since code in external P costs a
  cycle more an instruction.

## The order of the work

1. **Is MP3 possible on a stock Falcon's DSP?** Yes, with room (above): the two heaviest parts
   measured on Hatari's DSP core, the rest estimated. The C model of the whole decoder comes next,
   with the Huffman part counted when it is written in `.a56`.
2. **The sound core on the Falcon**: the ST RAM ring, the interrupts, the matrix and the external
   clock found and timed; WAV and AIFF playing. A test program first, for Erez's Falcon.
3. **The GEM program** around it: window, playlist, small player, files from the desktop.
4. **MP3 on the DSP**, whole, tested as above.
5. **TT and STE**, 8-bit WAV and AIFF.
6. 1.0.
