# Caliza interface rebuild — read-only architecture inventory

No files were edited. This report reflects the current tree under `/Users/felipe/Proyectos/caliza`.

## 1. User-facing operations that must remain reachable

### 1.1 Native library/shelf

The current native interface is a 3D cover-flow shelf plus an options sheet. Its input contract is the simple per-frame button struct [fe_app.h:27-33](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_app.h:27), and its platform dependencies are collected in [fe_app.h:35-61](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_app.h:35).

Operations implemented in [fe_app.cpp:371-516](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_app.cpp:371) and [fe_app.cpp:1320-1597](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_app.cpp:1320):

- Browse games left/right with D-pad or left stick; held input repeats.
- Jump five games with `L1`/`R1`.
- Launch the selected game with `Cross` or `Options`.
- Refuse launch of a damaged image, with a user-visible explanation.
- Refuse launch when no usable BIOS is available.
- Open the selected game’s settings sheet with `Square`; with no games, open global settings.
- Close the sheet with `Circle`, `Square`, or `Options`.
- Switch between game-specific and global sheets with `L1`/`R1`.
- Switch Settings/Controls/Achievements tabs with `L2`/`R2`.
- Navigate rows with up/down, including held-repeat.
- Change values with left/right or Cross.
- Reset a row with Triangle.
- Use two-press confirmation for destructive or launch-like actions.
- Toggle the enlarged companion-panel QR view with Triangle; close it with Triangle or Circle.
- Open the RetroAchievements account panel with the `L1+Square` chord.
- Hide/unhide games through per-game `Caliza/HideGame` and global `Caliza/ShowHiddenGames`; hidden games can be shown dimmed.
- Persist and restore the last selected/launched game through `lastgame.txt`.
- View achievement state and per-game achievement lists.
- Sign into RetroAchievements, including text entry through the system keyboard when available.
- See BIOS absence/problem status before launch.
- See texture-pack activity and BIOS/status lines.
- Launch the PS2 system menu with no disc.
- Launch Smart Tune for a game.
- Open the GPL About overlay.

The PS4 branch already contains partial Caliza visual treatment: the native wordmark is `Caliza`, the library layout shifts/scales the cover flow, and a Caliza disc panel is drawn beside game details in [fe_app.cpp:578-835](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_app.cpp:578). This is not yet a clean redesign boundary; it is conditional rendering inside the old shelf implementation.

### 1.2 Settings-sheet operations

The sheet schema is declared in [fe_options.h:20-57](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_options.h:20), row assembly in [fe_options.cpp:595-692](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_options.cpp:595), mutation behavior in [fe_options.cpp:1029-1171](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_options.cpp:1029), and two-phase actions in [fe_options.cpp:1278-1399](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_options.cpp:1278).

Operations that must survive:

- Folder management:
  - extra game folders;
  - BIOS folder;
  - NFS shares;
  - folder picker navigation, selection, removal, reset, and text/share editing.
- Disc configuration:
  - global cheat disc;
  - per-game “start from cheat disc”;
  - per-ELF disc image.
- Display:
  - internal resolution;
  - aspect ratio;
  - widescreen patches;
  - display filter;
  - frame generation.
- Graphics:
  - renderer selection;
  - texture filtering;
  - anisotropy;
  - blending accuracy;
  - mipmapping;
  - texture replacements;
  - texture-pack download/install/cancel/delete;
  - Smart Tune launch.
- Performance:
  - EE cycle rate/skip;
  - MTVU;
  - GPU readback mode.
- Game:
  - emulated PS2 language;
  - PS2 network adapter;
  - host filesystem;
  - fast boot.
- On-screen:
  - info box;
  - FPS graph;
  - overlay/bezel;
  - RetroAchievements challenge icons.
- Manual hardware fixes and crop.
- BIOS selection globally or per game.
- Memory-card slot selection, card disabling, and blank-card creation.
- Patch and cheat group enable/disable.
- Online patch/cheat fetch.
- Recommended settings.
- Reset global settings or reset a game to inherited settings.
- System-menu launch.
- About/license display.

The option table itself is in [fe_options.cpp:358-530](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_options.cpp:358). It must be manually synchronized with the web `GROUPS` table in [index.html:311-540](/Users/felipe/Proyectos/caliza/ps5/frontend/assets/web/index.html:311); the warning is explicit in [fe_options.h:1-4](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_options.h:1). A parity extractor exists in [options_parity.js:1-31](/Users/felipe/Proyectos/caliza/ps5/frontend/host/options_parity.js:1), but it is not part of the PS4 host-test runner shown in [run-host-tests.sh:15-29](/Users/felipe/Proyectos/caliza/ps4/tools/run-host-tests.sh:15).

### 1.3 Controls/input operations

The mapping schema and defaults are defined in [OrbisPadMap.h:1-17](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/OrbisPadMap.h:1), [OrbisPadMap.h:237-285](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/OrbisPadMap.h:237), and [OrbisPadMap.h:314-368](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/OrbisPadMap.h:314):

