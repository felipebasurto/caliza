# Caliza brand guide

This guide covers the name, the mark, colour, voice and attribution. Interface layout and motion are covered in [`docs/estratos/STYLE-GUIDE.md`](docs/estratos/STYLE-GUIDE.md) and [`CALIZA-UI-ARCHITECTURE.md`](CALIZA-UI-ARCHITECTURE.md).

## 1. Name

**Caliza** (Spanish for *limestone*), said *ka-LEE-sa*.

Limestone builds up in layers over time. Caliza does something similar with a game collection: it brings old games back to the television in calm, legible layers. The interface takes its name, **Estratos** (*strata*), from the same idea.

- The product name is `Caliza`. Do not add a platform suffix such as "Caliza PS4", and do not use a `PS` prefix or any other Sony-like product structure.
- The lowercase machine name is `caliza`.
- The platform can be named where it is relevant, for example "Caliza for PS4" in a release title or "a PlayStation 2 emulator for PS4" in a description.

## 2. Mark

The mark is a limestone block cut into three strata. The layers have different thicknesses and are stepped slightly, so each lower layer shows a lit ledge. The block shares its isometric geometry with the floating cubes in the interface background.

| File | Use |
| --- | --- |
| `ps4/sce_sys/icon0.png` | PS4 home-screen icon, 512×512 RGB on the indigo field |
| `ps4/docs/caliza-icon-master.png` | 1024×1024 transparent master for the README and releases |
| `caliza/web/assets/caliza-mark.svg` | Colour mark for the companion panel and favicon |
| `caliza/web/assets/caliza-mark-mono.svg` | Single-colour mark for small or high-contrast uses |
| `Logo()` in `caliza/native/estratos_scene.cpp` | Runtime vector mark and the `CALIZA` wordmark in the console interface |

All of these come from one geometry in [`tools/make_brand_assets.py`](tools/make_brand_assets.py). To change the mark, change the script, run it, and keep `Logo()` in step with it.

Rules:

- Keep clear space of at least a quarter of the mark's height on every side.
- The wordmark is `CALIZA` in capitals, letter-spaced, and set to the right of the mark.
- Do not rotate, recolour, outline or add effects to the mark. On light backgrounds, use the mono mark in the dark indigo `#0C1040`.
- Do not use controller symbols, a PlayStation-like letterform, Sony's system gradients or the PCSX2 logo in the mark.

## 3. Colour

The palette is defined in `caliza/native/estratos_theme.h` and mirrored by the companion panel.

| Role | Hex | Use |
| --- | --- | --- |
| Void | `#03040E` | Deepest background |
| Deep indigo | `#0C1040` | Panels and the icon field |
| Nebula | `#3340D0` | Background light and glows |
| Glass | `#A9B0FF` | Floating geometry and panel edges |
| Ice | `#A5E7FF` | Focus, selection and the top face of the mark |
| Text | `#F2F5FF` | Primary text |
| Text dim | `#A3ACDA` | Secondary text |
| Amber | `#FFD092` | Warnings and setup steps |
| Danger | `#FF8F9E` | Errors and destructive actions |

Colour never carries meaning on its own; pair it with a label or an icon.

## 4. Voice

- Write in plain English. Say what happened and what the user can do next.
- Prefer everyday words: "save state" over internal terms, and "game folder" over paths. Technical names can appear as secondary text in advanced settings.
- Keep labels short and use sentence case for descriptions.
- User-facing copy, documentation and release notes are written in English only.

### Legal boundaries

Caliza is an emulator. All copy, documentation, screenshots and release notes must:

- state that BIOS files, games, firmware and keys are not included and must come from the user's own hardware and discs;
- never link to, name or hint at sources for BIOS files or game images;
- never describe how to modify console firmware; say only that a PS4 able to install homebrew packages is required;
- use only fictional games and procedural cover art in screenshots;
- never imply that Caliza is affiliated with Sony or the PCSX2 project.

## 5. Attribution

The source headers use `SPDX-License-Identifier: GPL-3.0-or-later` (or the upstream `GPL-3.0+`). Never remove upstream copyright or licence notices.

### About screen text

```text
Caliza is free software based on PCSX2 and contains modified PCSX2 code.
Copyright (C) 2002-2026 PCSX2 Dev Team and contributors.

Caliza is licensed under the GNU General Public License version 3 or later
(SPDX: GPL-3.0-or-later). This program comes with ABSOLUTELY NO WARRANTY.
This is free software, and you are welcome to redistribute it under certain
conditions. See COPYING.GPLv3 or the source repository for details.

Source code: https://github.com/felipebasurto/caliza

The PCSX2 project does not endorse Caliza. PlayStation, PS2 and PS4 are
trademarks of Sony Interactive Entertainment. Caliza is not affiliated with,
endorsed by, or sponsored by Sony Interactive Entertainment.

No BIOS files, games, keys, firmware or other proprietary console data are
included with Caliza.
```

### Package short notice

```text
Caliza is based on modified PCSX2 code, licensed GPL-3.0-or-later.
Source: https://github.com/felipebasurto/caliza. No BIOS, games or keys included.
```

### Placement

- Do not use PCSX2 in the logo, icon, home-screen title, package name or tagline.
- Do name PCSX2 plainly in the README, the About screen, the package description and technical documentation.
- Ship the full GPL text with release packages, or on the same release page.
- Keep credits for the inherited Orbis adaptation work, the OpenOrbis toolchain, Mesa RADV and paraLLEl-GS in the README and in [`PORT-THIRD-PARTY.txt`](PORT-THIRD-PARTY.txt) while their code remains in the build.

## 6. Repository and releases

- Repository: `https://github.com/felipebasurto/caliza`
- Short description: `A PlayStation 2 emulator for PS4, based on PCSX2.`
- Topics: `emulator`, `ps2`, `ps4`, `homebrew`, `pcsx2`, `openorbis`, `vulkan`, `gplv3`, `cpp`
- Git tags: `v0.1.0`, then semantic versions.
- Release title: `Caliza v0.1.0`
- Package: `caliza-v0.1.0-ps4.pkg`
- Checksums: `caliza-v0.1.0-SHA256SUMS.txt`

## 7. Names in code and on disk

| Use | Value |
| --- | --- |
| Display name and package title | `Caliza` |
| Machine name | `caliza` |
| PS4 title ID | `ZERU00001` (kept from the first release so updates install over it) |
| Data directory | `/data/caliza/` |
| BIOS directory | `/data/caliza/bios/` |
| Default games directory | `/data/caliza/games/` |
| Memory cards | `/data/caliza/memcards/` |
| Save states | `/data/caliza/savestates/` |
| Settings | `/data/caliza/settings/` |
| Patches | `/data/caliza/patches/` |
| Texture packs | `/data/caliza/textures/` |
| Logs | `/data/caliza/logs/` |
| Log prefix | `[CALIZA]` |

Older installations are migration inputs only. Compatibility names belong in `caliza/core/legacy_compat.h`. Migration copies data into `/data/caliza/` and never moves or deletes the old folder.
