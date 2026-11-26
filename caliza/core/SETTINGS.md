# Caliza PS4 settings and catalog schema

This directory contains the portable schema inventory shared by the future native interface, the
companion web panel, host tests and the backend/orchestrator:

- `settings_schema.h/.cpp` — user-facing settings, composite/dynamic rows and actions;
- `catalog_model.h/.cpp` — scan-root, game identity, media, multi-disc and cached catalog shapes;
- `tests/caliza/test_settings_schema.cpp` and `.sh` — dependency-free inventory and compatibility tests.

The schema is data plus small parsing/validation helpers. It does **not** perform filesystem IO,
scan storage, write ini files, talk to the emulator, download texture packs or implement an HTTP/UI
layer. Platform adapters own those side effects.

## Inventory snapshot

Current pinned inventory:

- 21 groups;
- 116 setting definitions;
- 48 action definitions;
- 69 explicit legacy-key migration pairs;
- canonical scan roots: `/data/caliza/games`, `/data/caliza`, `/mnt/usb0`-`/mnt/usb7`,
  `/mnt/ext0`-`/mnt/ext1`;
- legacy internal compatibility roots: the retired internal data root and its `games/` child, both
  constructed at runtime by `legacy_compat.h` rather than spelled as a contiguous retired brand string;
- external-drive layouts include the generic `games`, `DVD`, `CD`, `PCSX2` and canonical lowercase
  `caliza` forms, plus the retired lowercase folder form constructed by `legacy_compat.h`.

The tests pin the complete compatibility-key set and FNV-1a metadata hashes. A change to a label,
choice, default, scope, inheritance rule, range, timing, confirmation or action payload is intended
to be a deliberate schema change that updates the hash.

## Compatibility rules that must not be broken

1. Active product-owned ini keys use the canonical `Caliza/` namespace. Examples:
   `Caliza/GameFolders`, `Caliza/Renderer`, `Caliza/ButtonCross`, `Caliza/CheatDisc` and
   `Caliza/ElfDisc`. These are the keys new files and active schema lookups use.
2. The retired brand namespace is not an active schema keyspace and is never spelled as one
   contiguous literal in source or user-visible copy. Pre-transition installations are migration
   input only: `legacy_compat.h` constructs the retired namespace and data root at runtime,
   `LegacyKeyMigrations()` enumerates the explicit key pairs that existed then, and
   `MigrateLegacyKey()` translates one key. `legacy::MigrateIniText` is the file-text translation
   used while copying or migrating an old installation. Lookup, parsing, mutation and writing operate
   on canonical keys and do not migrate implicitly.
3. New Caliza-only keys, including the `Caliza/Ui...` accessibility settings, have no legacy
   counterpart and are deliberately absent from `LegacyKeyMigrations()`. Do not synthesize one.
4. Bare ini keys remain PCSX2 `EmuCore/GS` keys. `upscale_multiplier` means
   `EmuCore/GS/upscale_multiplier`; `EmuCore/GS/` is stripped when canonicalizing a parsed key.
5. Keys in other sections retain their section form, such as `EmuCore/Speedhacks/vuThread`,
   `DEV9/Eth/EthEnable`, `Filenames/BIOS` and `MemoryCards/Slot1_Filename`.
6. `Patches/Enable` and `Cheats/Enable` are repeated list keys, not scalar assignments. They are
   rejected by the ordinary `set key=value` mutation path and use `patch+`, `patch-`, `cheat+` and
   `cheat-` mutations instead.
7. `/data/caliza` is the canonical data root. The retired internal root constructed by
   `legacy_compat.h` is migration/compatibility state; it must remain readable while migration is
   incomplete, but new writes should target the canonical layout unless an integration explicitly
   performs migration.
8. The legacy web game id is exactly the full file basename (`Game.iso`). The legacy settings stem
   is the basename without its final extension (`Game`). Neither is a stable identity across two
   files with the same name; use the stable catalog identity internally and keep these only as
   compatibility aliases.
