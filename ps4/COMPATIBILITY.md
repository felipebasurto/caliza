# Zeruko compatibility matrix (AI-assisted)

> **Rebrand note (2026-11-26):** this product is now **Zeruko** (repository: <https://github.com/felipebasurto/zeruko>), a separate GPL-3.0-or-later product built on the PCSX2 core - no longer branded as a PCSX2 fork variant. Evidence, hashes and commit references dated before the rebrand use the old PS4SX2/PS5SX2 identity and `/data/PCSX2` paths (shown here already renamed where operational); they remain valid historical records. The data root is now `/data/zeruko` and the first boot after upgrading copies the user's settings, memory cards, saves, BIOS and patches from the old folder automatically (games and texture packs are read from the old folder in place - nothing is moved or deleted). See `ps4/BRAND.md` for the identity guide.


This matrix separates inherited claims, static evidence, host verification, and console verification. Do not treat a row as proven on PS4 unless the evidence column says so.

## Game-level matrix

| Title | Serial | Status | Evidence | Settings/notes |
|---|---|---|---|---|
| Grand Theft Auto: San Andreas | SLUS-20946 | Inherited claim: runs; menus/cutscenes full speed; gameplay ~50%/~18 fps | README only; not retested tonight | CPU-limited on base PS4; widescreen patch claimed working |
| Mortal Kombat: Shaolin Monks | SLUS-21087 | Inherited claim: runs; 15-27 fps heavy scenes | README only; not retested tonight | GS renderer CPU work limited |

## Component matrix

| Component | State | Evidence/verification | Risk/notes |
|---|---|---|---|
| Build compile/link/package | Verified host/container | 584 objects; ELF, eboot, PKG produced; final candidate evidence in `ps4/STATUS.md` | Not console-run |
| Dynamic imports | Statically audited | Current candidate: 257 dynamic imports, 0 unresolved, no known PS5-only imports | Runtime module behavior still needs console validation |
| Executable memory/JIT | Inherited evidence + static design | README games imply recompiler runs; Memory.cpp uses executable direct memory by default | Must be console-verified per firmware/loader; executable-memory probe failure now selects interpreter fallbacks rather than pretending recompilers are usable |
| Vulkan renderer/RADV | Implemented; not console-verified tonight | RADV statically linked; `radv_GetInstanceProcAddr` present in ELF; WSI shim documented | Runtime feature/synchronization health needs console logs |
| Software renderer fallback | Compiled and selectable | SW renderer objects in Makefile; renderer selection logic present | Very slow on Jaguar; correctness oracle only |
| Threading/MTVU | Implemented; MTVU default on for PS4 | Prior branch work; no runtime verification tonight | GameDB can still disable MTVU per game |
| Input | Implemented | host pad remapping tests pass; DS4 path inherited | Disconnect/hotplug handling improved; PS4 connected-byte layout remains console-unverified |
| Audio | Implemented + telemetry | `[audio]` underrun/output-error counters added | Runtime latency and user ID behavior unverified on PS4 |
| BIOS detection/loading | Implemented | `/data/zeruko/bios`; embedded personal BIOS copy path exists | Redistributable PKG must remain BIOS-free |
| ISO loading | Implemented + failure-path hardened | `/data/zeruko/games`, USB scanning inherited; truncated/corrupt reads now propagate errors, partial async reads fail, and retry exhaustion completes with a CDVD error instead of wedging | plain NFSv3 shares are exposed through `/net/shareN`; console scan/load/error recovery behavior unverified |
| Memory cards | Implemented + failure-path hardened | file/folder cards now bound out-of-range writes, retain dirty pages after host write failure, close metadata atomically, and expose storage failures to log analysis | Needs real console persistence, power-loss, and corruption-recovery tests |
| Save states | Implemented + hostile-input hardened, console-unverified | load path streams entries; fixed memory truncation, oversized vectors/strings, GIF queue geometry, HostFS handle counts, and USB/OHCI/control state are validated | Legitimate-state save/load compatibility and flexible-memory headroom need console validation |
| Frontend/shelf | Implemented through Vulkan WSI | fe_ps5 Vulkan shelf | CPU overlay VideoOut buffers API is PS5-only/stubbed |
| Cover downloads | Implemented, console-unverified | Uses the mbedTLS HTTPS client with Mozilla roots; HTTP/SSL system library dependencies removed | Test online, offline, DNS failure, redirect, and certificate failure on console |
| PS4 Pro mode | Enabled in package SFO | attribute `0x00810000`; Neo mode default 2x upscale | Not measured on Pro |
| Smart tune auto-tuner | Implemented, experimental, compile-validated only | per-game sheet row arms a one-shot boot flag; ladder sweep applies live through gs.ini pipeline and persists the winner in the game's ini | Requires in-game validation with `[smarttune]` log lines; menu-scene measurements produce wrong winners by design |

## Required console smoke protocol

When a console is available, run exactly this sequence and preserve `/data/zeruko/boot.log`, `emulog.txt`, `settings.log`:

0. Install only the candidate whose current expected PKG SHA-256 is recorded in `ps4/STATUS.md`; verify that hash before installation.
1. Cold boot to shelf.
2. Confirm BIOS detection without any BIOS in PKG.
3. Start a legally owned test ISO at native resolution.
4. Record 60 seconds with default settings.
5. Toggle MTVU off/on only after restart and compare.
6. Test hardware renderer and software renderer on the same scene.
7. Disconnect/reconnect player 1 controller while input is held.
8. Save and load state slot 1.
9. Return to shelf and confirm memory-card/NVRAM flush.
10. Record GPU/audio/input/network lines: `[ps4wsi]`, `[audio]`, `[pad]`, `[covers]`, `[https]`, `[vkwait]`, `[threads]`, `[vsslow]`, `[copies]`, `[readbacks]`.
11. Optional USB input test: create empty `/data/zeruko/flags/usbkbm`, restart, connect a USB keyboard/mouse, and preserve `[kbm]` lines. Remove the flag if module loading or input misbehaves.
