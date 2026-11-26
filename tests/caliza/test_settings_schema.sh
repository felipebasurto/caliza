#!/bin/sh
# Focused host verification for the portable Caliza schema registry; no project build-system changes required.
set -eu
cd "$(dirname "$0")/../.."
CXX=${CXX:-c++}
OUT=${OUT:-/tmp/caliza-test-settings-schema}
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
	tests/caliza/test_settings_schema.cpp \
	caliza/core/settings_schema.cpp \
	caliza/core/catalog_model.cpp \
	-o "$OUT"
"$OUT"