9. RetroAchievements credentials are not ordinary settings. Username/password input is payload for
   the local sign-in action, and the token belongs to platform secret storage. The LAN web mutation
   surface must not accept `Achievements/...` scalar keys; `SafeKey` deliberately rejects them.

## Setting metadata model

Each `SettingDef` has:

- `id`: stable cross-interface schema id, independent of storage spelling;
- `compatibility_key`: exact active ini key;
- `label`, `hint`, `short_format`, `later_label`, `placeholder`: presentation metadata;
- `group_id` and `tab`: grouping independent of interface implementation;
- `scopes`: global, per-game and/or running-VM applicability;
- `inheritance`: `Fixed` or `InheritGlobal`;
- `type`: boolean, choice, text, path-list, number, memory-card-file or group-list;
- `presentation`: toggle, segmented choices, choice list, text, composite or hidden;
- `apply_timing`: observed backend timing, not merely the legacy restart badge;
- `default_value` and static `choices`, or `dynamic_choices` for catalog/BIOS/memory-card values;
- numeric `number_min/max/step` where applicable;
- `manual_hardware_fix` and `manual_hardware_fix_master` for the PCSX2 manual-fix surface;
- `player_owned` for state preserved by recommended-settings replacement;
- `implementation_detail` for keys coordinated by another row/action rather than rendered alone;
- `pair_id`/`pair_part` for two-button combo rows;
- `note` for non-user-facing integration caveats.

### Scope and inheritance

- Most emulation, display, graphics, performance, game, overlay, crop and control rows are both
  global and per-game and inherit global values before falling back to the schema default.
- Folder and NFS configuration, show-hidden-games, cheat-disc selection, system-menu launch and
  accessibility additions are global-only.
- `Caliza/HideGame`, manual hardware fixes, ELF disc selection, patch/cheat group membership and
  texture-pack actions are game-only.
- Memory-card slot assignment, BIOS selection and ordinary controls can exist at either layer.
- Hidden-game state is intentionally `Fixed`: a global hide key must not hide every game.
- `implementation_detail` rows may still inherit; for example a memory-card enable/file pair can be
  inherited together even though the UI renders one composite row.

Effective scalar state is:

```text
per-game settings/<settings-stem>.ini
  -> global gs.ini when inheritance is InheritGlobal
  -> schema default
```

This mirrors the legacy own/global/default display. Patch and cheat group membership is a repeated
list projection and is handled separately from scalar `EffectiveValue`.

### Apply timing

`ApplyTiming` values mean:

- `Live`: the running game or live frontend can observe the change through the settings reload path;
- `NextLaunch`: read while booting a game/ELF/system menu;
- `NextApplicationStart`: scan/mount configuration read at application startup;
- `NextMenuOpen`: shelf visibility state applied when the menu is opened again (or when the native
  sheet closes);
- `ImmediateUi`: interface-local accessibility/presentation state;
- `RunningCommand`: an action against a running VM rather than a stored emulation setting.

Important caveats already encoded in metadata:

- Renderer and MTVU are `NextLaunch`; the live loader refuses or defers those classes of change.
- The legacy UI labels the textures folder as a restart-ish setting, but the running frontend polls
  it through the live options reader; the schema marks it `Live` and carries the caveat in `note`.
- Game folders, BIOS folder and NFS shares are `NextApplicationStart`.
- Hide/show-hidden games are `NextMenuOpen`.
- Patch and cheat group changes are live through the explicit patch reload path.
- Memory-card slot assignment is live: PCSX2 compares slot enable/file state on settings reload, ejects changed
  cards and reopens the card set.

### PS4-specific rows and preset filtering

The schema includes the PS4 web panel's MTVU implementation rows:

- `EmuCore/Speedhacks/OrbisMTVUSpin`, default `true`, live, player-owned;
- `EmuCore/Speedhacks/OrbisMTVUBatch`, default `false`, live, player-owned;
- `EmuCore/Speedhacks/OrbisMTVURingKB`, numeric 256-2048 KiB in 256 KiB steps, default `2048`, live,
  player-owned.

On PS4 the MTVU master row defaults on, renderer choices are Vulkan/paraLLEl-GS/Software, and EE
cycle-rate labels carry the extra CPU warning. Recommended presets came from a higher-power inherited tuning baseline: the PS4
`ApplyRecommended` implementation drops preset `upscale_multiplier` and `EECycleRate` lines rather
than writing overly costly 4x/6x or 130%+ values. The filter applies only to generated recommended
text, never to the user's existing files.

Smart Tune writes `upscale_multiplier`, `MaxAnisotropy` and hidden `CASMode`. `CASMode` is in the
registry as an implementation detail so Smart Tune and settings serialization do not treat it as an
unknown key; it is not an independent console/web row.

## Composite and dynamic rows

The static choice rows are not the whole user-facing settings surface. The registry also models:

- `Caliza/ElfDisc` — game-only ELF disc association; dynamic choice of legacy catalog basenames or
  empty/no disc; next launch.
- `Caliza/CheatDisc` — global cheat-disc image selection; dynamic catalog basename; next launch.
- `Filenames/BIOS` — dynamic BIOS filename, global/per-game; empty means automatic; next launch.
  Platform enumeration currently treats regular files around 4-8 MiB as BIOS candidates.
- `MemoryCards/Slot1_Filename` + `MemoryCards/Slot1_Enable` and slot 2 equivalents — composite
  memory-card rows. `Enable=false` with no filename override represents “No card”; defaults are
  `Mcd001.ps2` and `Mcd002.ps2`. Creation sizes are 8/16/32/64 MiB.
- `Patches/Enable` and `Cheats/Enable` — repeated group-list keys. Enabled groups whose files later
  disappear must remain visible so users can disable them.
- `EmuCore/EnableCheats` — hidden master switch coordinated by cheat rows; not a separate user row.
- Accessibility additions `Caliza/UiTextScalePercent`, `Caliza/UiHighContrast`,
  `Caliza/UiReduceMotion` and `Caliza/UiLargeFocus` — new global immediate-UI settings with no
  pre-transition counterpart. They are absent from `LegacyKeyMigrations()`. Integration must include
  them in player-key preservation before recommended resets can be considered correct for them.

## Serialization and mutation compatibility

`ParseSettingsDocument` produces a semantic projection of canonical or already-migrated text in the
line-oriented ini format:

- comments and unknown lines are not represented in the projection;
- scalar keys are canonicalized and the last occurrence wins;
- patch/cheat enable lists are unique in first-seen order;
- legacy disable-list lines are ignored by this UI projection.

The parser does not silently translate pre-transition brand keys. A migration integration must first
use `legacy::MigrateIniText` for copied file text, or translate individual keys through
`MigrateLegacyKey()`/`LegacyKeyMigrations()`, and only then parse or write canonical state. Unknown
unmapped lines remain backend migration state rather than becoming active schema keys.

`SerializeSettingsDocument` emits a canonical semantic document for tests and future writers. It is
**not** a lossless rewrite of an existing file. The existing writer's preservation of comments,
unknown lines and sibling-temp-file atomic rename remains a backend responsibility. Do not replace it
with this projection without an orchestrator-reviewed migration.

`SplitFolderList`/`JoinFolderList` preserve the legacy folder-list behavior:

- separators are semicolon, pipe or newline;
- edge whitespace is trimmed;
- trailing slashes are removed except for `/`;
- duplicates keep first position;
- serialization joins with semicolons.

Web mutation language accepted by `ParseMutationLine`/`ParseMutationBatch`:

```text
set key=value
unset key
patch+ <group>
patch- <group>
cheat+ <group>
cheat- <group>
recommended
```