- Full button remapping.
- Per-button pressure strength.
- Stick swap, inversion, dead zones, and left-stick-as-D-pad.
- Rumble enable/strength.
- Multitap selection.
- USB keyboard/mouse mode, mouse aim, speed, and mouse-button mapping.
- Save-state combo, default `L3+R3 + D-pad up`.
- Load-state combo, default `L3+R3 + D-pad down`.
- Combo hold time.
- Fast-forward combo and speed.
- Change-disc combo, default `L3+R3 + D-pad right`.

In-game command producers are in [main-boot.cpp:1172-1433](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:1172):

- Keyboard F1/F3 or controller combo → save/load state slot 1.
- Fast-forward combo → toggle fast forward.
- Change-disc combo → next disc.
- `L1+R1+touchpad click` → back to menu.
- `L2 + D-pad down` for two seconds → open the settings panel in the console browser or dismiss the QR fallback.
- `L3+R3` held without D-pad use → cycle present filter.

There is no real pause menu. PCSX2 FullscreenUI entry points are headless no-ops; `OpenPauseMenu()` and `Render()` are empty in [ProsperoUI.cpp:63-134](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/ProsperoUI.cpp:63).

### 1.4 Web/companion panel

The server contract is documented in [fe_web.h:1-8](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.h:1) and configured by [fe_web.h:41-74](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.h:41). Routes are dispatched in [fe_web.cpp:728-801](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:728).

Operations/APIs:

- `GET /api/state` — app/build/language/mode/current game/localized strings.
- `GET /api/probe` — build/session/mode/VM-running/frame counters.
- `GET /api/games` — game list, badges, cover/settings/playing flags.
- `GET /api/cover?id=...` — cover JPEG/PNG.
- `GET /api/achievements` and `/api/achievement-badge` — current-game achievement state/images.
- `GET /api/settings?id=@global|<game>` — effective settings, patch/cheat groups, recommendations, game fixes.
- `POST /api/settings?id=...` — line-oriented mutations:
  - `set key=value`
  - `unset key`
  - `patch+`, `patch-`
  - `cheat+`, `cheat-`
  - body `recommended`
- `GET /api/report` — bundled logs/settings/game inventory download.
- `POST /api/note` — tester note into `settings.log`.
- `GET /api/discs`, `POST /api/disc` — inspect/change the running game’s disc.
- `GET /api/memcards`, `POST /api/memcards` — list/create blank memory cards.

The settings response shape is documented inline in [fe_web.cpp:1001-1112](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:1001). The mutation language and atomic write behavior are in [fe_web.cpp:1114-1269](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:1114). Recommended-reset semantics are in [fe_web.cpp:1271-1322](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:1271). Memory-card creation is synchronous and atomic-by-rename in [fe_web.cpp:1535-1647](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:1535).

Security/trust model: no token since vk-285-118; API access is restricted only by same-site/Host/Origin/Sec-Fetch-Site checks in [fe_web.cpp:700-767](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:700). Any device able to reach the LAN interface can still mutate settings if it opens the console address directly.

### 1.5 Host preview and validation surface

The host harness runs the same `fe::App` and `fe::Renderer` on desktop Vulkan, including SwiftShader, with scripted input and capture; see [fe_host.cpp:1-24](/Users/felipe/Proyectos/caliza/ps5/frontend/host/fe_host.cpp:1). It supports:

- `--data` as a stand-in for `/data/caliza`;
- `--games` synthetic catalog input;
- `--places` folder-picker places;
- wait/press/hold/shot/game/expect/rec steps;
- PNG, MP4, and WAV capture;
- virtual clock and texture-pack download-rate control.

Its default folder places are `Caliza's folder`, `USB drive 1`, and `Extended storage` at [fe_host.cpp:690](/Users/felipe/Proyectos/caliza/ps5/frontend/host/fe_host.cpp:690).

The committed non-console validation entry point is [run-host-tests.sh:15-29](/Users/felipe/Proyectos/caliza/ps4/tools/run-host-tests.sh:15), covering frame pacing, keyboard mapping, pad mapping, texture roots, WSI decisions, audio cache, CDVD subchannel, savestate bounds, package validation, savestate validation, console-log analysis, smoke-runner logic, and shell syntax.

---

## 2. Actual data/control flow and entry points

### 2.1 Build and package entry points

The PS4 build path is:

1. [build-ps4.sh:32-52](/Users/felipe/Proyectos/caliza/ps4/build-ps4.sh:32) compiles with `Makefile.ps4`.
2. [Makefile.ps4:381-393](/Users/felipe/Proyectos/caliza/ps4/Makefile.ps4:381) includes the frontend sources and embedded web/font/preset assets; `fe_host` is explicitly not built into the eboot.
3. [build-ps4.sh:54-112](/Users/felipe/Proyectos/caliza/ps4/build-ps4.sh:54) links through `ps4/link/CMakeLists.txt`.
4. [CMakeLists.txt:1-120](/Users/felipe/Proyectos/caliza/ps4/link/CMakeLists.txt:1) links Makefile objects with the CEMUPS4 runtime, RADV, PS4 system libraries, WSI wrapping, ordering script, and weak PS5-symbol null definitions.
5. [build-ps4.sh:114-165](/Users/felipe/Proyectos/caliza/ps4/build-ps4.sh:114) stages `eboot.bin`, `sce_sys/icon0.png`, resources, licenses, optional personal BIOS, and source notices; then calls `make-pkg.sh`.
6. [make-pkg.sh:35-67](/Users/felipe/Proyectos/caliza/ps4/make-pkg.sh:35) normalizes the content label, constructs `CONTENT_ID`, writes `param.sfo`, and [make-pkg.sh:68-103](/Users/felipe/Proyectos/caliza/ps4/make-pkg.sh:68) builds the PKG.

