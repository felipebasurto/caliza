#!/usr/bin/env bash
# Builds and runs the focused UI model host tests without touching the PCSX2 build.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=${CALIZA_TEST_OUT:-/tmp/caliza-ui-tests}
mkdir -p "$out"
CXX=${CXX:-clang++}
"$CXX" -std=c++20 -O1 -g -Wall -Wextra -Werror -I"$root/caliza/core" \
  "$root/caliza/core/ui_model.cpp" "$root/tests/caliza/test_ui_model.cpp" -o "$out/test_ui_model"
"$out/test_ui_model"
