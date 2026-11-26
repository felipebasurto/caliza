#!/usr/bin/env python3
"""Fail when user-visible Caliza PS4 surfaces reintroduce inherited PS5 wording.

This is deliberately stricter than a whole-repository grep. Internal ABI names, driver
environment variables, source-layout compatibility names, upstream controller databases,
licence notices and cryptographic material may contain the technical substring without
being product copy. Those do not belong in the UI, companion panel, settings schema,
runtime log messages, public troubleshooting guide, or package metadata.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NEEDLE = re.compile(r"ps5", re.IGNORECASE)

# Full-text surfaces: everything in them can be rendered or packaged for users.
TEXT_SURFACES = [
    ROOT / "caliza/web",
    ROOT / "ps5/frontend/assets",
]
TEXT_FILES = [
    ROOT / "README.md",
    ROOT / "PERFORMANCE.md",
]
TEXT_SUFFIXES = {".js", ".html", ".css", ".txt", ".ini", ".md", ".json", ".svg"}

# Settings strings are compiled into the native UI even when inherited presentation code is not used.
STRING_LITERAL_FILES = [
    ROOT / "caliza/core/settings_schema.cpp",
    ROOT / "ps5/frontend/fe_options.cpp",
]

# Runtime diagnostics are user-visible through boot.log/emulog.txt/settings.log and the web report.
LOG_SOURCES = [
    ROOT / "ps5/coreorbis/main-boot.cpp",
    ROOT / "ps5/frontend/fe_ps5.cpp",
    ROOT / "ps5/frontend/fe_web.cpp",
    ROOT / "ps5/frontend/fe_i18n.cpp",
    ROOT / "pcsx2/Achievements.cpp",
    ROOT / "pcsx2/GameDatabase.cpp",
    ROOT / "pcsx2/DEV9/AdapterUtils.cpp",
    ROOT / "pcsx2/OrbisTexturePak.h",
    ROOT / "pcsx2/GS/Renderers/HW/GSTextureReplacements.cpp",
    ROOT / "pcsx2/GS/Renderers/Vulkan/GSDeviceVK.cpp",
]
LOG_LINE = re.compile(r"(printf|fprintf|Console\.|orbis_eventf|OrbisDeferredPrintf|error\s*=|Warning|Error)")
ABI_ALLOW = re.compile(r"(PS5VK_|SDLPS5PlayerLED|PS5SX2TP|PS5FCAP|fe_ps5\.h|ps5/)")


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def text_files(root: Path):
    if root.is_file():
        yield root
        return
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix in TEXT_SUFFIXES:
            yield path


def string_literals(text: str):
    return re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', text)


def main() -> int:
    failures: list[str] = []

    for surface in TEXT_SURFACES:
        for path in text_files(surface):
            try:
                lines = path.read_text(errors="ignore").splitlines()
            except UnicodeDecodeError:
                continue
            for number, line in enumerate(lines, 1):
                if NEEDLE.search(line):
                    failures.append(f"{rel(path)}:{number}: {line.strip()[:160]}")

    for path in TEXT_FILES:
        for number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
            if NEEDLE.search(line):
                failures.append(f"{rel(path)}:{number}: {line.strip()[:160]}")

    for path in STRING_LITERAL_FILES:
        text = path.read_text(errors="ignore")
        for literal in string_literals(text):
            if NEEDLE.search(literal):
                failures.append(f"{rel(path)}: compiled string literal {literal[:160]!r}")

    for path in LOG_SOURCES:
        for number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("//"):
                continue
            if LOG_LINE.search(line) and not ABI_ALLOW.search(line):
                for literal in string_literals(line):
                    if NEEDLE.search(literal):
                        failures.append(f"{rel(path)}:{number}: runtime/log string {literal[:160]!r}")

    if failures:
        print("visible PS5 reference audit failed:", file=sys.stderr)
        for failure in failures:
            print("  " + failure, file=sys.stderr)
        return 1
    print("visible PS5 reference audit: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
