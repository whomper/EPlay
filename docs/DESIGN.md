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
- The DMA plays the ring in a loop (`Setbuffer`, `Buffoper` with repeat).
- **As built in 0.1:** GEM's steps only read the file, in 32 KB pieces, into a queue in TT RAM
  where there is some (a third of the largest free block, 8 MB at most: on Erez's Falcon minutes
  of music, on a stock Falcon about 7 s of CD sound). A tick from the MFP's Timer A
  (`atari/feed.c`, 250 a second for WAV and AIFF) converts from the queue into the ring, up to
  1024 frames a tick. So nothing the music needs waits for GEM: a menu held down, the file
  selector, a dialog or another program stop only the reading, and the queue lasts. The tick
  reads where the DMA is from its counter (`$FF8909`), two reads that agree, since a read as it
  moves on can be torn (Hatari counted a whole ring twice).
- Conversion: 8-bit to 16-bit, mono to stereo, AIFF is big-endian as the 68030 is, WAV is
  little-endian (bytes swapped), 24-bit is cut to 16 with dither. Done in the 68030's steps.
- The WAV and AIFF path needs the DSP only for a rate the clock hasn't (phase 2, below): then it
  goes through the DSP, resampled, if the DSP is free; else the DMA plays it at the nearest rate,
  as before.

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
- **As built in phase 2 (STREAM, command 12):** the DSP plays its own output. The ring is one
  frame in Y (`PCM` at `Y:$3000`, 2304 words, left and right in turn): two halves of a granule.
  The SSI's transmit is a fast interrupt (`movep y:(r7)+,x:TX`, `r7` modulo 2304, the SSI's
  alone). Before a granule's synthesis the decoder waits until the SSI is out of that half;
  a granule finished with the SSI already back in its half counts as late. The next frame's
  packet comes while it waits: from the moment the last granule's bits are read (PKT free), the
  DSP sets HF2, and the tick sends words whenever the host port has room, never waiting for it.
  After each frame the DSP sends a status word: granules late, and its waits (each about
  1.2 µs: its time to spare). EPLAY.LOG gives them, so a real Falcon says how the DSP keeps up.
  The SSI is put in step with the frame (TFS) before its interrupt is on, so left is left. No
  `rep` longer than a few µs is left in the decoder (they became `do` loops): the SSI's
  interrupt can't wait through one.
- **The feed, after the load, is the DSP's host interrupt** (level 6, vector 255 as IVR names it;
  `atari/hostirq.S`): the DSP writes a wake word ($800000) when it wants a packet, its status
  word after a frame, and while a packet goes the host port's room asks again (TREQ). The
  68030 waits a few µs for room while the DSP listens, else it leaves it to that interrupt;
  it makes the next packet only once the one going is in. No tick a thousand times a second:
  the tick took 20% of a stock Falcon's 68030 (7% making frames, 4% sending, 9% being a
  tick at all), the interrupt 15%. A handler leaves no word of the DSP's unread, else its
  request stays and comes again at once (Hatari hung so at a file's end, before this).
- **Hatari's DSP keeps the 68030's pace:** with the 68030 at 32 MHz (`Falcon`) the DSP had
  12,500 waits a frame to spare, at 16 MHz (`Falcon-4`) 3,720, on the same file. A real DSP
  runs at 32 MHz whatever the 68030 does, so `Falcon-4` is the measure of the DSP, and the
  32 MHz machine's DSP looks twice as fast as it is (0.1's "Hatari decodes faster").
- **As built in 0.1 (the team's choice, for one sound path):** the samples come back to the 68030
  through the host port (`PLAY`, command 11: each sample's top 16 bits) and go into the same ring
  as WAV's. The queue holds the MP3's bytes as they are; the tick (1000 a second here) makes a
  frame's packet (`mp3frame.c`) while the DSP decodes the one before, sends it as soon as the
  DSP's frame is read, and the DSP's program goes in a piece at a time from GEM's steps (21,500
  words). The cost: 98,000 host-port reads a second at 49.2 kHz, the 68030's largest share on a
  stock Falcon. The SSI path above takes it away (phase 2's proposal).

### The clocks: 44.1 kHz through the FDI, else resampled

The Falcon's own clock gives 49170, 32780 and 24585 Hz (and lower). Music is 44.1 or 48 kHz.
- **With an external clock on the DSP port (the FDI):** the matrix runs from it (`Devconnect`
  with the external clock, `Soundcmd` prescale): 44.1 or 48 kHz exactly, bit for bit to the S/PDIF
  output. EPlay finds the clock by starting the DMA on it with silence and timing it against the
  200 Hz timer: no clock, no movement. It leaves the FDI's own settings (made by its program at
  boot) as they are, and puts the matrix back as it found it when it quits.
- **Without one:** EPlay resamples to the Falcon's clock at or above the rate (49170 Hz for
  44.1 kHz). An MP3 on the DSP (phase 2, "Resampling on the DSP"); a WAV or AIFF on the DSP
  too where the host port carries it (mono to 25 kHz), else, for now, at the nearest rate
  ("WAV and AIFF through the DSP", below). Settings can name the output explicitly
  (`OUTPUT=auto|codec|external`).

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
- **Erez, 3 Nov 2026:** WAV and AIFF of 16 and 24 bits at 44.1 and 48 kHz should play well on the
  TT and the STE. Phase 6 as planned, after phases 3 to 5. The TT (68030, 32 MHz) converts to
  50,066 Hz with interpolation and a dither to 8 bits; the STE more simply (the nearest sample at
  50,066, or 25,033 Hz). **The plain ST is left out:** it has no DMA sound, and samples on its YM
  chip would be about 4 bits at 10 to 16 kHz, taking nearly all of the 68000.
- **Erez, 7 Nov 2026 (phase 6's decisions):** MP3 files on a TT or STE are shown greyed in the
  list with "MP3 needs a Falcon's DSP"; ST low (320 x 200), too narrow for the 56-column window,
  gets a message and EPlay stops; the plain ST stays out.

**Phase 6a, the TT, as built (atari/snd.c):** `snd_init` takes the 8-bit DMA where `_SND` has bit 1
and not the matrix (bits 2 and 4): no Locksnd, no FDI, its registers at \$FF8900 set in supervisor
mode (\$FF8901 play and repeat, \$FF8903-07 the frame's start, \$FF890F-13 its end, \$FF8921 the
mode), kept and put back. The ring is half a second of 2-byte frames (left, right; 50 KB at 50,066
Hz) in ST RAM (Mxalloc, or Malloc where GEMDOS has no Mxalloc). play.c writes 16-bit frames at the
file's rate as on the Falcon, into a staging buffer that `snd_ptr` hands out; `snd_wrote` takes
them to the DMA's rate, the lowest of 50,066, 25,033 and 12,517 Hz at or above the file's (a TT's
DMA has no 6,258): on a 68020 or better interpolated (a 16.16 step, the frame before carried over)
and cut to 8 bits with the error carried to the next sample (first-order noise shaping); on a
68000 the nearest sample. `snd_played` and `snd_written` give the file's frames, so play.c's time
and end are as they were. The volume is the samples' own (play.c's gain, as through the FDI). The
window says "DMA 8 bits 50.0 kHz (from 44.1 kHz)"; Settings has Auto alone (no FDI, no DSP).
MP3s are marked in the list as they are added (by their names, before their tags are read), drawn
dim, and passed over by Next, Previous and a file's end; files given or added with none playable
say "MP3 needs a Falcon's DSP." ST low is common's `screen_check` (the family's message, then
quit). `play_test` on Hatari's TT: the 20 cases, the MP3 ones refused in those words, the list and
the playlist playing A and C with B.MP3 passed over, the mini player with a WAV.

**Phase 6b, the STE, as built:** `EPLAYST.PRG`, the same sources for the 68000 (`make st`, in
build/st; `-fdisable-tree-bswap -fno-store-merging`, then `oddaccess.py`), the 68030's EPLAY.PRG
saying on a 68000 "This EPlay is for the Falcon and the TT. On an STE: EPLAYST.PRG." libgcc here is
68020 code, and common's tos.c gives the 68000 its 32-bit multiply and divide but nothing for 64
bits, so the 8-bit DMA's sums are 32-bit (the step 16.16 by its whole and its rest; the DMA's frames
as the file's by a ratio of 12 fraction bits on a ring's count at most; `snd_written` the file's
frames counted): EPLAYST.PRG met an illegal instruction in `__udivdi3` the first time. Only the
MP3's and the Falcon's paths, which never run on an STE, still divide in 64 bits. The 68000 takes
the file's bytes straight to the ring (`snd_put8_raw`): for 8 bits out a sample's top byte is all
there is, so no 16-bit pass and no staging; the nearest frame, a 16-bit fraction a frame out whose
carry steps the file on, in an assembly loop of two byte moves (about 78 cycles a frame: the C's
150 had the tick at 78 % of the 68000 for a 24.5 kHz WAV, and 44.1 kHz fell behind, 10 s taking 23).
Signed samples at or below the DMA's rate go that way; 8-bit WAV and files above 50 kHz take the C
loop. The volume is the LMC1992's master volume through the Microwire (\$FF8922, \$FF8924), EPlay's
1.5 dB steps as its 2 dB, 0 dB again at the end; the TT keeps play.c's gain on the samples. On
Hatari's STE (EmuTOS, mono and medium) the 20 cases pass, a CD's 44.1 kHz at 50,066 Hz with no gap
and no underrun, the tick some 72 % of the 68000 (24.5 kHz: 55 %). A row EPlay can't play is drawn
light (VDI effect 2) where there's no grey pen, as in mono and four colours, where the dim pen had
been the ground. The machine is common's `STE` (`machines.py`: TOS 1.62 where Erez has it, else
EmuTOS), sent to the hub as patch 5.

**Not released yet (7 Nov 2026):** the window is far over the ST's times on Hatari's STE (mono):
the About box 480 ms paused against 150, a button drawn pressed 105 against 50; and playing a CD's
44.1 kHz at 50,066 Hz leaves GEM so little that About took 1,025 ms (515 at 25,033 Hz). EPLAYST.PRG
is built (`make st`) and its sound passes, but it stays out of the release's files until the
window's drawing meets the ST's times. Erez chose 50,066 Hz for a CD's 44.1 kHz on an STE (7 Nov
2026), and the window to be made faster for it.

**The STE's loop, then (0.34):** the sample loop has no test but the fraction's carry, and 16-bit
stereo takes both channels' top bytes with one `movep.w`, two frames a pass: from 101 cycles a frame
to 56, then about 49 (Hatari's CPU profiler, the tick at a CD's rate from 72 % of the 68000 to some
55 %). Of the rest, TOS's ROM takes 41 % (GEMDOS copying the file in, 176 KB/s), the loop 36 %, and
the tick's other work 18 %. speed.py now clicks by the window's place (EPlay prints it: "EPLAY
window x y w h"), so its rows work on an STE's screens. On Hatari's STE (mono) the rows paused are
within the ST's times (About 120 against 150, a button pressed 25 against 50); playing a CD's rate
they are about twice them (About 330, Settings 355): the window's next work.

**In the release from 0.35 (Erez, 8 Nov 2026: "Yes, include it in"):** EPLAYST.PRG goes into the
release's files before its window meets the ST's times, the over-time stated in the notes (on
Hatari's STE, mono, a CD's rate playing: About 305 ms and Settings 330 against 150; paused, within
them). The window's work goes on.

**Placement, not work (0.34, Falcon-4):** the start with 8 files given went 830 to 845-850 ms
(limit 2,000), every run, with the common update (835 before it). Nothing in it runs more: each
drawn part is the same to 0.1 ms. art.c's fixed loop (`spin`, a 200 Hz count) gives the clue: 1,460
turns in 0.33, 1,370 before the update, 1,183 after, the same code where only its place moved, as
the 68030's 256-byte caches meet TOS's trap code. Another: crt0.S leaves the stack's top 2 bytes
off a longword (`_BasePage` 4 bytes, `_is_acc` 2, then the stack), so every long the stack takes
is two bus cycles; aligned to 4 (tried here, not committed: common's) it gave 1,197 turns and
840 ms, sent to the hub.

## The DSP rule (the hub writes it into the guidelines)

- A program holds the DSP only while it uses it: EPlay locks it (`Dsp_Lock`) while an MP3 plays,
  or a WAV or AIFF it resamples, and unlocks it on pause and stop.
- Nobody waits for the DSP or asks another program to let go of it: `Dsp_Lock` refused means the
  68030's code (TLS, EWeb's pictures).
- EPlay says when it can't have the DSP ("The DSP is in use by another program.") and doesn't try
  again by itself. The other way round, a TLS or JPEG job holds the DSP for a second or more:
  Play pressed then gets that alert, and Play again later works.

## The program

- **One window**, with the info line as the status: the track, its time, its rate and where it
  plays ("44.1 kHz  •  FDI"). The transport in the window's top row: previous, play/pause, stop,
  next, and the position as a bar that can be clicked or dragged. Under it, the playlist, with
  common's `panebar.c` beside it. Its rows follow the guidelines' 5.8 (Erez, 1 Nov 2026): mouse
  down draws the row selected at once (only the old and new rows, within 50 ms); a release over
  the same row starts the action, so a double click plays the track on its second release;
  let go elsewhere, nothing more; moved while held, a drag. Shift and Control change the
  selection on the press only. The speed rig times press-to-row apart from release-to-action.
  The window's own buttons (phase 1's Open, Play and Stop; later the transport) follow 5.8 too
  (the hub, guidelines 914368f): drawn pressed on the press, their action on the release over
  them, nothing if let go elsewhere, as GEM's dialog buttons. The music is fed while one is held
  (a loop of EPlay's own until common's `click.c` is on main).
