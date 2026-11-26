# P1-UPSTREAM — What upstream PCSX2 x86-64 expects architecturally

Research deliverable for the PCSX2-on-PS4 port. All findings verified against the
working tree at `/Users/felipe/PS4SX2` (branch `ps4-perf` @ `3b070bf`).
All work AI-assisted. No files outside `ps4/reports/` were modified.

## 1. Upstream vintage

- Single import commit `37615f27180913991cbdf86bd4de2ec0fdfeea07`
  "Import PCSX2 upstream at 450a39a" — upstream PCSX2 commit
  `450a39a37cb2353da7ae3a492cfb038af65bc46c` ("[ci skip] Qt: Update Base
  Translation.", 2026-09-05; verified on github.com/PCSX2/pcsx2).
- No upstream git history; every port change is a separate commit on top.
  `git log` shows the port delta is identifiable per-file.
- `pcsx2/BuildVersion.{h,cpp}` reads a build-time-generated `svnrev.h`.
- The PS5 console layer lives in `ps5/` (entry `ps5/coreorbis/main-boot.cpp`,
  host shims `ps5/coreorbis/orbis-shims/`). The PS4 layer is `ps4/`
  (Makefile/link/pkg + `ps4_compat.cpp`, `ps4_vk_wsi.cpp`, CEMUPS4 runtime).
- Build defines that matter: `ORBIS_VULKAN` (Vulkan GS path), `ORBIS_PS4`
  (PS4 deltas), `ORBIS_ORDER`, `PS5SX2_ACHIEVEMENTS` (rcheevos softcore).

## 2. The host boundary — `Host::` (the main port hook)

Upstream core never talks to a UI directly; it calls `Host::` functions
declared in `pcsx2/Host.h` (lines 23–185) plus the VM-lifecycle hooks at the
bottom of `pcsx2/VMManager.h` (~line 340–385: `OnSaveStateLoading/Loaded/Saved`,
`OnGameChanged`, `PumpMessagesOnCPUThread`).

Surface area:

- **Strings/i18n**: `TranslateToCString/StringView/String`,
  `TranslatePluralToString`, `ClearTranslationCache`. The port's
  `tests/ctest/core/StubHost.cpp:325` (`GetTranslatedStringImpl`) copies the
  English msg verbatim — in-core strings are English-only on console; the
  frontend has its own i18n (`fe_i18n.cpp`).
- **OSD**: `AddOSDMessage`, `AddKeyedOSDMessage`, `AddIconOSDMessage`,
  `RemoveKeyedOSDMessage`, `ClearOSDMessages` — implemented as near-no-ops in
  `ps5/coreorbis/orbis-shims/ProsperoUI.cpp:22-61` (achievements messages are
  forwarded to `OrbisNotifyPlain` under `PS5SX2_ACHIEVEMENTS`).
- **Thread hops**: `RunOnCPUThread(fn, block)`, `RunOnGSThread(fn)`.
- **Settings**: base getters/setters (`GetBase*SettingValue`,
  `SetBase*SettingValue`, `CommitBaseSettingChanges`), locked accessors
  (`Get*SettingValue`, `GetSettingsLock`, `GetSecretsSettingsLock`,
  `GetSettingsInterface`), and the layer stack `Internal::SetBaseSettingsLayer /
  SetSecretsSettingsLayer / SetGameSettingsLayer / SetInputSettingsLayer`.
  The console wires these in `main-boot.cpp` ~lines 3239–3244
  (`s_base_si`, `s_game_si`, `s_input_si`).
- **VM lifecycle**: `RequestVMShutdown`, `OnVMStarting/Started/VMDestroyed/
  Paused/Resumed`, `OnGameChanged`, `OnPerformanceMetricsUpdated`,
  save-state notifications. Most are no-ops in StubHost — VM lifecycle is
  driven by `main-boot.cpp` re-executing the app (frontend → game → menu).
- **Per-vsync pump**: `Host::PumpMessagesOnCPUThread()` is called once per
  guest vsync on the CPU thread (`VMManager.cpp:2998`, inside
  `PollInputOnCPUThread`). The console implementation
  (`StubHost.cpp:257-323`) is *the* place for per-frame host work: CPU
  sampling (`OrbisCpuSample`), USB kbd/mouse injection (`OrbisKbdMousePump`),
  EE profiler (`OrbisEEProfStart`), widescreen (`OrbisWidescreenTick`),
  `gs.ini` hot-reload, thread pinning requests, readback auto, and the
  `g_orbis_state_request` atomic dispatch (1=save slot 1, 2=load slot 1,
  3=back to menu, 4=fast-forward toggle, 5/6=disc change).

**Build note**: the PS4 binary compiles `pcsx2/Host.cpp` (real settings-layer
plumbing) *and* `tests/ctest/core/StubHost.cpp` as the `Host::` frontend
(`ps4/Makefile.ps4:263,462`). `pcsx2-qt/` is not part of the console build.

## 3. VM lifecycle and CPU-thread model

- Entry: `int main()` at `ps5/coreorbis/main-boot.cpp:2759`. Boot order:
  system-module loads (`orbis_ps4_load_system_modules`, `ps4/ps4_compat.cpp:49`),
  `/data/PCSX2` layout via `OrbisDir`, settings layers, `EmuConfig` overrides
  (~lines 3440–3458: renderer pick, `SWExtraThreads`, recompiler enables,
  speedhacks `WaitLoop/IntcStat/vu1Instant/vuFlagHack/vuThread`,
  achievements off unless `PS5SX2_ACHIEVEMENTS`), `CPUThreadInitialize`
  pre-init, `SysMemory::Reset()` pre-init (required — recompiler's first
  memory access would otherwise hit an uninitialized RWFT handler),
  `VMManager::Initialize` (3608), then `VMManager::Execute` on the CPU thread
  (3798/3959). A pad thread (`orbis_pad_thread`, 3684) and a 1 Hz ticker
  thread (3808) run alongside; an init watchdog prints progress (3465).
- `VMManager::Execute` (`VMManager.cpp:2804`) → `Cpu->Execute()`. It first
  handles interpreter↔recompiler switches (`s_cpu_implementation_changed` →
  `UpdateCPUImplementations` + `ClearCPUExecutionCaches` +
  `vtlb_ResetFastmem`).
- CPU dispatch (`UpdateCPUImplementations`, `VMManager.cpp:2746`), gated on
  `_M_X86` (upstream only ships x86 dynarecs; other arches are
  interpreter-only):
  - `Cpu` (`R5900cpu*`, `R5900.h:355`): `recCpu` vs `intCpu` per
    `CHECK_EEREC` (`EmuConfig.Cpu.Recompiler.EnableEE`).
  - `psxCpu` (`R3000Acpu*`, `R3000A.h:189`): `psxRec` vs `psxInt` per
    `CHECK_IOPREC`.
  - `CpuVU0`/`CpuVU1` (`BaseVUmicroCPU*`, `VUmicro.h:167+`):
    `CpuMicroVU0/1` vs `CpuIntVU0/1` per `EnableVU0/EnableVU1`.
  - `GSDumpReplayer` overrides all four when replaying a dump.
- `R5900cpu` is a function-pointer vtable (`R5900.h` ~300–352): Reserve,
  Reset, Step, Execute, ExitExecution, CancelInstruction, Clear. The
  interpreter (`intCpu`, `pcsx2/Interpreter.cpp:719`) and recompiler
  (`recCpu`, `pcsx2/x86/iR5900.cpp`) both implement it — this contract is
  what lets speedhack/UI code swap CPU cores at runtime.
- Thread naming/affinity: `CPUThreadInitialize` names the thread "CPU Thread";
  `InitializeProcessorList` sorts by frequency and ignores SMT;
  `GetSoftwareRendererProcessorList` picks same-cluster cores for the SW
  rasterizer; `SetEmuThreadAffinities` applies pinning. On PS4,
  `cpuset_setaffinity` is an `ENOSYS` stub (`ps4_compat.cpp:16-26`), so
  pinning is skipped with a logged error.

## 4. Memory, fastmem, and the code cache

- `pcsx2/Memory.h` `HostMemoryMap` (~lines 22–147) fixes the entire guest VA
  layout: EE main RAM ~143 MiB at offset 0, IOP RAM, VU micro memories, VTLB
  maps, then per-recompiler **code regions**: `EErec` 40 MiB,
  `IOPrec` 4 MiB, `VIF0rec` 2 MiB, `VIF1rec` 4 MiB, `mVU0rec` 6 MiB,
  `mVU1rec`, `VIFUnpackRec`, `SWrec` (SW rasterizer JIT). Accessors
  `SysMemory::GetEERec()`, `GetCodePtr(offset)`, etc. Any change to region
  sizes/offsets is an ABI break for already-emitted code paths — treat as
  frozen.
- `pcsx2/Memory.cpp` declares the Orbis allocators (`orbis_self_alloc`,
  `orbis_alloc_pool`, `orbis_alloc_jit`, `sceKernelAllocateDirectMemory`,
  `sceKernelMapDirectMemory`, `sceKernelReserveVirtualRange`, lines ~89–108)
  and maps vtlb blocks/handlers in `memMapPhy`/`memMapVUmicro` (~521–601).
- Fastmem: `pcsx2/vtlb.cpp` reserves `FASTMEM_AREA_SIZE = 0x100000000`
  (4 GiB) via `SharedMemoryMappingArea::Create(size, jit)` in
  `vtlb_Core_Alloc` (~line 1383). Mappings tracked in
  `s_fastmem_virtual_mapping`/`s_fastmem_physical_mapping`; faulting
  load/stores are backpatched to slowmem handlers via
  `s_fastmem_backpatch_info`/`vtlb_BackpatchLoadStore` (~line 1120).
  `vtlbdata.fastmem_base` is the base pointer JITed code uses.
- `common/HostSys.h:139` `SharedMemoryMappingArea` is the platform hook;
  implementations in `common/Linux/LnxHostSys.cpp` (Orbis: flexible-memory
  mmap shim, direct-memory fastmem path, `OrbisMcontext`-based page-fault
  decoding at ucontext offsets 0x88/0x98/0xa0) and
  `common/Windows/WinHostSys.cpp`.
- Orbis primitives: `ps5/coreorbis/orbis-shims/mmapshim.cpp` maps
  `mmap`→`sceKernelMapFlexibleMemory` (RWX prot=7, RW=3 fallback) and
  `orbis_reserve_range`→`sceKernelReserveVirtualRange`; `orbismem.cpp`
  provides `orbis_alloc_pool` (2 MiB-aligned RW) and `orbis_alloc_jit`
  (RWX via `sceKernelJit*SharedMemory`).
- PS4-specific: `sceKernelEnableDmemAliasing{,2}` must be called before
  direct-memory aliases work (`ps4_compat.cpp:85-98`); fastmem uses direct
  memory (`g_orbis_data_phys`) under `ORBIS_VULKAN`.
- Page faults: `PageFaultHandler` (common) decodes faults; the GS thread
  installs a secondary handler (`MTGS.cpp` ~168) for SMC write traps during
  `InitAndReadFIFO`. `fastjmp_set`/`fastjmp_jmp` (`common/FastJmp.*`) give
  JIT code a safe longjmp-style exit.

## 5. Recompilers — do not broadly refactor

All dynarecs are x86-64 only and extremely fragile; upstream marks the area
`TODO(Stenzek): Remove me once EE/VU/IOP recs are added` (`VMManager.cpp:2760`).

- **EE rec**: `pcsx2/x86/iR5900.h:15` `#define RFASTMEMBASE x86Emitter::rbp`
  — emitted code keeps the fastmem base in `rbp`; `RTEXTPTR = rbx`
  (`common/emitter/x86types.h:638`). `_isAllocatableX86reg`
  (`pcsx2/x86/iCore.cpp:34`) reserves rbp/rbx/arg regs. Dispatch/reset in
  `ix86-32/iR5900.cpp` (`recReserve`/`recResetRaw`/`recExecute`,
  ~766–1043); `EnterRecompiledCode` loads `vtlbdata.fastmem_base` into rbp
  when `CHECK_FASTMEM` (~604); `recLUT` tables; heavy `[dbg]` printf
  instrumentation is port-added. PS4 delta: `orbis_probe_rsp` removed under
  `ORBIS_PS4` (~575) because musl printf SIGBUSes on unaligned stack.
- **EE memory ops**: `ix86-32/recVTLB.cpp` emits `[RFASTMEMBASE + vaddr]`
  with `DynGen_HandlerTest` slowmem fallback; Orbis-specific emitter fixes
  for r12–r15/rbp/rsp in const-address paths (~449–581);
  `vtlb_DynBackpatchLoadStore` (~1102) implements fault backpatching.
- **IOP rec**: `pcsx2/x86/iR3000A.cpp` allocates from `SysMemory::GetIOPRec()`
  (~974–997).
- **microVU**: `pcsx2/x86/microVU.cpp` caches `GetVU0Rec`/`GetVU1Rec` (~33);
  `entryCacheGeneration` is bumped under `ORBIS_VULKAN`. VU1 can run on the
  MTVU thread (below). Interpreter VUs are `InterpVU0/1`
  (`VUmicro.h:83,101`).
- **VIF**: `Vif_UnpackSSE.cpp` (~370) and `Vif_Dynarec.cpp:13-16` (uses
  `VIF0rec`/`VIF1rec` offsets via `SysMemory::GetCodePtr`).
- **Config gates** (`Config.h:1502-1512`): `REC_VU1`, `THREAD_VU1`,
  `CHECK_EEREC`, `CHECK_CACHE`, `CHECK_IOPREC`, `CHECK_FASTMEM` — derived
  from `EmuConfig.Cpu.Recompiler.EnableEE/IOP/VU0/VU1`, `EnableEECache`,
  `EnableFastmem` (Config.h:632–657).

## 6. Threading model

Upstream expects exactly these cooperating threads; the ring protocols are
SPSC with cache-line-separated positions — do not change their layout.

- **CPU/EE thread**: runs `VMManager::Execute` → `Cpu->Execute()`. Per guest
  vsync it runs `VSyncOnCPUThread` (`VMManager.cpp:2973`: pad macro buttons,
  `Patch::ApplyVsyncPatches`, frame-advance, `Achievements::FrameUpdate`,
  Discord poll) and `PollInputOnCPUThread` (2991: `Host::PumpMessagesOnCPUThread`
  + `InputManager::PollSources` + input recording). Input sources are
  `pcsx2/Input/{DInput,SDL,XInput}InputSource.cpp`; the port adds pad/keyboard
  paths via `ProsperoInput.cpp`, `ProsperoPadMap.h`, `ProsperoKbdMouse.cpp`.
- **GS thread**: `MTGS::StartThread` (`MTGS.cpp:137`) — 16 MiB stack,
  named "GS", installs secondary page-fault handler + default FP rounding,
  then `MainLoop` (~347) consumes the command ring (`GIFPath1-3`, `VSync` →
  `GSvsync` ~609, `Freeze`, `GSPacket`, `MTVUGSPacket`, `AsyncCall`…).
  `s_ReadPos`/`s_WritePos` live on separate cache lines; port added
  `OrbisMtgsEE` (~74–96). Ring is `RingBufferSizeFactor=17` (~8 MiB of u128).
  `Host::RunOnGSThread`, `MTGS::WaitGS`, `MTGS::Freeze` (used by savestates)
  are the EE→GS synchronization surface.
- **MTVU VU1 thread**: `VU_Thread vu1Thread` (`MTVU.h:203`), opened from
  `microVU.cpp:338` when `THREAD_VU1`. 16 MiB ring
  (`buffer_size = _1mb*16/sizeof(s32)`), `m_ato_read_pos`/`m_ato_write_pos`
  on separate cache lines, kick coalescing (`m_defer_kicks`/`FlushKick`),
  `ExecuteRingBuffer` (`MTVU.h:163`), telemetry `g_orbis_mtvu_*` counters
  (190–203). VU1 interpreter/microVU both feed it.
- **SPU2**: runs on the CPU thread (`SPU2::Open/Close` in VMManager
  ~1580/1742; `SetOutputPaused`, `OnTargetSpeedChanged`,
  `CheckForConfigChanges`). Audio output is `AudioStream::CreateStream`
  (`spu2.cpp:120`) → port's `OrbisAudioStream`
  (`ps5/coreorbis/orbis-shims/ProsperoAudio.cpp`): `sceAudioOutOpen`
  (userId 255, FLOAT_STEREO, 48 kHz, 256-frame grains), blocking
  `sceAudioOutOutput` on its own thread. Cubeb/SDL backends are stubbed empty.
- **SW rasterizer**: `GSJobQueue<T,CAPACITY>` (`GS/GSJobQueue.h`) —
  std::thread + WorkSema + spsc ringbuffer; `GSRasterizerList::Create(threads)`
  (`GSRasterizer.cpp:1612`) pins workers via the processor list; also used by
  `GSPng`.
- **Misc**: pad thread, frontend/shelf thread, ticker thread (flag refresh +
  stats), boot-log pump thread, profiler thread — all port-owned, outside
  upstream invariants.

## 7. Renderers

- `GSRendererType` enum (`Config.h:271`): Auto/DX11/DX12/Metal/OGL/VK/SW/Null/
  ParallelGS (+ port value 18 = paraLLEl-GS, see `main-boot.cpp:3627`).
  `GSUtil.cpp:276` `GetPreferredRenderer`; SW codegen split x86 vs arm64 in
  `pcsx2/CMakeLists.txt` (~473–483, 568–580).
- Boot picks renderer in `main-boot.cpp:3442`: `GSConfig.Renderer =
  (g_use_gl_renderer && !g_sw_renderer) ? orbis_gpu_renderer() : SW`,
  `SWExtraThreads = 4 (SW) / 2`.
- **Vulkan** (`USE_VULKAN`, ON by default — `cmake/BuildParameters.cmake:24`;
  sources `pcsx2/CMakeLists.txt:629-650`):
  - `VKLoader.cpp`: under `ORBIS_VULKAN` the driver is **linked into the
    eboot** (no dlopen possible); `vk_icdGetInstanceProcAddr` substitutes for
    `vkGetInstanceProcAddr` (~lines 45–80).
  - Required device extension: `VK_KHR_PUSH_DESCRIPTOR`
    (`GSDeviceVK.cpp:178`); surface extension chosen by `WindowInfo::Type`
    (278–295); optional `KHR_swapchain_maintenance1`,
    `KHR_driver_properties`, `KHR_shader_non_semantic_info`,
    `EXT_fragment_shader_interlock` (520–548) with
    `fragmentShaderPixelInterlock` feature (736–767).
  - Shaders: `VKShaderCache.cpp` + port `orbis_gs_shaders.cpp` /
  `ps5_shaderc.cpp`; on GS init failure `orbis_set_aside_shader_caches()`
  (`main-boot.cpp:3655`).
  - Timing/telemetry: `VKOrbisTiming.h` emits `[vkwait]`/`[shaders]` lines.
- **PS4 WSI**: `ps4/ps4_vk_wsi.cpp` (1282 lines) `--wrap`s
  `vk_icdGetInstanceProcAddr` and answers `VK_KHR_surface`/`display`/
  `swapchain` itself over VideoOut + CEMUPS4 RADV (`ac_ps4_submit_flip`):
  one display "PSD1", one 1920×1080@60 mode "PSM1", 3 host-visible linear
  images registered as VideoOut buffers (2 MiB aligned), presents queue a
  flip behind submitted GPU work. VideoOut has 16 buffer slots consumed in
  groups of 4 with retire/retry logic; `Ps4WsiPresentStats` counters logged
  once/sec on failures only.
- GL path exists (`ps5-opengl`, `orbis_gl_probe`, CPU overlay ownership rules
  in `main-boot.cpp:3572-3596`) but Vulkan is the primary console renderer.

## 8. GameDB and per-game settings

- `pcsx2/GameDatabase.cpp` (1481 lines): `initDatabase()` parses
  `EmuFolders::Resources/GameIndex.yaml` via ryml (~1026–1075);
  `findGame(serial)` (1235); hash database (`loadHashDatabase` 1336,
  `lookupHash` 1403, `TrackHash`); `GameEntry::applyGameFixes /
  applyGSHardwareFixes` mutate `EmuConfig`/`EmuConfig.GS`.
- Application point: `VMManager::ApplyGameFixes` (`VMManager.cpp:737-755`),
  called from `ApplyCoreSettings`/per-game settings load; ends with
  `MaskUpscalingHacks()`. BIOS boot forces `InstantDMAHack` and disables
  manual HW hacks (741–746).
- Port addition under `ORBIS_VULKAN`: `kOrbisSpeedHackFixes[]` (~1089) —
  per-serial MTVU/EE-cycle overrides for games that stall under PS5SX2
  defaults (Contra, GT3 PAL, Castlevania…).
- Per-game settings: `GameSettings/<serial>_<crc>.ini` or `<crc>.ini`
  (`VMManager.cpp:829-830`); input profiles `InputProfiles/<name>.ini`;
  console `/data/PCSX2/settings/<disc image name>.ini`.
- Patches: `.pnach` in `EmuFolders::Patches`; `Patch::ApplyVsyncPatches`
  per vsync; cheats dir exists but console UI is stubbed.

## 9. Savestates

- `pcsx2/SaveState.{h,cpp}`: `g_SaveVersion = (0x9A59 << 16) | 0`
  (`SaveState.h:29`); zip-archive entries; freeze components = SPU2 + GS via
  `MTGS::Freeze`/`SysState_MTGSFreeze` (`SaveState.cpp` ~352–393); version
  gate ~1141.
- API used by the port: `VMManager::SaveStateToSlot` /
  `LoadStateFromSlot` (dispatched from `g_orbis_state_request` in
  `StubHost.cpp:271-293`).
- `ps5/coreorbis/orbis-shims/ProsperoSaveState.cpp` only stubs the
  `SaveStateSelectorUI` slot-picker (ImGui not built) — core savestate code
  is fully compiled.
- Host notifications `OnSaveStateLoading/Loaded/Saved` are no-ops; errors
  surface via `OrbisOSDLabel("SAVE FAILED"/"LOAD FAILED")`.

## 10. Logging and diagnostics

- `common/Console.h` `Log::` levels NONE→TRACE; `Console`/`DevCon` writers;
  `SetFileOutputLevel`. `emulog.txt` opened via
  `EmuFolders::OpenLogFile` (`VMManager.cpp:538`).
- Orbis paths (`ps5/coreorbis/include-orbis/OrbisPaths.h`): everything under
  `/data/PCSX2/` with subdirs `games/ bios/ memcards/ savestates/ settings/
  patches/ textures/ resources/ cache/ flags/ logs/`; flat-layout fallback.
  `OrbisLogPath(name)` → `logs/`; `OrbisFlag(name)` reads switch files
  (`flags/` then top folder) snapshotted once/sec by the ticker — the
  port's runtime feature-toggle mechanism (`vk_async`, `swjit`,
  `glprobe.off`, `nooverlay`, …).
