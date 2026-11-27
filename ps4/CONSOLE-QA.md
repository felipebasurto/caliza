# PS4 hardware testing

Install the candidate PKG on a PS4 already configured to run homebrew. Enable the console's FTP server for diagnostics before opening Caliza. Use the console's IP address and the exact build tag supplied with the PKG.

From the Mac, run the checks at each stage:

```sh
python3 ps4/tools/smoke-console.py "$PS4_HOST" --build "$CALIZA_BUILD" --expect menu --out /tmp/caliza-qa
python3 ps4/tools/smoke-console.py "$PS4_HOST" --build "$CALIZA_BUILD" --expect game --wait 45 --out /tmp/caliza-qa
```

1. Open Caliza and run the menu check once the library appears.
2. Select a legally obtained game. Run the game check while the game is visibly running. Start at native resolution with an ISO on the console's internal storage.
3. Save and load a state if the title supports it, return to the library, and run the menu check again. Use a regular in-game save and reopen the title to check memory-card persistence separately.

Use `--port` if the settings QR code shows a port other than 8844. Each invocation creates a separate run directory containing `result.json` and the console's report, or an FTP collection attempt if the report could not be downloaded. Use `--ftp` to retrieve raw logs as well. An FTP bundle can include evidence from an earlier launch if the current process died before opening its logs. Keep those files for diagnosis and use the probe samples for the verdict.

### Probe checks

- **QA-1.** A game passes only when the requested build reports a running VM and its frame count increases between samples of the same process session and game launch. The menu check requires the shelf presentation counter to increase. An identical counter or a transition alone leaves the result inconclusive.
- **QA-2.** A different build cannot pass. If the application's report is unavailable, the runner still attempts to collect logs independently over FTP.
- **QA-3.** FTP collection defaults to the Caliza data root. Use `PS4_LOG_ROOT=legacy` explicitly when collecting from an old build that wrote to `/data/PCSX2`.

These checks observe the application and its emulated frame counter. Observe the TV or a locally paired Remote Play session to verify the image, audio, controller response, menus and save content. The runner only reads from the console. Keep any console diagnostics on a trusted local network.

### Pause menu checks

The in-game pause menu (`ps5/coreorbis/orbis-shims/OrbisPauseMenu.cpp` + `caliza/core/pause_menu.cpp`) has host-tested state-machine, combo-migration and stubbed glue coverage, but no real PS4 hardware evidence yet. Verify on the TV:

- **QA-4 (First-run chooser, default `L3 + R3` & GoldHEN non-clash).** On a fresh config (or after seeding `Caliza/PauseButton1=Share`), launch a game and verify the Estratos pause-trigger chooser appears with `L3 + R3` pre-selected and 6 presets shown with descriptions/trade-offs. Confirm picking `L3 + R3` saves `Caliza/PauseTrigger=l3_r3` and boots the game. While running, press and release `L3 + R3` alone and verify the pause overlay opens on release (`[boot] controls: pause menu trigger: L3 + R3 (l3_r3)`, `[pause] opened (trigger: L3 + R3 [l3_r3], ...)`, `[pause] vm paused`). Hold `L3 + R3` and tap D-pad Up/Down/Right (quick save/load/disc swap) then release: confirm the shortcut runs and releasing `L3 + R3` does **not** open pause. Separately, long-press Share during gameplay and confirm GoldHEN behaves normally without opening Caliza's pause menu or locking up the console.
- **QA-5 (Paused presentation).** The last game frame stays visible behind the panel and the panel repaints on every focus, slot, or disc change while the VM is paused (exercises `MTGS::SetRunIdle` idle re-presents).
- **QA-6 (Input isolation, Touchpad/Options presets & clean resume).** Verify Touchpad click shows the PS2 Select warning in the chooser, Controls, and companion web panel (and offers to map R3 to Select). Test `touchpad` (zero Select leak on open/close), `touchpad_hold` (short tap registers PS2 Select in-game; 0.5 s hold opens pause with zero Select leak), and `options_hold` (short tap registers PS2 Start; 0.5 s hold opens pause with zero Start leak). Confirm no button or stick movement acts in the game while open, and closing presses do not leak on release (`[pause] closed (reason: ..., trigger: ...)` and `[pause] vm resumed` in `boot.log`).
- **QA-7 (Quick save/load & slot picker).** Focus Save State / Load State and cycle slots 1–10 with Left/Right or L1/R1. Saving writes the chosen slot atomically and logs `[pause] save slot N: ok (... ms)`. Loading an empty slot shows an error inside the overlay without closing. Loading a saved slot closes the overlay, resumes at the saved point, and logs `[pause] load slot N: ok (... ms)`. Delete the primary `.p2s` over FTP while keeping `.p2s.backup` and confirm backup fallback loads cleanly (`ok via backup`).
- **QA-8 (Disc swap).** On a multi-disc game, focus Change Disc, pick the target disc with Left/Right, confirm with Cross, and check `[pause] disc swap target N (...): ok (... ms)` in `boot.log`. On a single-disc game, confirm Change Disc is shown disabled.
- **QA-9 (Return to library).** Selecting Return to library opens a confirmation dialog with Cancel focused first. Cancelling returns to the pause menu untouched; confirming logs `[pause] returning to library` and re-executes into the Estratos library.
- **QA-10 (Audio, preset switching & migration).** While paused, game audio must stop or hold silence rather than loop a stuck tone. Switch `Pause menu trigger` (`Caliza/PauseTrigger`) in Controls and in the companion web panel across `l3_r3_l1` and `l1_r1_touchpad` and verify each works live in-game. Verify an old `gs.ini` with `Caliza/PauseButton1=Share` clears `PauseTrigger` so the first-run chooser appears, and `Caliza/PauseButton1=L3R3` + `Caliza/PauseButton2=L1` migrates automatically to `Caliza/PauseTrigger=l3_r3_l1`.
