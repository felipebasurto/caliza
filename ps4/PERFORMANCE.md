# Performance evidence and measurement protocol (AI-assisted)

## Current evidence

No new console performance measurement exists tonight. Do not claim speedups yet.

Inherited README claims:

- GTA San Andreas: gameplay around 50% speed / ~18 fps on base PS4;
- Mortal Kombat Shaolin Monks: around 15-27 fps in heavier scenes.

These are compatibility/reference claims only, not controlled benchmarks.

## Measurement protocol

For every candidate compare the same commit baseline, same game/scene, same settings, same console model, after warmup:

1. boot to shelf, start title;
2. load the same savestate or stand in the same scene;
3. collect 60 seconds after 10 seconds warmup;
4. record internal speed/FPS if available and all telemetry lines;
5. repeat 3 runs; report median and range;
6. capture CPU affinity/settings and upscale/renderer/MTVU state.

Metrics:

- internal emulation speed/FPS;
- frame time mean/p95/p99 if available;
- `[vkwait]` wait time per second;
- `[vsslow]` presentation backlog;
- `[threads]` CPU/thread distribution;
- `[copies]`, `[readbacks]`, `[shaders]` time;
- `[ps4wsi]` flip fallbacks/acquire timeouts/register retries;
- `[audio]` underrun grains/output errors;
- RSS/flexible/direct memory if measurable;
- boot-to-first-frame time.

## Instrumentation now available

- `[ps4wsi]`: noteworthy present/acquire/VideoOut registration deltas.
- `[audio]`: transitions into underrun-refill state, plus `sceAudioOutOutput` failures.
- Existing `[copies]`, `[readbacks]`, `[shaders]`, `[threads]`, `[vkwait]` lines remain the primary performance evidence.

## Safe performance hypotheses to test later

1. Measure EE thread breakdown before considering an IOP thread.
2. Validate 2048 KiB MTVU ring against 1/4/8/16 MiB on hardware using MTVU counters.
3. Measure spin vs sleep behavior by scene; do not assume spinning is always better.
4. Measure base PS4 native vs Pro 2x defaults separately.
5. Measure shader pipeline compile hitches from `[shaders]` before considering VU/pipeline caching.
6. Reduce CPU-side GPU copies/readbacks only after `[copies]`/`[readbacks]` evidence identifies the dominant kind.
