#!/usr/bin/env bash
# PS4SX2 host-side logic tests that do not need a console or OpenOrbis. Run in Linux/macOS with g++.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
core="$here/../ps5/coreorbis"
out="${PS4_HOST_TEST_DIR:-$(mktemp -d /tmp/ps4sx2-host-tests.XXXXXX)}"
mkdir -p "$out"
inc="-I$core/orbis-shims"
status=0
run_test() {
    local name="$1" src="$2" extra_flags="${3:-}"
    echo "== $name =="
    if g++ -std=c++17 -Wall -Wextra $extra_flags $inc "$core/$src" -o "$out/$name" && "$out/$name"; then
        echo "PASS: $name"
    else
        echo "FAIL: $name" >&2
        status=1
    fi
}
run_test frame_pacing tests/fgpacing/test_fg_pacing.cpp -Werror
run_test kbm_map tests/kbm/test_kbm_map.cpp
run_test pad_map tests/padmap/test_pad_map.cpp
run_test texture_roots tests/textures/textures_test.cpp
echo "== package_stage_validator =="
if python3 "$here/tools/test_validate_package_stage.py"; then
    echo "PASS: package_stage_validator"
else
    echo "FAIL: package_stage_validator" >&2
    status=1
fi
echo "== savestate_validator =="
if python3 "$here/tools/test_validate_savestate.py"; then
    echo "PASS: savestate_validator"
else
    echo "FAIL: savestate_validator" >&2
    status=1
fi
echo "== console_log_analyzer =="
if python3 "$here/tools/test_analyze_console_logs.py"; then
    echo "PASS: console_log_analyzer"
else
    echo "FAIL: console_log_analyzer" >&2
    status=1
fi
echo "host test binaries: $out"
exit "$status"