Important current package constants:

- build tag: `caliza-<commit>` at [build-ps4.sh:36-41](/Users/felipe/Proyectos/caliza/ps4/build-ps4.sh:36);
- title ID: `ZERU00001`;
- display title: `Caliza`;
- content label: `CALIZA00`, yielding `IV0000-ZERU00001_00-CALIZA0000000000.pkg`;
- source URL still `https://github.com/felipebasurto/caliza`;
  all in [build-ps4.sh:135-164](/Users/felipe/Proyectos/caliza/ps4/build-ps4.sh:135).

### 2.2 PS4/PS5 boot flow

`main()` begins at [main-boot.cpp:3471](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3471). Relevant sequence:

1. PS4 module loading and direct-memory aliasing at [main-boot.cpp:3473-3477](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3473).
2. Boot log opening or pre-jailbreak memory holding at [main-boot.cpp:3481-3489](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3481).
3. Legacy `/data/PCSX2` → `/data/caliza` migration before folder use, repeated after jailbreak because `/data` may initially be unavailable.
4. Language setup, pre-jailbreak USB scan, and cover prefetch at [main-boot.cpp:3587-3610](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3587).
5. PS5 jailbreak probing skipped on PS4 at [main-boot.cpp:3613-3630](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3613).
6. Data layout and folders at [main-boot.cpp:3729-3799](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3729):
   - `AppRoot`/`DataRoot`: `/data/caliza`;
   - `Settings`, `GameSettings`, `InputProfiles`: flat `/data/caliza`;
   - `Bios`, `Logs`, `MemoryCards`, `Savestates`, `Cheats`, `Patches`, `Cache`, `Covers` through `OrbisDir`;
   - `Textures`: `/data/caliza/textures`;
   - PS4 packaged resources fall back to `/app0/resources` if no `GameIndex.yaml`.
7. Renderer/flags environment at [main-boot.cpp:3802-3814](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3802).
8. Post-jailbreak USB scan, BIOS check, web server start, and Vulkan shelf at [main-boot.cpp:3823-3843](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3823).
9. Plain-list fallback if the shelf did not run at [main-boot.cpp:3854-3855](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3854).
10. Clean exit if no game is selected at [main-boot.cpp:3856-3865](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3856).
11. Per-game settings path derivation and launch logging at [main-boot.cpp:3867-3903](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3867).
12. One-shot Smart Tune flag consumption at [main-boot.cpp:3904-3918](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3904).
13. VM boot parameter construction, including ELF/disc/cheat-disc handling, at [main-boot.cpp:4179-4211](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:4179).
14. Memory pre-reset and `VMManager::Initialize` at [main-boot.cpp:4405-4429](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:4405).
15. Failure returns to the shelf only when the shelf originally ran; otherwise the process exits, at [main-boot.cpp:4487-4495](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:4487).
16. Pad/input thread starts only after VM state is set running at [main-boot.cpp:4498-4508](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:4498).
17. `VMManager::Execute()` runs on the main thread and is re-entered in a loop at [main-boot.cpp:4606-4850](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:4606).
18. Back-to-menu is process re-entry, not an in-process pause/menu transition: `VMState::Stopping` plus `g_orbis_menu_request` invokes `orbis_back_to_menu()` at [main-boot.cpp:4839-4843](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:4839).

The 1 Hz ticker performs flag/settings snapshot refresh, deferred-log drain, watchdog beats, stats, and flush work in [main-boot.cpp:4627-4813](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:4627).

### 2.3 Frontend path construction

`orbis_frontend_paths()` hard-wires the catalog/settings/web storage contract in [main-boot.cpp:2833-2878](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:2833):

- `/data/caliza/games` and `/data/caliza` are always catalog roots.
- `settings/`, `gs.ini`, patches, covers, cache, memcards, logs are fixed paths.
- scan roots are supplemented by the current `s_usb_dirs` snapshot.
- persistent caches include:
  - `cache/usb-games.txt`;
  - `cache/chd-serials.txt`;
  - `cache/texture-packs.json`;
  - `cache/online-patches.txt`.
- PS4 GameDB falls back to `/app0/resources/GameIndex.yaml`.

### 2.4 Shelf scan/launch flow

`orbis_frontend_run()` in [fe_ps5.cpp:1548-1969](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_ps5.cpp:1548):

