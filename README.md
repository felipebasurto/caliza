<p align="center">
  <img src="ps4/docs/caliza-icon-master.png" width="128" height="128" alt="Caliza logo: a limestone block cut into three strata">
</p>

<h1 align="center">Caliza</h1>

<p align="center">A PlayStation 2 emulator for PlayStation 4, designed to be used from the couch with a controller.</p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#your-games-and-bios">Your games and BIOS</a> ·
  <a href="ps4/COMPATIBILITY.md">Compatibility</a> ·
  <a href="#build-from-source">Build from source</a>
</p>

<p align="center">
  <img src="caliza/host/evidence/1080p/01b-home-continue.png" width="900" alt="Caliza home screen with the Continue card, the main menu and the floating strata background">
</p>

Caliza runs PlayStation 2 games on a PS4 using the [PCSX2](https://github.com/PCSX2/pcsx2) emulation core. The interface is built for the television: large text, a clear focus, every action on a controller button, and settings that explain themselves.

> [!NOTE]
> Caliza is experimental. The current interface works in the host preview but has not been validated on a real console yet. See the [project status](ps4/STATUS.md) and the [compatibility notes](ps4/COMPATIBILITY.md) before you rely on it.

## Features

- **Library.** Shows the games from the folders you add, on internal storage or USB drives, with filters for recent games, storage location and games that need attention. Caliza reads `.iso`, `.chd`, `.cso` and `.zso` images in place and never moves or rewrites them.
- **Per-game settings.** Resolution, renderer, aspect ratio, patches and fixes can be set for all games or for one game. **Smart Tune** (experimental) tests graphics settings while you play.
- **Memory cards.** Create cards, choose which card goes in each slot, and pick cards for a single game.
- **Save states.** Save and load from controller shortcuts while a game is running. Fast-forward has an optional shortcut.
- **Controls.** Remap interface and emulator actions.
- **Accessibility.** Text size from 100% to 200%, high contrast, reduced motion and a thicker focus outline.
- **Companion panel.** A web page served by the console. Open it from a phone or computer on the same network to browse the library, change settings and collect diagnostic reports.

<p align="center">
  <img src="caliza/host/evidence/1080p/03-library-grid.png" width="440" alt="Library grid with filters">
  <img src="caliza/host/evidence/1080p/08-settings.png" width="440" alt="Settings screen">
</p>

## Requirements

- A PS4 that can install and run homebrew packages.
- A PlayStation 2 BIOS dumped from your own console.
- Game images made from discs you own.

## Your games and BIOS

Caliza does not include, download or link to BIOS files, firmware, keys or games, and it will not help you find them. You supply them yourself:

- **BIOS:** dump it from your own PlayStation 2, where the law in your country allows it.
- **Games:** make images from discs you have bought. Some publishers also sell or provide copies directly; use those according to their terms.

Do not share BIOS files or game images, and do not attach them to issues.

## Install

1. Download the latest `.pkg` and its checksum file from the [releases page](https://github.com/felipebasurto/caliza/releases).
2. Check the download. In the download folder, run `shasum -a 256 -c` with the checksum file on macOS, or `sha256sum -c` on Linux.
3. Install the package with your console's package installer. **Caliza** appears on the home screen.
4. Open Caliza. The first-run screen asks for two things:
   - a BIOS folder, for example `/data/caliza/bios/`;
   - at least one game folder, on internal storage or USB.
5. Select a game and press **Cross**.

Start with an ISO on internal storage at native resolution. Once that works, try USB drives, compressed formats and higher resolutions.

### Upgrading from an earlier build

Earlier development builds kept their data in a different folder. The first time you open the new version, Caliza copies settings, BIOS files, memory cards and other small files into `/data/caliza/`. The copy:

- can be interrupted and resumes the next time Caliza opens;
- does not overwrite files that already exist in the new folder;
- never changes or deletes the old folder;
- leaves games and texture packs where they are.

If something did not copy, check the `[migrate]` lines in `boot.log`.

## Controls

| Button | In menus | In the library |
| --- | --- | --- |
| Cross | Select | Start the game |
| Circle | Back | Back |
| Square | Details or reset | Game details |
| Triangle | — | Search |
| D-pad | Move | Move |

### During a game

| Default shortcut | Action |
| --- | --- |
| Options | PS2 Start |
| L3 + R3 + D-pad up | Save state to slot 1 |
| L3 + R3 + D-pad down | Load state from slot 1 |
| L3 + R3 + D-pad right | Switch to the next disc in the set |
| Hold L1 + R1 and click the touchpad | End the session and return to the library |
| Hold L2 + D-pad down for two seconds | Open the companion settings panel or show its QR code |

The native pause overlay is available in the host preview. PS4 gameplay currently uses the shortcuts above. Save, load and disc shortcuts can be changed under **Controls**.

## Storage on the console

Everything Caliza writes lives under `/data/caliza/`:

| Folder | Contents |
| --- | --- |
| `bios/` | Your BIOS |
| `games/` | Default game folder (you can add others) |
| `memcards/` | Memory cards |
| `savestates/` | Save states |
| `settings/` | Global and per-game settings |
| `patches/` | PCSX2 patch files |
| `textures/` | Texture replacement packs |
| `logs/` | Startup and emulation logs |

## Reporting a problem

Include the Caliza version, PS4 model, system software version, the game's serial, where the game is stored and the steps that lead to the problem. Attach `boot.log` and `emulog.txt` from `/data/caliza/logs/`, with personal information removed. The **Diagnostics** screen shows the companion panel address, where you can generate a report.

Useful references: [known issues](ps4/KNOWN_ISSUES.md), [performance tips](PERFORMANCE.md), [hardware testing guide](ps4/CONSOLE-QA.md).

## Build from source

The repository is organised as follows:

| Path | Contents |
| --- | --- |
| [`caliza/`](caliza/) | Interface, backend contract, settings, storage and migration, Vulkan renderer, host preview and companion panel |
| [`ps4/`](ps4/) | PS4 build, packaging, icon and validation tools |
| [`pcsx2/`](pcsx2/), [`common/`](common/) | PCSX2 emulation core |

The console platform layer and the remaining inherited services are described in [`REPO-MAP.md`](REPO-MAP.md), together with what is active and what is transitional.

**Host checks** (macOS or Linux):

```sh
bash tests/caliza/run-all.sh
python3 tests/caliza/audit_feature_parity.py
```

**Interface preview.** This renders every screen to PNG through Vulkan, without a console:

```sh
bash caliza/host/build-host.sh
bash caliza/host/capture-estratos.sh
```

**PS4 package.** You need Linux or WSL, Clang with the [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), Mesa RADV and the dependencies listed in [`ps4/Makefile.ps4`](ps4/Makefile.ps4). See the [platform notes](ps4/PS4_PLATFORM.md).

```sh
bash ps4/build-ps4.sh package
```

The logo, the console icon and the companion-panel marks are generated from one source by `python3 ps4/tools/make_brand_assets.py`.

## Licence and credits

Caliza is free software under the [GNU General Public License, version 3 or later](COPYING.GPLv3).

- The emulation core is modified [PCSX2](https://github.com/PCSX2/pcsx2) code, copyright 2002–2026 the PCSX2 Dev Team and contributors.
- The console layer builds on GPL-licensed Orbis adaptation work credited in the third-party notices, and uses the [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), [Mesa RADV](https://docs.mesa3d.org/drivers/radv.html) and the optional paraLLEl-GS renderer. See the [third-party notices](ps4/PORT-THIRD-PARTY.txt).
- Fonts: Roboto (Apache License 2.0) and PromptFont (SIL Open Font License 1.1).
- The interface, artwork and companion panel are original work for Caliza. See [`caliza/ASSETS.md`](caliza/ASSETS.md).

Caliza is an independent, non-commercial project. It is not affiliated with, endorsed by or sponsored by Sony Interactive Entertainment or the PCSX2 project. "PlayStation", "PS2" and "PS4" are trademarks of Sony Interactive Entertainment Inc. and are used here only to describe the platforms involved.
