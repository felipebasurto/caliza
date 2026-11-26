# Caliza PS4 agent guide

## Project overview

Caliza PS4 is a couch-friendly PlayStation 2 emulator for homebrew-capable PlayStation 4 consoles. It uses the PCSX2 emulation core and an inherited Orbis platform layer, while the product interface, companion panel, storage flows and branding are original Caliza work.

The active product targets PS4 only. Desktop Qt, updater, GS-runner and legacy PS5 installer/prototype trees have been removed because they are not part of this product. Do not recreate inherited interface code unless it is needed as temporary behavioral reference outside the shipped target.

Read `REPO-MAP.md` before moving or deleting files. Read `ps4/CALIZA-UI-ARCHITECTURE.md` and `caliza/core/contract.h` before touching the interface, storage, settings or migration.

## Active structure

- `caliza/` — new interface, backend contract, settings schema, storage/migration, native renderer, host preview and companion web panel.
- `ps4/` — PS4 build, OpenOrbis packaging, validation tools, metadata, documentation and hardware QA tooling.
- `pcsx2/` — upstream emulation core. Preserve compatibility and attribution.
- `common/`, `3rdparty/` — shared upstream code and dependencies used by the core.
- `ps5/coreorbis/` — inherited Orbis platform layer, currently misnamed for historical reasons. It is required by the PS4 build and is planned to move to `orbis/core/` after interface integration.
- `ps5/frontend/` — transitional non-presentation services only. The old cover-flow presentation is condemned and must not enter the shipped target.

## Non-negotiable product rules

- Never commit BIOS, game images, keys or private user data.
- Preserve GPL and third-party attribution. Do not remove copyright notices.
- Do not copy interface views, shaders, CSS, components, animations, sounds or art from other emulator frontends. PCSX2 emulation code and platform services may be reused with attribution.
- `/data/caliza` is the canonical console data root. Legacy roots are migration inputs only and must never be deleted by migration.
- The retired product name must not appear as a contiguous string in source or user-visible copy. Run `python3 ps4/tools/audit_brand.py`; it must report zero references. Compatibility with old installations belongs in `caliza/core/legacy_compat.h` and migration tests.
- Do not change the installed title identifier or public repository slug without an explicit user decision.
- Do not create issues or pull requests. Do not rewrite git history.

## Verification

Fast host checks:

```sh
bash tests/caliza/run-all.sh
python3 tests/caliza/audit_feature_parity.py
```

The fast suite covers the UI model, schema, settings projection, console backend, contract, storage, migration, web smoke test and dead-brand audit. It must pass as an unprivileged host user. Permission tests can be invalid when run as root.

Existing port validation:

```sh
bash ps4/tools/run-host-tests.sh /tmp/caliza-host-tests
```

PS4 target/package validation on macOS uses the Linux OpenOrbis container:

```sh
docker exec caliza-dev sh -lc 'cd /src && bash ps4/build-ps4.sh package'
```

A successful compile is not hardware evidence. Clearly separate host evidence, package validation and real-console results.

## Coding guidance

- Keep mutation logic behind the backend command contract. The UI must not grow duplicate settings, save, import or VM-boot logic.
- Scanning and migration must be resumable/cancelable and must not block input or rendering.
- Use atomic writes for settings and user state.
- Preserve external game images in place; never move, format or write to a volume just to detect it.
- Destructive actions need confirmation with Cancel selected first.
- Add focused tests for behavior changes and mark hardware-dependent assumptions as needing proper testing.
