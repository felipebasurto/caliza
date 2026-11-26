# Caliza interface architecture

This document is the orchestrator contract for the Estratos interface rebuild. It is a working inventory and boundary definition; hardware-only items are marked pending rather than treated as evidence.

## Fixed boundary

The new interface lives in `caliza/` and enters through an explicit backend contract in `caliza/core/contract.h`:

- the presentation reads immutable `Snapshot` values and calls `BrowseFolder` for read-only folder pages;
- every mutation is a `Command` posted to the backend queue;
- scanning and long-running work are incremental, cancelable and outside the input/render thread;
- saved folders are links, not copies; removing one never deletes images;
- an absent volume never invalidates saved sources or their library entries;
- `/data/caliza` is the canonical data root. `/data/caliza` is accepted only as a migration source and as documented compatibility state;
- legacy `Caliza/` ini keys, the installed title id and the repository slug are compatibility exceptions unless a later explicit decision changes them.

## Responsibilities and file ownership

1. Architecture and contracts (read-only first): inventory, dependency map, migration/storage risk register.
2. Visual structure and navigation: owns `caliza/core/ui_model*`, `caliza/native/**`, `caliza/host/**`, `caliza/shaders/**`, `tests/caliza/test_ui*`.
3. Functional flows: owns `caliza/core/backend*`, `caliza/core/storage*`, `caliza/core/data_root*`, `tests/caliza/test_contract*`, `tests/caliza/test_storage*`, `tests/caliza/test_migration*`.
4. Web panel: owns `caliza/web/**` initially and, at integration time, the replacement assets under `ps5/frontend/assets/web/`; no native C++ edits.
5. Orchestrator: console adapter, build integration, brand/migration integration, package, final evidence and independent review resolution.

Concurrent agents must not edit files outside their ownership. The orchestrator integrates after their reports.

## Current verified baseline

- Git HEAD before the rebuild: `81b3bbf` (`main`, clean at inspection time).
- Existing host validation: `bash ps4/tools/run-host-tests.sh /tmp/caliza-host-baseline` passed in the Linux `caliza-dev` container.
- The previous target enters through `fe::App` in `ps5/frontend/fe_ps5.cpp`; that cover-flow presentation must not be the PS4 entry after integration.
- Existing reusable non-presentation services include disc scanning/serial reading (`fe_games.*`), atomic ini editing and memory-card creation (`fe_settings.*`), option semantics (`fe_options.*`), cover location/decoding (`fe_covers.*`), HTTPS/report endpoints (`fe_web.*`) and fonts/text rasterization (`fe_text.*`).
- Existing PS4 storage discovery is synchronous and scans `/mnt/usb0-7` plus `/mnt/ext0-1`; there is no confirmed stable Orbis volume identity in current code.
- The existing boot process has save/load hotkey combinations and a re-exec return to menu, but no integrated rendered pause menu. Hardware evidence for booting a game, BIOS loading and save persistence remains pending.

## Estratos authority

`ps4/docs/estratos/STYLE-GUIDE.md` and the ten concept renders are the only visual/structural references. The reference mark is the three staggered diagonal ice-blue mineral layers with the CALIZA wordmark in the upper-left of `01-home.png`; final logo artwork is supplied separately by the user, so implementation leaves a clean asset slot rather than inventing final brand art. PS2UI is atmosphere/material research only: no inherited views, shaders, CSS, components, animations, sound resources or proprietary silhouettes in the new implementation.


## Brand and artifact rules

Compatibility exceptions expected at this stage: title id `ZERU00001`, content label `CALIZA00`, ini keys under `Caliza/`, the public repository slug and historical documentation. User-visible product copy, package title, logs, diagnostics, panel, README, generated assets and new artifact names use Caliza. Do not rewrite Git history and do not change the installed title id without an explicit post-upgrade-test decision.

## Migration requirements fixed for implementation

The migration library must be host-testable outside `/data`: fresh copy, interruption/retry, differing-file collision, symlink rejection, missing source and source preservation. It must never overwrite differing canonical data. Large games and texture packs need an explicit documented exception or a verified hardlink/no-duplication strategy; they must not be silently duplicated.

## Evidence required at delivery

Host contract/navigation tests, rendered captures of the ten Estratos states at 1080p and representative 720p/focus/empty/error states, brand audit, migration tests, OpenOrbis compile/package validation, package SHA-256, package content inspection and a clear separation of compile evidence, host evidence and physical-console evidence.


Clarification: the concept captures are orientation, not pixel authority. Interface completeness and coverage of all settings take priority over reproducing their exact layout. Final logo artwork is user-supplied; use a restrained placeholder slot until then.

## User-facing settings inventory extracted from the current panel

The rebuilt interface must preserve a clear path to at least these existing controls (names may be clarified in UI copy, but behavior must remain reachable):

- More game folders
- Overlays folder
- NFS shares
- BIOS folder
- Hide from the shelf
- Show hidden games
- Start from the cheat disc
- Resolution
- Aspect ratio
- Widescreen patches
- Display filter
- Frame generation
- Renderer
- Texture filtering
- Anisotropic filtering
- Blending accuracy
- Mipmapping
- Texture replacements
- Textures folder
- EE cycle rate
- EE cycle skip
- MTVU (VU1 on its own thread)
- GPU readbacks
- Game language
- Network adapter
- Host filesystem
- Fast boot
- Info box
- FPS graph
- Overlay picture
- Challenge icons
- Manual hardware fixes
- Half-pixel offset
- Native scaling
- Round sprite
- Align sprite
- Merge sprite
- Bilinear upscale
- Texture offset X
- Texture offset Y
- Auto flush
- Texture inside target
- Crop left
- Crop top
- Crop right
- Crop bottom
- Hold time
- Fast forward speed
- Swap sticks
- Invert left stick
- Invert right stick
- Left stick as D-pad
- Left stick dead zone
- Right stick dead zone
- Rumble
- Rumble strength
- Multitap
- Keyboard and mouse
- Mouse movement
- Mouse speed
- Mouse buttons

Additional surfaces: game folder links, BIOS folder, NFS shares, cheat disc, memory-card creation/slot assignment, patch and cheat groups, recommended/reset settings, HD texture packs, online patches, disc switching, RetroAchievements account/game achievements, reports/probe/log collection, PS2 system menu, Smart Tune, hidden games, host filesystem/network adapter, keyboard/mouse and multitap controls.