- `main-boot.cpp` rotates `boot.log`/`stderr.log`/`emulog.txt` (8 sessions),
  has a deferred boot-log pipe (`orbis_boot_log_pump`), `ps4_boot_trace`,
  `orbis_stage(stage)` markers, a 5-line `ps5::debug` overlay, init watchdog,
  and an once-a-second stats ticker. printf channels seen in-tree: `[boot]`,
  `[dbg]`, `[mtgs]`, `[ps4wsi]`, `[state]`, `[audio]`, `[vkwait]`,
  `[shaders]`, `[cpuclk]` (`OrbisEEVsyncClock`, Counters.cpp:496),
  `[eestat]` (MTVU counters).
- Crash: `orbis_install_signal_handlers` (`main-boot.cpp:3544`) +
  `ProsperoCrash.cpp`; PS4 `ps4_crash_*` symbols are intentional no-op stubs
  (`ps4_compat.cpp:31-37`) because CEMUPS4's crashlog expects them.

## 11. Timing and speed control

- `pcsx2/Counters.cpp`: `vsyncCounter` is a `SyncCounter`; PS2 vsync is an
  independent 59.94/50 Hz crystal (`UpdateVSyncRate`, ~338). `VSyncStart`
  (~491) calls `OrbisEEVsyncClock()` then `VSyncOnCPUThread`; `GSVSync`
  (~551) handles CSR swap/GS vblank IRQ ~3.5 hblanks later; execution is
  gated by `cpuTestCycle`. Do not retime this — it's the emulator's master
  clock.
