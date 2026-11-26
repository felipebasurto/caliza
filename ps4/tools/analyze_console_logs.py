#!/usr/bin/env python3
"""Analyze PCSX2 PS4 console logs pulled back to a host.

The report is intentionally evidence-oriented: absence of optional telemetry is
not a failure, while explicitly requested smoke-test features are. It does not
prove compatibility or performance by itself.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass, field


@dataclass
class Evidence:
    lines: list[str] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def has(self, needle: str) -> bool:
        return any(needle in line for line in self.lines)

    def count(self, needle: str) -> int:
        return sum(1 for line in self.lines if needle in line)

    def matching(self, needle: str) -> list[str]:
        return [line for line in self.lines if needle in line]


def last_int(pattern: str, lines: list[str], group: str) -> int | None:
    found = None
    rx = re.compile(pattern)
    for line in lines:
        match = rx.search(line)
        if match:
            try:
                found = int(match.group(group))
            except (ValueError, IndexError):
                pass
    return found


def require(ev: Evidence, enabled: bool, name: str, ok: bool, detail: str) -> None:
    if ok:
        ev.notes.append(f"PASS {name}: {detail}")
    elif enabled:
        ev.missing.append(f"missing required evidence for {name}: {detail}")
    else:
        ev.notes.append(f"SKIP {name}: not part of this smoke test")


def analyze(paths: list[pathlib.Path], args: argparse.Namespace) -> Evidence:
    ev = Evidence()
    for path in paths:
        try:
            ev.lines.extend(path.read_text(errors="replace").splitlines())
        except OSError as exc:
            ev.failures.append(f"cannot read {path}: {exc}")
    if not ev.lines:
        ev.failures.append("no log lines were read")
        return ev

    boot_steps = []
    for line in ev.lines:
        match = re.search(r"\[pcsx2\]\s*(.+)$", line)
        if match:
            boot_steps.append(match.group(1).strip())
    if boot_steps:
        last_step = boot_steps[-1]
        ev.notes.append(f"INFO boot trace: last persistent milestone: {last_step}")
        if "failed" in last_step.lower():
            ev.failures.append(f"boot trace ended at failure milestone: {last_step}")
    else:
        ev.notes.append("INFO boot trace: no persistent [pcsx2] milestones found")

    memmap_ok = ev.has("[dbg] memmap: data=")
    require(ev, True, "memory map", memmap_ok, "successful direct data/code mapping line")
    if ev.has("[dbg] memmap: direct alloc failed") or ev.has("[dbg] memmap: direct memory initialization failed"):
        ev.failures.append("direct memory allocation failed; fastmem/JIT memory map is not valid")
    if (ev.has("[boot] SysMemory::Reset FAILED") or ev.has("[boot] CPUThreadInitialize FAILED") or
            ev.has("[dbg] sysmem: AllocateMemoryMap failed") or ev.has("[dbg] sysmem: Allocate failed") or
            ev.has("[dbg] sysmem: vtlb_Core_Alloc failed") or ev.has("CPU/memory initialization failed")):
        ev.failures.append("VM memory initialization failed; boot should stop rather than reset invalid memory")
    rejected = ev.count("did not execute; rejecting")
    if rejected:
        ev.warnings.append(f"executable-memory probe rejected a candidate mapping {rejected} time(s); inspect firmware/loader behavior")
    if ev.has("non-executable interpreter fallback") or ev.has("recompiler memory is not executable"):
        ev.warnings.append("recompiler memory was not executable; boot used the slow interpreter compatibility fallback")

    pad_handle = ev.has("[pad] player 1 has no handle yet") or ev.has("[pad] player 1 controller handle appeared") or ev.has("[pad] player 1 data[56..84]") or ev.has("[pad] lib=")
    require(ev, args.expect_input, "controller input", pad_handle, "player-1 pad initialization/hotplug/data evidence")
    if ev.has("[pad] player 1 connected byte observed nonzero"):
        ev.notes.append("PASS controller ABI: connected byte observed nonzero")
    elif args.expect_input:
        ev.warnings.append("player-1 connected byte was not observed nonzero; PS4 connected-byte ABI remains uncertain")

    wsi = ev.count("[ps4wsi]") > 0
    require(ev, args.expect_wsi, "WSI telemetry", wsi, "at least one [ps4wsi] telemetry line")
    present_failures = last_int(r"\[ps4wsi\].*failures=(\d+)", ev.lines, 1) or 0
    acquire_timeouts = last_int(r"\[ps4wsi\].*acquire_timeouts=(\d+)", ev.lines, 1) or 0
    if present_failures or acquire_timeouts:
        ev.warnings.append(f"WSI counters show failures={present_failures}, acquire_timeouts={acquire_timeouts}")

    audio = ev.count("[audio]") > 0
    require(ev, args.expect_audio, "audio telemetry", audio, "at least one [audio] telemetry line")
    underruns = last_int(r"\[audio\].*underruns=(\d+)", ev.lines, 1) or 0
    audio_errors = last_int(r"\[audio\].*errors=(\d+)", ev.lines, 1) or 0
    if underruns or audio_errors:
        ev.warnings.append(f"audio counters show underruns={underruns}, output errors={audio_errors}")

    state_write = ev.count("[state] wrote ") > 0
    state_load = ev.count("[state] loaded ") > 0
    require(ev, args.expect_state, "savestate round trip", state_write and state_load, "both [state] wrote and [state] loaded lines")
    if state_write and not any("(streaming)" in line for line in ev.matching("[state] wrote ")):
        ev.warnings.append("savestate write did not use the streaming path (expected only for threaded/non-pad saves)")
    if ev.has("Save state corruption") or ev.has("Savestate data corruption"):
        ev.failures.append("savestate corruption evidence present")
    elif ev.has("Failed to load state"):
        ev.warnings.append("savestate load failure evidence present; inspect whether the .backup fallback succeeded")

    mcd_write_failures = sum(("WRITE FAILED" in line or "close/checksum write failed" in line) for line in ev.matching("[mcd]"))
    if mcd_write_failures:
        ev.failures.append(f"memory-card cache write failure evidence present in {mcd_write_failures} line(s); guest saves are at risk")
    if ev.has("[Folder] backing allocation failed"):
        ev.failures.append("folder memory-card backing allocation failed; a configured folder card is unavailable and guest saves cannot be trusted in this run")
    if ev.has("Failed to save NVRAM"):
        ev.warnings.append("NVRAM save failure evidence present; settings may be lost on exit")
    if ev.has("Failed to write MEC") or ev.has("Failed to replace MEC"):
        ev.warnings.append("mechacon-version persistence failure evidence present")

    if ev.has("MDEC backing allocation failed"):
        ev.failures.append("MDEC backing allocation failed; PS1-mode FMV decode cannot be correct in this run")
    if ev.has("CDVD: sector cache allocation failed"):
        ev.warnings.append("CDVD sector cache allocation failed; direct reads remain correct but streaming performance may degrade")
    if ev.has("[vkhw] big draw ring allocation failed"):
        ev.warnings.append("big Vulkan diagnostic ring allocation failed; GPU-hang evidence is limited to the small ring")
    if ev.has("DEV9: flash backing allocation failed"):
        ev.warnings.append("DEV9 flash backing allocation failed; flash reads degrade to erased memory and writes are ignored")
    if ev.has("[gpuprof] backing allocation failed"):
        ev.warnings.append("GPU profiler backing allocation failed; gpu-profiling telemetry is unavailable but rendering is unaffected")
    if ev.has("[prof] backing allocation failed"):
        ev.warnings.append("sampling profiler backing allocation failed; CPU-profiling telemetry is unavailable but emulation is unaffected")

    nfs_connected = bool(re.search(r"network share .* at /net/share\d+: (?=Connected|connected)", "\n".join(ev.lines), re.IGNORECASE))
    nfs_left_out = ev.count("network share left out") + sum(
        (" (left out)" in line or " (not added to scan)" in line) for line in ev.matching("network share ")
    )
    require(ev, args.expect_nfs, "NFS share", nfs_connected and nfs_left_out == 0, "connected /net/shareN line and no left-out share")
    if args.expect_nfs and nfs_left_out:
        ev.warnings.append(f"{nfs_left_out} network share(s) were left out")

    kbm = ev.count("[kbm]") > 0
    require(ev, args.expect_kbm, "USB keyboard/mouse", kbm, "at least one [kbm] line")

    gameplay = ev.count("[threads]") > 0 or ev.count("[copies]") > 0 or ev.count("[readbacks]") > 0
    require(ev, args.expect_gameplay, "gameplay telemetry", gameplay, "[threads], [copies], or [readbacks] lines after gameplay")
    draws = last_int(r"\[threads\] draws=(\d+)", ev.lines, 1)
    if draws is not None:
        ev.notes.append(f"INFO gameplay: latest draw counter {draws}")

    for needle, message in [
        ("SIGSEGV", "fatal SIGSEGV evidence present"),
        ("SIGBUS", "fatal SIGBUS evidence present"),
        ("segmentation fault", "fatal segmentation-fault evidence present"),
        ("Assertion", "assertion evidence present"),
    ]:
        if ev.has(needle):
            ev.failures.append(message)

    return ev


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=pathlib.Path, help="boot.log/emulog.txt/settings.log/crash.log files")
    parser.add_argument("--expect-input", action="store_true")
    parser.add_argument("--expect-wsi", action="store_true")
    parser.add_argument("--expect-audio", action="store_true")
    parser.add_argument("--expect-state", action="store_true")
    parser.add_argument("--expect-nfs", action="store_true")
    parser.add_argument("--expect-kbm", action="store_true")
    parser.add_argument("--expect-gameplay", action="store_true")
    args = parser.parse_args()

    ev = analyze(args.logs, args)
    print("console log analysis")
    print(f"  lines: {len(ev.lines)}")
    for line in ev.notes:
        print(f"  {line}")
    for line in ev.warnings:
        print(f"  WARN {line}")
    for line in ev.missing:
        print(f"  MISSING {line}")
    for line in ev.failures:
        print(f"  FAIL {line}")
    bad = bool(ev.failures or ev.missing)
    print("console log analysis: " + ("FAIL" if bad else "PASS"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