Validation preserves legacy constraints: scalar keys use letters/digits/`_`/`/`, are at most 80
characters, reject repeated list keys and reject the `Achievements` section; values are at most 200
characters and cannot contain newline, carriage return or `#`. Choice validation accepts exact,
case-insensitive and numerically equivalent legacy spellings, including percent spellings such as `20%`
for fractional pressure choices, and normalizes to the schema choice.

`RegistryToJson` and `ActionsToJson` export schema-shaped snapshots for adapters and tests. They contain
active canonical keys only; legacy-key migration remains a backend concern through `LegacyKeyMigrations()`
and `legacy_compat.h`. They are serialization exports, not a promise to accept arbitrary JSON back.

## Action semantics

`ActionDef` centralizes the action rows currently split across the native sheet and web panel:
library launch/system menu/Smart Tune, settings set/reset/recommended, folder/BIOS/NFS management,
memory-card assign/create, patch/cheat toggles and online fetch, texture-pack select/download/retry/
cancel/delete, running-VM save/load/fast-forward/disc/menu actions, RetroAchievements account/list
actions, report generation and tester notes.

Important confirmation behavior:

- two presses within four seconds: PS2 system menu, Smart Tune, apply recommended settings, reset
  settings scope, reset controls, texture-pack cancel/delete and RetroAchievements sign-out;
- texture-pack cancel/delete use the same armed pattern but historically on Triangle;
- save state and load state can overwrite state or discard unsaved progress and are marked
  `destructive=true`, but the legacy combo path currently has **no** confirmation;
- return-to-library can discard unsaved progress and also has no legacy confirmation;
- ordinary setting changes, folder unlinking, BIOS/memory-card assignment, patch/cheat toggles,
  texture-pack download/retry and report generation do not currently confirm.

The schema records risk and existing confirmation separately. Do not silently treat `destructive` as
implying that confirmation already exists.

## Catalog model

`catalog_model` separates four identities:

1. `stable_id` — preferred internal identity. With a known volume id and root it is
   `volume:<volume-id>:<relative path>`; otherwise it is `path:<normalized absolute path>`. Treat it
   as opaque. Adapters must provide a collision-free volume token when using volume form.
2. `legacy_web_id` — exact full basename used by the old web API.
3. `settings_stem` — exact basename-without-extension used in `settings/<stem>.ini`.
4. `content_key` — media + serial + byte size diagnostic key; useful for collision evidence, not a
   replacement for location identity.

`GameEntry` carries path/file/title/region/extra/serial/source root/bytes/media kind/hidden/damaged/
damage reason/badges/cover path/disc number/disc-set key/scan revision. `MediaKind` covers disc
images, ELF executables and the PS2 system menu pseudo-entry.

File classification matches the legacy scanner:

- `.iso`, `.chd`, `.cso`, `.zso`, `.bin`, `.img` and `.elf` names, case-insensitive;
- hidden dot files excluded;
- `.bin`/`.img` are only candidates: platform scanning must still enforce the 16 MiB floor and
  ISO9660/data-disc validation;
- `.elf` is only catalog media after ELF magic validation;
- scan descent is one folder level, at most 64 subfolders, skipping emulator/system folders.

`ParseDisplayTitle`, `DiscNumber`, `DiscSetKey` and `CatalogLess` mirror the legacy Redump-style
presentation, multi-disc inference and sorting rules. `.m3u` expansion remains scanner IO, but the
scanner should store a common `disc_set_key` in the entries it proves belong together.

`GameEntryToJson`/`GameEntryFromJson` are immutable JSONL cache helpers. The parser is deliberately
schema-shaped: it validates required fields, rejects duplicates/trailing data and skips unknown
fields, but it is not a general JSON library.

## Integration guidance

### Native interface