- Speed: `VMManager::GetEffectiveVSyncMode` (~2840) → FIFO (blocking) vs
  Mailbox present mode; `ShouldAllowPresentThrottle` has an `ORBIS_VULKAN`
  framegen hold (~2860+). `LimiterModeType` Nominal/Turbo/Unlimited via
  `VMManager::SetLimiterMode`; `EmuConfig.EmulationSpeed.TurboScalar`,
  `SyncToHostRefreshRate`; `SPU2::OnTargetSpeedChanged` keeps audio in sync.
  Fast-forward toggled at runtime from `StubHost.cpp:298-322`.
- Speedhacks live in `EmuConfig.Speedhacks` (`Config.h:1143+`): `IntcStat`,
  `WaitLoop`, `vu1Instant`, `vuFlagHack`, `vuThread`, plus `EmuConfig.Gamefixes`.

## 12. Configuration plumbing

- `Pcsx2Config`/`EmuConfig` in `pcsx2/Config.h` (~1500 lines): Cpu.Recompiler.*,
  Speedhacks, Gamefixes, GS (Renderer, UpscaleMultiplier, VsyncEnable,
  DisableMailboxPresentation, ManualUserHacks, SWExtraThreads), SPU2 backend,
  Pad, Achievements, EmulationSpeed, Profiler.
