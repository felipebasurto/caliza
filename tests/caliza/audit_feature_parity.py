#!/usr/bin/env python3
"""Static feature-parity audit for the schema-driven Caliza surfaces.

This is a guard against frontend drift, not proof of console behavior. It compares the canonical
settings registry with the companion panel's key construction and reports rows that no surface can reach.
"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPORTER = r'''
#include "caliza/core/settings_schema.h"
#include <iostream>
int main(){std::cout << caliza::settings::RegistryToJson(false) << "\n" << caliza::settings::ActionsToJson(false) << "\n";}
'''

def compile_exporter(tmp: Path) -> Path:
    src = tmp / "export_schema.cpp"
    exe = tmp / "export_schema"
    src.write_text(EXPORTER)
    cmd = ["clang++", "-std=c++17", "-I.", str(src), "caliza/core/settings_schema.cpp", "caliza/core/catalog_model.cpp", "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return exe

def web_keys(js: str) -> set[str]:
    keys = set(re.findall(r'item\("([A-Za-z0-9_/]+)"', js))
    keys.update("Caliza/" + suffix for suffix in re.findall(r'calizaKey\("([A-Za-z0-9_]+)"\)', js))
    keys.update("Caliza/" + suffix for suffix in re.findall(r'comboItem\("([A-Za-z0-9_]+)"', js))
    # buttonItem/strengthItem factories generate these canonical remap and pressure rows.
    names = ["Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Options", "Touchpad", "Up", "Down", "Left", "Right"]
    for name in names:
        keys.add("Caliza/Button" + name)
        keys.add("Caliza/Button" + name + "Pressure")
    return keys

def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        exe = compile_exporter(Path(td))
        out = subprocess.run([str(exe)], cwd=ROOT, check=True, stdout=subprocess.PIPE, text=True).stdout.splitlines()
    registry = json.loads(out[0])
    actions = json.loads(out[1])
    js = (ROOT / "caliza/web/app.js").read_text()
    keys = web_keys(js)
    visible = [s for s in registry["settings"] if s.get("presentation") != "hidden"]
    missing = []
    memory_covered = []
    for setting in visible:
        key = setting["key"]
        if key in keys:
            continue
        if setting.get("presentation") == "composite" or setting.get("type") == "memory-card-file":
            memory_covered.append(setting)
            continue
        missing.append(setting)
    print(f"schema: {len(registry['settings'])} settings, {len(visible)} visible, {len(actions)} actions")
    print(f"web keys: {len(keys)} constructed/literal keys")
    print(f"memory-card composite rows covered by memory surface: {len(memory_covered)}")
    if missing:
        print("web settings gaps:")
        for setting in missing:
            print(f"- {setting['id']} ({setting['key']}) group={setting['group']} tab={setting['tab']}")
    else:
        print("web settings coverage: PASS")
    # Native coverage is adapter-dependent: ui_model renders every snapshot SettingItem, so the parity point is
    # whether the console backend projects all visible schema rows. This script intentionally fails until a
    # production projection test exists.
    projection_test = ROOT / "tests/caliza/test_console_backend.cpp"
    adapter = (ROOT / "caliza/core/console_backend.cpp").read_text() if (ROOT / "caliza/core/console_backend.cpp").exists() else ""
    if not projection_test.exists() or "ProjectSettings" not in adapter:
        print("native projection coverage: MISSING TEST OR ADAPTER PROJECTION")
        return 1
    print("native projection coverage: STATIC PASS (run test_console_backend for behavior)")
    return 1 if missing else 0

if __name__ == "__main__":
    sys.exit(main())