1. Configures serial and GameDB caches.
2. Builds scan roots from `games_dir`, `top_dir`, and `usb_dirs`.
3. Calls synchronous `ScanGames`.
4. Reads serials, applies GameDB titles, reads badges, detects damaged CHDs.
5. Sorts and writes `cache/usb-games.txt`.
6. Preselects the last game.
7. Opens controller, display, fonts, renderer, cover service, audio.
8. Constructs `AppConfig` with settings paths, callbacks, folder places, BIOS callbacks, texture-pack and online-patch services.
9. Runs the frame loop synchronously until a pick or failure.
10. Stops cover/texture/patch workers with bounded waits; workers that fail to stop are intentionally left detached.
11. Returns either a game path or `kOrbisSystemMenuPath`.

This means scan cost, serial reads, image probing, and initial synchronous I/O all occur before the first shelf frame. There is no scan progress UI and no cancellation path.

### 2.5 Web-server data flow

`orbis_web_start()` embeds assets and callbacks in [fe_ps5.cpp:1410-1455](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_ps5.cpp:1410). The server is one BSD-socket thread, one request per connection, port 8844 plus fallback ports.

`WebServer::Games()` rescans at most once every ten seconds and synchronously reads missing serials in [fe_web.cpp:803-827](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:803). Because the web server is single-threaded, a slow USB/NFS scan or serial probe blocks every other API route, including state, settings, disc switching, and report generation.

Settings writes use sibling temp files plus rename in [fe_settings.cpp:55-87](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_settings.cpp:55). The emulation side polls cached snapshots refreshed once per second by the ticker; the snapshot mechanism is in [orbis_paths.cpp:56-147](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/orbis_paths.cpp:56). This is eventually consistent, not command-acknowledged.

### 2.6 In-game command flow

Current command transport is the atomic bitmask `g_orbis_state_request`:

- producers call `orbis_queue_state_request()` in [main-boot.cpp:88-100](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:88);
- the bits are defined/consumed in [StubHost.cpp:248-370](/Users/felipe/Proyectos/caliza/tests/ctest/core/StubHost.cpp:248):
  - 1 save slot 1;
  - 2 load slot 1, then `.backup` fallback;
  - 3 back-to-menu/shutdown;
  - 4 fast-forward toggle;
  - 5 web-requested disc change;
  - 6 combo next-disc change.
- `Host::PumpMessagesOnCPUThread()` consumes at most one bit per CPU-thread pump in priority order `{3,1,2,4,6,5}`.
- guest power-off is routed to bit 3 by `Host::RequestVMShutdown()` in [StubHost.cpp:235-246](/Users/felipe/Proyectos/caliza/tests/ctest/core/StubHost.cpp:235).
- NVRAM is persisted every 30 seconds in the same pump at [StubHost.cpp:270-279](/Users/felipe/Proyectos/caliza/tests/ctest/core/StubHost.cpp:270).

This is lossy: repeated identical commands coalesce; ordering beyond the fixed priority is absent; payloads are external mutable state; there is no completion/failure result for producers.

---

## 3. Proposed explicit UI/backend contract types and command-queue semantics

### 3.1 Contract location and ownership

Introduce a small shared contract header, for example:

- `ps5/frontend/fe_contract.h` or `ps5/coreorbis/include-orbis/OrbisUiContract.h`

It should contain only types, IDs, and serialization-neutral payloads — no renderer, HTTP, INI, or PCSX2-private dependencies. Native UI, web backend, host preview, and boot shell should all speak through it.

### 3.2 State model

Recommended types:

```cpp
enum class UiMode {
  Boot,
  Scanning,
  Menu,
  Launching,
  Running,
  ReturningToMenu,
  Fatal
};

enum class GameMediaKind { DiscImage, Elf, SystemMenu };

struct GameId {
  std::string stable_key; // not merely basename
};

struct GameSummary {
  GameId id;
  std::string path;
  std::string file_name;
  std::string title;
  std::string serial;
  std::string region;
  uint64_t bytes = 0;
  GameMediaKind kind = GameMediaKind::DiscImage;
  bool hidden = false;
  bool damaged = false;
  std::string damage_reason;
  std::vector<std::string> badges;
  bool has_cover = false;
  bool has_own_settings = false;
};

struct StorageRoot {
  std::string path;
  std::string label;
  enum class Kind { Internal, UsbSlot, ExtendedSlot, Custom, Nfs } kind;
  bool present = false;
  bool readable = false;
  std::string volume_identity; // empty if unavailable
  std::string error;
};

struct VmSnapshot {
  UiMode mode = UiMode::Boot;
  bool vm_running = false;
  std::string current_game_key;
  std::string current_disc;
  uint64_t game_sequence = 0;
  uint64_t settings_revision = 0;
  uint64_t catalog_revision = 0;
};

struct UiSnapshot {
  VmSnapshot vm;
  std::vector<GameSummary> games;
  std::vector<StorageRoot> storage;
  bool bios_present = false;
  std::string bios_problem;
  std::string web_url;
  std::string web_url_short;
};
```

### 3.3 Command model