- Load path: `VMManager::LoadSettings/LoadCoreSettings` →
  `Host::GetSettingsInterface()` under `GetSettingsLock()` → layered INI
  (base + per-game + input profile + secrets). `ApplySettings` /
  `ApplyCoreSettings` (`VMManager.cpp:772-830`) reset `EmuConfig`,
  `CopyRuntimeConfig`, reload, `CheckForConfigChanges` (CPU recache,
  `memBindConditionalHandlers`, SPU2 config check ~3186).
- Boot-time overrides in `main-boot.cpp` (~3440) run *before*
  `VMManager::Initialize`; flag files can flip them without rebuild.

## 13. Filesystem contract

`EmuFolders` (`Config.h:1455-1493`, impl `Pcsx2Config.cpp:154+`):
AppRoot, DataRoot, Settings, Bios, Snapshots, Savestates, MemoryCards, Logs,
Cheats, Patches, Resources, UserResources, Cache, Covers, GameSettings,
Textures, InputProfiles, Videos, DebuggerLayouts, DebuggerSettings — with
`SetAppRoot/SetResourcesDirectory/SetDataDirectory/SetDefaults/LoadConfig/
EnsureFoldersExist/OpenLogFile/GetOverridableResourcePath`.
The console maps these onto `/data/PCSX2/*` via `OrbisDir`; resources must
contain `GameIndex.yaml` (GameDB) and shader caches go to `cache/`.
BIOS is opt-in (`PCSX2_PS4_BIOS_DIR` at packaging; `/data/PCSX2/bios` on
console) — never bundled.

