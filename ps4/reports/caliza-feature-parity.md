# Caliza feature-parity matrix

Status: post-Estratos-integration audit, 2026-11-26. Host evidence is separated from console evidence; nothing here is hardware validation.

## Canonical sources

- Backend/UI contract: `caliza/core/contract.h`
- Settings and actions schema: `caliza/core/settings_schema.*`, `caliza/core/SETTINGS.md`
- Native production adapter: `caliza/core/console_backend.*`, `caliza/native/ps4_shell.*`
- Architecture inventory: `ps4/reports/caliza-architecture-inventory.md`
- Static drift guard: `tests/caliza/audit_feature_parity.py`
- Fast host suite: `tests/caliza/run-all.sh`

## Settings and actions

| Surface | Current evidence | Gap |
| --- | --- | --- |
| Canonical schema | 21 groups, 116 settings, 48 actions, 69 explicit migration-key pairs; strict and sanitizer tests pass | Schema itself is integrated; future emulator features still need explicit schema rows |
| Companion web settings | Static audit covers every visible non-composite schema row. Memory-card composite rows are exposed through the memory-card surface. Web smoke passes | BIOS image is typed as an exact file name because the web API does not expose BIOS-file choices yet |
| Native settings projection | Production `ConsoleBackend` uses `settings_projection.*`; `test_console_backend: PASS`; static parity audit passes | Game-scoped rows are visible in the merged projection but accurate mutation requires the encoded `game:<schema-id>:<game-key>` seam; the UI does not yet naturally supply selected-game scope |
| Native settings navigation | UI model renders contract-provided categories/items; host tests pass | Hardware controller/presentation validation pending |
| Actions/services | Smart Tune, online-patch lookup, texture-pack download/cancel and PS2 system-menu rows are exposed in the native game detail/home surfaces. Production texture/patch services are wired through `ConsoleBackend` hooks and report generic operation state | Achievements have no native account/browser surface; texture-pack deletion is not exposed as a confirmed destructive native flow; catalog retry is a backend command but not yet a distinct native row; runtime VM controls remain intentionally unbound |

## Library, storage and migration

| Feature | Current evidence | Gap |
| --- | --- | --- |
| Immutable library snapshots and command events | `test_contract: PASS`; `ConsoleBackend` republishes storage scan events under outer command IDs | Hardware validation pending |
| Incremental/cancelable scans and saved sources | `test_storage: PASS` on unprivileged host | Real PS4 USB/extended identity, hotplug and mount-slot stability evidence pending |
| `/data/caliza` canonical layout | `test_migration: PASS`; migration is integrated in `main-boot.cpp` | OpenOrbis hardlink, lock and directory-fsync behavior needs physical-console testing |
| Resumable migration from legacy roots | Boot migrates the immediately previous retired root first and `/data/PCSX2` as a gap filler; INI keys are translated; collisions are not overwritten | Collision repair is manual; large games/textures deliberately stay referenced in place |
| Folder picker and removal semantics | UI model tests cover add/scan ordering; `Caliza/GameFolders` is the user-facing owner and removal is unlink-only | Production console validation pending |
| External USB/extended games | Storage model preserves original paths and does not copy games | NFS/NAS scanning and durable disk identity remain incomplete |

## Interface and operations

| Feature | Host/web evidence | Console gap |
| --- | --- | --- |
| Estratos home/library/search/detail/settings/controls/accessibility/diagnostics/folder picker/first-run BIOS recovery | 34 deterministic host captures at 1080p/720p plus accessibility captures regenerated after the service-row integration; UI model tests pass | PS4 direct-display swapchain and controller path compile but have no hardware evidence |
| Production shell | `orbis_frontend_run` is now a thin adapter around `caliza::ps4shell::RunEstratos`; the inherited cover-flow implementation is deleted and absent from the link | Launch is a validated handoff only; it does not prove PCSX2 boot, BIOS execution or gameplay |
| Pause overlay | Runtime menu implemented end to end: `caliza::PauseMenu` model host-tested, in-game overlay composited over the paused frame (OpenOrbis fonts, CPU raster, Vulkan blend). Create button opens it; four actions: resume, save state, load state (slot 1), return to library with Cancel-first confirmation | Needs proper testing on the console: whether the system lets the Create button reach the app, input release/no-stuck-buttons, save/load while paused, re-exec back to the library, idle re-present while paused |
| Memory cards | Production creation/listing/assignment hooks are wired through `fe::settings`; native and web surfaces exist | Hardware validation pending |
| Texture packs | Production archive.org manager is started/stopped with the shell; downloads can be queued and cancel requested from the game-detail row; operation state is projected | Native row does not yet expose multiple-pack selection, delete confirmation or a separate catalog-retry row; hardware/network validation pending |
| Online patches/cheats | Production GitHub fetch service is wired; game-detail row queues lookup and operation state is projected | Fetched group rows depend on settings-file refresh and game-scope seam; hardware/network validation pending |
| Achievements | Web surface and inherited account/browser services remain | No native account sign-in/password flow or achievement browser projection yet |
| Reports/tester notes | Web diagnostics surface exists and legacy API supports them | `GenerateReport` in the native backend still reports unsupported instead of writing a new shared report bundle |

## Verification commands

```sh
bash tests/caliza/run-all.sh
python3 tests/caliza/audit_feature_parity.py
bash ps4/tools/run-host-tests.sh /tmp/caliza-host-tests
```

Current host result after integration:

- `bash tests/caliza/run-all.sh`: PASS, including `test_console_backend`.
- `python3 tests/caliza/audit_feature_parity.py`: PASS (web settings coverage and static native-projection coverage).
- `bash ps4/tools/run-host-tests.sh`: PASS.
- OpenOrbis compile/link: PASS to a roughly 40 MiB `eboot.bin` with 257 dynamic imports, 0 unresolved and no known PS5-only imports.
- Package validation: not final while the tree is dirty; `SOURCE.txt` correctly lacks an exact clean build-commit line. This must pass only after bounded commits produce a clean reproducible tree.
