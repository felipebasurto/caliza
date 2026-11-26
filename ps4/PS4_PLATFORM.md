# PS4/Orbis platform notes (AI-assisted)

## Executable memory and recompiler memory

PCSX2's recompilers require writable memory that becomes executable. The port currently uses executable direct memory by default:

- guest DATA is allocated in direct memory, preferred around `0x600000000`, type 0/WB_ONION on PS4;
- fastmem depends on direct-memory aliasing (`g_orbis_data_phys` plus `sceKernelMapDirectMemory`);
- code memory normally uses direct memory, RW followed by `sceKernelMprotect` to RWX;
- PS5 JIT shared-memory functions are not used by default and, after tonight's patch, are not dynamically imported in PS4 builds.

CEMUPS4 evidence shows executable-memory behavior is firmware/loader dependent. Its runtime probes write and execute a one-byte `ret` under fault protection before choosing system flexible, regular flexible, or direct memory. PS4SX2 now probes execution before accepting direct code memory, but the complete firmware matrix still requires hardware validation. On hardware, record:

- `[memmap] code memory direct RWX ...` success/failure;
- `[jitprobe] skipped on PS4 (direct memory runtime)`;
- any `fastmem` or Memory.cpp failure lines;
- whether the first recompiled block executes or the process crashes.

A future P1 improvement is a small runtime execution probe before committing recompiler memory, plus a clear user-facing failure if no executable source exists.

## GPU/renderer

The PS4 renderer is Mesa RADV-on-GNM statically linked into the eboot. `ps4/ps4_vk_wsi.cpp` wraps `vk_icdGetInstanceProcAddr`, answers WSI entry points itself, and forwards everything else to RADV.

WSI properties:

- `VK_KHR_display`, one 1920x1080 60 Hz mode, one plane;
- exactly three swapchain images;
- linear host-visible/host-coherent device-local memory;
- images registered with `sceVideoOutRegisterBuffers`;
- GPU address equals CPU address through unified memory;
- FIFO and IMMEDIATE present modes only;
- acquire is CPU-side through VideoOut flip status/equeue; acquire semaphores are stripped from queue submissions;
- present queues `ac_ps4_submit_flip` behind GPU work.

New telemetry prints `[ps4wsi]` when failures/retries/fallbacks occur. Preserve `/data/PCSX2/emulog.txt` lines with `[ps4wsi]`, `[vkwait]`, `[vsslow]`, `[threads]`, `[copies]`, `[readbacks]`, `[shaders]`.

## CPU/threading

PS4 Jaguar constraints:

- weak single-thread performance; EE and VU1 can each consume a core;
- MTVU defaults on for PS4;
- ring default is 2048 KiB on PS4 versus 16384 KiB on PS5;
- spin waits default on, batching default off;
- Jaguar-aware pinning/topology work already exists;
- PS4 Pro detection selects 2x upscale default, which can shift load from GPU to CPU bandwidth and must be measured.

No optimization should be claimed without same-scene baseline/candidate logs.

## Input/audio/filesystem

- DualShock 4 uses shared ScePad infrastructure. Player 1 now receives a neutral state when disconnected, matching extra players.
- USB keyboard/mouse is disabled on PS4 because module load previously failed; retest is desirable.
- Audio uses fixed 256-frame grain (~5.3 ms at 48 kHz); return values/latency settings are not fully honored.
- Main writable data root is `/data/PCSX2`.
- Package resources are `/app0/resources`; optional personal BIOS builds use `/app0/bios` then copy to `/data/PCSX2/bios`.
- NFS game shares use CEMUPS4's `/net` network filesystem on PS4, not the PS5 `/nfs` libc-wrap layer. Plain NFSv3 `nfs://host/export` mounts appear as `/net/share1`, `/net/share2`, ...; query options and custom ports are rejected with boot-log lines.

## Firmware/loader matrix to fill on hardware

| Firmware/loader | Cold boot | JIT execute | Vulkan first frame | Notes |
|---|---|---|---|---|
| 9.00 + GoldHEN | untested | untested | untested | |
| 11.00 + GoldHEN | untested | untested | untested | CEMUPS4 notes stricter behavior here |
| 12.00/12.52 + GoldHEN | untested | untested | untested | |
| 13.xx/14.00 + loader | untested | untested | untested | inherited bug motivated JIT import fix |