## 14. Other host-facing subsystems

- **Achievements**: `pcsx2/Achievements.cpp` (rcheevos `rc_client_t`,
  `HTTPDownloader`, `ClientReadMemory` callback into emulated RAM,
  `IdleUpdate`/`FrameUpdate` hooks). Port: `PS5SX2_ACHIEVEMENTS`,
  `ProsperoAchievements.cpp`, login/notify glue; disabled by default
  (`main-boot.cpp:3440,3454-3458`).
- **HTTP/IO**: `ProsperoHTTPDownloader.cpp`, `ProsperoHTTPConsole.cpp`,
  `ProsperoZip.cpp` (savestate/deploy zip), `ProsperoTLS.S`,
  `ProsperoNet.cpp`, `OrbisNfs.cpp` (PS4: effectively dead per STATUS P1-NFS),
  `ProsperoCDVD.cpp` (disc access).
- **Covers**: `sceHttp2*` are `kNotSupported` stubs (`ps4_compat.cpp:106-121`)
  — P1-COVERS; PS4 has `libSceHttp` only.
- **Input devices**: pad via `scePad` (`OrbisPadMap.h`), USB kbd/mouse →
  PS2 USB HID (`ProsperoKbdMouse.cpp`, pumped at vsync), `ProsperoInput.cpp`.
