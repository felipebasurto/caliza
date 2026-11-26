#!/usr/bin/env bash
# Builds the standalone Caliza Estratos scripted host preview. This does not touch the PS4 Makefile.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=${CALIZA_HOST_OUT:-/tmp/caliza-build}
mkdir -p "$out"
"$root/caliza/host/build-shaders.sh"
CXX=${CXX:-clang++}
CC=${CC:-clang}
"$CC" -std=c99 -O1 -c "$root/ps5/frontend/third_party/qrcodegen/qrcodegen.c" -o "$out/qrcodegen.o"
"$CXX" -std=c++20 -O1 -g -Wall -Wextra -Werror \
  -Wno-missing-field-initializers -Wno-unused-function \
  -I"$root" -I"$root/caliza/core" -I"$root/caliza/native" -I"$root/caliza/host" \
  -I"$root/ps5/frontend" -I"$root/3rdparty/vulkan/include" \
  "$root/caliza/host/caliza_host.cpp" \
  "$root/caliza/host/host_demo.cpp" \
  "$root/caliza/core/ui_model.cpp" \
  "$root/caliza/core/settings_schema.cpp" \
  "$root/caliza/core/settings_projection.cpp" \
  "$root/caliza/native/draw_list.cpp" \
  "$root/caliza/native/procedural_covers.cpp" \
  "$root/caliza/native/estratos_scene.cpp" \
  "$root/caliza/native/vk2d_renderer.cpp" \
  "$root/ps5/frontend/fe_text.cpp" \
  "$root/ps5/frontend/fe_vk.cpp" \
  "$out/qrcodegen.o" \
  -lz -o "$out/caliza_host"
printf 'built %s/caliza_host\n' "$out"
