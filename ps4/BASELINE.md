# PS4SX2 baseline (AI-assisted)

Captured 2026-11-25/26 UTC by the autonomous orchestrator.

## Repository state

- Workspace: `/Users/felipe/PS4SX2`.
- Branch: `ps4-perf` at `d082d64139cad10a3faaec2afaa23f0063208808` before this session's edits.
- `main`/`origin/main`: `7afa5c43e7827193ac165eecd5b36d34d07761be` (`PS4SX2: PCSX2 on the PS4, built on PS5SX2`).
- Remotes: `fork=https://github.com/felipebasurto/PS4SX2`, `origin=https://github.com/iHaiDeeZ/PS4SX2`.
- Raw evidence: `ps4/logs/git-baseline.txt`, `ps4/logs/main-to-ps4-perf.diff`, `ps4/logs/ps4-14-jit-fix.diff`.
- Prior merged work on `ps4-perf`: boot defaults, settings knobs, Jaguar pinning, Vulkan WSI correctness/instrumentation, MTVU cache/ring work, build knobs, rapidyaml/link fixes, container build documentation.
- Unmerged inherited candidate: `fork/fix/ps4-14.00-jit-boot` (`fda8e2a`), audited and adapted in this session rather than blindly merged.

## User-visible compatibility claimed by the inherited README

These are inherited compatibility claims, not newly verified in this session:

| Game | Claimed baseline |
|---|---|
| Grand Theft Auto: San Andreas (SLUS-20946) | Runs; menus/cutscenes full speed; gameplay about 50% speed (~18 fps), CPU-limited on base PS4 |
| Mortal Kombat: Shaolin Monks (SLUS-21087) | Runs; 15-27 fps in heavier scenes, GS CPU work limited |

Compatibility preservation rule: these games must not regress. No game test was run on a real console tonight.

## Toolchain baseline

Host is macOS arm64; OpenOrbis PS4 build is Linux x86_64 only. OrbStack was started successfully. Existing container `ps4w` (image `ps4sx2-snap`) contains:

- Ubuntu 24.04 x86_64 userland;
- clang/ld.lld/llvm 21;
- PacBrew OpenOrbis at `/opt/pacbrew/ps4/openorbis`;
- CEMUPS4 at `/root/cemups4`;
- sysroot + Mesa RADV at `/root/cemups4-build`;
- `ryml` and `shaderc` deps at `/root/pcsx2ps4-build/deps`.

Evidence: `ps4/logs/toolchain-inventory.txt`, `ps4/logs/docker-inventory.txt`, `ps4/logs/ps4w-start-inspect.log`, `ps4/logs/ps4w-deps-inspect.log`.

## Verified baseline build/package

A pristine source copy of commit `d082d64139cad10a3faaec2afaa23f0063208808` was built in the container with:

```sh
PCSX2PS4_BUILD=/tmp/ps4baseline-d082d64 \
OBJDIR=/tmp/ps4baseline-d082d64/obj \
JOBS=$(nproc) NARROW=1 LTO=0 \
bash ps4/build-ps4.sh package
```

Result: success.

| Artifact | Size | SHA-256 |
|---|---:|---|
| ELF `/tmp/ps4baseline-d082d64/build/link/pcsx2` | 141M | `c4b498d69c14a094d750351a3745e711d5fdb1d17ca59f954f57f0bb1f2eecbd` |
| `eboot.bin` | 40M | `ba9cb7bfe0f88dca48dbe33ae74f4c3d05d8d4ac158c4a70519e1cdda2eac2a7` |
| PKG `IV0000-PCSX00002_00-PCSX2PS400000000.pkg` | 57M | `9d1f4253d1d371badc890426dd04a9324200f1a8620a451c224f1331a0e41095` |

Other measurements:

- 583 objects compiled.
- Build tree about 720 MiB.
- 148 compiler warning lines in `compile.log` (not yet triaged).
- 263 dynamic undefined imports; 17 NEEDED libraries.
- PS5-only JIT imports present in baseline ELF: `sceKernelJitCreateSharedMemory`, `sceKernelJitMapSharedMemory`.
- Old `llvm-nm -u` import audit produced 8005 apparent unresolved names, almost entirely false positives from weak/static Vulkan/Mesa symbols. Correct dynamic audit evidence: `ps4/logs/dynamic-imports-existing.txt`, `ps4/logs/dynamic-unresolved-existing.txt`.
- No BIOS directory existed in the build environment; inherited package build did not intentionally bundle BIOS files.

Evidence: `ps4/logs/baseline-build-d082d64.log`, `ps4/logs/baseline-build-metrics.txt`, `ps4/logs/jit-import-evidence.txt`.

## Baseline host tests

Run inside the Linux container against the pristine source:

| Test | Result |
|---|---|
| frame-generation pacing | PASS, 911/911 checks |
| keyboard/mouse pad mapping | PASS |
| controller remapping | PASS |
| texture root discovery | PASS |

Evidence: `ps4/logs/host-tests-baseline.log`.

## Immediate baseline conclusions

1. Build and PKG are reproducible in the inherited container.
2. The largest platform-critical defect found statically is the pair of PS5-only JIT dynamic imports. OpenOrbis stub libraries export them, so a naive export-index audit hides the runtime risk.
3. RADV detection should be true in the inherited link: the ELF contains a local `radv_GetInstanceProcAddr` symbol. Evidence: `ps4/logs/radv-export-check.log`.
4. Packaging was legally clean only by environmental accident when `~/pcsx2ps4-build/bios` was absent; the implicit fallback needed hardening.