```cpp
enum class CommandType {
  RescanStorage,
  LaunchGame,
  LaunchSystemMenu,
  LaunchSmartTune,

  SetSetting,
  UnsetSetting,
  ApplyRecommendedSettings,
  ResetSettingsScope,

  CreateMemoryCard,
  SelectMemoryCard,
  SelectBios,
  SetGameFolders,
  SetBiosFolder,
  SetNfsShares,

  FetchOnlinePatches,
  DownloadTexturePack,
  CancelTexturePack,
  DeleteTexturePack,

  SaveState,
  LoadState,
  ToggleFastForward,
  ChangeDisc,
  ReturnToMenu,
  OpenCompanionPanel
};

enum class CommandOrigin { NativeUi, WebUi, HostPreview, Guest, System };
enum class CommandScope { Global, Game, RunningVm };

struct Command {
  uint64_t id = 0;
  uint64_t generation = 0;
  CommandType type;
  CommandOrigin origin;
  CommandScope scope;
  std::string game_key;
  std::variant<
    std::monostate,
    SettingChange,
    MemoryCardCreate,
    DiscChange,
    FolderListChange
  > payload;
};

enum class CommandStatus {
  Queued,
  Accepted,
  Running,
  Completed,
  Failed,
  Cancelled,
  Rejected
};

struct CommandResult {
  uint64_t id = 0;
  CommandStatus status;
  std::string error;
  uint64_t catalog_revision = 0;
  uint64_t settings_revision = 0;
};
```

### 3.4 Required semantics

1. **Single mutation owner per domain.**
   - VM commands execute only on the CPU thread at the existing safe pump point.
   - Settings mutations are serialized through one settings service.
   - Catalog scans are single-flight through one catalog service.

2. **Generation isolation.**
   - Increment a generation on launch, return-to-menu, or process/session transition.
   - Reject or drop stale commands/results whose generation no longer matches.
   - Preserve the existing rule that stale save/load/disc/fast-forward requests are cleared during menu transition, but make the clearing explicit rather than a raw bitmask reset.

3. **VM-command ordering.**
   - `ReturnToMenu` must outrank all other VM commands, matching current bit 3 behavior.
   - A state save and load submitted in the same frame must have deterministic ordering; the current fixed priority is save before load.
   - Disc changes must carry the target path atomically in the command payload, not through shared mutable side state.
   - Every command needs a completion/failure result; OSD display should be an event consumer, not the transport.

4. **Launch rules.**
   - `LaunchGame`, `LaunchSystemMenu`, and `LaunchSmartTune` are legal only in `Menu`.
   - They atomically transition to `Launching` and reject subsequent launch commands.
   - Damaged images and missing BIOS are pre-launch validation errors reported through `CommandResult`.

5. **Settings rules.**
   - Writes remain atomic temp+rename.
   - Each successful mutation increments `settings_revision`.
   - Results distinguish “accepted but applies live” from “requires next launch.”
   - Shelf and web must use the same schema registry, replacing the duplicated GROUPS/OptionGroups tables.

6. **Scan rules.**
   - Only one scan at a time.
   - A rescan request while scanning either coalesces into a pending generation or returns `Rejected`.
   - Cancellation is cooperative at directory boundaries and serial probes.
   - Results are published as immutable snapshots; readers never iterate a vector being mutated.
   - Web state should expose scan progress rather than blocking a request thread.

7. **Long-running downloads.**
   - Texture/online-patch jobs get stable job IDs and explicit state events.
   - Shutdown must not intentionally abandon threads as the normal design; detached-timeout behavior can remain a compatibility fallback but not the target contract.

---

## 4. Storage discovery reality for `/mnt/usb0-7` and `/mnt/ext0-1`

### 4.1 Discovery algorithm

USB/extended-storage roots are enumerated by [orbis_usb_game_dirs()](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_ps5.cpp:1113) in [fe_ps5.cpp:1113-1185](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_ps5.cpp:1113):

- probes `/mnt/usb0` through `/mnt/usb7`;
- then `/mnt/ext0` and `/mnt/ext1`;
- `opendir` failure is ignored except non-`ENOENT` logging;
- an empty directory is treated as “no drive in this slot”;
- any non-empty mount root is treated as present;
- scan candidates are:
  - mount root;
  - case-insensitive `DVD/`, `CD/`, `caliza/`, `games/`;
  - `PCSX2/games`, `PCSX2/DVD`, `PCSX2/CD`;
  - `caliza/games`, `caliza/DVD`, `caliza/CD`.

Custom folders come from `Caliza/GameFolders` in [main-boot.cpp:2467-2508](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:2467). They must be absolute existing directories at boot; invalid entries are skipped.

NFS shares come from `Caliza/NfsShares`; PS4 mounts plain NFSv3 through CEMUPS4 `/net/shareN`, checks up to four mounts concurrently with an eight-second budget, and only adds successful mounts to the scan in [main-boot.cpp:2515-2734](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:2515). NFS is mounted/scanned only after the jailbreak phase in [main-boot.cpp:2744-2752](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:2744).

### 4.2 Game scanning

The actual catalog scan is [fe_games.cpp:1007-1087](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_games.cpp:1007):

- scans each supplied root;
- scans one additional level of subfolders;
- considers at most 64 subfolders per root;
- skips dot/dollar folders and known data folders (`bios`, `cache`, `settings`, `textures`, etc.);
- recognizes `.iso`, `.chd`, `.cso`, `.zso`, `.bin`, `.img`, `.elf`;
- validates ELFs by magic bytes;
- filters small/non-data `.bin`/`.img`;
- deduplicates by **file basename**, not full path or volume identity;
- sorts by title then filename.

