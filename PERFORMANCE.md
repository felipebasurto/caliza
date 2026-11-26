# Slow game? Try this before you report it

Caliza runs many PS2 games well, but some games are heavier than others. Most slowdowns improve with a few per-game settings. Try these first; it takes about five minutes. If the game is still slow afterwards, the report tells us exactly where to look.

## First: is it slow, or is it broken?

- **Slow:** the game runs, but the frame rate drops, the sound stutters, or everything moves in slow motion. This guide is for that.
- **Broken:** the game does not start, crashes, shows a black screen, or has missing or garbled graphics. Settings rarely fix that. Report it with logs.
- **Many PS2 games run at 30 fps**, or 25 on European (PAL) discs. A steady 30 is how those games were made, not a slowdown. Some games have a 60 FPS patch in the Patches section.

## Step 1: read the info box

The box shows something like:

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