Render groups, rows, choices, hints, ranges, pair metadata and confirmation badges from `Settings()`
and `Actions()`. Do not copy the tables into native code. Composite rows can use several hidden or
coordinated definitions but should expose one user row where the legacy UI did so (memory cards are
the primary example). Native-only glyphs can remain a presentation layer concern; the schema labels
are plain text.

When a row is changed, post the corresponding action/command rather than writing ini files directly.
Use `ValidationResult` before displaying a mutation as accepted. For dynamic choices, intersect the
schema metadata with live BIOS/catalog/memory-card state but preserve an already-set unlisted value
so users can see and repair it.

### Web panel

The web panel should consume `RegistryToJson`/`ActionsToJson` or a backend projection of the same
data. It must not resurrect a hardcoded `GROUPS` table. Keep server-side validation and mutation
parsing in C++; browser JavaScript is presentation only. Dynamic rows still require backend-provided
catalog/BIOS/card/patch/cheat state. Credentials must remain local-only and outside the LAN settings
mutation surface.

### Backend/orchestrator and `contract.h`

`contract.h` currently exposes a weaker `SettingItem` model. Adapt rather than duplicate:

- `SettingItem.id` should be the schema `id`, not the compatibility key;
- keep the compatibility key in the backend command payload (`SetSetting`/`ResetSetting`) because
  storage is key-compatible;
- map schema type/presentation to `SettingKind` and use schema choices/ranges/defaults;
- derive `restart_required` from `apply_timing`, but do not collapse all timing classes into a
  boolean internally;
- expose `player_owned`, inheritance, destructive/confirmation metadata and pair/composite metadata
  through extensions or UI-side registry lookup until `contract.h` is extended;
- action names in `ActionDef::command_type` use the contract command when one exists. Some legacy
  actions (online patch fetch, texture-pack operations, companion panel, achievements services) do
  not yet have complete contract commands; the orchestrator must add or intentionally map them
  instead of treating the strings as already implemented API.

The schema does not endorse the current lossy runtime bitmask. Runtime actions are described with
payloads and risk metadata; the backend should provide command completion events and errors before a
snapshot revision claims their visible effect.

### Migration and scanning

Use canonical `/data/caliza` roots for new scans. Union in `LegacyCompatibilityScanRoots()` while an
unmigrated installation under the retired internal root is supported. External-drive expansion
intentionally includes both canonical lowercase `caliza` names and the retired lowercase folder names
constructed by `legacy_compat.h`. Scanning must be cancellable, revisioned and represented as
immutable snapshots; the legacy synchronous scan in the web server is a hazard, not a model to copy.

## Known gaps and hazards not solved here

- Smart Tune is experimental and needs proper hardware testing on representative games before its measured
  recommendations are treated as reliable.
- There is no real legacy pause menu. `PauseVm`/`ResumeVm` in the contract should not be exposed as
  currently working legacy behavior without orchestrator confirmation.
- Save/load/return-to-menu data-loss risk currently lacks confirmation.
- Texture-pack and online-patch operations need explicit contract command completion/progress.
- Memory-card creation and scanning are synchronous in parts of the legacy implementation and can
  block the single-threaded web server; the schema does not fix that.
- Hidden database-seeded manual-fix keys written when manual mode is enabled are not independent
  user-facing rows. They should be preserved as unknown implementation state unless a future schema
  revision deliberately models them.
- RetroAchievements credential storage is platform/secret-storage specific and intentionally outside
  ordinary ini mutation helpers.
- Accessibility settings are new proposed Caliza keys; no legacy emulator/frontend consumer exists
  yet, and recommended-settings player-key preservation must be extended for them.

## Focused verification

From the repository root:

```sh
tests/caliza/test_settings_schema.sh
```

Expected output:

```text
caliza settings/catalog schema tests passed
```

The script uses only the host C++ compiler, `settings_schema.cpp`, `catalog_model.cpp` and the
self-contained test; it does not require or modify the project build system.
