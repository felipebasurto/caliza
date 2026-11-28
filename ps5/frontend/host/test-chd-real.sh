#!/usr/bin/env bash
# Host verification linking real 3rdparty/libchdr (not chd_stub.c) against synthetic CHDs built with chdman.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if ! command -v chdman >/dev/null 2>&1; then
  echo "skipping real libchdr host test (chdman not on PATH)"
  exit 0
fi

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../.." && pwd)
out=${CALIZA_TEST_OUT:-/tmp/caliza-tests}
mkdir -p "$out/libchdr-real"

CC=${CC:-clang}
CXX=${CXX:-clang++}

chd_inc=(
  -I"$root/3rdparty/libchdr/include"
  -I"$root/3rdparty/lzma/include"
  -I"$root/ps5/third_party/zlib"
  -I"$root/ps5/third_party/zstd/lib"
)
chd_cflags=(-O2 -DZ7_ST -DZSTD_DISABLE_ASM=1 -DZSTD_LEGACY_SUPPORT=0 -DZSTD_NO_TRACE=1 -w)

objs=()
compile_c() {
  local src=$1
  local base
  base=$(echo "${src#$root/}" | tr '/' '_')
  local obj="$out/libchdr-real/${base%.c}.o"
  if [[ ! -f "$obj" || "$src" -nt "$obj" ]]; then
    "$CC" "${chd_cflags[@]}" "${chd_inc[@]}" -c "$src" -o "$obj"
  fi
  objs+=("$obj")
}

for f in "$root"/3rdparty/libchdr/src/*.c; do
  compile_c "$f"
done
compile_c "$root/3rdparty/lzma/src/LzmaDec.c"
for f in adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c; do
  compile_c "$root/ps5/third_party/zlib/$f"
done
for f in "$root"/ps5/third_party/zstd/lib/common/*.c "$root"/ps5/third_party/zstd/lib/decompress/*.c; do
  compile_c "$f"
done

"$CXX" -std=c++17 -O2 -Wall -Wextra -I"$root/3rdparty/libchdr/include" \
  "$here/chd_real_test.cpp" "${objs[@]}" -o "$out/test_chd_real"

work=$(mktemp -d "${TMPDIR:-/tmp}/caliza-chd-real.XXXXXX")
trap 'rm -rf "$work"' EXIT

# Generate synthetic patterned DVD data (256 KiB = 128 x 2048-byte sectors) and CD raw data (32 x 2352-byte sectors).
python3 - "$work/dvd.bin" "$work/cd.bin" "$work/cd.cue" <<'PY'
import sys
dvd_path, cd_path, cue_path = sys.argv[1], sys.argv[2], sys.argv[3]
# Patterned non-trivial sectors so LZMA actually compresses and decompresses real bytes.
dvd = bytearray(256 * 1024)
for i in range(len(dvd)):
    dvd[i] = ((i * 37) ^ (i >> 5) ^ 0xA5) & 0xFF
with open(dvd_path, "wb") as f:
    f.write(dvd)

cd_sectors = 32
cd = bytearray(cd_sectors * 2352)
sync = bytes([0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00])
for s in range(cd_sectors):
    base = s * 2352
    cd[base:base+12] = sync
    cd[base+15] = 2  # Mode 2
    for j in range(24, 2072):
        cd[base + j] = ((s * 53 + j * 17) ^ (j >> 3)) & 0xFF
with open(cd_path, "wb") as f:
    f.write(cd)

with open(cue_path, "w") as f:
    f.write('FILE "cd.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
PY

chdman createdvd -f -i "$work/dvd.bin" -o "$work/dvd_lzma.chd" -hs 16384 -c lzma >/dev/null 2>&1
chdman createcd -f -i "$work/cd.cue" -o "$work/cd_cdlz.chd" -c cdlz >/dev/null 2>&1

"$out/test_chd_real" "$work" "$work/dvd_lzma.chd" "$work/dvd.bin" "$work/cd_cdlz.chd" "$work/cd.bin"
