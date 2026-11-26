#!/usr/bin/env bash
# Run the committed host-side validation tests that do not require a PS4.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$(mktemp -d /tmp/ps4sx2-host-tests.XXXXXX)}"
mkdir -p "$OUT"
CXX="${CXX:-c++}"
run_py() { echo "== $1 =="; python3 "$ROOT/$2"; }
run_cpp() {
  local name="$1" src="$2"; shift 2
  echo "== $name =="
  "$CXX" -std=c++17 -Wall -Wextra "$@" "$ROOT/$src" -o "$OUT/$name"
  "$OUT/$name"
}
run_cpp test_fg_pacing ps5/coreorbis/tests/fgpacing/test_fg_pacing.cpp -I"$ROOT/ps5/coreorbis/orbis-shims"
run_cpp test_kbm_map ps5/coreorbis/tests/kbm/test_kbm_map.cpp -I"$ROOT/ps5/coreorbis/orbis-shims"
run_cpp test_pad_map ps5/coreorbis/tests/padmap/test_pad_map.cpp -I"$ROOT/ps5/coreorbis/orbis-shims"
run_cpp test_texture_roots ps5/coreorbis/tests/textures/textures_test.cpp -I"$ROOT/ps5/coreorbis/orbis-shims"
run_cpp test_wsi_decision ps4/tests/test_wsi_decision.cpp -I"$ROOT/ps4/include"
run_cpp test_spu2_pcm_cache ps4/tests/test_spu2_pcm_cache.cpp
run_cpp test_cdvd_subq ps4/tests/test_cdvd_subq.cpp -I"$ROOT"
run_cpp test_statewrapper_bounds ps4/tests/test_statewrapper_bounds.cpp -std=c++20 -Wno-unused-parameter -I"$ROOT" -I"$ROOT/3rdparty/fmt/include" "$ROOT/pcsx2/StateWrapper.cpp"
run_py package_stage_validator ps4/tools/test_validate_package_stage.py
run_py savestate_validator ps4/tools/test_validate_savestate.py
run_py console_log_analyzer ps4/tools/test_analyze_console_logs.py
echo '== shell_tool_syntax =='
bash -n "$ROOT/ps4/build-ps4.sh" "$ROOT/ps4/make-pkg.sh" "$ROOT/ps4/tools/collect-console-logs.sh"
echo 'shell syntax: PASS'
echo "host test binaries: $OUT"
