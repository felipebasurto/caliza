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

compile_c "$root/ps5/third_party/lz4/lz4.c"

"$CXX" -std=c++17 -O2 -Wall -Wextra -Wno-unused-function \
  -I"$root" -I"$root/ps5/frontend" -I"$root/3rdparty/libchdr/include" -I"$root/ps5/third_party/lz4" -I"$root/ps5/third_party/zlib" \
  -pthread "$here/chd_real_test.cpp" "$root/ps5/frontend/fe_games.cpp" "${objs[@]}" -o "$out/test_chd_real"

work=$(mktemp -d "${TMPDIR:-/tmp}/caliza-chd-real.XXXXXX")
trap 'rm -rf "$work"' EXIT

# Generate synthetic patterned DVD data, CD raw data, and a minimal synthetic ISO9660 (SLUS-20010) for parent/child CHD tests.
python3 - "$work/dvd.bin" "$work/cd.bin" "$work/cd.cue" "$work/iso_parent.iso" "$work/iso_child.iso" <<'PY'
import struct, sys
dvd_path, cd_path, cue_path, iso_parent_path, iso_child_path = sys.argv[1:6]
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

def make_minimal_iso(extra_byte):
    buf = bytearray(32 * 2048)
    # Sector 16: ISO9660 PVD
    pvd = 16 * 2048
    buf[pvd] = 1
    buf[pvd+1:pvd+6] = b"CD001"
    buf[pvd+6] = 1
    # Root directory record at offset 156: LBA 18, size 2048
    buf[pvd+156] = 34
    struct.pack_into("<I", buf, pvd + 158, 18)
    struct.pack_into("<I", buf, pvd + 166, 2048)
    # Sector 18: root directory entry for SYSTEM.CNF;1 at LBA 19
    cnf = b"BOOT2 = cdrom0:\\SLUS_200.10;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"
    root = 18 * 2048
    name = b"SYSTEM.CNF;1"
    rec_len = 33 + len(name)
    if rec_len % 2:
        rec_len += 1
    buf[root] = rec_len
    struct.pack_into("<I", buf, root + 2, 19)
    struct.pack_into("<I", buf, root + 10, len(cnf))
    buf[root + 32] = len(name)
    buf[root + 33:root + 33 + len(name)] = name
    # Sector 19: SYSTEM.CNF
    buf[19 * 2048:19 * 2048 + len(cnf)] = cnf
    # Sector 24: small difference between parent and child
    buf[24 * 2048] = extra_byte
    return buf

with open(iso_parent_path, "wb") as f:
    f.write(make_minimal_iso(0x11))
with open(iso_child_path, "wb") as f:
    f.write(make_minimal_iso(0x22))
PY

chdman createdvd -f -i "$work/dvd.bin" -o "$work/dvd_lzma.chd" -hs 16384 -c lzma >/dev/null 2>&1
chdman createcd -f -i "$work/cd.cue" -o "$work/cd_cdlz.chd" -c cdlz >/dev/null 2>&1
mkdir -p "$work/with-parent" "$work/orphan-only"
chdman createdvd -f -i "$work/iso_parent.iso" -o "$work/with-parent/parent.chd" -hs 16384 -c zlib >/dev/null 2>&1
chdman createdvd -f -i "$work/iso_child.iso" -op "$work/with-parent/parent.chd" -o "$work/with-parent/child.chd" -hs 16384 -c zlib >/dev/null 2>&1
cp "$work/with-parent/child.chd" "$work/orphan-only/child.chd"

"$out/test_chd_real" "$work" "$work/dvd_lzma.chd" "$work/dvd.bin" "$work/cd_cdlz.chd" "$work/cd.bin" \
  "$work/with-parent/parent.chd" "$work/with-parent/child.chd" "$work/orphan-only/child.chd"
