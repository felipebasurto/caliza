<p align="center">
  <img src="ps4/sce_sys/icon0.png" width="100" height="100" alt="">
</p>

<h1 align="center">Caliza PS4</h1>

<p align="center">PlayStation 2 emulation on PlayStation 4, built around your controller and your own game collection.</p>

<p align="center">
  <a href="#install-on-ps4">Install</a> ·
  <a href="ps4/COMPATIBILITY.md">Compatibility</a> ·
  <a href="#build-from-source">Build from source</a>
</p>

<p align="center">
  <img src="caliza/host/evidence/1080p/01b-home-continue.png" width="900" alt="Caliza PS4 Estratos home screen rendered by the host preview">
  <br>
  <sub>Estratos interface rendered by the executable host preview with fictional games and procedural cover art. Final logo art is pending.</sub>
</p>

Caliza PS4 is an independent, noncommercial fan project based on the PCSX2 emulation core. The new Estratos interface is controller-first and exposes library, game settings, memory cards, controls, accessibility, diagnostics and folder management. The companion panel is an original Caliza interface. PS2UI is credited as visual research only; its components and styles are not part of the implementation.

> **Experimental.** The [public v0.1.0 preview](https://github.com/felipebasurto/caliza/releases/tag/v0.1.0) uses the earlier Caliza interface. Current source is migrating to the Caliza Estratos interface, with `/data/caliza` as the console data directory. See the [build and test evidence](ps4/STATUS.md).

## Use your own collection

Use game images you create from your own legally purchased discs, or copies you download directly from the rights holder or an authorized distributor after purchase, when their terms permit this use. Use a BIOS you dump from your own PlayStation 2 where permitted by applicable law. The project distributes the emulator and its resources. Game images, BIOS files, firmware and keys are supplied by the user.

## Install on PS4

You need a PS4 already configured to run homebrew, your own PS2 BIOS, and one game image. Caliza PS4 reads `.iso`, `.chd`, `.cso`, and `.zso` files.

1. Download `caliza-v0.1.0-ps4.pkg` and its checksum file from the [release page](https://github.com/felipebasurto/caliza/releases/tag/v0.1.0). In the download directory, run `shasum -a 256 -c caliza-v0.1.0-SHA256SUMS.txt` on macOS, or `sha256sum -c caliza-v0.1.0-SHA256SUMS.txt` on Linux.
2. Install the package using your console's homebrew package installer. **Caliza** appears on the home screen.
3. For a fresh installation, put your BIOS in `/data/caliza/bios/` and your game image in `/data/caliza/games/`. For an upgrade from a development build, follow the migration steps below first.
4. Open Caliza, select a game, and press Cross. Start with an ISO on internal storage at native resolution. You can try USB and other formats after confirming the game starts.

### Upgrade from a development build

If you already use `/data/PCSX2/`, install Caliza and open it once. Boot migration copies settings, BIOS files, memory cards and other small user data into `/data/caliza/`. The copy is resumable, does not overwrite different destination files, does not write or delete the old folder, and leaves large games and texture packs referenced in place rather than duplicating gigabytes.

If migration was interrupted, open Caliza again and check the `[migrate]` entries in `boot.log`. Collisions are reported there; resolve them from a backup with both applications closed.

## Use the library

| Control | Action |
| --- | --- |
| Left / Right | Browse games or move through the focused row |
| L1 / R1 | Change library filters |
| Cross | Start the selected game |
| Square | Open the selected game's settings |
| Circle | Close the settings sheet |

You can change resolution, configure memory cards, and manage texture packs per game. **Smart Tune** is experimental. It tests graphics settings while you play, so run it in a demanding scene if you want the result to reflect gameplay.

## Storage on the console

| Path under `/data/caliza/` | Contents |
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

When reporting a problem, include the app version, PS4 model, system software version, game serial, storage type, and steps to reproduce it. Attach `boot.log` and `emulog.txt` from `/data/caliza/logs/`, or from `/data/caliza/` if they are stored directly there. Share logs with personal information removed. See the [hardware testing guide](ps4/CONSOLE-QA.md), the [console test procedure](ps4/COMPATIBILITY.md#required-console-smoke-protocol) and [known issues](ps4/KNOWN_ISSUES.md).

## Build from source

The PS4 target lives in [`ps4/`](ps4/). The new Caliza interface and companion panel live in [`caliza/`](caliza/). The inherited Orbis adaptation layer is transitional and is being renamed behind the Caliza backend boundary; [`REPO-MAP.md`](REPO-MAP.md) explains what is active, transitional or unused.

The build requires Linux or WSL, Clang with the OpenOrbis SDK, the Crema/CEMUPS4 runtime and sysroot, Mesa RADV, and the dependencies listed in [`Makefile.ps4`](ps4/Makefile.ps4). See the [PS4 platform notes](ps4/PS4_PLATFORM.md) and the [build setup](AGENTS.md#verification).

```sh
bash ps4/tools/run-host-tests.sh
bash ps4/build-ps4.sh
bash ps4/build-ps4.sh package
```

## Licence and credits

Caliza PS4 contains modified [PCSX2](https://github.com/PCSX2/pcsx2) code, copyright 2002–2026 the PCSX2 Dev Team and contributors. Caliza PS4 is licensed under the [GNU General Public License, version 3 or later](COPYING.GPLv3). The port builds on GPL-licensed Orbis adaptation work documented in the third-party notices, and uses the [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and [Mesa RADV](https://docs.mesa3d.org/drivers/radv.html). [Third-party notices](ps4/PORT-THIRD-PARTY.txt) cover additional components, including the optional paraLLEl-GS renderer.

Sony Interactive Entertainment owns the PlayStation marks. Caliza PS4 is independent of Sony Interactive Entertainment and the upstream PCSX2 project. Product names identify the platforms and software involved.

PS2UI is documented as visual research only. The new Caliza companion panel does not use PS2UI components, styles, markup or assets; any remaining inherited file is transitional and listed in [`REPO-MAP.md`](REPO-MAP.md).
