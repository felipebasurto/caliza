# Zeruko

Zeruko is an independent, noncommercial fan project that brings the PCSX2 PlayStation 2 emulation core to PlayStation 4. Its controller-driven library lets you browse your collection from the couch and apply settings per game.

[Download the experimental PS4 release](https://github.com/felipebasurto/zeruko/releases/tag/v0.1.0) · [Compatibility and test results](ps4/COMPATIBILITY.md) · [Build evidence](ps4/STATUS.md)

## Use your own collection

Use game images you create from your own legally purchased discs, or copies you download directly from the rights holder or an authorized distributor after purchase, when their terms permit this use. Use a BIOS you dump from your own PlayStation 2 where permitted by applicable law. The project distributes the emulator and its resources. Game images, BIOS files, firmware and keys are supplied by the user.

## Install on PS4

You need a PS4 already configured to run homebrew, your own PS2 BIOS, and one game image. Zeruko reads `.iso`, `.chd`, `.cso`, and `.zso` files.

1. Download `zeruko-v0.1.0-ps4.pkg` and its checksum file from the [release page](https://github.com/felipebasurto/zeruko/releases/tag/v0.1.0). In the download directory, run `shasum -a 256 -c zeruko-v0.1.0-SHA256SUMS.txt` on macOS, or `sha256sum -c zeruko-v0.1.0-SHA256SUMS.txt` on Linux.
2. Install the package using your console's homebrew package installer. **Zeruko** appears on the home screen.
3. For a fresh installation, put your BIOS in `/data/zeruko/bios/` and your game image in `/data/zeruko/games/`. For an upgrade from a development build, follow the migration steps below first.
4. Open Zeruko, select a game, and press Cross. Start with an ISO on internal storage at native resolution. You can try USB and other formats after confirming the game starts.

### Upgrade from a development build

If you already use `/data/PCSX2/`, back up your memory cards and settings, then open Zeruko once before creating `/data/zeruko/`. The first launch can copy settings, BIOS, memory cards, and other small files. The old folder is retained, and Zeruko can read its games and texture packs in place.

If the destination already exists or the first copy was interrupted, check the `[migrate]` entries in `boot.log`. Copy any missing files you need from the backup with both applications closed.

## Use the library

| Control | Action |
| --- | --- |
| Left / Right | Browse games |
| L1 / R1 | Jump five games |
| Cross | Start the selected game |
| Square | Open the selected game's settings |
| Circle | Close the settings sheet |

You can change resolution, configure memory cards, and manage texture packs per game. **Smart Tune** is experimental. It tests graphics settings while you play, so run it in a demanding scene if you want the result to reflect gameplay.

## Storage on the console

| Path under `/data/zeruko/` | Contents |
| --- | --- |
| `bios/` | Your PS2 BIOS and its companion files |
| `games/` | Your game images |
| `memcards/` | Virtual memory cards and in-game saves |
| `savestates/` | Instant saves |
| `settings/` | Per-game settings |
| `patches/` | Local PCSX2 patch files |
| `textures/` | Texture packs |
| `logs/` | Startup and emulation logs |
| `gs.ini` | Global settings |

## Compatibility and troubleshooting

Version 0.1.0 is an experimental preview. The PS4 build, package, and host tests have passed. The compatibility matrix identifies what still needs testing on real hardware. Performance varies by game, console model, and settings. Heavy games can be CPU-limited on a base PS4. PS4 Pro performance is still unmeasured.

When reporting a problem, include the Zeruko version, PS4 model, system software version, game serial, storage type, and steps to reproduce it. Attach `boot.log` and `emulog.txt` from `/data/zeruko/logs/`, or from `/data/zeruko/` if they are stored directly there. Share logs with personal information removed. See the [console test procedure](ps4/COMPATIBILITY.md#required-console-smoke-protocol) and [known issues](ps4/KNOWN_ISSUES.md).

## Build from source

The PS4 target lives in [`ps4/`](ps4/) and uses PCSX2 with the [console layer in `ps5/coreorbis/`](ps5/coreorbis/). The build requires Linux or WSL, Clang with the OpenOrbis SDK, the Crema/CEMUPS4 runtime and sysroot, Mesa RADV, and the dependencies listed in [`Makefile.ps4`](ps4/Makefile.ps4). See the [PS4 platform notes](ps4/PS4_PLATFORM.md) and the [build setup](AGENTS.md#ps4-pkg-build-verified-2025-11-24-ai-assisted).

```sh
bash ps4/tools/run-host-tests.sh
bash ps4/build-ps4.sh
bash ps4/build-ps4.sh package
```

## Licence and credits

Zeruko contains modified [PCSX2](https://github.com/PCSX2/pcsx2) code, copyright 2002–2026 the PCSX2 Dev Team and contributors. Zeruko is licensed under the [GNU General Public License, version 3 or later](COPYING.GPLv3). The port builds on [PS5SX2](https://github.com/Swordpdf/PS5SX2) and uses the [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and [Mesa RADV](https://docs.mesa3d.org/drivers/radv.html). [Third-party notices](ps4/PORT-THIRD-PARTY.txt) cover additional components, including the optional paraLLEl-GS renderer.

Sony Interactive Entertainment owns the PlayStation marks. Zeruko is independent of Sony Interactive Entertainment and the upstream PCSX2 project. Product names identify the platforms and software involved.

Project development and documentation are AI-assisted.