- **OS services**: `ProsperoNotify.cpp` (OSD→system notify),
  `ProsperoMisc.cpp`, `ProsperoUI.cpp`, `ProsperoHenJailbreak.cpp`,
  `ProsperoDiscord.cpp`, `demo_renderer.cpp`/`game_select.cpp` (shelf),
  `debug_overlay.cpp`, `ps5dbgclient.cpp`, `orbis_eeprof.cpp` (EE profiler),
  `orbis_memprobe.cpp`, frame generation (`ps5_framegen.cpp`, `framegen/`,
  `OrbisFrameGenPacing.h`).
- **Excluded from PS4 build** (`Makefile.ps4:217` filter, re-added selectively
  at 263–266, 462): `GSDumpReplayer.cpp`, `Achievements.cpp`, `PINE.cpp`,
  `PerformanceMetrics.cpp`, `IopModuleNames.cpp`; `MTGS.cpp`/`MTVU.cpp`/
  `Host.cpp`/`AudioStream.cpp` are re-added; `StubHost.cpp` supplies `Host::`.

## 15. Build system

- `ps4/Makefile.ps4`: `clang++-21`, `-target x86_64-pc-freebsd12-elf`,
  `-march=btver2` (PS4 Jaguar); knobs `NARROW`/`LTO`/`ORBIS_ORDER`;
  per-file flag overrides (e.g. `Host/AudioStream.o` line 578).
