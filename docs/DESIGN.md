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

## Phase 2: three ways to play MP3, chosen in the settings (Erez, 1 Nov 2026)

Erez started phase 2 after 0.3's log, and set what it is: every way works, and the user picks the
one the machine has.

| Way | For | The decoding, and the sound |
|---|---|---|
| **68030** | a TT, a Falcon without its DSP free, any 68030 machine | the 68030 decodes; the sound DMA plays (the ring, as WAV) |
| **DSP** | every Falcon | the DSP decodes and plays from its SSI on the Falcon's clock; a rate the Falcon hasn't (44.1 kHz) resampled on the DSP |
| **FDI** | a Falcon with the FDI | Cubase's way: the FDI set to the file's rate (44.1 or 48 kHz, each measured), the DSP's SSI on its clock, bit for bit |

- **Settings:** Output: Auto (the default: the FDI where its driver answers, else the DSP on a
  Falcon, else the 68030), or one of the three; only what the machine has is offered. Volume:
  0 dB, the default. Both in EPLAY.INF.
- **The 68030's quality follows the machine** (Erez's choice): the highest it keeps up with, full
  on a fast 68030 (a DFB1), half the rate or mono on a stock Falcon or a TT; the window says which
  ("68030, half rate"). The music doesn't break up. An ST or STE has no MP3.
- **The order:** the FDI way first (Erez's Falcon, and it removes 0.3's two faults: the clock
  and the DSP's output through the host port), then the DSP way with resampling, then WAV and
  AIFF through the DSP, the settings, and the 68030's decoder last (a new part). The TT's WAV and
  AIFF (phase 6) come with the 68030 way where they share the code.

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
6. 1.0.
