<p align="center">
  <img src="ps4/docs/zeruko-icon-master.png" alt="Zeruko" width="180">
</p>

<h1 align="center">Zeruko</h1>

<h3 align="center">Your classics, back in orbit.</h3>

<p align="center">
  <img alt="Platform: jailbroken PS4" src="https://img.shields.io/badge/platform-jailbroken%20PS4-4DA3FF">
  <img alt="Renderer: Vulkan" src="https://img.shields.io/badge/renderer-Vulkan-8B5CFF">
  <img alt="Based on PCSX2" src="https://img.shields.io/badge/based%20on-PCSX2-101A36">
  <img alt="Licence: GPL-3.0-or-later" src="https://img.shields.io/badge/licence-GPL--3.0--or--later-3d3a8c">
</p>

---

**Zeruko is a couch-friendly PlayStation 2 emulator for jailbroken PlayStation 4 consoles, based on PCSX2.** It is a native PS4 application: you pick a game from a cover shelf with the gamepad and play. No keyboard, no mouse, no emulator jargon.

> **Status: experimental.** Zeruko is early software. Expect rough edges, and expect performance to vary a lot by game — the base PS4's CPU is the main limit. Read the compatibility notes before installing.

## What Zeruko does

- **A shelf, not a file menu.** Your games sit on a cover-flow shelf. Cross starts the last game you played. Everything is driven with the gamepad, in plain language.
- **Per-game tuning without the jargon.** Each game's options are one sheet of plain-language rows: resolution, widescreen patches, memory cards, HD texture packs.
- **Smart tune (experimental).** One press starts the game and measures the graphics options that matter *while you play*, then keeps the best one that still holds full speed — per game, automatically.

## What is not included

No BIOS files, games, keys, firmware, or other proprietary console data are included with Zeruko. You need your own legally dumped PS2 startup files (BIOS) and your own legally dumped games.

## Requirements

- **A jailbroken PS4** that can install homebrew packages (for example with GoldHEN).
- **Your own PS2 BIOS**, dumped from your own console.
- **Your own games**, as `.iso`, `.chd`, `.cso` or `.zso` files.

## Install

1. Install the package (`zeruko-v0.1.0-ps4.pkg`). It shows up on the home screen as **Zeruko**.
2. Copy your BIOS file (4 MB, e.g. `SCPH-70012.bin`) into `/data/zeruko/bios/`.
3. Copy your games into `/data/zeruko/games/` — or point Zeruko at a USB drive or a network folder.
4. Optional: PCSX2 patch files (`.pnach`, widescreen, 60 FPS...) go in `/data/zeruko/patches/`.
5. Start Zeruko and pick a game on the shelf.

If you used an earlier development build, the first run offers to copy your existing settings and memory cards from `/data/PCSX2/`. The old folder is never moved or deleted.

## Controls

| Button | What it does |
|---|---|
| **Cross** | Confirm / start the game |
| **Circle** | Back |
| **Square** | The game's options sheet |
| **Triangle / Options** | Settings |

## Storage layout

```
/data/zeruko/
  games/       your disc images (.iso, .chd, .cso, .zso)
  bios/        your PS2 BIOS and its .mec/.nvm
  memcards/    Mcd001.ps2, Mcd002.ps2 — your virtual memory cards
  savestates/  instant save files (*.p2s)
  settings/    per-game settings, one ini per game
  patches/     .pnach cheat and patch files
  textures/    HD texture replacement packs
  covers/      cover artwork cache
  cache/       shader caches
  flags/       on/off switch files
  logs/        boot.log, emulog.txt — send these with any bug report
  gs.ini       settings for all games
```

## Features

- **Smart tune** — per-game automatic graphics tuning measured during real gameplay (experimental; re-run it in a heavy scene if a pick feels wrong).
- **Memory card manager** — copy, delete and rescue saves in plain language, with crash-safe writes.
- **HD texture packs** — downloads and installs community HD packs per game from the shelf.
- **Extra in-between frames** — optional frame generation for smoother motion on heavy games.
- **Network folders** — load games over the network instead of copying them to the console.
- **Fast forward, save/load with the gamepad** — hold L3+R3 and a D-pad direction.

## Compatibility and performance

The base PS4's CPU is the main limit: the EE recompiler and the VU1 thread can each fill one core. Lighter 2D and early 3D games should run well; heavy late-generation games will struggle on a base PS4. The package enables PS4 Pro mode, but Pro performance is not yet measured.

Detailed per-game notes live in [ps4/COMPATIBILITY.md](ps4/COMPATIBILITY.md). Reports are welcome — include the logs from `/data/zeruko/logs/`.

## Building from source

The console build lives in [`ps4/`](ps4). It uses the console layer in `ps5/coreorbis` and the PCSX2 core. You need, in Linux or WSL:

- a clang toolchain targeting the PS4 with the [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) SDK,
- a PS4 sysroot with libc++ and glslang, and the PS4 Vulkan (RADV) runtime the eboot links against,
- PCSX2's dependencies built for the PS4 (see `ps4/Makefile.ps4`).

Then:

```bash
ps4/build-ps4.sh            # compile and link the eboot
ps4/build-ps4.sh package    # also make the .pkg
ps4/build-ps4.sh upload     # package, then send it to the console over FTP (set PS4_HOST)
```

## Troubleshooting

Logs are written under `/data/zeruko/logs/` — `boot.log` is the most useful thing to send with a bug report, and the settings page can bundle a full log report for you.

## Credits

- [PCSX2](https://github.com/PCSX2/pcsx2) and its contributors: the emulation core.
- [PS5SX2](https://github.com/Swordpdf/PS5SX2) by swordpdf: the console port this one is built on, its shelf and settings.
- [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain): the PS4 homebrew toolchain.
- [Mesa](https://www.mesa3d.org/) RADV: the Vulkan driver behind the renderer.
- [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) by Themaister (optional renderer, LGPL-3.0-or-later).

This port was written with AI assistance (AI-assisted).

## Licence and attribution

Zeruko is based on [PCSX2](https://github.com/PCSX2/pcsx2) and contains
modified PCSX2 code. PCSX2 is copyright 2002-2026 the PCSX2 Dev Team and
contributors. Zeruko is licensed under the GNU General Public License version
3 or later (`GPL-3.0-or-later`); see [COPYING.GPLv3](COPYING.GPLv3).

Zeruko also incorporates or builds upon work from
[PS5SX2](https://github.com/Swordpdf/PS5SX2), the
[OpenOrbis PS4 toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain),
[Mesa](https://www.mesa3d.org/) RADV, and optional paraLLEl-GS components.
See the credits and third-party notices for their respective licenses.

The PCSX2 name is used only to identify the upstream project; the PCSX2
project does not endorse Zeruko. PlayStation, PS2 and PS4 are trademarks of
Sony Interactive Entertainment. Zeruko is independent homebrew and is not
affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.

No BIOS files, games, keys, firmware, or other proprietary console data are
included in this repository or its release packages.

Source code: https://github.com/felipebasurto/zeruko
