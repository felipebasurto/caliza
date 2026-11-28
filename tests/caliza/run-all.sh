#!/usr/bin/env bash
# Fast host verification for the portable Caliza contract, schema, storage and UI model.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=${CALIZA_TEST_OUT:-/tmp/caliza-tests}
mkdir -p "$out"
cd "$root"

cxx=${CXX:-clang++}
common_flags=(-std=c++17 -Wall -Wextra -I.)

echo '== ui model =='
bash tests/caliza/test_ui_build.sh

echo '== estratos scene =='
cc=${CC:-clang}
"$cc" -std=c99 -O2 -c ps5/frontend/third_party/qrcodegen/qrcodegen.c -o "$out/qrcodegen.o"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Wno-unused-function -I. -I3rdparty/stb -pthread \
  tests/caliza/test_estratos_scene.cpp caliza/native/estratos_scene.cpp caliza/native/draw_list.cpp \
  caliza/native/procedural_covers.cpp ps5/frontend/fe_text.cpp ps5/frontend/fe_covers.cpp ps5/frontend/fe_i18n.cpp \
  "$out/qrcodegen.o" \
  -o "$out/test_estratos_scene"
"$out/test_estratos_scene"

echo '== cover art (download/cache/path mechanics) =='
"$cxx" -std=c++17 -O2 -Wall -Wextra -Wno-unused-function -I. -Ips5/frontend -pthread \
  tests/caliza/test_covers.cpp ps5/frontend/fe_covers.cpp ps5/frontend/fe_text.cpp ps5/frontend/fe_i18n.cpp \
  -o "$out/test_covers"
"$out/test_covers"

echo '== pause menu model =='
"$cxx" -std=c++17 -O1 -Wall -Wextra -Werror -Icaliza/core \
  caliza/core/pause_menu.cpp tests/caliza/test_pause_menu.cpp -o "$out/test_pause_menu"
"$out/test_pause_menu"

echo '== settings schema =='
bash tests/caliza/test_settings_schema.sh

echo '== settings projection =='
"$cxx" "${common_flags[@]}" tests/caliza/test_settings_projection.cpp \
  caliza/core/settings_projection.cpp caliza/core/settings_schema.cpp caliza/core/catalog_model.cpp \
  -o "$out/test_settings_projection"
"$out/test_settings_projection"

echo '== console backend projection =='
"$cxx" "${common_flags[@]}" -pthread tests/caliza/test_console_backend.cpp \
  caliza/core/console_backend.cpp caliza/core/backend.cpp caliza/core/storage.cpp caliza/core/data_root.cpp \
  caliza/core/settings_schema.cpp caliza/core/settings_projection.cpp caliza/core/catalog_model.cpp \
  -o "$out/test_console_backend"
"$out/test_console_backend"

echo '== contract/backend =='
"$cxx" "${common_flags[@]}" tests/caliza/test_contract.cpp \
  caliza/core/backend.cpp caliza/core/fake_backend.cpp caliza/core/storage.cpp caliza/core/data_root.cpp \
  -pthread -o "$out/test_contract"
"$out/test_contract"

echo '== storage =='
"$cxx" "${common_flags[@]}" tests/caliza/test_storage.cpp caliza/core/storage.cpp caliza/core/backend.cpp caliza/core/data_root.cpp \
  -o "$out/test_storage"
"$out/test_storage"

echo '== migration =='
"$cxx" "${common_flags[@]}" tests/caliza/test_migration.cpp caliza/core/storage.cpp caliza/core/data_root.cpp \
  -o "$out/test_migration"
"$out/test_migration"

echo '== serial cache =='
"$cc" -O2 -c ps5/third_party/lz4/lz4.c -o "$out/lz4.o"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Wno-unused-function -I. -Ips5/frontend -I3rdparty/libchdr/include -Ips5/third_party/lz4 -pthread \
  tests/caliza/test_serial_cache.cpp ps5/frontend/fe_games.cpp "$out/lz4.o" -lz -o "$out/test_serial_cache"
"$out/test_serial_cache"

echo '== cso/zso reader =='
"$cxx" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  -I. -Ipcsx2 -I3rdparty/fmt/include -I3rdparty/fast_float/include -Ips5/third_party/lz4 -DFMT_HEADER_ONLY=1 \
  tests/caliza/test_cso_reader.cpp pcsx2/CDVD/CsoFileReader.cpp "$out/lz4.o" -lz -o "$out/test_cso_reader"
"$out/test_cso_reader"

echo '== web panel non-blocking serials =='
"$cxx" -std=c++17 -O2 -Wall -Wextra -Wno-unused-function \
  -I. -Ips5/frontend -I3rdparty/libchdr/include -Ips5/third_party/lz4 -pthread \
  tests/caliza/test_web_nonblocking.cpp ps5/frontend/fe_web.cpp ps5/frontend/fe_games.cpp \
  ps5/frontend/fe_covers.cpp ps5/frontend/fe_text.cpp ps5/frontend/fe_i18n.cpp \
  ps5/frontend/fe_modern_controls.cpp ps5/frontend/fe_settings.cpp "$out/lz4.o" -lz \
  -o "$out/test_web_nonblocking"
"$out/test_web_nonblocking"

echo '== web panel smoke =='
(cd caliza/web && node smoke-test.mjs)

if command -v chdman >/dev/null 2>&1; then
  echo '== real libchdr =='
  bash ps5/frontend/host/test-chd-real.sh
fi

echo '== dead-brand audit =='
python3 ps4/tools/audit_brand.py
python3 ps4/tools/audit_visible_ps5.py

echo 'caliza host verification passed'
