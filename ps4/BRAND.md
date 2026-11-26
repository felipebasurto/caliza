# Zeruko Brand Identity
Status: recommended consumer identity for the native PS4 homebrew emulator port. Research date: 2026-11-26; practical naming research, not legal advice or a trademark clearance opinion.

## 1. Naming
### Decision
**Use `Zeruko` as the product name.**
Say it **zeh-ROO-koh**. It is short, crisp, and works in both English and Spanish. It is three syllables rather than the preferred two, but it wins decisively on distinctiveness: meaningful two-syllable candidates were already crowded, while Zeruko is ownable, easy to spell after one hearing, and has no exact emulator, console-homebrew, or significant game-software collision found in this research.
`Zeruko` is a [Basque word](https://hiztegiak.elhuyar.eus/es/celestial) meaning **celestial / of the sky**. That gives the brand a quiet orbital association without copying Sony's Orbis hardware codename. It also fits the emotional promise: an old game library lifted out of storage and placed back on the living-room screen.
### Brand line
- English: **Your classics, back in orbit.**
- Spanish: **Tus clásicos, de nuevo en órbita.**
Use the line in documentation, release notes, and splash artwork. Do not bake it into the executable name or title id.
### Candidate shortlist and collision research
Exactly three finalists were retained. Searches combined the exact quoted name with terms such as `software`, `app`, `game`, `emulator`, `console`, and `homebrew`, followed by targeted GitHub and PS4-homebrew-scene checks. Search results can change and absence of evidence is not evidence of absence.
| Candidate | Pronunciation | Meaning / story | Collision findings | Risk | Verdict |
|---|---|---|---|---|---|
| **Zeruko** | zeh-ROO-koh | Basque-derived “celestial”; sky, orbit, blue boot-light atmosphere | No exact emulator/homebrew product or significant commercial software product found. Results were weak: an unofficial fan-made world for the old game *Creatures* ([Creatures Wiki](https://creatures.wiki/Zeruko)) and player handles. `zeruko.com` appears occupied by unrelated non-software use, so do not depend on that exact domain. | **Low-to-moderate** | **Primary recommendation** |
| **Orbel** | OR-bel | Coined from orbital imagery; sounds like a small object circling a light | No game or emulator product found, but Orbel Ingeniería is an active industrial robotics company with its own Orbel System Manager software ([orbelgrupo.com](https://www.orbelgrupo.com/press/)), and `orbel.io` is a personal software portfolio. | **Moderate** | Alternate |
| **Reorb** | ree-ORB | “Return to orbit”: direct nostalgia/motion meaning | Only small/noncommercial collisions surfaced: a Ludum Dare tower-defense game ([ReOrb](https://jamludumdare.com/events/ludum-dare/46/reorb)) and old/current hobby repositories. No major commercial product found. The final `rb` cluster is awkward in Spanish, and orbit-derived naming is crowded in PS4 homebrew culture. | **Low-to-moderate legally, moderate linguistically** | Alternate |
### Why Zeruko wins
1. **Distinct in the actual niche.** No exact match was found among PS4 homebrew emulator names or in direct emulator searches. The scene already contains DuckStation, Flycast, PPSSPP, RetroArchV, GB-JIT, NPS64, PCPS4, EmuSuNi, NX on Orbis, and Orbis Eden; Zeruko does not collide with any of them.
2. **Safer than orbit-first names.** During elimination, obvious orbit/emotion/constellation names repeatedly collided with existing software or emulators. Examples checked and rejected included Orvo, Orbito, Orbem, Orbyx, Vela, Chroma, Neso, Lume, Leda, Pyxis, Indigo, and Ankaa/Anka-adjacent forms. Several were direct emulator or homebrew collisions.
3. **More ownable than common celestial words.** It is rare enough to search for, but still pronounceable and only six letters.
4. **No Sony-derived prefix or mark.** It does not use `PS`, `PlayStation`, `Orbis`, `Prospero`, or a Sony-like product-family structure.
5. **Bilingual in practice.** The vowels are stable across English and Spanish, and the central `u` prevents the English/Spanish vowel drift seen in names such as Hoshi, Avior, or Lyra.
### Digital availability plan
- Treat `zeruko.com` as unavailable or impractical.
- Recommended canonical GitHub namespace: **`github.com/zerukoemu/zeruko`**. A targeted exact search for `zerukoemu` found no project using that namespace, but GitHub indexing is not a reservation system. Create/verify the organization before announcing the name.
- If a web domain is wanted later, prefer a compound such as `zerukoemu.org`, `zeruko-emulator.dev`, or `zerukoemu.dev`; verify availability at purchase time. Do not use a domain implying Sony affiliation.
- Reserve the name only after a final trademark search in the intended distribution countries. Homebrew distribution is noncommercial, but software-class conflicts can still matter.

## 2. Visual identity
### Personality
Zeruko should feel like a calm night-sky shelf: precise, quiet, welcoming, and a little magical. It must not imitate Sony system software, the PlayStation logo, XMB waves, controller-symbol iconography, or PCSX2's logo.
Three personality words: **clear**, **weightless**, **reassuring**.
### Color palette
Designed for a dark 10-foot interface. Contrast ratios below are against Void Navy unless noted.
| Role | Hex | Use | Notes |
|---|---:|---|---|
| Void Navy | `#080B1A` | Main background | Deep blue-black, not pure black; avoids OLED smearing and harsh contrast. |
| Nebula Indigo | `#101A36` | Panels, side sheets, paused-game backdrop | Main elevated surface. |
| Shelf Graphite | `#1A2340` | Tiles, inactive cards, separators | Keep borders subtle; use luminance steps rather than heavy outlines. |
| Zeruko Blue | `#4DA3FF` | Focus ring, primary action, selected state | 7.45:1 on Void Navy. Use Void Navy text/icons on filled blue. |
| Aurora Violet | `#8B5CFF` | Secondary accents, enhancement badges | 4.73:1; use for large text/icons only, not dense body copy. |
| Phosphor Teal | `#42E8D4` | Success, “tuned”, healthy status | 12.79:1. Use sparingly so success remains meaningful. |
| Warm Amber | `#FFBF47` | Caution, storage low, experimental features | 11.92:1. Pair with an icon or label; never color alone. |
| Coral Red | `#FF5D73` | Errors and destructive actions | 6.58:1. Reserve for real errors; do not use decoratively. |
| Text White | `#F4F7FF` | Primary text | 18.26:1 on Void Navy. |
| Muted Text | `#A9B4D0` | Secondary text and metadata | 9.44:1 on Void Navy. |
Recommended proportions: 65% Void Navy/Nebula Indigo, 20% Shelf Graphite, 10% Zeruko Blue, 4% Aurora Violet, 1% status colors. Keep gradients very subtle; flat panels photograph better on televisions.
### Typography
- **Interface:** Inter, with Noto Sans as the multilingual fallback. Both are openly licensed and have strong screen rendering. Use Medium or SemiBold for shelf and menu labels; avoid thin weights.
- **Wordmark:** custom lettering based on the geometry of Space Grotesk or Inter, then manually adjusted. Do not simply type the name in a recognizable font and call it a logo.
- **Debug/log display:** JetBrains Mono or another open monospace font. Never expose monospace in normal consumer flows.
Minimum sizes for a 1080p 10-foot UI:
| Element | Minimum | Recommended |
|---|---:|---:|
| Body/help text | 26 px | 28-32 px |
| Shelf/game labels | 28 px | 30-34 px |
| Menu rows | 30 px | 32-36 px |
| Screen titles | 42 px | 44-56 px |
| Primary button captions | 30 px | 32-36 px |
| Focus stroke | 4 px | 4-6 px |
Use tabular numbers for storage, version, and performance readouts. Line length in help text should stay below roughly 55 Latin characters per line; Spanish copy is often 15-25% longer, so test both languages before locking layouts.
### Logo and icon concepts
**Primary concept: Orbiting Z.** A rounded, slightly forward-leaning lowercase `z` crosses a thin elliptical ring. The ring reads as both an orbit and a disc edge. A single small particle sits at the upper-right end of the ring. The negative space must remain readable at 32 px.
**Secondary concept: Shelf light.** Three horizontal rounded bars form a stylized `z`; the middle bar extends into a small circle, suggesting a game tile sliding back into place. This is the most literal shelf concept.
**Fallback small icon: Z spark.** A geometric `z` inside a rounded square with a tiny star/particle cutout. Use this when the ring would disappear at small sizes.
Execution rules:
- Master icon: 512×512 px PNG, centered in a rounded-square-safe composition with at least 12% inner padding.
- Export additional checks at 128, 64, 48, and 32 px. If the ring disappears at 48 px, use the fallback mark.
- Use flat color plus one subtle inner gradient at most. No bevels, glass blobs, lens flare, or pseudo-official console chrome.
- Do not use the four controller symbols, a PlayStation-like `P`, a PS2-style typeface, or Sony's blue system gradient.
- Do not reuse the PCSX2 logo. PCSX2 appears only as attribution text.
### Motion and sound
- Focus movement: 120-180 ms; shelf navigation: 180-240 ms; screen transitions: 220-300 ms.
- Use ease-out motion and very small scale changes. Focused tiles may scale to 1.08×; avoid large bouncing.
- Provide **Reduce motion** in Settings. When enabled, replace tile parallax and zooming with fades.
- UI sounds should be optional, short, and quiet: a soft tick for movement, a warm chirp for confirm, and a lower tick for back. Never play loud boot audio by default.

## 3. Gamepad menu and UX guide
### Experience principles
1. **One press to play.** The default highlighted object is the most recently played game; the primary action starts it.
2. **No emulator vocabulary in the main path.** Technical terms may appear in Advanced screens as secondary labels, never as the first explanation.
3. **Every state has a next step.** “Not working” is never the whole message; say what the user can do.
4. **Safe by default.** Experimental enhancements are reversible, visibly labeled, and never silently enabled.
5. **Controller only.** No mouse cursor, no keyboard dependency, no tiny touch targets.
6. **The shelf is the product.** Settings support the shelf; they do not compete with it.
### Home layout
```text
Zeruko                                             Storage 42%
Continue

      [small cover] [small cover] [ LARGE COVER ] [small cover]
               Game name                     Ready
               Last played: today            Memory card 1

Cross: Play     Triangle: Game options     Options: Settings     Circle: Back
```
Behavior:
- A horizontal cover shelf occupies the visual center. The focused cover is larger, brighter, and has a blue focus ring.
- Beneath it, show only three facts: game name, run state, and last-played or memory-card context.
- Put **Continue** first when a game can resume. Otherwise put the first ready game first.
- Do not show raw file paths or file extensions on the shelf. Put those in Game details.
- Use large, honest empty states. An empty shelf should teach the next step, not pretend the library is populated.
### Controller map
| Input | Primary behavior |
|---|---|
| Left stick / D-pad | Move focus; hold repeats at a comfortable speed |
| Cross | Confirm / Play |
| Circle | Back / Cancel |
| Triangle | Options for the highlighted game |
| Square | Contextual memory-card action when a memory-card panel is focused |
| L1 / R1 | Move between shelf filters or settings tabs |
| L2 / R2 | Jump one shelf page left/right |
| Options button | Open Settings |
| Options button in game | Open the quick menu; do not require keyboard shortcuts |
Follow the console's regional confirm/cancel convention if the platform layer can detect it. Otherwise ship Cross=confirm and Circle=back, and state this in Help. Every destructive action needs a confirm dialog with the safe choice focused by default.
### Menu structure
```text
Library shelf
├─ Game options (Triangle)
│  ├─ Play
│  ├─ Tune for this game
│  ├─ Picture and enhancements
│  ├─ Memory card
│  ├─ Instant saves
│  └─ Details
├─ Memory cards
├─ Settings
│  ├─ Display
│  ├─ Sound
│  ├─ Performance
│  ├─ Storage and folders
│  ├─ Network storage
│  ├─ Controller
│  └─ Advanced
└─ Help / About / Licenses
```
Advanced settings may use upstream-compatible names, but each row gets a plain-language subtitle. Example: title `EE cycle rate`, subtitle `Changes how much emulated processor work is attempted; can break games.`
### Game states
Use these exact user-facing states:
- **Ready** — the game file and required console startup files are present.
- **Needs startup files** — the BIOS requirement is not satisfied.
- **Missing file** — the indexed game file no longer exists.
- **Not checked yet** — no compatibility/tuning result exists.
- **May slow down** — measured or known performance problems.
- **Unsupported file** — the file type or image structure cannot be loaded.
Do not use “compatible/incompatible” as a binary promise. Compatibility is per game, per scene, and sometimes per revision.
### Per-game options in plain language
Recommended labels:
- **Play**
- **Start from the beginning** rather than “reset”
- **Continue where you left off** rather than “load state”
- **Tune for this game** rather than “auto configure”
- **Picture quality**: `Original`, `Sharper`, `Custom`
- **Widescreen picture**: `Automatic`, `Off`, `On`
- **HD artwork**: `Off`, `On` when a texture pack is installed
- **Extra in-between frames**: `Off`, `On (experimental)` for frame generation
- **Memory card**: `Memory card 1`, `Memory card 2`, `Shared card`, `Game-specific card`
- **Instant saves** for save states
- **Advanced game settings** for upstream technical options
### Smart tune UX
“Smart tune” is a strong feature, but it must not sound magical or risk-free.
Button label: **Tune for this game**
First-run explanation:
```text
Zeruko will try a few safe performance and picture settings for this game.
Your normal settings are kept. You can stop at any time, and you can undo
the recommended setup afterwards.
```
Progress copy:
```text
Trying settings for this game...
This can take a few minutes. The game may restart during the test.
```
Result states:
- **Recommended setup found** — show three bullet points in plain language and an `Apply` action.
- **No safe improvement found** — reassure the user and keep current settings.
- **Needs manual attention** — link directly to the relevant setting, not a generic settings page.
Never enable frame generation or HD artwork silently as part of tuning. They can be recommended, but each needs its own explicit consent and an `experimental` or `optional add-on` label.
### Plain-language replacements
| Avoid as the first label | Use instead |
|---|---|
| BIOS | Console startup files |
| ROM / ISO / CHD | Game file or disc copy |
| Emulator | Game player, or avoid the term entirely in consumer copy |
| Renderer | Graphics method |
| Internal resolution | Picture detail |
| Upscaled | Sharper picture |
| Frame generation | Extra in-between frames |
| Input latency | Controller response delay |
| Shader | Screen effect |
| Save state | Instant save |
| Memory card image | Memory card file |
| Widescreen patch | Widescreen picture fix |
| Texture pack | HD artwork pack |
| NFS share | Network folder |
| JIT / dynarec | Fast processor translation (Advanced only) |
| EE / IOP / VU / GS | Original-console part names (Advanced only); explain in subtitles |
| Compatibility | How well it runs |
| FPS | Frames each second, only if the number must be shown |
| Aspect ratio | Screen shape |
| Deinterlacing | Interlaced-picture cleanup |
| Config / INI | Settings file |
Technical terms can appear parenthetically after the plain term when needed for searchability or support. Example: `Console startup files (BIOS)`.
### First-run onboarding
Target: fewer than four required decisions, and no account creation.
1. **Welcome and language.** Show `English` and `Español` first; more languages can follow. Default to the console language when detectable.
2. **Controller check.** Ask the user to press Cross once. If input works, continue automatically after a short confirmation.
3. **Console startup files.** Explain that Zeruko does not include them. Show the exact folder `/data/zeruko/bios/` and a `Scan again` action. Allow the user to continue and finish this later.
4. **Games.** Show the default folder `/data/zeruko/games/` plus optional network-folder setup. Provide `Scan for games` and `I will do this later`.
5. **Scanning.** Show count and current folder, not raw mount details. Let scanning continue in the background after the first usable game appears.
6. **First success.** Highlight the first ready game with one line: `Press Cross to play.` Do not force the user through tuning.
7. **Optional tune offer.** After the first successful launch, offer tuning once. If dismissed, do not nag on every launch.
The first boot may mention legal responsibility in plain language, but do not use a wall of text. Put full licenses in Help/About and the README.
### Required empty/error copy
Use close variants of these strings:
```text
Zeruko needs console startup files from your own PlayStation 2.
Copy them to /data/zeruko/bios/, then choose Scan again.
```
```text
No games found yet.
Copy game files you legally own to /data/zeruko/games/, then choose Scan again.
```
```text
This game file is missing.
It may have been moved, renamed, or removed. Choose Scan for games to update the shelf.
```
```text
Zeruko is running low on storage.
Free some space before copying memory cards, covers, or HD artwork.
```
```text
This game may slow down on this console.
You can try Tune for this game, or lower picture detail in Game options.
```
Never say “invalid BIOS” when the real problem may be a missing file, wrong folder, unsupported region, or corrupt dump. Diagnose narrowly and state what is known.
### Accessibility and localization
- Full D-pad navigation must mirror stick navigation.
- Focus must always be visible, including on covers and inline buttons.
- Do not communicate status by color alone; pair color with text and an icon.
- Provide text scaling of at least 100/125/150%.
- Provide Reduce motion and a UI sound volume control.
- English and Spanish strings should be written together from the start. Avoid idioms, puns, and long compound nouns.
- Button prompts should use shapes/icons plus names at first use. Users learning a controller should not have to guess what `X` means on a Sony-style pad.

## 4. GPLv3 and upstream attribution
The source headers in this tree use `SPDX-License-Identifier: GPL-3.0+`; use modern public wording `GPL-3.0-or-later` while retaining upstream license text and notices. The consumer brand must never hide the PCSX2 lineage.
### Exact in-app About text
Use this block verbatim in **Help / About / License**, replacing only the version line as versions change:
```text
Zeruko is free software based on PCSX2 and contains modified PCSX2 code.
Copyright (C) 2002-2026 PCSX2 Dev Team and contributors.

Zeruko is licensed under the GNU General Public License version 3 or later
(SPDX: GPL-3.0-or-later). This program comes with ABSOLUTELY NO WARRANTY.
This is free software, and you are welcome to redistribute it under certain
conditions. See COPYING.GPLv3 or the source repository for details.

Source code: https://github.com/zerukoemu/zeruko

PCSX2 is used under the terms of its license. The PCSX2 project does not
endorse Zeruko. PlayStation, PS2 and PS4 are trademarks of Sony Interactive
Entertainment. Zeruko is not affiliated with, endorsed by, or sponsored by
Sony Interactive Entertainment.

No BIOS files, games, keys, firmware, or other proprietary console data are
included with Zeruko.
```
### Exact package/installer short notice
Use this where only one short paragraph fits:
```text
Zeruko is based on modified PCSX2 code, licensed GPL-3.0-or-later.
Source: https://github.com/zerukoemu/zeruko — No BIOS, games or keys included.
```
### Exact GitHub README attribution section
Use this verbatim as the README `Licence and attribution` section:
```markdown
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
```
### Attribution placement rules
- Do not use PCSX2 in the product logo, icon, home-screen title, package name, or marketing tagline.
- Do use PCSX2 plainly in the README, About screen, package description, and technical documentation.
- Keep the full GPL text in release packages or provide it alongside them in the same release page.
- Preserve upstream copyright and license headers in source files. Add new headers for new files; do not replace upstream notices.
- If PS5SX2 or other upstream code remains in the shipped binary, keep those project credits in the same About/Licenses area rather than only in the README.

## 5. Repository identity
### Canonical repository
- **Organization/owner:** `zerukoemu`
- **Repository:** `zeruko`
- **Canonical URL:** `https://github.com/zerukoemu/zeruko`
- **Display title:** `Zeruko`
- **Short description:** `Couch-friendly PlayStation 2 emulator for jailbroken PlayStation 4 consoles, based on PCSX2.`
- **README badge alt text:** `Zeruko`, not a Sony-like acronym.
Verify the organization name before public announcement. If it is unavailable, use `zerukoemu` plus a controlled personal namespace rather than inventing a second product name.
### GitHub topics
Use these topics:
```text
emulator
ps2
ps4
ps4-homebrew
homebrew
pcsx2
openorbis
vulkan
retro-gaming
gplv3
c-plus-plus
```
Do not use topics that imply piracy, official Sony status, or commercial console affiliation.
### README structure
Use this order:
1. Logo/banner and one-sentence product description.
2. Status warning: experimental, expected performance range, supported consoles.
3. What Zeruko does in three bullets: shelf, gamepad-only control, per-game tuning.
4. What is not included: BIOS, games, keys, firmware, proprietary data.
5. Requirements: jailbroken console capable of installing homebrew packages, own legally dumped startup files, own legally dumped game files.
6. Install: package installation, `/data/zeruko/bios/`, `/data/zeruko/games/`, first scan.
7. Controls: one small table with Cross/Circle/Triangle/Options.
8. Storage layout: full `/data/zeruko/` tree.
9. Features: Smart tune, memory cards, HD artwork, extra in-between frames, network folders.
10. Compatibility and performance expectations.
11. Building from source.
12. Troubleshooting and log locations.
13. Credits.
14. Licence and attribution (exact section above).
### Release naming
- Git tag: `v0.1.0`, then semantic versions.
- Release title: `Zeruko v0.1.0`
- Package asset: `zeruko-v0.1.0-ps4.pkg`
- Checksums asset: `zeruko-v0.1.0-SHA256SUMS.txt`
- Do not use `PS4SX2`, `PCSX2`, or a `PS` prefix in consumer-facing asset names.

## 6. Naming in code and on disk
| Use | Exact value |
|---|---|
| Display name | `Zeruko` |
| Lowercase machine name | `zeruko` |
| Canonical repository | `zerukoemu/zeruko` |
| Internal bundle identifier | `io.github.zerukoemu.zeruko` |
| PS4 title id prefix | `ZERU` |
| Example PS4 title id | `ZERU00001` |
| Package display title | `Zeruko` |
| Data directory | `/data/zeruko/` |
| BIOS directory | `/data/zeruko/bios/` |
| Default games directory | `/data/zeruko/games/` |
| Patch directory | `/data/zeruko/patches/` |
| Cover/artwork cache | `/data/zeruko/covers/` |
| Memory-card directory | `/data/zeruko/memcards/` |
| Per-game settings directory | `/data/zeruko/gamesettings/` |
| Log directory | `/data/zeruko/logs/` |
| Log prefix | `[ZERUKO]` |
| Config file stem | `zeruko` |
| Temporary directory | `/data/zeruko/tmp/` |
| Crash dump directory | `/data/zeruko/crashes/` |
The platform may still require executable filenames such as `eboot.bin`; those are packaging details, not brand names. Internal process names and CMake targets may use lowercase `zeruko`, but user-facing strings must use `Zeruko`.
### Migration from the development identity
The current development tree uses PS4SX2/PCSX2-style paths. Do not silently abandon user data:
1. On first run, if `/data/PCSX2/` exists and `/data/zeruko/` does not, offer **Copy existing settings and memory cards**.
2. Copy; never move or delete the old directory automatically.
3. Mark migrated per-game settings as `Not checked yet` until each game has run once.
4. Keep one small migration note in the release notes so experienced users understand where files went.

## 7. Immediate action checklist
1. Verify and reserve `github.com/zerukoemu` before announcing the brand.
2. Run a proper trademark screening for `Zeruko` in software/game-emulation classes.
3. Replace the consumer display name everywhere outside technical attribution; keep PCSX2 attribution visible.
4. Design the Orbiting Z master icon at 512×512 and test it at 48/32 px.
5. Implement `/data/zeruko/` with migration import from `/data/PCSX2/`.
6. Rewrite first-run and empty-state strings using the exact plain-language patterns above.
7. Add the exact licence text to the app's About/License screen and README.
8. Use `ZERU` as the title-id prefix and `/data/zeruko/` as the data root.

## Resumen en español
La recomendación principal es **Zeruko**: se pronuncia «ze-RU-ko», significa aproximadamente «celestial» y no usa marcas ni prefijos de Sony. Las alternativas son **Orbel** y **Reorb**, pero ambas tienen desventajas: Orbel ya aparece en software/robótica industrial y Reorb termina en un grupo consonántico incómodo en español.
La identidad visual debe ser una interfaz oscura, tranquila y legible desde el sofá, con azul `#4DA3FF` como color principal. El logotipo recomendado es una «z» cruzada por un anillo orbital muy fino. La interfaz se controla solo con mando: Cruz confirma o inicia, Círculo vuelve, Triángulo abre opciones del juego y Options abre ajustes. La documentación y la interfaz deben evitar jerga: «archivos de arranque de la consola» en lugar de BIOS como primera explicación, «partida instantánea» en lugar de save state y «carpeta de red» en lugar de NFS.
El repositorio recomendado es `github.com/zerukoemu/zeruko`. El identificador interno es `io.github.zerukoemu.zeruko`; el prefijo de title id es `ZERU`; la carpeta de datos es `/data/zeruko/`; y el prefijo de registro es `[ZERUKO]`. La atribución a PCSX2 y la licencia GPL-3.0-or-later deben aparecer exactamente en la pantalla Acerca de y en el README, junto con el aviso de que no se incluyen BIOS, juegos, claves ni firmware.
