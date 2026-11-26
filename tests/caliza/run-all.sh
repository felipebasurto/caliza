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

echo '== web panel smoke =='
(cd caliza/web && node smoke-test.mjs)

echo '== dead-brand audit =='
python3 ps4/tools/audit_brand.py
python3 ps4/tools/audit_visible_ps5.py

echo 'caliza host verification passed'