- **The small player** (View > Small player, saved in the INF): the window shrinks to the
  transport row and the title. Under MiNT, XaAES or MagiC it plays beside everything else. Iconified
  (`iconify.c`) it shows the icon and the time played.
- **Files**: File > Open… (^O) adds files, a folder or a playlist (`.M3U`); VA_START from the
  desktop (double-click a `.MP3`, `.WAV`, `.AIF`), and files dragged onto the window
  (`AP_DRAGDROP`). File > Save playlist… (^S).
- **Menus**: Desk · File · Edit · View · Play · Options · Help. Play: Play/Pause (Space),
  Stop, Next, Previous, Repeat, Shuffle. The family's shortcuts as the guidelines have them.
- **Settings** (^,): Output (Auto, FDI, DSP, 68030; phase 2). Volume is the player's, in its window, not a setting (Erez, 2 Nov 2026).
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

(Measured with the first benchmark, since retired: `dsp/mp3.a56`'s own counts are below.) It ran the two heaviest parts of the decode on Hatari's
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

## The decoder's model (31 Oct 2026)

The DSP's half of the decode is written first in C, in the DSP's arithmetic (`src/mp3/dspmath.h`:
24-bit words, 56-bit accumulators, the 56001's convergent rounding and limiting), as
`src/mp3/mp3dsp.c`; `dsp/mp3.a56` will do the same bit for bit. The 68030's half,
`src/mp3/mp3frame.c`, is the code the Atari runs: it reads the headers and side information, keeps
the bit reservoir, and sends each frame as a packet (its layout in `mp3frame.h`). The tables, 7,875
words, are made by `tools/mkmp3tab.py` from the standard's (`data/`).

- **Accuracy:** `make test` decodes 20 files made with sox and lame (tones, sweeps, pink noise,
  clicks, square waves at −0.1 dBFS; MPEG-1 at 44.1, 48 and 32 kHz, MPEG-2 at 22.05, 24 and 16,
  MPEG-2.5 at 11.025, 12 and 8; 16 to 320 kbit/s and VBR; mono, stereo and joint stereo) and
  compares every sample with minimp3. All are within ISO/IEC 11172-4's limits for full accuracy,
  at 0.07 to 0.18 of the RMS limit and 0.05 to 0.28 of the largest error allowed.
- **Headroom:** the dequantised lines and subband samples are held halved (`MP3_XS` 1). Each step
  of headroom doubles the error (3 gave 0.7 of the limit); with 1, nothing is limited inside the
  decoder even for full-scale square waves.
- **Covered:** mid/side stereo, short blocks, start and stop windows, big values with linbits.
  MPEG-2 and 2.5 (one granule a frame, their own scalefactor partitions, ISO/IEC 13818-3) since
  31 Oct 2026. **Not yet covered:** intensity stereo and mixed blocks (written, after minimp3's
  and FFmpeg's reading of the standard, but lame never makes them: ISO's compliance streams, or
  files from older encoders, are wanted). Where the two disagree (region 1's start in MPEG-2's
  mixed blocks), EPlay follows FFmpeg.

## Measured: the Huffman part on the DSP (31 Oct 2026)

The Huffman part (first written alone as `dsp/huff.a56`, now in `dsp/mp3.a56`) decodes a granule's big values and count1 quadruples on the DSP: the bits held at
the top of accumulator A and refilled 16 at a time by a multiply-accumulate (as common's
`jpeg.a56`), a 6-bit lookup at each table's root and up to 4 bits at each node below, the pair
table giving x and y. Its test then ran every granule of the 20 test files
through the model and then through `huff.a56` on Hatari's DSP core: all 13,000-odd granules give
the same 576 values, and it counts the cycles.

| File | Bit rate | Share of a 32 MHz DSP |
|---|---|---|
| 8 to 24 kHz (MPEG-2, 2.5) | 16 to 68 kbit/s | 1.6 to 6.4 % |
| 44.1 kHz | 128 kbit/s | 11 to 12 % |
| 44.1 and 48 kHz | 176 to 192 kbit/s | 16 to 18 % |
| 44.1 kHz | 320 kbit/s | 26 % |

The Huffman part grows with the bit rate (about 26 cycles a bit, mostly the two values' tests,
signs and calls), so with the plain synthesis filter (31 %) and transform (17 %), and an estimated
10 % for dequantising, stereo and alias reduction, a 320 kbit/s file would take about 85 % of a
stock Falcon's DSP: too close. The headroom planned above is now needed:
- the synthesis filter's 32-point transform done fast (about 31 % to 15 %);
- the 18-point transform done fast, and skipped for all-zero subbands (17 % to under 10 %);
- the Huffman part's common case in one step: an 8-bit lookup that takes a short code with its
  sign bits and gives x and y signed (about 2 to 3 times faster for small values), with the
  present code for long codes and linbits; and the calls inlined.

Together they were to bring 320 kbit/s to roughly 45 to 50 %. What they gave is below.

## The speed-ups (31 Oct 2026)

Each is checked bit for bit against the model on all 20 test files (`make dsptest`), and counted
on Hatari's DSP core.

- **The Huffman part, 26 % to 16 % at 320 kbit/s** (12 % to 8.8 % at 128). A pair table's root
  now looks at 8 bits: where they hold the whole code and its sign bits, one lookup gives x and y
  already signed (a 961-word signed pair table), and the pair is done in one step, inline in the
  region's loop. Long codes, linbits and codes whose signs don't fit go the old way. The main data
  now comes 23 bits to a word (each word positive, as the bit reader's multiply-accumulate needs),
  so a refill adds 23 bits, not 16. The rest of the time is spread thin (refills 12 %, the slower
  path for linbits about a third at 320 kbit/s).
- **The synthesis filter, written for real: 29.7 % at 44.1 kHz stereo (32.3 % at 48).** The
  32-point DCT is one butterfly and two 16 x 16 matrices (512 multiply-accumulates a slot, not
  1,024; the model changed with it, and its accuracy didn't: 0.07 to 0.18 of the limit). V is kept
  as the standard's 64-word rows in a 1,024-word ring a channel, the halves of odd slots swapped,
  so every window tap is 64 words back; outputs j and 32 - j share their window values, which sit
  in internal Y and are read once for both. The multiply-accumulates are 61 % of its time. (The
  bench's plain figure, 31 %, left out the real window's access pattern and V's writes; written
  for real, the plain form would be some 45 %.)
- **The transform: the fast algorithm was tried and dropped.** Szu-Wei Lee's 18-point DCT-IV (as
  libmad has it: 43 multiplies) ends in a chain of differences that adds up every rounding before
  it. With 24-bit words, quartering its inputs for headroom put the decoder over ISO's limit (1.37
  of it); halving them kept it under (0.82) but the sweep overflowed. On the DSP a matrix costs
  one instruction a multiply-accumulate, loads included, so the fast form would gain well under
  2x anyway. The transform stays a matrix, and skips subbands that are all zeros (exact): that
  comes with its kernel.

**Where 320 kbit/s at 44.1 kHz stands:** Huffman 16 %, synthesis 30 %, the transform about 17 %
(the bench's; its kernel comes next), dequantising, stereo and alias reduction about 10 %
(estimated): some 73 % of a stock Falcon's DSP, with the FDI's clock. Resampling to 49,170 Hz
without it adds about 5 %. 128 kbit/s: about 65 %. Further room, if the rest measures worse: a
second butterfly in the synthesis DCT (about 2 %), and the transform's zero subbands.

## The whole decoder on the DSP (31 Oct 2026)

`dsp/mp3.a56` is the whole DSP side: a packet in, a frame's samples out, bit for bit as the model
(`make dsptest`, `tests/dspmp3`: every sample of every frame of the 20 files compared, and the
cycles counted on Hatari's DSP core). It holds the scalefactors (MPEG-1 and 2), the Huffman part,
dequantising, mid/side and intensity stereo, reordering, alias reduction, the transform (long,
short, start, stop and mixed blocks; subbands that are all zeros skipped) and the synthesis
filter. The memory map is made by `tools/mkmp3tab.py` (`mp3map.inc` for the DSP, and the load list
the 68030 will use: 13,800 words of tables, 2,500 of program outside internal P). The hot loops are
in internal P; the rest runs from P:$7000.

Two changes to the model came with it, neither changing its accuracy: a long block's first 18
outputs are rounded once with the overlap they are added to (the DSP's `macr`), and the sign of a
dequantised line and the frequency inversion are put on before the one limit.

**Measured, a 32 MHz DSP (a stock Falcon's):**

| Files | Share of the DSP |
|---|---|
| MPEG-2 and 2.5, 8 to 24 kHz | 12 to 41 % |
| 44.1 kHz mono, 96 kbit/s | 41 % |
| 44.1 kHz stereo, 128 kbit/s | 64 to 76 % |
| 44.1 kHz stereo, 160 to 192 kbit/s, VBR | 70 to 83 % |
| 48 kHz stereo, 192 kbit/s | 87 % |
| 44.1 kHz stereo, 320 kbit/s | 92 % |
| full-scale square waves, every granule short blocks | 114 % |

Where the time goes at 320 kbit/s: the synthesis filter 29 %, the transform 17.5 %, dequantising
15.5 %, the Huffman part 14 %, alias reduction 4 %. About half (the filter, the transform, the
alias reduction) doesn't depend on the bit rate: 128 kbit/s is already 76 %.

### The DSP's last gains (option 1, 31 Oct 2026)

Erez chose to take what the DSP alone can still give first, and measure again. Each step kept
every file bit for bit:
- internal P (512 words) holds only loops now: the synthesis filter's set-up runs from external
  P and calls its four loops; the transform's 32 subbands are one internal loop with the line,
  overlap and output pointers running on (no per-subband address work); alias reduction,
  mid/side, the window's two single outputs and the dequantiser's arithmetic moved in;
- the synthesis DCT has a second butterfly: 384 multiply-accumulates a slot, not 512 (the model
  changed with it; accuracy unchanged, 0.18 of the limit at most);
- the dequantiser's cache is at internal Y 0, so |v| is its address, and its loop lost three
  instructions.

| Files | Before | Now |
|---|---|---|
| 44.1 kHz stereo, 128 kbit/s | 64 to 76 % | 60 to 70 % |
| 44.1 kHz stereo, 160 to 192 kbit/s, VBR | 70 to 83 % | 66 to 77 % |
| 48 kHz stereo, 192 kbit/s | 87 % | 81 % |
| 44.1 kHz stereo, 320 kbit/s | 92 % | 86 % |
| MPEG-2 and 2.5 | 12 to 41 % | 11 to 38 % |
| full-scale square waves, all short blocks | 114 % | 111 % |

At 320 kbit/s: the synthesis filter 29 %, the transform 19 %, dequantising 17 %, the Huffman part
17 % (43 % of it from external P: count1 and the linbits' slow path), alias reduction 3 %. The
filter and the transform are at the floor of their form (their time is multiply-accumulates).
What is left, a few percent: count1 and the short blocks' transform in internal P (it is full),
and the frequency inversion moved into the filter's butterfly to make room.

**So: a stock Falcon plays 128 to 192 kbit/s with 20 to 35 % to spare, 320 kbit/s at 86 %, and
music with many short blocks at high bit rates only just or not.**

## Phase 1: the sound core in a minimal window (31 Oct 2026; releases 0.1 to 0.3)

The phases are named by what they bring (the hub, 31 Oct 2026): 1 first sound; 2 every Falcon,
with output and volume settings; 3 the player window; 4 playlists and folders; 5 the small player,
keys and MiNT/MagiC; 6 the TT and STE; then 1.0. Release numbers count releases, not phases.

`EPLAY.PRG` plays a WAV, an AIFF or an MP3 given on its command line (the desktop's way) or from
File > Open…, in one small window (the file, its format, the output, the time; Open, Play, Stop).
Checked in Hatari (`tests/hatari/play_test.py`, `Falcon` and `Falcon-4`, EmuTOS): Hatari's sound
recorded and analysed: the tone the file holds at the pitch the output rate gives, both
channels, no gap, the length, silence after the end, and no underrun counted by the tick. Ten
cases: a WAV, an AIFF, a short and a long MP3 (320 kbit/s, ten seconds: the ring and the queue
round several times), and the file selector held open 5 s while an MP3 plays. All pass.

What the rig found and 0.1 fixes:
- a frame that ended across the ring's end left the DSP holding its last 104 samples ("The DSP
  stopped answering"), the second part read with the first part's count;
- the DMA's position, read a byte at a time, torn as it moved on (a ring counted twice: an AIFF
  stopped at 1.7 s of 3);
- Hatari's external clock runs at 50 kHz: only rates near 44.1 and 48 kHz are taken as a clock;
- `Super(0)` around the host port crashed on Stop on a stock Falcon (Line F at $480): the host port
  is now reached through `Supexec`;
- the clock measurement played the ring's old contents: it plays 64 KB cleared.

Hatari 2.4.1 (Ubuntu's) swaps left and right after a DMA stop, keeping the stereo parity of the
words played before (fixed in later Hatari: "1st frame is left"). The rig allows it there and says
so; the Falcon starts each frame on the left.

The speed rig (`tests/hatari/speed.py`, EmuTOS; the longest stretch, ms):

| Action | `Falcon-4` MP3 | `Falcon-4` WAV | `Falcon` MP3 | `Falcon` WAV | Time |
|---|---|---|---|---|---|
| started, playing | 420 | 390 | 330 | 325 | 2000 |
| 5 s playing | 30 | <20 | <20 | <20 | 50 |
| About box opened, playing | **265** | **175** | 125 | 110 | 150 |
| About box closed | 75 | 55 | 25 | 25 | 300 |
| Stop | 40 | 40 | 20 | 20 | 50 |
| About box opened, stopped | 130 | 130 | 90 | 90 | 150 |
| Play again (the DSP loaded) | 50 | 45 | 30 | 25 | 50 |

MP3 at 320 kbit/s and WAV at 44.1 kHz, both played at 49170 Hz (no external clock): the most a
stock Falcon is asked. Over: the About box on a stock Falcon while it plays, because the tick takes
the 68030's time (the host-port reads for MP3, the conversion and ST RAM for WAV). The plan: phase 2
takes the samples off the 68030 (MP3 out of the DSP's SSI port, WAV through the DSP, which also
resamples).

### 0.1 on Erez's Falcon (31 Oct 2026), and what 0.2 changed

Erez's first try, an 8-minute MP3 with the FDI on:
1. **The length said 22:10.** It came from the first frame's bit rate, and a VBR file's first
   frame is LAME's header frame (Xing), at a low rate. Now the Xing, Info or VBRI frame gives the
   frames (`mp3_info_frames`, checked by `tests/mp3/test_info.c`), the length, and the average bit
   rate ("VBR 262 kbit/s"); the header frame isn't played. A WAV's length overflowed past 13
   minutes (frames × 1000): fixed.
2. **It played "almost double speed", and said "Falcon 49.1 kHz" with the FDI running.** The FDI's
   clock wasn't found. 0.1 measured the DMA at prescale 1 and took only 44.1 or 48 kHz from it,
   so it knew only a 22.5792 MHz clock. Now the clock's frequency is worked out from the measure,
   the prescale that makes 44.1 or 48 kHz of it is chosen (11.2896 MHz: 0; 22.5792: 1; 45.1584:
   3) and measured again to be sure. The FDI's driver is asked too (`Sndstatus($DD02)`, `$4711`,
   from `FDI_INIT.PRG` V 1.02). With the Falcon's own clock going to the FDI, whose S/PDIF runs on
   its own clock, the sound can't be right.
3. **"The DSP stopped answering" after 3 seconds.** Not seen in Hatari, nor in the DSP's test on
   the same kinds of file (VBR with a header frame, CRC, a big ID3v2 tag with a picture, mono,
   MPEG-2). The log now says where it stopped: sending or reading, which word of how many, the
   host port's status, the frame.

`EPLAY.LOG` (next to the program) says what EPlay measured and did: the machine, the FDI's driver,
the clock as measured and as taken, each file's header, the output, how playing ended. It is
written whole when a file ends or is stopped, when the DSP fails, and at the end.

Also: the DSP's program goes in from the tick (32 words a tick, 0.7 s), not from GEM's steps,
which the file selector held up on a stock Falcon (the rig's new timing showed it). It starts once
Play's redraw is done and GEM's next steps have reset the DSP (a pulse of 5 ms and a wait of 5),
so Play's stretch stays within its time (50 ms on `Falcon-4`; 75 when the load ran during the
draw). The test files
now include LAME's header frames (VBR and CBR) and CRC-protected frames, and the play rig a VBR
file (its length checked).

### 0.2 on Erez's Falcon (1 Nov 2026), and what 0.3 changes

Erez's try of an MP3 with the FDI on: still "49 kHz" where it should be 44.1, so faster than the
file, and after a second severe crackles that bring the playing down.

1. **The FDI's clock still wasn't found.** One cause is certain, in 0.2's measure: it read the
   DMA's position only at the start and the end of 200 ms, over a ring of 16384 frames. A clock
   past 41.9 MHz (more than 82 kHz at prescale 1; 45.1584 MHz makes 88.2) goes round the ring in
   that time and was taken for a slow one, so the 45.1584 MHz case 0.2 said it knew could never be
   seen. Now the position is read all the while and the steps added up: any clock is measured.
   Whether the FDI's clock is that one, `EPLAY.LOG` will say.
2. **The crackles.** With the Falcon's own clock going to the FDI, whose S/PDIF runs on its own,
   the sound can't be right; and at 49,170 Hz the DSP has a ninth more to decode than at 44.1 kHz,
   near its budget on a real DSP (Hatari's runs faster). The log now tells the two apart: when the
   first gap came, and the least lead the decoder kept over the DMA after the first second
   (gaps with a good lead: the clocks; a lead running down to none: the DSP).

The log also has the DSP port's GPIO as EPlay found it (the FDI's program leaves it at direction 7,
data 2) and each prescale tried with its measure.

### 0.3 on Erez's Falcon (1 Nov 2026), and what Cubase Audio does

Erez's log (an MP3 of 23 MB, VBR, 44.1 kHz, 11:45):

```
the FDI's driver answers ($4711)
the DSP port's clock: 48150 Hz at prescale 1 (a clock of 24652800 Hz); taken as 48000 Hz
the DSP port's GPIO: direction 7, data 2; tried 48000 Hz at prescale 1: 48150 Hz
output: 49170 Hz, the Falcon's clock
the DSP stopped answering reading word 5 of 2304 (host status $06), frame 202; 21803 ms played, 13870 underruns
the sound: the first at 2010 ms; the least lead 0 ms
```

1. **The clock is found now, and it is 48 kHz** (24.576 MHz, prescale 1), with the GPIO as the
   FDI's program leaves it (data 2). A 44.1 kHz file can't play on it bit for bit, so EPlay used
   the Falcon's own 49,170 Hz: faster than the file, and not the FDI's clock.
2. **The DSP fell behind and stopped.** About 86 frames (2 s, the ring) went before the DMA
   started; after that 116 frames in 20 s, about a sixth of what playing needs, and at last no
   answer for a second. Hatari plays a heavier file (pink noise, LAME -V0, 239 kbit/s) with a
   least lead of 575 ms, and its DSP counts the Falcon's zero-wait external memory and its
   penalties. Not the cause: a loop count of 0 (Hatari runs it 65,536 times, as the DSP does),
   the bus control register (the decoder clears it), the file's reading (seconds ahead). Not
   known yet: what on the real machine makes it six times slower.

**Cubase Audio 2.06** (Erez's copy; packed with LArc's PFX lz5, unpacked to read it) knows the FDI
and plays from the DSP:

- **The FDI:** the same sign, `Sndstatus($DD02)` = `$4711`, then `Gpio(0, 7)` (all three lines out).
- **The rate:** `Gpio(2, 4)`, 50 ms, then `Gpio(2, 3)` for 48 kHz or `Gpio(2, 2)` for 44.1 (its
  rate table: index 0 44,100, 1 48,000), 50 ms. With the S/PDIF input as the clock: `Gpio(2, 0)`;
  in its 16-track modes 7 and 5. Erez's FDI measured 48 kHz at data 2, which Cubase calls 44.1: so
  EPlay must measure each, not trust the table.
- **The FDI's own chip** is set by a control word sent as bits (`$1100` a one, `$1000` a zero) in
  a DMA buffer played to `EXTOUT` at prescale 8 with GPIO 4, then GPIO 5 or 7: two words, one for
  the FDI's input as the clock and one for its own.
- **The clock checked:** the rate counted over 200 ms, twice, the two to agree and be 44.1 or 48 kHz
  ("No (or instable) External Sample Clock!").
- **The sound from the DSP:** `Devconnect(DSPXMIT, DAC | EXTOUT, CLKEXT, prescale, NO_SHAKE)`, the
  other sources on the external clock too, `Dsptristate(1, 1)`. The DSP's SSI plays straight into
  the matrix on the FDI's clock: no buffer in ST RAM, nothing through the host port, no 68030 in
  the way. That is phase 2's plan for MP3; Cubase does it for 8 tracks at 44.1/48 kHz in 16 or 24
  bits, and 16 tracks compressed on the DSP.

### 0.4 on Erez's Falcon (1 Nov 2026; the DFB1X off: a stock Falcon)

"Better in the sense of speed, and it did not stop. However there are many crackles." The log:

```
the FDI to 44100 Hz: 2: 48050 Hz; 3: 47775 Hz
output: 49170 Hz, the Falcon's clock
the DSP stopped answering streaming word 4863 of 0 ..., frame 4864; 113911 ms played
the sound: 4863 frames decoded, 3697 granules late; the DSP's waits a frame: least 0, mean 1022;
  the tick 25% of the 68030 (frames made 15%, sent 7%)
```

1. **The FDI stayed at 48 kHz** on 2 and on 3, so the DSP played on the Falcon's 49,170 Hz into
   an S/PDIF at 48 kHz: crackles. Erez's set-up: a sound card's optical output into the FDI's
   S/PDIF input at 44.1 kHz, and Cubase plays 44.1 kHz right. Cubase's other setting, 0, takes
   the input's clock ("S/PDIF controls the clock"): 0.5 tries it too, measured as the others.
   Cubase's routine that programs a chip by a DMA bit pattern is for its own interface (the
   C.A.C.), not the FDI: with the FDI's driver answering, Cubase only sets the lines.
2. **The DSP fell behind:** 38 % of the granules late (played again: crackles), with about 1 ms
   to spare a frame on average, where Hatari has 4 for a heavier file (pink noise, 239 kbit/s).
   At 49,170 Hz a 320 kbit/s frame needs 96 % of the DSP by Hatari's count; Erez's music (VBR,
   266 kbit/s) at it, on a real DSP, is past it. Next: the decoder made faster, and the output
   at 44.1 or 48 kHz (Erez's choice for a FDI at 48 kHz: resampled to 48 kHz on the DSP).
3. **The stream stopped twice**, at frames 4,864 and 224 of the same file: timing, not the
   data, and never in Hatari. 0.5's log says what both sides were doing then (the host port's
   flags, the handler's, the packet going).

### 0.5 on Erez's Falcon (1 Nov 2026)

The window said "External clock 44.1 kHz": the FDI on its input's clock (0) is at the file's
rate. Then some 24 bombs as it began to play: a spurious interrupt (one asked for that no one
answered at its acknowledge). New in 0.5 was the FDI's 0; the FDI's own program reads one of
its lines on the MFP's GPIP bit 3, so the FDI may drive an MFP line there. 0.6 answers vector 24
itself while playing (counted, let go; given back at stop), logs how many, and logs the MFP's
interrupt set-up and inputs before and after the FDI is set.

0.6 also keeps left and right in step each frame (the DSP's salign: its interrupt held until a
slot begins, TFS says which, r7 a word on if needed): the start's alignment alone met Hatari
with them swapped now and then. The play rig no longer excuses a swap for MP3 (the SSI's, not
Hatari's DMA).

### 0.6 on Erez's Falcon (1 Nov 2026)

```
the FDI to 44100 Hz: 2: 47925 Hz; 3: 48000 Hz; 0: 44125 Hz
output: 44100 Hz, the external clock
the DSP stopped answering streaming ... frame 403; 10422 ms played
the sound: 400 frames decoded, 0 granules late; the DSP's waits a frame: least 0, mean 1085
the stream then: ISR $06 (HF2 0, RXDF 0, TXDE 1), ICR $00, wanted 3, busy 0; a packet going 1, word 1 of 297
spurious interrupts while playing: 111
```

- **The FDI on its input's clock is at 44.1 kHz** (0), and at 44.1 kHz the DSP kept up: no
  granule late. The first run hung after 15 s with a click looping (the DSP playing its ring on,
  the 68030 stuck); the second stopped after 10 s.
- **Spurious interrupts only with the FDI on 0** (none in 0.4's 113 s on 2 and 3), and the host
  port's requests found off with a packet half sent: the DSP's host interrupt (level 6, its
  vector from IVR) doesn't hold up there. Cubase Audio doesn't use it (no vector of its own at
  $3FC nor $60). 0.7 feeds the stream from the tick again, polling the host port, at 500 a
  second (the packet is asked for in the frame's middle, so 2 ms is lost in the DSP's waits):
  in Hatari the DSP has more to spare so (mean 4,945 waits a frame, against 3,720 by the
  interrupt), and the 68030 gives 16 %. The spurious interrupts are still counted and let go,
  and the log will say whether they come without the DSP's interrupt.

### 0.7 on Erez's Falcon (1 Nov 2026): "It played very well"

```
the FDI to 44100 Hz: 2: 48025 Hz; 3: 48025 Hz; 0: 44050 Hz
output: 44100 Hz, the external clock
stopped at 234527 ms; 0 underruns (8979 frames decoded, 15 granules late; the DSP's waits a
  frame: least 0, mean 1456; the tick 12% of the 68030 (frames made 3%, sent 4%))
```

Nearly four minutes at 44.1 kHz on the FDI's input clock, bit for bit, with no spurious
interrupt (the DSP's host interrupt left alone) and 15 granules late of some 18,000 (0.08 %).
The DSP had about 1.7 ms to spare a frame on average, and none in the heaviest: some 93 % of it
busy on this music (VBR, 265 kbit/s). So the faster decoder still matters, for heavier files
and for a Falcon without the FDI (49,170 Hz, and resampling).

### 0.7 again (1 Nov 2026), another track, and 0.8

"It stopped after 10 seconds with DSP not responding." The log: 730 frames decoded where the
68030 had sent 641 packets, 1,280 granules late, and then the DSP silent, HF2 off, the host
port empty: it had lost the packets' step (a word lost or misread on the way: Erez's DFB1X on
again, the 68030 at 50 MHz), taken data words for counts, decoded what came, and at last taken a
0 for the end. 0.8: a packet's count has bit 23 set, which no data word has (23 bits a word),
so the DSP passes stray words and takes up the stream at the next count, that frame dropped
("packets broken" in the log); the status word's counts run on (frames, broken packets, late
granules), so one read twice or missed takes nothing away. Tested in Hatari with a word
dropped on purpose: one packet broken, playing on to the end.

Erez also saw channel 3 of his SoundPool Analog 8 at full scale: the matrix's tracks as another
program left them (Cubase Audio leaves 4). The DSP sends two words a frame and decodes two
channels; 0.8 sets one stereo pair (Settracks, Setmontracks) when the DSP plays.

And the DSP's reset is the tick's now (port A bit 4 at level 7, 60 ms after Play's draw): in
GEM's steps the file selector, opened at once, held it and with it the load (the play rig's
fsel failed once in seven).

## Phase 2: three ways to play MP3, chosen in the settings (Erez, 1 Nov 2026)

Erez started phase 2 after 0.3's log, and set what it is: every way works, and the user picks the
one the machine has.

| Way | For | The decoding, and the sound |
|---|---|---|
| **68030** | a TT, a Falcon without its DSP free, any 68030 machine | the 68030 decodes; the sound DMA plays (the ring, as WAV) |
| **DSP** | every Falcon | the DSP decodes and plays from its SSI on the Falcon's clock; a rate the Falcon hasn't (44.1 kHz) resampled on the DSP |
| **FDI** | a Falcon with the FDI | Cubase's way: the FDI set to the file's rate (44.1 or 48 kHz, each measured), the DSP's SSI on its clock, bit for bit |

- **Settings:** Output: Auto (the default: the FDI where its driver answers, else the DSP on a
  Falcon, else the 68030), or one of the three; only what the machine has is offered. In
  EPLAY.INF. **As built (Erez, 2 Nov 2026):** Options > Settings... (^,), one page: one sentence
  on what Output is ("Output: how EPlay plays the music."), then the four choices, Auto, FDI, DSP
  and 68030, as radio buttons with no notes. A choice the machine hasn't is greyed (the 68030
  until its decoder is in). A change takes effect at the next Play; what plays goes on as it is.
  An FDI chosen and its driver not loaded this time plays Auto and stays chosen. **Volume is not
  a setting** (Erez): it is the player's, a control in its window (phase 3), so 0 dB until then.
  FDI puts the FDI at the file's rate as Auto does, else plays on its clock as it is,
  resampled; DSP leaves the FDI and its clock out (the Falcon's clock, resampled).
- **The 68030's quality follows the machine** (Erez's choice): the highest it keeps up with, full
  on a fast 68030 (a DFB1), half the rate or mono on a stock Falcon or a TT; the window says which
  ("68030, half rate"). The music doesn't break up. *Since 2 Nov 2026 (Erez): Settings' Quality,
  Normal (to about 15 kHz) or High, the default, sets it, for the DSP on the Falcon's clock as
  well (below).* An ST or STE has no MP3.
- **The order:** the FDI way first (Erez's Falcon, and it removes 0.3's two faults: the clock
  and the DSP's output through the host port), then the DSP way with resampling, then WAV and
  AIFF through the DSP, the settings, and the 68030's decoder last (a new part).
- **The 68030's decoder runs in slices from the timer's interrupt** (the hub asked: slices or
  interrupt-driven, and why). Not in GEM's steps: GEM stops in a menu held down, the file
  selector and a dialog, and nothing the music needs may wait on the AES. Not a frame in one
  handler either: a stock 68030 needs 20 to 50 ms for one, all other interrupts held. So the
  tick, after its own work, lowers the interrupt mask below its level and decodes a granule at
  a time while the ring is short of a second's lead, guarded against coming in again; the tick
  itself, the VBL and the keyboard go on meanwhile, and GEM gets what the music leaves. The
  quality it decodes at is chosen once at the start from a granule timed then. The TT's WAV and
  AIFF (phase 6) come with the 68030 way where they share the code.

### The faster decoder (1 Nov 2026)

0.7 played Erez's music at about 93 % of the DSP, with a few granules late; the FDI at 48 kHz
and resampling need room too. Three changes, each bit for bit between the model and the DSP and
with the error against minimp3 where it was (0.19 of ISO's limit at most):
- **The long block's transform as two 9-point DCTs and a rotation.** The 18 lines go into sums
  and differences in pairs, a 9-point DCT of each (its outputs m and 8 - m share the sums of its
  even and its odd inputs), and the two DCTs' outputs 8 - r rotate into rows r and 18 + r. Every
  factor is a cosine, at most 1, and nothing is chained: unlike Lee's form (above), there is
  nothing for the roundings to add up in, and no input needs scaling. On a master made loud
  (hot.mp3, a new test file: clipped, 12 dB over) nothing on the way gets bigger than the rows
  the matrix made (0.60 and 0.84 of full scale). About 260 instructions a subband, not 420.
- **The short block's the same way**, with 3-point DCTs, and the window folded into the
  rotation's factors: each of a window's 12 contributions is a cd[m] + b sd[m]. Its core runs in
  internal P, its contributions in internal X; from external P every access to external memory
  cost a cycle more.
- **The overlap holds the frequency inversion.** An odd subband's windows have their odd samples
  negated, so its overlap carries the sign and no block type needs a path of its own. That and
  the subbands' loop (internal memory only, so as fast from external P) made the room in
  internal P. The reorder's two copies run there too, a word an instruction.

The DSP test reports the system stack's deepest too: 10 of 15.

| Files (Hatari's DSP, 32 MHz) | 0.8, average | Now | 0.8, the worst frame | Now |
|---|---|---|---|---|
| 44.1 kHz stereo, 128 kbit/s | 70.5 % | 66.3 % | 81.7 % | 72.3 % |
| 44.1 kHz stereo, VBR (-V 2, -V 0) | 74.5 to 81.5 % | 70.0 to 76.3 % | 87.1 to 87.6 % | 77.8 to 78.3 % |
| 44.1 kHz stereo, 320 kbit/s | 86.5 % | 81.5 % | 95.5 % | 85.6 % |
| 48 kHz stereo, 192 kbit/s | 81.0 % | 76.2 % | 100.7 % | 90.3 % |
| clicks (short blocks), 160 kbit/s and VBR | 52.7 to 65.8 % | 49.7 to 60.5 % | 106 to 115 % | 91.5 to 100 % |
| full-scale square waves, all short blocks | 111.3 % | 96.6 % | 124 % | 109 % |

The worst frames gained most: they are the short blocks', which make granules late. Next, if
needed: the synthesis filter's 16-point DCT-IV the same way (about 3 %), and its set-up for each
slot (4 %).

### Resampling on the DSP (1 Nov 2026)

A file at a rate the clock doesn't have was played at the clock's: on a stock Falcon a 44.1 kHz MP3
came out at 49,170 Hz, 11.5 % fast. Now the DSP resamples, between the synthesis filter and the
SSI:
- **Which clock** (`snd_rate_dsp`): the FDI put at the file's rate where it can be (no
  resampling, bit for bit as before); else the external clock as it is (the FDI's 44.1 or 48
  kHz) when the ratio is within x0.9 to x1.12; else the Falcon's lowest clock at or above the
  rate. Every MP3 rate has one within x1.115 (44.1 to 49,170, 48 to 49,170 x1.024, 22.05 to
  24,585...). The window says it: "Falcon  49.2 kHz (from 44.1 kHz)".
- **The filter:** 8 taps of a windowed sinc, 32 phases, each phase summing to exactly 1; the band
  to 0.9 of the input's Nyquist going up, of the output's going down (48 to 44.1). Measured
  beforehand on tones of 1 to 18 kHz: spurious tones below 16 kHz at -40 dB or less for 10 to
  16 kHz tones, -50 dB at 5 kHz, -62 dB at 1 kHz, the top end 0.6 to 0.8 dB down at 10 to 14 kHz.
  More taps gained nothing; the phases' steps set it, and more phases want memory there isn't.
- **The position** steps through the input, si + sf / 2^24 input samples an output (the 68030
  works it out, as the model's `mp3rs_set` does: drift under a sample in three minutes). On the
  DSP it is one accumulator, the sample in A1 and the fraction in A0, the step added as a 48-bit
  word; the phase is the fraction's top 5 bits, from one multiply.
- **Both channels at once:** the synthesis runs a slot of each channel in turn (both transformed
  first, in place over their lines), into temps in internal X, and the resampler takes a slot of
  both: one position for the two. The output ring is then 2,576 words, two granules' outputs at
  x1.115, and a granule waits while the SSI is in the 1,290 words its outputs go to (the halves'
  rule, for outputs that don't come in halves); it is late if the SSI got into them before it
  was done. A mute frame resamples silence: as many outputs, its last inputs silence too.
- **Tested** as the decoder is: the model (`mp3rs`) against minimp3's samples through the same
  taps in double precision, within ISO's limits on every file (0.17 of the rms limit at most);
  the DSP against the model bit for bit, every file to the Falcon's clock and 48 kHz to 44.1
  (`make dsptest`, through the frame command: RSSET, command 13).
- **The cost:** 36 cycles an output pair, 9 to 11 % of the DSP (mix320.mp3 82.9 to 94.0 % at
  49,170 Hz; mix48.mp3 77.8 to 87.8 % at 44.1 kHz). 320 kbit/s on a stock Falcon without the FDI
  is near the top; the synthesis filter's 16-point DCT and its slots' set-up are what is left to
  win back. The FDI at the file's rate costs 1.4 % more than before (the slots' copy).

### WAV and AIFF through the DSP (1 Nov 2026)

A WAV at 44.1 kHz on a stock Falcon is played by the DMA at 49,170 Hz, 11.5 % fast. The DSP can
now take PCM and resample it as it does an MP3's samples:
- **At the file's rate** (the Falcon's own clock has it, or the FDI put at it, as for an MP3): the
  DMA, as before; bit for bit, no DSP.
- **Else, through the DSP, resampled, where the host port can carry it:** the DSP free and the
  samples 25,000 a second at most (mono to 25 kHz). The tick makes packets of 576 frames from the
  queue (`next_pcm_packet`: converted to 16 bits as for the DMA, the last filled with silence),
  each sample a word in offset binary, (s + 32768) << 7, since the stream's framing keeps bit 23
  for its counts. PSTREAM (14) is STREAM with these packets (PK_PCM), which go over the
  transform's overlap (1,264 words) since PCM doesn't use it. The DSP takes a packet into its
  lines' buffers, lets the next one come, and runs the granule's slots as for an MP3, copying
  where the synthesis filter would give: the resampler, the ring, its waits and its late count
  are the same code.
- **Else** (more samples, the DSP in use, or none): the DMA at the nearest rate, as before; the
  log says why.

**Why the limit:** each sample is a host-port word, 3 to 4 µs of a stock Falcon's 68030 on its
bus (a status read and a long write, whatever the 68030's own speed: a DFB1's goes through the
same bus). Measured on Falcon-4: CD stereo through the DSP took 46 % of the 68030 (the About box
330 ms to open, its time 150); 22.05 kHz stereo 29 %; 22.05 kHz mono 18 %, about what an MP3
takes (15 %). On Hatari's DSP the PCM path is 7 % of it as it is, 18 % resampled (`make dsptest`,
SIMPCM: the model's samples sent back as PCM, bit for bit both ways).

**For CD's rate, a proposal (not built):** the matrix can take the DMA's playback into the DSP's
SSI receive (DMAPLAY to DSPRECV), which costs the 68030 nothing more than the DMA path does now.
The link runs on the Falcon's clock, 49,170 frames a second, so the 68030 would fill the ring a
step through the file at a time (as the TT's path will: some frames twice), and the DSP, knowing
the same steps, would drop the doubles and resample what is left. It wants the DSP's receive
interrupt, an address register of its own, and a real Falcon to check the crossbar's timing.

### 0.9 on Erez's Falcon (2 Nov 2026)

Three logs:
- **Through the FDI at 44.1 kHz**, AUTOMATA.MP3 played 255 s clean: no granule late, no packet
  broken, the tick 13 % of the 68030. UNCRTNTY.MP3 then stopped at 152 s, while Erez moved the
  mouse, with AERA's accessories running. By then 45 granules had been late and 22 packets broken,
  and the DSP had all of its last packet but sent no frame after it. A DSP that waits for its
  SSI looks like that: one whose clock or matrix changed under it. A word lost or doubled on the
  host port gives the broken packets. Nothing in EPlay writes the host port but the tick, so
  the log now says what it needs to find out (below).
- **Without the FDI's driver and clock**, the DSP resamples to 49,170 Hz: 829 of 2,958
  granules late, heard as crackles. That is the DSP's budget: mix320.mp3 takes 94 % of it at
  that rate in the test, and Erez's music more. A late granule costs more than its own time:
  with a ring of two granules, a DSP that is late finds the SSI in the half it writes, waits for
  it to pass (13 ms), and a granule plays twice. Hatari's DSP doesn't show it (it runs faster
  than its cycles), so the cycle counts are the measure. Where the cycles go (mix320.mp3 at
  49,170 Hz): the resampler 12 %; the synthesis filter's DCT 11 % and its window 10 %;
  dequantising about 15 %; Huffman about 15 %. Next: the DCT in Lee's form (#14), about 8 %.
- **The Analog 8's channel 3** played white noise at full volume while the music played on
  channels 1 and 2, and so did the Falcon's own analog out. Setting one stereo pair (0.8) hadn't
  changed it. The DAC was joined to the DSP on the FDI's clock, which it doesn't run on. On the
  external clock the DSP now goes to the FDI alone, as in Cubase Audio, and the Falcon's own
  output is silent. On the Falcon's clock (no FDI) it goes to the DAC as before.

What the log says now, after each file:
- the tick's longest gap while the sound runs (what held the 68030's interrupts off), and where;
- the first granule late and the first packet broken, as a time in the file;
- the rate the DSP's frames came at, which is the clock it played on;
- whether another program changed the matrix while EPlay played, when, and to what;
- a file stopped by opening another says how it played, as Stop does.
(The tick's clock, Timer C's count and HZ200, could read 5 ms early while Timer C's interrupt
waited: the first longest gap, on Hatari, came out as 22,366 ms. The pending bit is now read too.)

### 0.10 on Erez's Falcon (2 Nov 2026)

0.10 played at 48 kHz, resampled, with 279 of 930 granules late. The FDI's own clock lines give
48 kHz at both 2 and 3 on Erez's Falcon (so since 0.4). 44.1 kHz comes only from 0, the clock
of what comes into its S/PDIF input. There 0.9 measured 44,100 Hz and 0.10 39,300 Hz: 44.1 kHz
with its first 25 ms missing, a receiver that locked later than the 200 ms it is given. 0.11
measures again at 0, up to three times 100 ms apart, while it isn't at rate, and the log gives
each measure. Five more tries on 0.10 all came out at 48 kHz, so the signal at the FDI's input
may not be there; Erez is checking it.

The Falcon's own output was silent with 0.10, as it should be, but the Analog 8's channel 3
still plays noise at full volume. The DAC wasn't it. What is left: the DSP's two 16-bit words
may not fill the FDI's frame, so its data line floats in a slot the FDI takes for channel 3. At
the stream's start the DSP now counts the port's frame for the log: its slots between two
frame syncs (8 asked, DC 7, the sync starting the count again), and its polls in 256 slots,
which give the slots' length whatever the frame sync does. Two slots a frame at 44.1 kHz are
about 360 of the DSP's clocks each; eight, 90. On Hatari the count of slots reads 1 or 2,
since its frame sync isn't the Falcon's, and the polls a steady 19,063.

### 0.11 on Erez's Falcon (2 Nov 2026), and 0.12

Every clock 0.11 measured was 3 to 4 % fast, before the DSP had started. The FDI's 48 kHz read
49,679 and 49,901 Hz; its input's 44.1 kHz read 45,392 to 45,837 Hz, past the 2.5 % allowed; at
the start, 49,387 Hz. So EPlay found no clock at 44.1 kHz and played on the Falcon's 49,170 Hz,
resampled. The same log has the queue at 1.9 MB (8 MB before) and the tick at 31 % of the 68030
(12 to 14 %): another set-up. Most likely the 200 Hz clock lost steps to interrupts held off for
more than its 5 ms. A measure is now timed by the MFP's Timer A, free while nothing plays, counting
12,288 a second, 256 a round. Interrupts held off for up to 20 ms lose nothing. If another
program is using Timer A, the 200 Hz clock times it as before, and the log says which. The
Falcon's own 49,170 Hz as a reference was tried first: on Hatari the external clock then read
0 Hz until EPlay started again.

Playing went wrong in 0.11 as well: 704 of 864 granules late, 112 packets broken, and frames
coming 1.8 times as fast as the file's rate. Its new frame count ran the SSI with 8 slots a frame
asked (DC 7), then changed its control registers while the port was the SSI's. The DSP56001's
manual changes them only in the SSI's reset state. 0.12 takes the count out: the DSP program is
0.10's again. The count gave its answer first: **8 slots a frame** (2,152 polls in 256 slots, the
length of eight), so EPlay's two words leave six undriven, and channel 3 is the first of them.


### 0.12 on Erez's Falcon (2 Nov 2026; the DFB1X off)

"Sound all good." Four files through the FDI, each opened while the one before played: the
clocks timed by Timer A (the FDI's 48 kHz at 47,961 and 47,992 Hz, its input's 44.1 kHz at
44,089), each file at 44.1 kHz on the external clock, no granule late, no packet broken; the
file's samples at 44,097 to 44,144 Hz; the tick's longest gap 2.3 ms. On the stock 68030 the
tick is 33 to 36 % of it (the frames made 10 to 15 %, sent 7 to 9 %), three times the DFB1's.

The first queue was 1.94 MB, the others 1.28: the VDI's block of ST RAM (512 bytes, for its
bitmaps), taken at the first draw that wanted it, came while the first file played, just past
its queue, and split the free ST RAM in two. It is taken at the start now. The log also gives the
largest free ST and TT RAM at the start (TT RAM: the DFB1's).


### The DSP's budget without the FDI (2 Nov 2026)

**The fast DCT, dropped.** The synthesis DCT's odd half (a 16-point DCT-IV) was done the fast way
in the model: pairs' sums and differences, two 8-point DCT-IIIs, a rotation. 88 multiplies for
256, and within ISO's limits as before (0.19 of the rms limit at most). On the DSP, though, the
matrix it would replace runs as `rep` multiply-accumulates, one instruction cycle each, about 640
clocks a slot. The fast form's sums, DCTs and rotation came to about 530: some 1 % of the DSP,
not worth a bit-exact decoder's risk. Reverted.

**Leaner instead, bit for bit:**
- **The slot's set-up** (3.3 % of the DSP to 1.8): its rows' places in the V ring are the slot's
  start, plus 32 in an odd slot, plus a constant, modulo 64 (or 1,024 into the next slot). So
  r4 holds that start, and `lua` with m4 makes each place, where adds and masks did each one.
- **The resampler's loop:** both channels' taps pointers made at once (r4 and r6; the input's
  base in r2), and a DO loop of the DSP's for the outputs, with no test and jump an output.
  Their number: from a position at -4 or more (every slot's: the last ended at 28 to 28 + S,
  less 32) it is c or c + 1, c S < 32 <= (c + 1) S (RSSET works c out once). One comparison a
  slot chooses, and the stream's first slot (at 0) is counted one by one.

| Files (Hatari's DSP, 32 MHz) | Before | Now |
|---|---|---|
| mix320.mp3, resampled to 49,170 Hz | 94.2 % | 92.1 % |
| square.mp3, resampled to 49,170 Hz (the worst) | 109.3 % | 107.2 % |
| mix48.mp3, resampled to 44.1 kHz | 87.9 % | 85.9 % |

The butterflies' first loop (2.2 %) reads both its inputs from external X while it runs from
external P, a cycle more each; in internal P there's no room for it.


### File names in lowercase (Erez, 2 Nov 2026; the guidelines' 6.6)

On MiNT an ext2 drive tells `EPLAY.INF` from `eplay.inf`, and a tester found the family's programs
missing their own files there. Every name EPlay gives a file is lowercase now: `eplay.inf` (and
`eplay.in$` while it is saved), `eplay.log`; and in the release `eplay.prg`, `notices.txt`,
`icons/eplay.rsc`, `eplaynv.rsc`, `eplay.icn`, `eplaymk.icn`, in `eplay-x.y.zip`. Texts the user
reads (the README, the log's lines, the status line) still write the names in capitals, as Atari
users do. A file the user chooses or drops is opened by its name as it came. An earlier install on
an ext2 drive is not looked for under its old names: its folder is deleted and the new release put
in fresh (the release's notes say so once).


### Quality: Normal and High (Erez, 2 Nov 2026), chosen by itself (4 Nov 2026)

*Since 4 Nov 2026 (Erez): no longer a setting.* An MP3 on the DSP and the Falcon's own clock
plays at High; where the DSP falls behind (3 granules late, past the file's first 8 frames:
`settings_qlate`), it goes on at Normal from where it is, as a seek does (a moment's break), until
another file opens. The log says so ("the DSP behind at 49,170 Hz ..."), and the bar and the
info line show 32.8 kHz. Settings has Output alone; an INF's `QUALITY=` is read no more.
`QLATE=` (read, never written) is the play rig's: its `auto` case sets 0, as Hatari's DSP now
keeps up at High with every file the rig can make (square waves and pink noise at 48 kHz, 320
kbit/s, all short blocks: 2,080 waits to spare a frame at least). What follows is the setting as
it was.

Erez's choice for the crackles without the FDI: a Quality setting, **High** (the default) or
**Normal**, "Normal is 15 kHz", for both the 68030's output and the DSP on the Falcon's own
clock. Through the FDI the music is always whole. In Settings, under Output, the same way: one
sentence ("Quality: Normal plays up to 15 kHz, for a slower machine."), then Normal and High.
`QUALITY=` in eplay.inf.

**Normal on the DSP:** a file above 32,780 Hz plays at the Falcon's 32,780 Hz, and the decoder
leaves out the subbands above about 15 kHz (22 of 32 at 44.1 kHz, to 15.2 kHz; 20 at 48, to
15.0: `MP3_SBLIM`), as it leaves out those that are all zeros: no transform, their overlap out
once. So nothing is left above 32,780 Hz's Nyquist (16.39 kHz) to fold back. The resampler goes
down with its band at all of that Nyquist. Its taps are made for the step (44.1 and 48 kHz to
32,780 Hz: `t_rsdn441`, `t_rsdn48`). The 68030 loads them over RSDN before the stream (LOADY),
and RSSET takes the subbands' limit. With 8 taps, 0.9 of the Nyquist would have been 5 dB down at
14 kHz, and an 18 kHz tone, had the subbands been there, only 13 dB down, folded to 14.8 kHz.
The band as built is 3 dB down at 14 kHz.

| Files (Hatari's DSP, 32 MHz) | High, 49,170 Hz | Normal, 32,780 Hz |
|---|---|---|
| mix320.mp3 (320 kbit/s) | 92.1 % | 86.4 % |
| xingvbr.mp3 (VBR -V 0) | — | 81.0 % |
| mix48.mp3 (48 kHz, 192 kbit/s) | 85.9 % (to 44.1) | 81.7 % |
| square.mp3 (all short blocks) | 107.2 % | 99.5 % |

Tested as before: the model's resampler against the double-precision one at 32,780 Hz (0.15 of
ISO's rms limit at most), the DSP against the model bit for bit (`make dsptest`, SIMRS=3), and the
play rig's `normal` case (eplay.inf with QUALITY=NORMAL: 32,780 Hz, its tones whole). The
68030's output follows the setting when its decoder is in.


### The 68030's decoder: what a 68030 can do (3 Nov 2026)

Before writing it, its heaviest part measured: the synthesis filter's window, 16
multiply-adds for each output sample (`tests/bench68`: MULS.W of 16-bit samples by 16-bit
factors, the samples strided as the filter reads them, in Hatari). 44.1 kHz stereo is 2,756
slots of 32 a second:

| Machine | Slots a second | 44.1 kHz stereo's window alone |
|---|---|---|
| Falcon-4 (stock, 68030 at 16 MHz) | 909 | 303 % of the 68030 |
| Falcon in Hatari (32 MHz, its most) | 1,833 | 150 % |
| Erez's DFB1 (50 MHz), scaled from 32 | about 2,860 | about 96 % |

The rest of a decoder (Huffman, dequantising, the transform, the DCT) about doubles it. So no
68030 plays MP3 at High, nor at Normal's 15 kHz (two thirds of the rate). Half the rate (16
subbands, about 11 kHz) halves the window: about 50 % of a DFB1 for it, 150 % of a stock
Falcon. Mono halves it again. Put to Erez as a decision (the capability is his): every
Falcon has the DSP, which EPlay holds only while it plays.

**Dropped (Erez, 3 Nov 2026).** Settings' Output is Auto, FDI or DSP; an eplay.inf that says
OUTPUT=68030 reads as Auto, and the next save writes what is chosen. Without the DSP free an MP3
says so, as before; WAV and AIFF play on the sound DMA as ever.

### The Analog 8's channel 3: why the DSP can't fill the frame (3 Nov 2026)

The DSP port's frame is 128 bits, eight 16-bit slots (0.11's count; the Falcon's matrix frames
four stereo tracks). EPlay's SSI sends two words a frame (network mode, DC 1) and leaves its
data line undriven in slots 2 to 7, which the FDI takes for channels 3 to 8. Driving them
means DC 7 and a word in every slot: eight transmit interrupts a frame, 352,800 a second at
44.1 kHz. The fast interrupt is one fixed instruction, `movep y:(r7)+,x:TX`, so it can only
read the ring in a straight line:
- **Zeros in the ring** (eight words a frame, six of them zero) would make the FDI's ring 9,216
  words, four times 2,304. External Y is full (PCM at $3000, then the resampler's taps and the
  packet).
- **A long interrupt** that picks the slot's word (the ring's or a zero) costs some 20 clocks a
  slot, about 22 % of the DSP: more than Quality High on the FDI has to spare (square.mp3
  needs about 95 % on its own).
- **A small frame buffer** read in a circle by the fast interrupt needs its two words refreshed
  once a frame, and the 56001 has nothing that comes once a frame (no frame sync interrupt, no
  timer on the frame's clock).
- **TSR** (the SSI's time slot register) skips a slot by leaving the line undriven, as now.

So it is the matrix's or the FDI's to settle. The test for Erez: a 44.1 kHz WAV through the FDI
plays on the sound DMA (DMA to EXTOUT on the FDI's clock), not on the DSP. If channel 3 is
quiet then, the DMA drives all eight slots, and the DSP's sound can go the same way: the DSP's
SSI recorded by the DMA into a ring in ST RAM and played from it by the DMA (no DSP time, a
little more delay at Play and Pause). If it is noisy with the DMA too, it is the FDI's
set-up (its channels, from its boot program) or the matrix's tracks (`Settracks`).

**Erez's answer (3 Nov 2026, 0.19):** with a WAV or an AIFF (the sound DMA, one stereo track,
`Settracks(0, 0)`), all eight of the Analog 8's channels are active. So the DMA drives every slot,
but it isn't yet known with what: the pair repeated in each of the four slot pairs, or one channel
in all of them. `lrtest.wav` (44.1 kHz, 16-bit stereo, made with Python's `wave`) tells them apart:
4 s of a 440 Hz tone on the left alone, 1 s of silence, 4 s of 1,760 Hz on the right alone, 1 s,
then both. Whatever it shows, the DMA can choose what each slot carries: with four tracks
(`Settracks(3, 3)`, eight words a frame), tracks 2 to 4 are zeros written once at open, and the
tick writes only the first pair (the ring four times as large in ST RAM: 1.6 MB for two seconds at
49,170 Hz, or 0.8 MB for one). The DSP's sound can go the same way (above), which would end the
noise on channel 3 with Output DSP as well. Whether channels 3 to 8 carry the music or stay silent
is Erez's call.

**lrtest.wav's answer (Erez, 3 Nov 2026):** channel 1 has the left, channel 2 the right, and
channels 3 to 8 all have the left again. A stereo player's channels past 2 are silent, so the
sound DMA now plays four tracks (`Settracks(3, 0)`, the DAC on the first, `Setmontracks(0)`):
16 bytes a frame, the music in the first four, the other twelve zeros written once when the
ring is taken (`Mxalloc` mode 0, as before). The tick writes a frame's two words and steps on
16 bytes (`pcm_convert_to`, its step 8 words; `tests/pcm` checks that the other six words are
left as they were). The ring is half a second long now, 393 KB at 49,170 Hz, the ST RAM one track's two seconds took
(a second, 786 KB, cost a stock Falcon under NVDI the room a menu needs, and 125 ms more at the
start to clear): the queue in TT RAM holds the file ahead, the ring only the tick's lead. The
DSP's own sound (MP3, and a WAV it resamples) still leaves slots 2 to 7 undriven.

**Parked (Erez, 5 Nov 2026: "if it is too much, let's not focus on Analog 8 issues").** With
Output DSP, the Analog 8's channel 3 is still noise (0.25). The way out found: the DSP's SSI to the
DAC and to the DMA's record channel, the tick copying each recorded frame into the four-track play
ring, the DMA playing that to the port, the music in the first track and zeros in the other three
(some 200 KB/s copied by the 68030, the port about 80 ms behind the DAC). A WAV or AIFF resampled
on the DSP can't go that way: the play DMA feeds the DSP's receive (XSTREAM). It changes how
every MP3 plays and only Erez's Falcon can judge it, so it waits until it is asked for again.

### A 24-bit AIFF on Erez's Falcon (3 Nov 2026), and 0.18

Erez's 24-bit stereo AIFF at 44.1 kHz crackled through the FDI and with Output DSP alike (both
play it on the sound DMA: stereo at 44.1 kHz is past what the DSP resamples): 330 underruns in 6 s,
the first at 0.86 s, the ring's lead down to nothing, the tick's longest gap 4.2 ms. The tick made
each sample through `sample()`, a call and a switch each, where 16 bits have loops of their own.
On Hatari's stock Falcon a 24-bit stereo AIFF at 49,170 Hz took the tick 67 % of the 68030 that
way, its ring's lead down to 650 ms; 24 bits stereo now have a loop of their own, rounded as
`sample()` rounds them (the same samples as sox's: `tests/pcm`): 29 %, the lead 2 s. The play
rig has the case (`aiff24`, ten seconds). The log now says how the disk kept up as well: the KB
read, the time the reads took and the rate that makes, against what the music takes (24-bit
stereo at 44.1 kHz 258 KB/s), and the queue's least lead.

### Phase 2's report (3 Nov 2026)

**Set out to do:** MP3 three ways, chosen in the settings: through the FDI at the file's rate,
on the DSP on the Falcon's clock (resampled), on the 68030; WAV and AIFF through the DSP; the
settings in eplay.inf.

**Done** (0.4 to 0.17; 0.11 withdrawn, 0.12 its rollback):
- The FDI's way, as Cubase's: the FDI put at the file's rate, each clock measured (0.4 to 0.8;
  timed by Timer A since 0.12). Clean on Erez's Falcon with the DFB1X off (0.12).
- The DSP's way: the faster decoder, resampling on the DSP, WAV and AIFF through it (0.9); the
  DAC out on the FDI's clock (0.10); 2 % more room (0.15).
- The log of a stream's troubles (0.10), the free ST RAM kept whole (0.13).
- Settings: Output (0.14), Quality (0.17); file names in lowercase (0.16).

**Left:**
- The 68030's decoder: no 68030 keeps up at High or Normal (above). Dropped (Erez, 3 Nov).
- The Analog 8's channel 3 (above): Erez's test with a 44.1 kHz WAV.
- Output DSP's crackle in the right channel (0.15's log): whether 0.14 had it, and whether it
  comes at the same places each time.

**Next, proposed: phase 3, the player window** (The program, above): the transport in the window's
top row (play/pause, stop, the position bar, clicked or dragged to seek), the volume in the
window, the info line as the status, Play/Pause (Space) and Stop in a Play menu, and the About
box's line 2. The playlist and Next/Previous come with phase 4.

## Phase 3: the player window (Erez, 3 Nov 2026)

Erez started phase 3 ("a very nice and beautiful player UI similar to modern players"), chose
design D of four (the canvas "EPlay player designs": A Now Playing, B Hi-fi deck, C Studio
meters, D Compact bar and list), with album art, and with B's small kbps and kHz labels.

**The window, as chosen:** the window's own parts (name, closer, mover, iconifier, fuller,
sizer, the info line as the status: "Playing 2 of 6 · 44.1 kHz · FDI"). Its work area:
- **The bar** across the top, 68 pixels high, dark (`#1d2230` in 256 colours; black in 16
  colours and mono, its text white):
  - at the left, the **album art**, 56 × 56: the file's own picture (ID3v2's APIC, a JPEG or PNG,
    through common's `pic.c` at that size, dithered to the screen's pens), else EPlay's;
  - the **title** and **artist** (ID3 tags, else the file's name), and under them the small
    labels **320 KBPS** and **44.1 KHZ** (the file's; a VBR file's average);
  - in the middle, **Shuffle, Previous, Play/Pause** (a round button), **Next, Repeat**, and
    under them the **position bar** with the time played and the length, clicked or dragged to
    go to a place;
  - at the right, the **output** label (FDI, DSP or DMA) and the **volume**: a speaker and a
    bar.
- **The list** under it, with its columns (#, Title, Artist, Album, Time), the playing row in
  the bar's colours, and the AES's own slider column at the right (5.1).
- **The small player** (phase 5) is the bar alone, the album art 40 × 40.

Every button in the bar works as 5.8 has it (drawn pressed on the press, its action on the
release over it). In mono and 16 colours every state shows without colour (5.2): Play/Pause by
its symbol, Shuffle and Repeat with a bar under them when on, the position and the volume by
their bars' lengths.

**The order** (each a release):
1. The tags: ID3v2 (2.2 to 2.4) and ID3v1, with host tests; WAV's and AIFF's names where they have
   them.
2. The bar: the new window, laid out and drawn (only what changes: the times, the position,
   Play/Pause), Play/Pause and Stop, the position bar going to a place (MP3: by the Xing or VBRI
   table, else by the bit rate; WAV, AIFF: by the sample), the labels, the output.
3. Pause: the sound stopped where it is and the DSP given back (the DSP rule), Play going on
   from there. (Built: below.)
4. The album art.
5. The volume (the Falcon's own output by its attenuation; the FDI's on the DSP, so that 0 dB
   stays bit for bit).
6. The list, with the one file opened; the playlist, Shuffle, Repeat, Previous and Next are
   phase 4's.

**Part 2 as built** (`ui/bar.c`): every shape is the VDI's filled rectangles and lines (a triangle
or the round button is its rows), so nothing needs a bitmap in ST RAM. Pens 16 to 22 hold the
bar's colours in 256 colours and more, put back at the end; 16 colours take the nearest of pens 0
to 15 with the bar black. The times and the position are drawn each second off the screen
(common's offdraw) and shown in one step. Until Pause (part 3) the round button plays and stops,
as Space does; Shuffle, Previous, Next and Repeat are drawn dim. The volume shows 0 dB, as it
plays, until part 5. A press on the position bar shows the time under the pointer while it is
held; on the release `play_seek` opens the file again there: a WAV or AIFF at the sample frame,
an MP3 by its Xing frame's table (VBR; `mp3_info_toc`, checked in `tests/mp3/test_info.c` to
2/256 and a frame) or by its bytes, at the first frame that a second frame header follows. The
DSP starts it as after any seek: its first frames muted until the bit reservoir is there.
The release only shows the place; the going there is the next turn's (5.8): together they were 55
ms on a stock Falcon, apart the seek's turn is 35 (it reads 4 KB at the place to find a frame,
not 64, and the file's tags aren't read again).

**Part 3, Pause, as built:** the round button and Space are Play/Pause; Stop is in the Play menu
and goes back to the start. Pause stops the sound and gives the DSP back (the DSP rule): the file
is closed and its place kept. An MP3's is the frame playing: the tick keeps where each of the last
32 frames it sent began in the file, and the DSP's count of frames decoded says which plays, so
a VBR file is taken up exactly, not by its Xing table's per cents; a WAV's or AIFF's is the sample
frame. Play opens it again there. The position bar while paused or stopped moves the place and
opens nothing. The speed rig pauses and goes on: the same millisecond, the file opened once.

**Part 4, the album art, as built (`ui/art.c`):** the picture's bytes read in 32 KB steps,
unpacked at the art's size by common's `pic.c` (TJpgDec a row at a time, PNG whole), each row
dithered as it comes, then built a pen at a time into a small form in ST RAM taken at the start;
from then on one copy a redraw. GEM has a whole turn from inside the decoder every 40 ms. But a
stock Falcon's 68030 unpacks a 500 x 500 cover of 48 KB in 6.8 s, and TJpgDec takes its input
512 bytes at a time (it can't take less: with 128 its headers don't fit), each 70 to 100 ms there
and up to 70 on Hatari's 32 MHz Falcon: over the 40 ms GEM may be kept waiting. So the 68030
unpacks the art only where a 50 ms measure at the start finds it three or four times a stock
Falcon (5,800 turns of its loop; a stock Falcon 1,342, Hatari's 32 MHz one 3,312, a DFB1 about
5,200): a 68060's Falcon. Elsewhere EPlay's picture stays, and the log says so. The DSP's way, for
every Falcon, is put to Erez: the art unpacked on the DSP (common's `dspjpeg`) before an MP3's
own program goes in, or while a WAV or AIFF plays on the DMA.

**The album art on the DSP, and its cache (Erez, 3 Nov 2026):** "decode on the DSP. Store a cache
version on file in EPlay cache folder." So an MP3's art is unpacked on the DSP before its own
program goes in (its music starts that much later, the first time); a WAV's or AIFF's while it
plays on the DMA. The art made is kept in `cache\` next to the program, as the guidelines' 6.5
has many small items: one pack (`art.pak`, a record a picture) and its index (`art.idx`), keyed
by the picture's length and its first 4 KB, so every track of an album with the same cover finds
it. A picture found there is shown at once, without the DSP.

**As built (art.c):** the picture's bytes read in 32 KB pieces, then unpacked on the DSP by common's
`pic.c` (TJpgDec with `dspjpeg`, at 1/4: `k = 2`, averaged down to the art's side), GEM given its
turn at the decoder's next poll after 20 ms of work (with 40 the turns came 60 to 70 ms apart:
the hub's polls every 8 MCUs and between the set-up's steps come every 15 to 25 ms). On
Falcon-4 a 500 x 500 cover of 48 KB: read by 95 ms, unpacked in 0.9 s, its turns 35 ms at most,
shown after 2.0 s; from the cache the next time at once.

### The window narrower, the volume, the list, the small player (Erez, 3 Nov 2026)

After 0.19 Erez found the window too wide (76 columns, 608 of a VGA screen's 640 pixels) and asked
for it thinner, for files added in bulk (playlists later), the small player, and the volume
working. Of two drawings on a 640 x 480 screen (48 columns in two rows; 56 in one) he chose **56
columns, one row** (448 pixels):
- the album art 56 x 56; under the title and artist the KBPS and KHZ labels;
- in the middle Previous, Play/Pause and Next, the position bar under them with the times beside it;
- at the right the output (FDI, DSP, DMA) over the speaker and the volume;
- **Shuffle and Repeat** go to the Play menu (greyed until the playlist, phase 4);
- the list's columns #, Title (20), Artist (11), Album (12), Time.

**The volume** is "like the progress bar, click and drag", and + and - too:
- 17 places, from 0 (silent) to 16 (full, 0 dB), steps of 1.5 dB; it changes as the pointer
  moves; + and - go two steps (3 dB). Kept in EPLAY.INF as `VOLUME=`, saved at the end when it
  changed (not at each step).
- **On the Falcon's own output:** the codec's attenuation (its steps are 1.5 dB, 0 to 22.5);
  silent by the DAC's adder off.
- **On the FDI it is digital, in the same steps.** An MP3's `global_gain` is lowered in each
  granule's side information as the packet is made (one step of it is 2^(1/4), 1.505 dB). This
  costs the DSP nothing, and the DSP's arithmetic stays bit for bit; silent is `global_gain` 0.
  WAV and AIFF are scaled as the tick writes them (Q15).
- **At 0 dB nothing is touched.**
- **A change never clicks** (the family's rule for a control on the sound):
  - MP3 on the FDI: the new global_gain comes in from the next packet (a frame, 26 ms at
    44.1 kHz). A frame's granules go through the transform's overlap-add with the last
    granule's half (576 samples), and through the synthesis filter's 512 samples of memory, so
    the change glides over about 13 ms by the decoder's own arithmetic. There is no edge in the
    middle of a frame: the step is at a granule's edge, inside that overlap.
  - WAV and AIFF on the FDI: the gain glides to the new value over 256 frames (5.8 ms at
    44.1 kHz), a step each frame (`pcm_gain`; `tests/pcm/test_gain`: no jump between two
    frames over 1/128 of full scale, the new volume exact after the glide, 0 dB bit for bit).
  - The Falcon's own output: the codec's attenuation changes at once by its own steps (1.5 dB
    each). A click far along the bar is one step of several dB at once; it can be made to go a
    step at a time from the tick if it is heard.

**Files added in bulk:** files dragged onto the window are added to the list (AP_DRAGDROP under
MiNT and MagiC, VA_DRAGACCWIND from an AV desktop: common's `dd.c`). The menu's Add item takes
several files at once where the file selector can give them (Selectric's protocol), and one at a
time with any other.

**The list, as built (0.21, `ui/list.c`, `src/probe.c`):**
- Up to 1,000 files, in TT RAM where there is some. Each is shown by its name, then by its tags
  and length once they are read: one file a turn in the background (one open, a few short reads;
  `probe.c` takes the length from a WAV's or AIFF's header, or an MP3's Xing, Info or VBRI frame,
  else its bit rate; `tests/probe`).
- Files come from the command line (the desktop's drop on EPLAY.PRG, several at once), VA_START
  (the first plays at once), a drop on the window (added; played if nothing plays), and File >
  Open... (^O, the family's Open): the file chosen, or with none chosen every MP3, WAV and AIFF in
  the folder, in name order. A folder given or dropped adds its files the same way. Several
  files picked at once in the file selector wait for Erez's answer on which selector he uses
  (TOS's can't; Selectric's protocol can).
- At a file's end the next plays. A double click or Return plays a row; Up and Down select;
  Delete removes (the one playing stopped first); a right click or Insert opens the row's menu
  (3.3: Play, Remove from the list, Clear the list), whose commands are in the Edit menu too.
  Previous and Next go through the list. The window has a sizer and the list a slider.
- Another file's start is a job (5.x: 50 ms between event calls): the press is answered by its
  two rows; the next turn opens the file; then the info line, the bar's left (art, text), its
  right (output, volume) and Play/Pause with the times, a part a turn; a turn that draws reads no
  file. A row taken out draws the rows under it two a turn. On Falcon-4: a double click 40 ms,
  Next 45, Return 50, Delete 30.

**The small player** is the bar alone, the album art 40 x 40 (as above).

**The mini player, as built (0.22):** View > Mini player (a check mark when on) makes the window
the bar alone: the art, the title and artist with their labels, Previous, Play/Pause and Next with
the position, the output and the volume; no list, no info line, no sizer. Its top left corner
stays where the window's was. EPLAY.INF keeps it (`MINI=1`), so EPlay starts as it was left. The
list goes on underneath (the next file plays at a file's end). The art is still 56 x 56, the
bar's: 40 x 40 needs the cover made again at that side (the cache's 64 x 64 records allow it),
and comes if Erez wants the mini player smaller. On Falcon-4: Pause and Play 25 ms, the switch
to the whole window 140, back 80 (within 300).

The order: the 56 columns and the volume (0.20); the list with the files added (0.21); the small
player (0.22).

### Add files: several files at once (Erez, 4 Nov 2026)

TOS 4.04's file selector gives one file. Of three ways (EPlay's own dialog, Selectric's
protocol, both) Erez chose EPlay's own, and set how it looks over four drawings: a dialog box with
the platform's look (3D where the AES has it), Selectric's width (about 40 columns), the drive in
one pop-up button, and a selection with no check boxes.
- **The tree** is the AES's own objects, built at run time (`ui/addfiles.c`, since 7 Nov 2026
  common's `ui/filepick.c` with EPlay's `FP_SPEC`) and given the 3D
  flags by common's `dlg_3d`; it opens in a window under MiNT and MagiC (`dlg_open`). The list in
  its white well is drawn by EPlay, as the Font dialog's lists are (`dlg_more`, `dlg_cut`).
- **From Selectric:** the drive pop-up at the folder line's start; Folders, the last ten added
  from (EPLAY.INF's `FOLDER1=` to `FOLDER10=`, and `ADDDIR=` the folder it opens at); the type
  (Music files, MP3, WAV, AIFF, All files) and the sort (name, date: the newest first, size: the
  biggest first, type), folders always first; a name mask (`*` and `?`) that narrows the list as
  it is typed; each file's size and date; the selected files' count and bytes; any number of
  entries, as memory holds.
- **The selection** as the desktop's: a click selects the file alone, Shift-click adds or takes
  out one, a drag selects the rows it passes; Up and Down move, Shift with them extends; ^A, All
  and None; a double click opens a folder, or adds the file at once. What a mask hides is no
  longer selected. Add puts the files at the list's end in the order shown, and plays the first
  if nothing plays. The music goes on while it is open (its queue fed every 20 ms).
- **Speed on Falcon-4:** opened 135 ms (the tree 125 by the AES, the list in a turn of its own);
  a folder opened 45; a click 25; Shift-click 30; Add 130. A click draws only the rows whose
  selection changed (the whole list was 50).
- `src/dirlist.c` holds the folder's entries, the sorts, the types, the mask and the selection,
  tested on the host (`tests/dirlist`). Both are common's since 7 Nov 2026 (AERA-Atari 74daba8, the
  family's file dialog: the hub, 5 Nov), the types given by the app as a table; the move changed
  no pixel (common's `tests/filepick` `--old`, on Erez's TOS 1.04 and 4.04 and under NVDI).

### Drawing whole (Erez, 4 Nov 2026; the guidelines' Speed, 4fd0a96)

"Drawing on screen should be the fastest possible": a picture is drawn from start to end with no
event call between, and "a rule never costs speed". Four were drawn a piece a turn: another file's
start (the info line, then the bar's left, its right, Play/Pause with the times), the info line
after Play from a pause, a removed row's rows (two a turn), and the Add files list (the turn after
the tree, and after a folder's path). They are drawn whole now: another file's start the turn
after its open (in the open's own turn the two added up: the uncached cover's start turn 105 ms,
Next 100), the resume in its click's turn, the rows in one redraw clipped to them, the dialog's list with the tree
on TOS (in a window, the AES's redraw draws both, as before). "EPLAY picture <name> <ms>" on the
rigs' console is each picture's time from when it is due to its last piece.

| Picture (Falcon-4:16) | In pieces | Whole |
|---|---|---|
| another file's start | 90 to 230 ms | 85 to 175 |
| the info line after Play | 20 to 45 | 15 to 45 |
| the rows after Delete | 60 | 45 |
| Add files opened | 150 to 155 | 150 |
| a folder opened in it | 65 | 65 |

Whole, a picture is one stretch: the speed table's rows that hold one are over its 50 ms (a row
played 75, Next 75, Return 85, Delete 70, Play after a pause 55; Add files opened 155 to 160 of
150, a folder 70), reported to the hub as the rule asks, not sliced. The uncached cover's start
turns: 20 ms.

### The 24-bit tick on a stock Falcon (Erez, 4 Nov 2026: the DFB1X off)

"This app should appeal to Falcon users": a 24-bit AIFF at 44.1 kHz on a stock Falcon (no TT RAM:
code, queue and ring in ST RAM) took 57 % of its 68030 in the tick, and the disk 246 KB/s of the
295 it needed: 185 underruns. The 24-bit stereo loop is the 68030's own now (a frame of 6 bytes
starts on an even byte: the even half of a sample a word read; rounded as the C, its top kept):
the tick on Hatari's Falcon-4 29 % before, 18 % after. `tests/hatari/pcm_test.py` holds the
loops (16-bit WAV, 24-bit AIFF and WAV) against the C, bit for bit, on Hatari's 68030: the play
rig can't hear the DMA's cases on Hatari 2.4.

### WAV and AIFF at any rate on the DSP: XSTREAM (Erez, 4 Nov 2026)

"Resample on the DSP" for a WAV or AIFF the Falcon's clock hasn't the rate of (0.22 played CD's
stereo on the DMA at 49,170 Hz: 11 % fast, two semitones high). Through the host port, a word a
sample, CD's stereo took 70 % of a stock Falcon's 68030 (50 % the words, 17 % making them), the
About box 520 ms, twelve of the WAV session's twenty timings over. So the sound DMA carries the
samples instead:

- **The ring** (`snd_open_feed`) is one track to the DSP's receive (`Devconnect(DMAPLAY,
  DSPRECV)`), at the DSP's output rate, the clock the DSP's SSI sends on. The SSI's frame size
  (CRA's DC) is the transmit's and the receive's both, two words, so a frame can't carry a flag
  of its own: the tick spreads the file's frames over the ring's (`pcm_spread`, its loop in
  asm), a frame with a sample where the step (the file's rate in, the DSP's out) has one, both
  words' lowest bit set; zeros where it hasn't. A sample's lowest bit is the cost: about 90 dB of
  range, more than the Falcon's codec gives. A file at the DSP's output rate or above (96 kHz)
  can't be carried a sample a frame: it plays at the nearest rate, as before.
- **The DSP** (XSTREAM, 15) is PSTREAM with the SSI's receive on (CRB $F800): a long interrupt
  (P:$0C, P:$0E; `rd` moved to the end of internal P for them) puts each word into a ring of 2,048
  over the Huffman tables (a PCM stream doesn't use them, and each Play loads them again), r6
  kept and m6 back to linear (the PCM path's always is). `getrx` makes the 576 frames' packets
  the host would have sent and hands them to the PCM path as they were: the resampler, its taps
  and the ring the SSI plays are PSTREAM's. A pair with one zero is a word out of step (the DMA
  started a word on): its first dropped, counted as a packet broken. The end word still comes
  from the host: two packets of silence that has samples, zeros, then the count 0 once the DMA
  has played them.
- **The four-track ring** the DAC plays has its three silent tracks cleared again after a feed
  (the bytes a feed wrote: its frames were four bytes).

| Falcon-4:16 (Hatari) | The tick | |
|---|---|---|
| aiff44: 24-bit stereo, 44.1 kHz, Output DSP | 24 % | 1000 and 1500 Hz, 10.06 s, no gap |
| wav22: 16-bit mono, 22.05 kHz | 12 % | 1000 Hz, 3.02 s, no gap |

(The C spread took 40 % with the 24-bit conversion; the asm 34 %; the tick at 250 a second, as
the DMA's, not the MP3's 500: 24 %.) `pcm_test` holds the spread's asm against the C, the source
running out among its 48 runs. A first build stalled after its first packet: the ring's count,
`and` on B1 alone, kept B2's sign when the interrupt's place had wrapped past getrx's.

### The FDI: bit for bit, never Normal (Erez, 5 Nov 2026)

"On FDI it can not drop to normal and stay true to exact bits." Quality by itself stays off on the
FDI's clock, and the FDI is set for each file as it opens, to the file's rate: a 48 kHz file on the
FDI's own 48 kHz, a 44.1 kHz file on 44.1 kHz, back and forth from one file to the next (`snd_fdi_set`
for every open; the rate kept only while it is the one measured). On Erez's FDI 44.1 kHz comes only
from its S/PDIF input (its lines at 0; Cubase Audio 2.06 the same, read 5 Nov 2026); while that input
has no 44.1 kHz clock a 44.1 kHz file is resampled to 48 kHz, and the DSP's time is what decides
whether it keeps up (the faster decoder, next).

### A stock Falcon's DSP at 49,170 Hz (Erez, 7 Nov 2026)

"Many users will not have an FDI, so I need to have the stock falcon with DSP able to play mp3";
"optimize as much as you can to make it happen". Erez's DFB1 speeds up the 68030 only: its DSP is a
stock Falcon's, so his DSP-mode logs are the case. On it a 320 kbit/s file at 49,170 Hz left the
DSP some 940 waits a frame (Hatari's DSP: about 6,900 on the same kind of file), and a few granules
came late, each a click, then Normal by itself.

Where the time goes (tests/dspmp3, mix320.mp3 resampled to 49,170 Hz: 92.1 % of Hatari's DSP):
the synthesis window 12 %, the resampler 10 %, dequantising about 16 %, the bits and Huffman codes
about 19 %, the synthesis DCT 11 %, the transform (9-point DCTs, windows) about 13 %. Without the
resampling (the FDI at 44.1 kHz) the same file takes 82 %.

**Not done, measured:**
- *The Huffman codes on the 68030* (tests/huff68: the model's values from the main data's bytes,
  checked against the model on all 17,389 granules of the test files): a stock 68030 decodes 63
  granules a second of mix320.mp3 where playing needs 153, 243 % of it (mix128.mp3: 114 %). As
  with the window (3 Nov), the stock 68030 can't take a part of the decode.
- *Fewer resampler taps:* going up from 44.1 kHz the images of anything below about 19.5 kHz land
  above the output's Nyquist (24.6 kHz) and fold back to where they are heard (a 10 kHz tone's to
  15 kHz): the 8 taps' stopband is what keeps them out.
- *The synthesis DCT on zero subbands:* it runs on the butterflies' sums and differences, where
  zero subbands save nothing.

**Done, bit for bit with the model (make dsptest), the same cycles:**
- *The output ring 3352 words*, where it was 2576: all of Y above \$3000 but the packet. 2576 held
  the granule playing and the one decoded, so a heavy granule had nothing to borrow from the light
  ones before it; now the decoder may run 1.6 granules ahead. The resampler's taps, up or down, are
  one slot at Y:\$100 (external memory with the data ROMs off: OMR's DE, which EPlay never sets),
  loaded by the 68030 before every stream resampled; its last inputs went to \$2722.
- *A packet's words taken by the host receive's interrupt* (P:\$20, `hri`, 7 words), while the DSP
  decodes. They were taken only in its waits, a word each 1.2 us and only while the 68030's tick
  pushed: at 49,170 Hz a stock Falcon's DSP has almost no waits, and a 68030 held by GEM (the album
  art drawn) sent its words late, so the next frame waited for its packet. A word now costs some
  14 cycles, about 1 % of the DSP for a 320 kbit/s packet. `slout` (only where nothing is
  resampled) went to external P for the room: 0.6 % more there (square.mp3 97.9 %, from 97.3).
  HF2 (a packet wanted) goes off with a packet's first word, in the interrupt: the routine that
  took the last word cleared it before, and with the interrupt taking it the 68030 saw HF2 still
  on and sent the next packet over the end of this one (play_test's first MP3: the DSP stopped).
  Hatari 2.4.1 (the rigs', and the hub's) raises no host receive interrupt for a word the host
  writes into an empty port (its dsp_core.c sets HRDF there without the interrupt; later Hatari,
  and a DSP56001, raise it while HRDF and HRIE are both set). So `pkpoll`, in the waits, also
  takes a word the interrupt left, with the interrupts held off a moment: on a real Falcon the
  interrupt takes nearly all of them while the DSP decodes, and under 2.4.1 the waits take them
  as they did. stream.c passes on both cores.

`tests/dspmp3/stream.c` (in `make dsptest`) runs STREAM as EPlay plays: the SSI clocked at the
output's rate, the packets sent on HF2, the status words read; it fails when the DSP stops, a
packet breaks, or frames are missing. Two knobs stand for a real Falcon: SIMLOAD=P, the DSP P %
slower than Hatari's, and SIMLAT=N, the 68030 ready with each word N cycles after the last.
mix320.mp3 resampled to 49,170 Hz, 150 frames (300 granules), granules late:

| SIMLOAD, SIMLAT | 0.26 | the ring | the ring and the interrupt |
|---|---|---|---|
| 100, 500 | 95 | 58 | 0 |
| 105, 0 | 114 | 77 | 49 |
| 105, 500 | 98 | 74 | 49 |
| 110, 0 | 121 | 78 | 83 |
| 110, 500 | 147 | 85 | 83 |

(At SIMLAT 1500, some 47 us a word, a packet takes longer than its frame and nothing keeps up.)
Better, not yet enough where the DSP is 5 % or more slower than Hatari's, as Erez's seems to be:
the decoder itself has to get faster still, or the file plays at Normal.

**0.29 on Erez's Falcon (7 Nov 2026), Output DSP:** two files of about 264 kbit/s (MPEG-1, 44.1 kHz,
VBR), with seeks: 0 underruns, 0 late granules, 0 broken packets, the DSP's waits 844 a frame on
average, the disk 35 s ahead at most and 9 s at least. He heard crackles. They fit the output, not
the decoder: in Output DSP the DSP's output goes to the DAC and the DSP port together on the
Falcon's own clock (49,170 Hz), and his FDI on the port plays at its own (48,011 Hz measured): 2.4 %
more samples than it plays, a slip a click. What goes to the FDI in Output DSP is put to Erez (the
DAC alone, or the FDI's clock as Auto). Output DSP never touches the FDI's clock, so that log
holds no "the FDI to 44100 Hz" line; the FDI at 44.1 kHz needs a log with Output FDI.

Erez's next run (7 Nov): no crackles; Output DSP plays on the Falcon's own output and on the FDI,
FDI on the FDI alone; and the FDI "does not adhere to the file's sample rate and stays constant at
48 kHz. This should be handled." Every log since 0.4 read 48 kHz at Cubase's 2 and 3 (its 8-track
values for 44.1 and 48), and 44.1 kHz only at 0, from what came into the FDI's input. Cubase
Audio's FDI routine (\$2A08 in Erez's copy) sets only these lines (its control word sent as
samples, \$5190, is for another interface: with the FDI's driver answering it is jumped over), with
5 and 7 for the same rates in its 16-track modes. **0.30** tries 2, 5, 3 and 7 for 44.1 (3, 7, 2, 5
for 48), each measured, the first at rate kept, then 0, and logs every reading: the log will say
which line, if any, moves this FDI's own clock.

0.30 on Erez's Falcon, Output FDI, a 44.1 kHz MP3: "the FDI to 44100 Hz: 2: 48017 Hz (48036 at 200
ms, 47997 at 500); 5: 39284 Hz (39272, 39283); 3: 48036 Hz; 7: 48011 Hz; 0: 39292 Hz (by the 200 Hz
clock 39280)". So the lines choose the clock's source and nothing else: with their middle bit set
(2, 3, 7) the FDI's own, 48 kHz; with it clear (0, 5) what comes into its S/PDIF input, here
about 39.3 kHz, a receiver with nothing to lock to (0.6 and 0.7 read 44.1 kHz at 0 with Erez's
sound card feeding it; 0.25 and 0.26 read 38.6). EPlay played on at 48 kHz, resampled, with 0
underruns. Erez: the input is his sound card's optical output at 44.1 kHz; he unplugged it and
plugged it in again, "and now it is in 44.1". So the FDI's receiver had lost its lock on a signal
there (the 38.6 to 39.3 kHz since 0.25: its oscillator free), and EPlay's 0 plays the file at
44.1 kHz bit for bit once it has it. Put to Erez: a word in the window when the input has no
signal to follow (the FDI's own 48 kHz then, resampled).

**What is left to gain, measured (7 Nov 2026, mix320.mp3 resampled, 92.1 %)**, each about 1 %, so
waiting for Erez's DSP-mode log of 0.28 to say how much more is needed before taking them on:
- *The synthesis window* (`swpair`, 12.4 %) is at its floor: one MAC an instruction, both
  accumulators.
- *Dequantising* (15.2 % with its paths): 87 % of the lines are not zero, and a cached value takes
  some 13 instructions with four branches (zero, the range, kept, the sign). A table by the signed
  value would take two of the branches out (about 2.5 %), but its 31 words have no room in internal
  Y (CQ at \$00, the window's pairs from \$10 to \$FF). Working out values (`dqval`, about 850 calls
  a frame, most of them a band's first use) is some 2 %; filling a band's 15 at its start would
  cost as much as the test it saves. A loop without the range test where the region's table has
  no linbits would save about 1 %.
- *The resampler* (`rsl`, 10.4 %): left and right read the same taps twice. One chain over
  interleaved samples, with both accumulators, would save about 1 %, but the position kept in A
  would have to go elsewhere.

### A dialog held open while it plays (the hub, 4 Nov 2026; 0.24)

A modal dialog (Settings, About) waits in common's `dlg_evnt`, not in EPlay's loop, so 0.23 read
nothing from the disk while one was open: the queue (about 4.4 s of CD's stereo) ran dry after
that (the speed rig's WAV session: 451 underruns on 0.22, 207 on 0.23). Common's `dlg_idle`
(AERA-Atari) wakes such a wait every 20 ms and calls the app's work; EPlay sets it to
`feed_only` (the queue refilled, no drawing) each turn while the music plays, and clears it when
it doesn't. The Add files dialog has its own timer and idle, as before.

| Falcon-4:16 (Hatari), CD WAV at 44.1 kHz, Settings held 6 s | sound | the queue's least lead |
|---|---|---|
| without `dlg_idle` | 15.60 s for 14 s: the DSP waited 1.5 s for samples | 92 ms |
| with it | 14.04 s, no gap | 4,098 ms |

(Fed by the DMA, a dry queue isn't an underrun: the ring has frames without samples, and the DSP
waits for them; the time is lost instead. `play_test`'s `settings` case checks the length.)

## Phase 4: playlists and folders (Erez, 4 Nov 2026)

Erez's choices: Repeat Off, All and One, one item in the Play menu that goes round; Shuffle a
random order with the list left as it is, each file once a round; playlists in through Add files
(and a drop), out through **File > Save playlist…** (Control+S) and the file selector; a folder
added with its subfolders.

- **The order** (`src/order.c`, `tests/m3u/test_order.c`): which row plays next. In order, the
  next row; shuffled, a row not yet played this round, at random (a 1,024-row played mask, the
  round over when every row has played); Previous goes back through what played (a history of
  1,024), not the list's row above. Repeat All starts the list (or a new round) again at its
  end; One plays the file again; Off stops there. The list keeps it in step as rows come and go
  (a row taken out leaves the history and the mask; rows added aren't played yet). Previous and
  Next on the bar are lit as the order has one.
- **Playlists** (`src/m3u.c`, `tests/m3u/test_m3u.c`): M3U, each line a path whole or relative
  to the playlist's folder (`..` too, `/` or `\`), `#` lines notes, URLs passed over, CR/LF, LF
  and a UTF-8 BOM taken. Read 16 KB at a time, a line cut by a piece's end carried into the next
  (a playlist EPlay saves for 1,000 files is about 80 KB), the music fed between pieces. Saved
  with `#EXTM3U`, each file's `#EXTINF` (its length and tags when they're read) and its path,
  relative where it is in the playlist's folder; written 4 KB at a time, an existing file
  replaced only after an alert.
- **Folders**: Add files selects folders as it does files (Return opens one only when it is
  alone, or nothing is selected); a folder's music is walked eight folders deep at most, the
  queue fed every 16 entries, then put in path order with a shell sort of the new rows (a tree's
  walk isn't sorted, and an insertion sort of 1,000 is seconds on a 68000).

`play_test`'s m3u, tree, repeat, shuffle and save cases; manym3u, manytree and many hold 1,000
files (46 KB of list; on Falcon-4 the largest ST RAM block 2,297 KB before, 1,444 KB after,
the queue's ring and the list both taken).

## The order of the work

1. **Is MP3 possible on a stock Falcon's DSP?** Yes, with room (above): the two heaviest parts
   measured on Hatari's DSP core, the rest estimated. The C model of the whole decoder is done
   (above), MPEG-2 and 2.5 with it, and the whole DSP decoder, `dsp/mp3.a56`, bit for bit and
   counted (above), with the DSP's last gains: 320 kbit/s at 86 % on a stock Falcon. To decide
   with Erez: the 68030 taking the Huffman part and dequantising where it is fast (the DFB1), a
   lighter decode of the top subbands when a stock Falcon falls behind, or both.
2. **The sound core on the Falcon**: the ST RAM ring, the interrupts, the matrix and the external
   clock found and timed; WAV and AIFF playing. Done in phase 1 (above), with MP3 through the DSP
   and a minimal window, for Erez's Falcon.
3. **The GEM program** around it: window, playlist, small player, files from the desktop.
4. **MP3 on the DSP**, whole, tested as above.
5. **TT and STE**, 8-bit WAV and AIFF.
6. 1.0: phase 6 closed as EPlay 1.0 (Erez, 8 Nov 2026).
7. **Phase 7, streaming** (below).
8. **Phase 8, the player's features** (below).

## Phase 6 closed: EPlay 1.0 (Erez, 8 Nov 2026)

Phase 6 (0.33 to 1.0) set out to play WAV and AIFF on the TT and the STE. Done: the TT in 0.33
(interpolated to 50,066 Hz and dithered to 8 bits), EPLAYST.PRG for the STE in 0.35 (the nearest
sample, the LMC1992's volume), and 1.0 with the STE's tick lighter. Left: an STE's window while a
CD-rate file plays is still over the ST's times (About 285 ms and Settings 295 against 150 on
Hatari's STE, mono; paused, within them). The next step is known: the tick less often on an
STE (its fixed cost is some 13 % of the 68000 at 250 a second), then the once-a-second times
drawn more cheaply. It goes on as speed work beside phase 7.

## Phase 7: streaming (Erez, 8 Nov 2026)

Web radio and audio streams from a URL, on the Falcon (MP3 on its DSP), on Ataris with Ethernet:
- the stream as a source of the queue in place of the file (Icecast and Shoutcast: HTTP with a
  continuous MP3), seconds held in memory so that the network's pauses don't break the music;
  reconnecting when the connection drops; no position bar or length for a stream;
- the station's titles from the stream's ICY metadata in the bar;
- an Open URL entry, and station lists in `.m3u` and `.pls`;
- **how it connects, as the other AERA programs do** (Erez): a direct connection, or through the
  AERA gateway, chosen in the menu. Through the gateway, HTTPS and the stations the Falcon can't
  take (AAC, HLS) can be given to it as plain MP3;
- the STE is left out (no MP3 there, and its 68000 already busy with the sound).

## Phase 8: the player's features (Erez, 8 Nov 2026)

From the list put to Erez:
1. gapless playback (the encoder's delay and padding from LAME's header);
2. ReplayGain (track and album gain from the tags);
3. search and sort in the list (by text; by artist, album, track);
4. lyrics from the tags (USLT, and timed lyrics);
5. more formats: tracker modules (MOD) on the DSP, FLAC (to be measured on the 68030), OGG
   and AAC (DSP projects);
6. working with the family: a "now playing" message to the other AERA programs, remote control
   from a phone through the gateway, scrobbling (Last.fm) through the gateway.
