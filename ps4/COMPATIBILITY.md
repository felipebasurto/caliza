# PS4SX2 compatibility matrix (AI-assisted)

This matrix separates inherited claims, static evidence, host verification, and console verification. Do not treat a row as proven on PS4 unless the evidence column says so.

## Game-level matrix

| Title | Serial | Status | Evidence | Settings/notes |
|---|---|---|---|---|
| Grand Theft Auto: San Andreas | SLUS-20946 | Inherited claim: runs; menus/cutscenes full speed; gameplay ~50%/~18 fps | README only; not retested tonight | CPU-limited on base PS4; widescreen patch claimed working |
| Mortal Kombat: Shaolin Monks | SLUS-21087 | Inherited claim: runs; 15-27 fps heavy scenes | README only; not retested tonight | GS renderer CPU work limited |

## Component matrix

| Component | State | Evidence/verification | Risk/notes |
|---|---|---|---|
| Build compile/link/package | Verified host/container | 583 objects; ELF, eboot, PKG produced; `baseline-build-metrics.txt` | Not console-run |
| Dynamic imports | Statically audited | Final candidate: 260 dynamic imports, 0 unresolved, no known PS5-only imports | Runtime module behavior still needs console validation |
| Executable memory/JIT | Inherited evidence + static design | README games imply recompiler runs; Memory.cpp uses executable direct memory by default | Must be console-verified per firmware/loader; interpreter fallback not automatic |
| Vulkan renderer/RADV | Implemented; not console-verified tonight | RADV statically linked; `radv_GetInstanceProcAddr` present in ELF; WSI shim documented | Runtime feature/synchronization health needs console logs |
| Software renderer fallback | Compiled and selectable | SW renderer objects in Makefile; renderer selection logic present | Very slow on Jaguar; correctness oracle only |
| Threading/MTVU | Implemented; MTVU default on for PS4 | Prior branch work; no runtime verification tonight | GameDB can still disable MTVU per game |
| Input | Implemented | host pad remapping tests pass; DS4 path inherited | Player-1 disconnect bug fixed in candidate; needs console test |
| Audio | Implemented + telemetry | `[audio]` underrun/output-error counters added | Runtime latency and user ID behavior unverified on PS4 |
| BIOS detection/loading | Implemented | `/data/PCSX2/bios`; embedded personal BIOS copy path exists | Redistributable PKG must remain BIOS-free |
| ISO loading | Implemented | `/data/PCSX2/games`, USB scanning inherited | NFS is effectively unsupported on PS4 candidate until wrap layer ported |
| Memory cards | Implemented | inherited PCSX2/PS5SX2 code | Needs console persistence test |
| Save states | Implemented | custom format inherited | Large-state allocation rule violation documented in audit |
| Frontend/shelf | Implemented through Vulkan WSI | fe_ps5 Vulkan shelf | CPU overlay VideoOut buffers API is PS5-only/stubbed |
| Cover downloads | Broken/unimplemented on PS4 | `sceHttp2*` compatibility stubs return unsupported | Needs libSceHttp port or alternate curl implementation |
| PS4 Pro mode | Enabled in package SFO | attribute `0x00810000`; Neo mode default 2x upscale | Not measured on Pro |

## Required console smoke protocol

When a console is available, run exactly this sequence and preserve `/data/PCSX2/boot.log`, `emulog.txt`, `settings.log`:

1. Cold boot to shelf.
2. Confirm BIOS detection without any BIOS in PKG.
3. Start a legally owned test ISO at native resolution.
4. Record 60 seconds with default settings.
5. Toggle MTVU off/on only after restart and compare.
6. Test hardware renderer and software renderer on the same scene.
7. Disconnect/reconnect player 1 controller while input is held.
8. Save and load state slot 1.
9. Return to shelf and confirm memory-card/NVRAM flush.
10. Record GPU/audio lines: `[ps4wsi]`, `[audio]`, `[vkwait]`, `[threads]`, `[vsslow]`, `[copies]`, `[readbacks]`.
11. Optional USB input test: create empty `/data/PCSX2/flags/usbkbm`, restart, connect a USB keyboard/mouse, and preserve `[kbm]` lines. Remove the flag if module loading or input misbehaves.