Serial extraction is expensive and synchronous. ISO-like images are opened directly; CHD/CSO/ZSO serials are cached by `path + size + mtime` in `cache/chd-serials.txt`, as implemented around [fe_games.cpp:462-585](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_games.cpp:462).

### 4.3 Volume identity

There is effectively **no volume identity**:

- no filesystem UUID, device ID, volume label, serial, or mount-generation tracking;
- presence is inferred solely from non-empty mountpoint directories;
- `/mnt/usbN` and `/mnt/extN` are slot names, not durable disk identities;
- if a disk moves slots, cached paths are wrong or duplicated;
- if two roots contain identically named images, the first basename wins and the second silently disappears;
- the web API uses basename (`g.file`) as public game ID in [fe_web.cpp:839-846](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:839), so identity instability leaks into settings and web commands.

### 4.4 Timing, threading, cancellation, persistence

- USB roots are probed twice during boot: before and after jailbreak at [main-boot.cpp:3604-3609](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3604) and [main-boot.cpp:3831](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3831).
- The shelf catalog scan is synchronous before the first frame.
- There is no cancellation.
- There is no hotplug listener.
- There is no user-triggered rescan operation.
- The web server rescans the fixed root list lazily every ten seconds, but that does not rediscover newly attached USB slots because `game_dirs` were captured at startup.
- `cache/usb-games.txt` is only a cover-prefetch aid, not a persistent catalog; see [fe_ps5.cpp:803-863](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_ps5.cpp:803).
- `Caliza/GameFolders`, `Caliza/BiosFolder`, and `Caliza/NfsShares` are the only persistent storage configuration.
- Before jailbreak, `access()` can falsely report `EPERM`; code deliberately uses `stat()` instead, documented in [orbis_paths.cpp:35-45](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/orbis_paths.cpp:35).

---

## 5. Branding and migration surface: Caliza → Caliza, `/data/caliza` → `/data/caliza`

### 5.1 Central contradiction

[README.md:23](/Users/felipe/Proyectos/caliza/README.md:23) explicitly says current source builds use Caliza while retaining the same title ID and console data directory. The rebuild brief instead asks about migrating `/data/caliza` to `/data/caliza`.

That is a product/platform decision that must be settled before implementation:

- retaining `ZERU00001` means the new package replaces the old installed app and existing data remains associated with the title;
- changing title ID creates a parallel installation and makes migration semantics more complicated;
- moving the data root without changing title ID can orphan data unless migration is perfect;
- copying rather than moving doubles storage use for BIOS/savestates but is safer; games/texture packs are too large to copy.

### 5.2 Existing migration machinery

Current migration is `/data/PCSX2` → `/data/caliza`, implemented in [main-boot.cpp:269-565](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:269):

- runs only if `/data/caliza` does not exist;
- source must be `/data/PCSX2`;
- copies, never moves;
- directory allowlist: `bios`, `memcards`, `settings`, `savestates`, `patches`, `cheats`, `covers`;
- top-level files: `gs.ini`, `live.ini`, `playtime.dat`, `lastgame.txt`;
- skips symlinks and non-regular files;
- file-size limit 64 MiB;
- recursion depth limit 4;
- partial destination files are removed;
- old games are not copied; `/data/PCSX2/games` is added as a read-only scan root in [main-boot.cpp:2753-2767](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:2753).

This machinery cannot simply be retargeted without defining precedence among three possible generations: `/data/PCSX2`, `/data/caliza`, and `/data/caliza`.

### 5.3 Hard-coded data-root surface

Important files/constants:

- `kOrbisDataRoot = "/data/caliza"` and `kOrbisLegacyDataRoot = "/data/PCSX2"` in [main-boot.cpp:269-272](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:269).
- `kRoot = "/data/caliza"` in [orbis_paths.cpp:20](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/orbis_paths.cpp:20).
- `EmuFolders` assignments in [main-boot.cpp:3729-3799](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3729).
- frontend path assembly in [main-boot.cpp:2833-2878](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:2833).
- BIOS copying destination `/data/caliza/bios` in [main-boot.cpp:3748-3775](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3748).
- Smart Tune flag `/data/caliza/flags/smarttune` in [fe_app.cpp:1541](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_app.cpp:1541) and [main-boot.cpp:3907-3916](/Users/felipe/Proyectos/caliza/ps5/coreorbis/main-boot.cpp:3907).
- folder picker first place `Caliza's folder` in [fe_ps5.cpp:1758-1769](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_ps5.cpp:1758).
- host-preview equivalent in [fe_host.cpp:690](/Users/felipe/Proyectos/caliza/ps5/frontend/host/fe_host.cpp:690).
- layout documentation in [OrbisPaths.h:1-17](/Users/felipe/Proyectos/caliza/ps5/coreorbis/include-orbis/OrbisPaths.h:1).

### 5.4 Branding strings and compatibility schema

User-visible/runtime names still mixed:

