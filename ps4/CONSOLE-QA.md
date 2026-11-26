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

The in-game pause menu (orbis-shims/OrbisPauseMenu.cpp) has host-tested logic but no console evidence yet. Verify on the TV:

- **QA-4.** The Create button opens the overlay while a game runs. If nothing opens, the system may intercept Create (ScePad bit 0x1) before it reaches the application; check `[pause]` lines in the log to tell an input-detection failure apart from a presentation failure.
- **QA-5.** The last game frame stays visible behind the panel and the panel repaints on every focus change while the VM is paused (this exercises the idle re-present path; a frozen or black background means `MTGS::SetRunIdle` or `GSHasDisplayWindow` did not hold).
- **QA-6.** Open and close the menu twenty times in a row. No button may act in the game while the overlay is open, and the press that closes it (cross, circle or Create) must not leak into the game after it closes.
- **QA-7.** Save state writes slot 1. Load state with no state present must fail inside the overlay without closing it. Load state with a state present must close the overlay and resume the game at the saved point, including the backup fallback after deleting the primary file manually over FTP.
- **QA-8.** Return to the library asks for confirmation with Cancel focused first. Confirming re-executes into the menu; cancelling resumes the game untouched.
- **QA-9.** While paused, the game's audio must stop or hold silence rather than loop a stuck tone.
