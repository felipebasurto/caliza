# Slow game? Try this before you report it

Caliza runs many PS2 games well, but some games are heavier than others. Most slowdowns improve with a few per-game settings. Try these first; it takes about five minutes. If the game is still slow afterwards, the report tells us exactly where to look.

## First: is it slow, or is it broken?

- **Slow:** the game runs, but the frame rate drops, the sound stutters, or everything moves in slow motion. This guide is for that.
- **Broken:** the game does not start, crashes, shows a black screen, or has missing or garbled graphics. Settings rarely fix that. Report it with logs.
- **Many PS2 games run at 30 fps**, or 25 on European (PAL) discs. A steady 30 is how those games were made, not a slowdown. Some games have a 60 FPS patch in the Patches section.

## Step 1: read the info box

Set **Info box** to **FPS + load** first. The default shows only the frame rate, with no box. The box shows something like:

```text
42 FPS EE99 GS61 VU70
640x448 > 1920x1344
```

- **FPS** is the frame rate.
- **EE** is the PS2 main processor thread: the game's own code.
- **GS** is the PS2 graphics thread: drawing work and work handed to the PS4 GPU.
- **VU** is the vector-unit thread when MTVU is on. With MTVU off it shows 0 and that work counts under EE.
- **The second line** is the game's own picture size, then the size it is drawn at.

Go to a place where the game is slow and watch the three percentages. **The one at or near 100 is what is holding the game back.**

## Step 2: change settings for that game

Open the game in Caliza, choose **Game Settings**, and change one thing at a time. Most changes apply straight away; **MTVU** and **Renderer** apply when the game starts.

| What you see | What it means | What to try, in this order |
|---|---|---|
| **EE** at 95–100 | The PS4 CPU cannot keep up with the game's code | 1. **EE cycle rate** 100%, then 75%.<br>2. Turn off the game's **60 FPS patch**.<br>3. **EE cycle skip**: Mild. Try Moderate or Max only if Mild is not enough.<br>4. Keep **MTVU** on. |
| **GS** at 95–100 | Graphics are the limit | 1. Lower **Resolution** one step.<br>2. **Blending accuracy**: Basic, or Min.<br>3. **Anisotropic filtering**: Off.<br>4. **Mipmapping**: Off.<br>5. **Display filter**: Classic.<br>6. For games that read the picture back from the GPU: try **GPU readbacks** Don't wait. If effects flicker, set it back. |
| **VU** at 95–100 | The game's 3D geometry is the limit | Turn off the **60 FPS patch**. Few other settings help here; report it. |
| **Nothing near 100**, but FPS is still low | The emulated PS2 itself runs out of time, often with a 60 FPS patch | Try a higher **EE cycle rate** only for that game. If it speeds up, glitches, or becomes unstable, go back. |

## What else costs speed

- **Resolution** is the biggest cost. Heavy games may need 2x or 1x on PS4.
- **60 FPS patches** double work for the CPU and GPU. A patched game may need a lower resolution or a different EE cycle rate.
- **The software renderer** is much slower than Vulkan. Use it only for games Vulkan draws incorrectly.
- **HD texture packs** can stutter while textures load. Turn **Texture replacements** off to check whether a pack is the cause.
- **Widescreen patches** show more of the scene, so there is a little more to draw.
- **The network adapter** costs nothing unless the game uses it.

## Still slow? Report it like this

A useful report looks like this:

```text
Game: example game, serial and region
Console: PS4 or PS4 Pro, firmware version
Where: exact level/scene and what was happening
Settings: resolution, MTVU, EE cycle rate, blending, patches
Info box there: 41 FPS EE99 GS60 VU70
Tried: what changed, and what happened after each change
Logs: attached report
```

**Getting the logs:**

1. Play the slow part for at least a minute.
2. Open the companion panel's **Diagnostics** page.
3. Write what happened in the note field, press **Add note**, then **Download report**.
4. Send the downloaded report file. It contains the timeline, current and previous session logs, settings and hardware evidence.

## Broken game? Bisecting CPU/VIF fast paths with `boot.log`

If a game crashes, hangs, desyncs, or shows corrupted 3D geometry after an update, check these lines in `/data/caliza/boot.log` (or the downloaded diagnostic report) to isolate which CPU/VIF fast path was active.

### 1. Boot status lines (printed once per game launch)

```text
[perf] vif_unpack_rec: on (default)
[perf] cop0_event_gating: on (default)
[perf] vif1_fast_and_mpg_skip: on (default)
```

Each line prints `on` or `off` and the reason: `default`, `non-executable code memory`, or the exact flag file that disabled it.

### 2. Safety fallback lines (printed at most once if a guard trips)

```text
[perf-fallback] <change_name>: <reason>, falling back to ...
```

If any `[perf-fallback]` line appears, Caliza detected an unexpected buffer/bounds or cycle-delta condition in that optimization and fell back to the original path. Always include any `[perf-fallback]` line in bug reports.

### 3. Periodic telemetry line (folded into `[eefast]`, once per second when counters move)

```text
[eefast] per s: ... | dvif compile=C hit=H fallback=F | cop0 skip/forced: ERET=Es/Ef EI=Is/If status=Ss/Sf tmr=T (kept intc=I dmac=D tmr=M vu0=V) | vif1 fast=Vf slow=Vs whole_upk=Wu | vu1 mpg same=Ms/Mt | ...
```

- `dvif compile / hit / fallback`: VIF unpack blocks compiled, cache hits, and fallbacks to the interpreter unpack (e.g. VU memory wraparound or compile fallback).
- `cop0 skip/forced`: `ERET`, `EI`, and `WriteCP0Status` event tests skipped vs forced (`cpuSetNextEventDelta(4)`), `tmr` exact Count/Compare reschedules, and which source kept `ERET` event tests (`intc`, `dmac`, `tmr`, `vu0`).
- `vif1 fast / slow / whole_upk`: VIF1 codes handled by the fast loop vs `vifCmdHandler`, and whole-packet MTVU unpacks dispatched directly.
- `vu1 mpg same=Ms/Mt`: VU1 microprogram uploads skipped as unchanged (`Ms`) out of total uploads (`Mt`).

### 4. Disabling one change at a time (flag files)

Create an empty file under `/data/caliza/flags/` and relaunch the game to isolate which change caused a regression (remove the file afterwards to re-enable):

| Change | Boot line | Disable flag file | What it turns off |
|---|---|---|---|
| VIF unpack recompiler | `[perf] vif_unpack_rec:` | `/data/caliza/flags/novifrec` | Falls back from `dVifUnpack` to the SSE/C unpack loop (`_nVifUnpack`). Requires relaunch. |
| COP0 event-test gating | `[perf] cop0_event_gating:` | `/data/caliza/flags/noeretfast` | Forces immediate event tests on every `ERET`, `EI`, and `WriteCP0Status` (including JIT `recERET`/`recEI`). |
| VIF1 fast loop + unchanged MPG skip | `[perf] vif1_fast_and_mpg_skip:` | `/data/caliza/flags/vifslow` | Disables the VIF1 register-resident fast loop, whole-UNPACK MTVU direct dispatch, and unchanged-microcode `MTVU_VU_WRITE_MICRO` skip. |

When reporting a broken game, include all three `[perf]` lines, any `[perf-fallback]` lines, the last 3–5 `[eefast]` and `[load]` lines before the issue, and which flag file (if any) made the game work.