- `ps4/build-ps4.sh`: make → CMake link step (`ps4/link`, toolchain
  `ps4-cemu.cmake` from CEMUPS4 `~/cemups4`) → `make-pkg.sh` (PKG
  `IV0000-PCSX00002`, title "PS4SX2"). `build-pgs.sh` optional paraLLEl-GS.
- Upstream CMake: `USE_OPENGL`/`USE_VULKAN` ON (`BuildParameters.cmake:22-24`),
  PNG/ZLIB/Vulkan discovery (`SearchForStuff.cmake:14-30`). The console build
  bypasses most of this via the Makefile source list.

## 16. What NOT to refactor (boundaries)

Frozen / handle-with-care upstream internals:

1. **x86 recompilers** — `pcsx2/x86/ix86-32/*`, `iR3000A.cpp`, `microVU*`,
   `Vif_*`, `common/emitter/*`: register allocation (rbp=fastmem, rbx=rt),
   code-cache regions, recLUTs, backpatch encoding. Only surgical,
   console-verified fixes (the existing `ORBIS_PS4`/`ORBIS_VULKAN` ifdef
   sites are the pattern).
2. **vtlb/fastmem** — `vtlb.cpp`, `Memory.cpp` mapping tables,
   `SharedMemoryMappingArea` contract, backpatch info formats.
