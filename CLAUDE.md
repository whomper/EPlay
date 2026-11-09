# Notes for working on EPlay

EPlay is one of the AERA Atari apps (ECal, EContact, EMail, EFTP, EHypView, ClaudeST, EWeb, EUpdate,
EPDF). They are meant to
look and behave as one family.

## For every AERA app

These come from AERA-Atari's repo template and are the same in every app's CLAUDE.md.

- **Follow the AERA Atari app guidelines** ([ATARI-APP-GUIDELINES.md](https://github.com/whomper/AERA-Atari/blob/main/standards/ATARI-APP-GUIDELINES.md)). Pay most
  attention to:
  - About box
  - family shortcuts
  - PRG/ACC rules
  - context menus
  - VA_START, help, alerts
  - INF location

  When a change touches one of these, do it the guideline's way, even if the old code here did it
  differently.
- **Speed is set by the stock machines** (Erez, 26 Oct 2026; the guidelines' "Speed"): an 8 MHz ST
  and a stock Falcon (`ST`, `ST-1`, `Falcon-4`), not the DFB1. Design and code for them: draw only
  what changed, keep the 68000's slow operations out of inner loops, do work once. A change to
  drawing, a loop over the data or the start gives its `AESGAP` stretches on `ST` and `Falcon-4`,
  before and after, in its commit message. A GEM release passes the speed rig within the
  guidelines' times.
- **Shared code lives in [AERA-Atari](https://github.com/whomper/AERA-Atari).** Files under `common/` come from it. Change them
  there, not here, and pull them in with `tools/sync-common.sh`. A fix made only in this repo will drift
  again.
- **Build and test before every push:** `make` with no warnings and `make test`, plus this repo's
  own checks (below): `make itest` when network code changed, the Hatari rigs in `tests/hatari/` when
  the GEM side changed.
- **Versions and releases:** `VERSION` changes only in a release commit. A release: `tools/bump.py`,
  commit, push, then run the Release workflow on `main` with the notes (or push a tag `vX.Y` made with
  `git tag -a`); it tests, builds, tags vX.Y and publishes each file and the zip as a GitHub Release.
  A build must leave every tracked file unchanged and shows the version in `VERSION` as it is (there is
  no `-dev` label). Don't
  commit built programs, help files or certificates (`binary/`, `dist/`). Never reuse a number.
- **Only formal releases leave the build machine.** Erez gets vX.Y releases made by the Release workflow from
  `main`, nothing else: no test builds, no CI artifacts, no builds from a branch.
  Work lands on `main`; to try a fix on real hardware, release it. If a release doesn't work, fix
  forward: a new release with the next number, before any other work; never a rollback (Erez, 7 Nov 2026).
- **Releases go out without asking Erez** (his decision, 23 Oct 2026; guidelines 1.2). Release when
  work on `main` is ready:
  - this repo's checks pass;
  - the rigs this file asks for pass (on EmuTOS where the session has nothing else).

  Then give Erez the link in one line, with anything he has to do, and send the hub the link. The
  hub's checks (Erez's TOS and NVDI, the configurations of AERA-Atari's standards/TEST-MATRIX.md) follow
  the release; a fault they find is fixed forward by a new release that comes before any other work.
  Decisions that are his still go to him first.
- **Who decides what** (Erez, 31 Oct 2026; the guidelines' "Who decides what"). Erez's part, put
  to him before the work as a decision with a short context:
  - new programs and ideas, their core capabilities, and how they work with the other programs;
  - the user experience: the look, the layout, the way through a task, the words, the icons, the
    defaults;
  - phases: work stays inside the phase he approved; at its end, one phase report (what it set out
    to do, what was done with the release links, what is left, the next phase as a proposal), and
    the next phase starts when he says so;
  - priorities and conflicts between programs or sessions (through the hub, with its options and
    recommendation);
  - packages on the Pi and ports.

  How it is made is the team's, without asking: code, structure, shared code, tests, measurements,
  fixes, docs and releases inside an approved phase.
- **Only the hub tests interdependency and the family** (Erez, 8 Nov 2026). An app's own tests run the
  app alone: no other AERA program or accessory loaded, started or fetched for it (`getrelease.py`,
  `machines.py accs()` and `acc_all.py` stop outside AERA-Atari). What it does with another program is
  tested against a stand-in this repo owns; the real programs together are the hub's family checks.
  The app does test its own gateway code, from its `gateway/` copy (guidelines 1.2).
- **Every new source file starts with the SPDX line and copyright** (see any `.c` file); a generator
  writes them into the files it makes.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright lines
  (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help file's `@author`, the docs, and as
  the author of every commit: `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer, so GitHub shows the commits as Verified (AERA-Atari,
  `standards/GITHUB-IDENTITY.md`).
- **Never commit:**
  - real addresses from the owner's network: use `192.168.1.x`, `PI_IP`, `ATARI_IP`
  - passwords, session keys, `.KEY` / `.INF` files from a test run
  - photos that still carry EXIF/GPS data
- **README:** keep the section order from the template (Highlights, Getting started, How it reaches the
  servers, Building, Source, Tests, Licence, family footer). Use British English.

## EPlay

EPlay plays MP3 (on the Falcon's DSP, or through the FDI as Cubase does; MP3 on the 68030 was
dropped, as no 68030 keeps up: Erez, 3 Nov 2026), WAV and AIFF (Falcon, TT, STE). It is a program only, no
accessory (Erez, 31 Oct 2026). The design, and the order of the work, are in `docs/DESIGN.md`:
read it first, and change it with the code when a decision changes.

- **The sound hardware reads ST RAM only.** The ring the DMA plays is taken with `Mxalloc` mode 0
  and checked; code and the rest of the data are in TT RAM, Global under MiNT's memory protection (`PRGFLAGS=0x17`:
  the interrupts run while other programs are current).
- **Every host-port word waits for its flag** (`HISR` TXDE/RXDF). Never time a transfer by the
  68030's speed: Erez's DFB1 runs it at 50 MHz.
- **The sound is fed by interrupts from memory**; GEM only refills the buffers, in bgwork steps.
  Nothing the music needs waits on the AES, the disk or another program.
- **The DSP rule:** lock it only while it plays (an MP3, or a WAV or AIFF it resamples), unlock
  on pause and stop; if `Dsp_Lock` is refused, an MP3 says so ("The DSP is in use by another
  program.") and a WAV plays at the nearest rate, and neither retries by itself.
- **The DSP decoder** is tested as common's DSP programs are: a C model of its arithmetic against a
  reference decoder (ISO 11172-4 full accuracy), then the `.a56` against the model, bit for bit, on
  Hatari's DSP core. Third-party code: name its licence first; it goes into `NOTICES.TXT`.
- **Erez's set-up**: Falcon with the DFB1 (50 MHz 68030, 68882, 14 MB ST RAM, 128 MB TT RAM),
  TOS 4.04, BlueSCSI with an SD card, VGA, a SoundPool FDI (S/PDIF, 44.1 and 48 kHz clocks on the
  DSP port) set up at boot by its FDI program. Speed is still measured on `Falcon-4`.