- web `/api/state` reports `"app":"Caliza"` in [fe_web.cpp:848-876](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:848).
- report filenames are `Caliza-test<N>-...` / `Caliza-logs-...` in [fe_web.cpp:1435-1445](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:1435).
- generated settings comments say “written by the Caliza settings page” / “written from the Caliza shelf” in [fe_web.cpp:1141-1147](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:1141) and [fe_options.cpp:1029-1046](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_options.cpp:1029).
- browser storage keys are `caliza.tab` and legacy `caliza.token` in [index.html:565-577](/Users/felipe/Proyectos/caliza/ps5/frontend/assets/web/index.html:565).
- fallback selector draws `CALIZA` in [game_select.cpp:203-211](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/game_select.cpp:203).
- native PS4 wordmark is already `Caliza` in [fe_app.cpp:700-707](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_app.cpp:700).
- web title/header is patched at serve time to Caliza/PS2UI in [fe_web.cpp:315-328](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_web.cpp:315).
- the patch asset is embedded as `/caliza-ps2ui.css` in [fe_ps5.cpp:1434-1440](/Users/felipe/Proyectos/caliza/ps5/frontend/fe_ps5.cpp:1434).
- package display title is already `Caliza`, but title/content IDs remain ZERU/CALIZA in [build-ps4.sh:158-163](/Users/felipe/Proyectos/caliza/ps4/build-ps4.sh:158).
- SOURCE.txt still names Caliza and `github.com/felipebasurto/caliza` in [build-ps4.sh:135-140](/Users/felipe/Proyectos/caliza/ps4/build-ps4.sh:135).
- [ps4/BRAND.md](/Users/felipe/Proyectos/caliza/ps4/BRAND.md) is largely a Caliza branding guide; its naming table says display name `Caliza`, data dir `/data/caliza`, title prefix `ZERU`, bundle `io.github.calizaemu.caliza`, and log prefix `[CALIZA]`.

### 5.5 Do not treat ini keys as simple branding

`Caliza/GameFolders`, `Caliza/HideGame`, `Caliza/Renderer`, `Caliza/Button*`, etc. are persisted user-settings schema. Renaming them to `Caliza/*` without aliases would silently discard user configuration. Treat this namespace as a compatibility contract. Either:

1. keep `Caliza/*` forever as a stable wire/storage schema, or
2. implement versioned read aliases and one-way writes with explicit migration/revision tracking.

The same applies to browser `localStorage` keys if preserving per-browser UI state matters.

---

## 6. Recommended disjoint ownership split for three implementation agents

The current files are not naturally disjoint: `fe_ps5.cpp` glues platform, scan, web, renderer, and services; `fe_options.cpp` is shared schema/UI; `main-boot.cpp` owns boot, storage, VM, input, migration, and disc logic. The split below is strict by current files and planned extraction boundaries.

### Agent A — platform shell, VM commands, storage orchestration

Exclusive ownership:

- `ps5/coreorbis/main-boot.cpp`
- `ps5/coreorbis/include-orbis/OrbisPaths.h`
- `ps5/coreorbis/orbis-shims/orbis_paths.cpp`
- `ps5/coreorbis/orbis-shims/game_select.cpp`
- `ps5/coreorbis/orbis-shims/OrbisPadMap.h`
- `ps5/coreorbis/orbis-shims/ProsperoUI.cpp`
- `ps5/coreorbis/orbis-shims/ProsperoSmartTune.cpp`
- `tests/ctest/core/StubHost.cpp`
- `ps4/ps4_*`
- `ps4/link/**`
- `ps4/Makefile.ps4`
- `ps4/build-ps4.sh`
- `ps4/make-pkg.sh`
- `ps4/tools/**` for platform/package tests
- new command-queue/platform contract implementation files under `ps5/coreorbis/`

Responsibilities:

- boot/restart flow;
- VM lifecycle command execution;
- save/load/disc/fast-forward semantics;
- input-combo production;
- migration implementation;
- storage-root discovery and scan orchestration;
- package/title/data-root decisions once product direction is given.

### Agent B — native Caliza interface and host visual harness

Exclusive ownership:

- `ps5/frontend/fe_app.cpp`
- `ps5/frontend/fe_app.h`
- `ps5/frontend/fe_renderer.*`
- `ps5/frontend/fe_vk.*`
- `ps5/frontend/fe_text.*`
- `ps5/frontend/fe_sound.*`
- `ps5/frontend/fe_covers.*`
- `ps5/frontend/fe_shaders.inc`
- native font/texture/shader assets used only by the shelf
- `ps5/frontend/host/fe_host.cpp`
- host visual scripts/tests for shelf/folder picker/sheet behavior

Responsibilities:

- controller navigation model;
- library presentation and animations;
- settings-sheet presentation/input behavior;
- QR/account/about overlays;
- accessibility/focus/empty/error states;
- screenshot/video regression harness.

Agent B consumes `GameInfo`, settings mutations, scan state, and command results through the contract; it does not own scanning or INI mutation implementation.

### Agent C — shared settings schema, catalog model, companion web UI

Exclusive ownership:

- `ps5/frontend/fe_web.cpp`
- `ps5/frontend/fe_web.h`
- `ps5/frontend/assets/web/**`
- `ps5/frontend/caliza_ps2ui_markup.inc`
- `ps5/frontend/fe_settings.*`
- `ps5/frontend/fe_options.*`
- `ps5/frontend/fe_games.*`
- `ps5/frontend/fe_bios.*`
- `ps5/frontend/fe_texpacks.*`
- `ps5/frontend/fe_patchdl.*`
- `ps5/frontend/fe_i18n.*`
- nonvisual host tests for settings/schema/web/catalog
- new canonical settings-schema registry and web contract adapters

Responsibilities:

- one canonical settings schema shared by sheet and web;
- INI compatibility and migration aliases;
- web API versioning;
- memory-card/patch/cheat/BIOS mutation services;
- catalog metadata, serials, badges, stable game IDs;
- companion panel visual implementation.

### Boundary rule for `fe_ps5.cpp`

Assign current `fe_ps5.cpp` temporarily to Agent A because it is mostly platform glue. Before parallel implementation, split it into contract-facing pieces:

- platform/display/input/loop glue → Agent A;
- shelf presentation wiring → Agent B;
- web-server configuration/catalog services → Agent C.

Until that split happens, Agents B and C should not edit `fe_ps5.cpp`; they should require narrow adapter functions from Agent A. Otherwise this file will become the first merge/conflict bottleneck.

---

## 7. High-risk unknowns and design hazards

1. **Unresolved product decision: title ID and data root.**  
   README says Caliza retains title ID and `/data/caliza`; the brief presumes `/data/caliza`. This affects install replacement, migration, save data ownership, package identity, and user data safety. Do not implement before the orchestrator/user chooses.

2. **Three-generation migration ambiguity.**  
   Existing logic handles only `/data/PCSX2` → `/data/caliza`. A `/data/caliza` migration must define precedence when multiple old roots exist, whether migration is copy-only, whether scan fallback includes old game roots, and how partial copies resume.

3. **Flat-layout fallback hazard.**  
   `OrbisDir()` silently falls back to `/data/caliza` when a subdirectory is missing. [KNOWN_ISSUES.md:57](/Users/felipe/Proyectos/caliza/ps4/KNOWN_ISSUES.md:57) already warns that casual directory creation can switch layouts and orphan flat user data. Any migration or interface work that creates folders must account for this.

4. **No stable game identity.**  
   Basename dedupe plus basename web IDs is fragile across USB slots, duplicate file names, custom folders, and moved volumes. Settings are also keyed by filename stem. A stable ID scheme must preserve compatibility with existing settings files.

5. **No volume identity or hotplug model.**  
   Mount slots are not disks. There is no device UUID/label/generation and no insertion/removal event flow. A rescan feature designed without identity can duplicate or lose games.

6. **Synchronous scanning blocks UI.**  
   Shelf scan occurs before first frame; web scans block the single server thread. USB/NFS latency, serial extraction, and damaged images can make the entire interface appear hung. A rebuild needs asynchronous scan snapshots and progress/cancellation semantics.

7. **Bitmask command transport is lossy.**  
   Repeated commands coalesce, payloads are implicit, there is no completion event, and ordering is hard-coded. It is adequate for existing hotkeys but inadequate as the contract for a multi-interface UI.

8. **Back-to-menu is process re-exec, not pause.**  
   There is no in-process suspend/resume menu. Rebuilding an interface that assumes seamless pause/resume would be a major VM lifecycle change, not a UI-only task.

9. **Settings concurrency is eventual and revisionless.**  
   Writes are atomic, but shelf/web can submit overlapping changes; live application depends on ticker snapshots and file-content polling. There is no compare-and-swap, revision, or conflict result.

10. **Dual schema maintenance.**  
    Web `GROUPS` and native `OptionGroups()` are parallel hand-written schemas. The parity helper exists but is not part of the main PS4 host-test runner. A single canonical schema is strongly recommended.

11. **Web trust model is LAN-open.**  
    Same-site checks prevent some browser attacks but do not authenticate LAN clients. Settings mutation and memory-card creation are available to anything that can reach the panel.

12. **Memory-card creation blocks the web server.**  
    Writing up to 64 MiB synchronously in the single request thread can stall all panel operations.

13. **Detached worker shutdown is normalized.**  
    Cover, texture-pack, and online-patch workers can be left running if they do not stop within 1.5 seconds. This is dangerous for a more dynamic UI that may scan/mutate storage concurrently.

14. **Smart Tune is experimental and behavior-dependent.**  
    Its own header states that measurement truth depends on the user reaching real gameplay and that it lacks console validation; see [ProsperoSmartTune.cpp:1-18](/Users/felipe/Proyectos/caliza/ps5/coreorbis/orbis-shims/ProsperoSmartTune.cpp:1).

15. **PS4/PS5 conditional inheritance.**  
    The PS4 target reuses PS5SX2 frontend/core files with `ORBIS_PS4` branches. Interface changes can regress host preview, PS5-only code, or PS4 shims even when the PS4 target appears correct.

16. **Licensing/attribution must remain visible.**  
    GPL attribution exists in the native About row and packaged notices. PS2UI is MIT-licensed and currently patched into the companion panel. Any rebuild must preserve these notices and package conveyance files.