3. **Ring protocols** — `MTGS.cpp` command enum + cache-line positions,
   `MTVU.h` ring layout/sema pairing, `GSJobQueue` SPSC.
4. **Timing** — `Counters.cpp` vsync/hblank state machine, `cpuTestCycle`
   gating, limiter/present-mode logic.
5. **Savestate format** — `SaveState.cpp` zip layout + `g_SaveVersion`;
   changing it breaks existing `.p2s`.
6. **GameDB schema** — `GameIndex.yaml` keys/fix names are shared with
   upstream; port additions belong in the `ORBIS_VULKAN` block, not the yaml.
7. **`EmuConfig` semantics** — speedhacks/gamefixes layering and
   `CopyRuntimeConfig` behavior are load-bearing for per-game settings.

Appropriate hook boundaries (where new port work should go):

- `Host::` functions → `tests/ctest/core/StubHost.cpp` (per-frame pump,
  lifecycle) + `ps5/coreorbis/orbis-shims/ProsperoUI.cpp` (OSD/FullscreenUI).
- OS services → `ps5/coreorbis/orbis-shims/*.cpp` (audio, pad, http, zip,
  nfs, notify, crash, tls) and `ps4/ps4_compat.cpp` (PS4-only syscall
  shims/stubs).
- Memory/allocation → `mmapshim.cpp`, `orbismem.cpp`,
  `common/Linux/LnxHostSys.cpp` Orbis paths.
- Vulkan/WSI → `ps4/ps4_vk_wsi.cpp` (`--wrap` layer) and `VKLoader.cpp`
  `ORBIS_VULKAN` path; never ask RADV for window-system extensions it lacks.
- Runtime toggles → `OrbisFlag`/`flags/` files, not new config keys, unless
  the setting must persist per-game.
- Console-only deltas inside core files → narrow `#ifdef ORBIS_PS4` /
  `#ifdef ORBIS_VULKAN` blocks with a `vk-285-*`/`eerec-*` tag comment, as
  existing code does.
- User-visible strings → bilingual EN+ES (`TR()`/frontend `fe_i18n`); in-core
  `TRANSLATE` currently passes English through verbatim.

## 17. Gaps and risks worth tracking

- `Host::` is the ctest stub: `RequestVMShutdown`, clipboard, fullscreen,
  text input, resize are no-ops — anything upstream adds there silently does
  nothing on console.
- Savestate slot UI is stubbed; only pad-driven slot 1 save/load works.
- PS4-only dead ends already documented: NFS (`OrbisNfs`), cover downloads
  (superseded after this report by the mbedTLS client), `cpuset_setaffinity` (ENOSYS).
- `tests/ctest/core/StubHost.cpp` is compiled into the shipping eboot —
  upstream test-only code is now production host code; changes there affect
  both ctest and console.
- Interpreter fallback exists (`intCpu`/`psxInt`/`CpuIntVU*`) and
  `s_cpu_implementation_changed` can swap at runtime, but the port defaults
  to all recompilers + MTVU; keep interpreter paths buildable.
- Heavy `[dbg]` printf instrumentation inside `iR5900.cpp`/boot is port-added
  and stdout-bound — remember stderr is line-buffered to `/data` (slow);
  don't add new per-frame printfs.
- `g_orbis_*` atomics and telemetry counters are scattered through upstream
  files (MTVU.h, VMManager.cpp, Counters.cpp) — they are the existing
  instrumentation pattern; extend rather than replace.
