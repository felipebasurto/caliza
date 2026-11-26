# Caliza PS4 repository map

This map separates the active Caliza product from inherited compatibility code and unused upstream/desktop material. It is the cleanup authority for the interface rebuild; file moves happen after the running implementation agents finish so their work is not broken mid-flight.

## Active Caliza product

| Path | Status | Purpose |
| --- | --- | --- |
| `caliza/` | **active, new** | Original Caliza interface module, host preview, shared UI/backend contract, storage/migration logic, settings schema and new web-panel source. This is the product-facing implementation going forward. |
| `ps4/` | **active** | PS4 target, OpenOrbis package, icon/metadata, validation tools, hardware-test tooling and current platform documentation. |
| `pcsx2/` | **active upstream core** | PCSX2 emulation core: EE/IOP/VU/GS/SPU2/CDVD/memory cards/input and hardware behavior. Kept with GPL attribution. |
| `common/` | **active upstream support** | Shared PCSX2 utilities used by the emulation core. |
| `3rdparty/` | **active dependencies** | Vendored third-party code required by the core and console build. |
| `ps5/coreorbis/` | **active inherited platform layer, misnamed directory** | Orbis boot, threading, memory, controller, audio, storage, NFS, logging and instrumentation adaptations. Despite the directory name, this is not a PS5 product target in Caliza; it is the inherited Orbis layer currently shared by the PS4 build. Planned cleanup name: `orbis/core/`. |
| `ps5/third_party/` | **active dependencies** | Console-port dependencies such as compression, TLS, NFS and archive support. Planned cleanup name: `orbis/third_party/`. |

## Transitional inherited services

`ps5/frontend/` is temporary. The inherited cover-flow presentation has now been removed from the product and from the tree; the remaining files are services worth preserving behind explicit Caliza contracts.

Keep and adapt behind explicit Caliza contracts:

- disc-image scanning/serial detection (`fe_games.*`);
- atomic settings/memory-card/patch helpers (`fe_settings.*`);
- option behavior and compatibility semantics (`fe_options.*`);
- cover lookup/decoding/download mechanics (`fe_covers.*`);
- BIOS discovery helpers (`fe_bios.*`);
- HTTPS/API server mechanics (`fe_web.*`, `fe_https*`);
- texture-pack and patch services (`fe_texpacks.*`, `fe_patchdl.*`);
- localization mechanics after copy is rewritten for Caliza (`fe_i18n.*`).

Removed presentation material:

- cover-flow shelf implementation (`fe_app.*`), renderer/sound implementation, inherited shaders and the old host visual harness;
- PS2UI-derived presentation markup and any animation, layout or component copied from another emulator/interface.

The PS4 target enters through `caliza/native/ps4_shell.*`; the old cover-flow implementation is no longer linked or present. Retained service files still carry inherited naming/comment lineage and should be split into `orbis/services` only after the current integration is committed and validated.

## Legacy or unrelated upstream material

The inherited desktop Qt interface, GS-runner, updater, desktop CMake/CI metadata, legacy PS5 installer, prototype and metadata trees have already been removed as non-product material. Remaining non-runtime material needs this same test:

| Path | Assessment |
| --- | --- |
| `ps5/docs/` | Removed as a separate legacy-docs tree. Useful technical lineage notes now live in `ps4/docs/lineage/`; inherited banner artwork was deleted. |

## Naming cleanup plan

1. Estratos integration and cover-flow removal are now implemented in the working tree; commit and validate that state first.
2. Move `ps5/coreorbis` to `orbis/core` and update include/build paths in one mechanical commit.
3. Move required console dependencies to `orbis/third_party`.
4. Split retained frontend services into `orbis/services`; condemned presentation files have already been deleted.
5. The unreferenced legacy PS5 installer, prototype and metadata trees have been removed; validate the remaining dependency graph before any further deletion.
6. Rename the public repository only as a separate explicit operation. GitHub reports that `felipebasurto/caliza` is currently unclaimed; the existing remote still uses the retired slug and has not been changed by this cleanup.

## Influences, lineage and attribution

Caliza is original product/interface work, but it is not built from nothing and must not hide its lineage:

- **PCSX2** — GPLv3 emulation core, compatibility behavior, memory-card/savestate formats and much of the underlying emulator architecture.
- **PS5SX2 / PS4SX2 lineage** — Orbis adaptation techniques, platform shims, Vulkan/CEMU runtime integration, service mechanics and hard-won hardware diagnostics. This is technical lineage, not visual authority.
- **PS2UI** — visual/material inspiration only for depth, contrast and rhythm. No PS2UI CSS, components, markup, shaders, animation or assets belong in the new implementation.
- **OpenOrbis, CEMUPS4/Crema, Mesa RADV and bundled third-party libraries** — platform/toolchain/runtime dependencies with their own licenses.

Attribution stays. Inherited presentation does not.
