#!/usr/bin/env bash
# PCSX2 PS4 port (AI-assisted): compile (Makefile.ps4), link (link/CMakeLists.txt) and package PCSX2 for the PS4.
#   ps4/build-ps4.sh [imports|package|upload]
#     imports: after the link, list the imported names no PS4 system library provides (PS5-only calls)
#     package: also make the pkg (ZERU00001) in ~/pcsx2ps4-build/out
#     upload:  package, then put the pkg in the console's /data/pkg over FTP (PS4_HOST, PS4_FTP_PORT)
# Uses CEMUPS4's toolchain and runtime (~/cemups4) and its sysroot and Mesa build (~/cemups4-build), read only.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PCSX2="$(cd "$HERE/.." && pwd)"
PORT="${PS4_PORT_DIR:-$HOME/cemups4}"
PSB="${CALIZA_PS4_BUILD:-$HOME/pcsx2ps4-build}"
mkdir -p "$PSB"
export PS4_SYSROOT="${PS4_SYSROOT:-$HOME/cemups4-build/sysroot}"
OPENORBIS="${OPENORBIS:-/opt/pacbrew/ps4/openorbis}"
mode="${1:-}"
case "$mode" in
  ""|imports|package|upload) ;;
  *) echo "usage: bash ps4/build-ps4.sh [imports|package|upload]" >&2; exit 2 ;;
esac

# Performance experiment knobs. The environment is the interface, as with ORBIS_ORDER on PS5SX2's link scripts:
#   NARROW=0|1 and LTO=0|1 select Makefile.ps4's compile flags; ORBIS_ORDER=<file|none> selects the order file.
narrow="${NARROW:-1}"
lto="${LTO:-0}"
order="${ORBIS_ORDER:-$HERE/orbis-ps4-hot.order}"
# CMake wants a full path; a relative override is the caller's, as a shell path would be.
if [ "$order" != none ] && [ "${order#/}" = "$order" ]; then
    order="$PWD/$order"
fi

echo "[build-ps4] compiling (NARROW=$narrow LTO=$lto ORBIS_ORDER=$order)"
# paraLLEl-GS as a second renderer when ps4/build-pgs.sh has built its libraries.
pgs=0
[ -s "$PSB/pgs/libs.txt" ] && pgs=1 && echo "[build-ps4] with paraLLEl-GS"
build_commit="$(git -C "$PCSX2" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
build_tag="caliza-$build_commit"
if [ -n "$(git -C "$PCSX2" status --porcelain 2>/dev/null)" ]; then
    build_commit="$build_commit + uncommitted changes"
    build_tag="$build_tag-dirty"
fi
mkdir -p "$PSB/obj"
tag_file="$PSB/obj/.build-tag"
if [ ! -f "$tag_file" ] || [ "$(cat "$tag_file")" != "$build_tag" ]; then
    printf '%s\n' "$build_tag" > "$tag_file.tmp"
    mv "$tag_file.tmp" "$tag_file"
fi
objects_file="$PSB/objects.txt"
rm -f "$objects_file"
make -C "$HERE" -f Makefile.ps4 -j"${JOBS:-20}" OBJDIR="$PSB/obj" OBJECTS_FILE="$objects_file" PGS=$pgs ORBIS_BUILD_TAG="$build_tag" NARROW="$narrow" LTO="$lto" all "$objects_file" > "$PSB/compile.log" 2>&1 \
    || { grep -E "error" "$PSB/compile.log" | head -40; exit 1; }
tail -1 "$PSB/compile.log"

echo "[build-ps4] linking"
source "$PORT/scripts/ps4-env.sh"
link="$PSB/build/link"
# Reconfigure every time: CMake caches PS4_LTO and ORBIS_ORDER_FILE, so an existing link tree would otherwise keep
# the previous experiment's values. This does not recompile the port objects; Makefile.ps4 owns those.
ps4_cmake -S "$HERE/link" -B "$link" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$PORT/toolchain/ps4-cemu.cmake" -DPS4_PORT_DIR="$PORT" \
    -DPCSX2_OBJECTS_FILE="$objects_file" -DPGS_LIBS_FILE="$PSB/pgs/libs.txt" -DPS4_PAID=0x3800000000000035 \
    -DPS4_NARROW="$narrow" -DPS4_LTO="$lto" -DORBIS_ORDER_FILE="$order" > "$PSB/link-configure.log" 2>&1 \
    || { tail -30 "$PSB/link-configure.log"; exit 1; }
elf="$link/pcsx2"
eboot="$link/pcsx2_eboot/eboot.bin"
rm -f "$eboot" "$elf" # a failed link must not leave stale artifacts to audit or package
if ! make -C "$link" -j"${JOBS:-20}" > "$PSB/link.log" 2>&1; then
    tail -40 "$PSB/link.log" >&2
    echo "[build-ps4] link failed (log: $PSB/link.log)" >&2
    exit 1
fi
grep -E "error|Failed|missing" "$PSB/link.log" | grep -v "no version information" | head -20 || true

[ -f "$elf" ] || { echo "[build-ps4] no ELF: the link failed (log: $PSB/link.log)" >&2; exit 1; }
{
    # Every name the system libraries export, then the ELF's undefined names that none of them has.
    index="$PSB/ps4-exports.txt"
    if [ ! -s "$index" ]; then
        for so in "$OPENORBIS"/lib/*.so; do
            llvm-nm-21 -D --defined-only "$so" 2>/dev/null | awk -v lib="$(basename "$so" .so)" '{print $NF" "lib}'
        done | sort -u -k1,1 > "$index"
    fi
    # Audit dynamic imports only. `llvm-nm -u` also reports weak/static Vulkan/Mesa names that are not
    # runtime imports at all (thousands of false positives). readelf's UND dynamic symbols are the names
    # the console's module loader must resolve.
    llvm-readelf-21 --dyn-syms "$elf" 2>/dev/null | awk '$7=="UND" && $8!="" {print $8}' | sed 's/@.*//' | sort -u > "$PSB/undefined.txt"
    join -a1 "$PSB/undefined.txt" "$index" | awk 'NF==1' | grep -v "^annotate_" > "$PSB/unresolved.txt" || true
    echo "[build-ps4] $(wc -l < "$PSB/undefined.txt") dynamic imports, $(wc -l < "$PSB/unresolved.txt") with no PS4 library (list: $PSB/unresolved.txt)"
    head -80 "$PSB/unresolved.txt"
    # OpenOrbis stub libraries export some PS5-only names, so the export-index check above cannot catch
    # them. Keep an explicit denylist for imports proven unsafe on retail PS4 firmware.
    grep -E '^sceKernelJit(CreateSharedMemory|MapSharedMemory|CreateAliasOfSharedMemory)$|^sceKernelGetProsperoSystemSwVersion$|^sceHttp2|^sceVideoOut(SetBufferAttribute2|RegisterBuffers2)$' \
        "$PSB/undefined.txt" > "$PSB/ps5-only-imports.txt" || true
    if [ -s "$PSB/ps5-only-imports.txt" ]; then
        echo "[build-ps4] WARNING: PS5-only dynamic imports remain (list: $PSB/ps5-only-imports.txt):"
        cat "$PSB/ps5-only-imports.txt"
        [ "${PS4_ALLOW_PS5_IMPORTS:-0}" = 1 ] || {
            echo "[build-ps4] refusing to package known-bad imports (set PS4_ALLOW_PS5_IMPORTS=1 only for analysis builds)" >&2
            exit 1
        }
    else
        echo "[build-ps4] no known PS5-only dynamic imports"
    fi
    if [ -s "$PSB/unresolved.txt" ]; then
        [ "${PS4_ALLOW_UNRESOLVED_IMPORTS:-0}" = 1 ] || {
            echo "[build-ps4] refusing to package unresolved dynamic imports (set PS4_ALLOW_UNRESOLVED_IMPORTS=1 only for analysis builds)" >&2
            exit 1
        }
    fi
    [ -f "$eboot" ] || exit 1
}
echo "[build-ps4] eboot: $eboot ($(du -h "$eboot" | cut -f1))"

if [ "$mode" = package ] || [ "$mode" = upload ]; then
    stage="$PSB/build/stage"
    rm -rf "$stage"
    mkdir -p "$stage/sce_sys"
    cp "$eboot" "$stage/"
    cp "$PCSX2/ps4/sce_sys/icon0.png" "$stage/sce_sys/" # Caliza icon, 512x512 RGB, as the PS4 wants
    cp -r "$PCSX2/bin/resources" "$stage/resources"    # /app0/resources (main-boot.cpp, ORBIS_PS4)
    # GPL conveyance and bundled-asset notices travel in the package itself.
    cp "$PCSX2/COPYING.GPLv3" "$stage/COPYING.GPLv3"
    cp "$PCSX2/ps5/frontend/assets/SOURCES.txt" "$stage/ASSET-SOURCES.txt"
    cp "$PCSX2/ps5/frontend/assets/fonts/FontAwesome-LICENSE.txt" "$stage/FontAwesome-LICENSE.txt"
    cp "$PCSX2/bin/docs/ThirdPartyLicenses.html" "$stage/ThirdPartyLicenses.html"
    cp "$PCSX2/ps4/PORT-THIRD-PARTY.txt" "$stage/PORT-THIRD-PARTY.txt"
    mkdir -p "$stage/licenses"
    cp "$PCSX2/ps5/third_party/zlib/LICENSE" "$stage/licenses/zlib-LICENSE.txt"
    cp "$PCSX2/ps5/third_party/zstd/LICENSE" "$stage/licenses/zstd-LICENSE.txt"
    cp "$PCSX2/ps5/third_party/lz4/LICENSE" "$stage/licenses/lz4-LICENSE.txt"
    cp "$PCSX2/ps5/third_party/libarchive/COPYING" "$stage/licenses/libarchive-COPYING.txt"
    cp "$PCSX2/ps5/third_party/mbedtls/LICENSE" "$stage/licenses/mbedtls-LICENSE.txt"
    cp "$PCSX2/ps5/third_party/libnfs/LICENCE-LGPL-2.1.txt" "$stage/licenses/libnfs-LGPL-2.1.txt"
    cp "$PCSX2/ps5/third_party/libnfs/LICENCE-BSD.txt" "$stage/licenses/libnfs-BSD.txt"
    {
        echo "Caliza is a GPL-3.0-or-later derivative of PCSX2."
        echo "Corresponding source: https://github.com/felipebasurto/caliza"
        echo "Upstream PCSX2 source: https://github.com/PCSX2/pcsx2"
        echo "Build commit: $build_commit"
    } > "$stage/SOURCE.txt"
    # BIOS files are never bundled by default. Setting CALIZA_PS4_BIOS_DIR is an explicit personal-build
    # opt-in; the implicit ~/pcsx2ps4-build/bios fallback was too easy to include accidentally.
    bios="${CALIZA_PS4_BIOS_DIR:-}"
    if [ -n "$bios" ] && [ -d "$bios" ] && [ -n "$(ls -A "$bios")" ]; then
        mkdir -p "$stage/bios"
        cp "$bios"/* "$stage/bios/"
        cat > "$stage/PERSONAL-BUILD.txt" <<'EOF'
This package contains console BIOS files explicitly added by its builder for personal use.
Do not redistribute it. Public Caliza packages never include BIOS files.
EOF
        echo "[build-ps4] BIOS files in the package: $(ls "$stage/bios" | wc -l) from $bios (personal build: don't share it)"
    else
        echo "[build-ps4] BIOS files in the package: none (redistributable build)"
    fi
    # Plain homebrew launch values (CEMUPS4's), plus PS4 Pro mode and the seventh CPU core. No Sony modules: no Piglet.
    out="$PSB/out"
    mkdir -p "$out"
    rm -f "$out"/IV0000-ZERU*.pkg "$out"/IV0000-PCSX*.pkg # only the newest package is kept (PCSX*: the development identity's, drop once no build root has them)
    # CALIZA00 is the content label: make-pkg.sh keeps A-Z0-9 only and zero-pads it to 16, so the content id
    # comes out IV0000-ZERU00001_00-CALIZA0000000000 and the pkg IV0000-ZERU00001_00-CALIZA0000000000.pkg.
    pkg="$(SFO_STYLE=plain NO_RIGHT_SPRX=1 SFO_ATTRIBUTE="${SFO_ATTRIBUTE:-0x00810000}" \
        DOLPHIN_PS4_MODULES_DIR=/nonexistent "$HERE/make-pkg.sh" "$stage" ZERU00001 "Caliza" \
        "${CALIZA_PS4_VERSION:-00.03}" CALIZA00 "$out" | tail -1)"
    python3 "$HERE/tools/validate_package_stage.py" "$stage" --pkg "$pkg" --expect-commit "$(git -C "$PCSX2" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
    echo "[build-ps4] package: $pkg ($(du -h "$pkg" | cut -f1))"
    # Optionally a copy elsewhere (CALIZA_PS4_PKG_DIR, e.g. a Windows folder): the current package, and the one it replaces
    # moved into backups/ with its date, keeping the last 5. Personal builds carry your BIOS: keep them to yourself.
    keep="${CALIZA_PS4_PKG_DIR:-}"
    if [ -n "$keep" ] && mkdir -p "$keep/backups" 2>/dev/null; then
        for prev in "$keep"/IV0000-ZERU*.pkg "$keep"/IV0000-PCSX*.pkg; do
            [ -f "$prev" ] || continue
            mv "$prev" "$keep/backups/$(basename "$prev" .pkg)_$(date -r "$prev" +%Y-%m-%d_%H%M).pkg"
        done
        ls -1t "$keep"/backups/IV0000-ZERU*.pkg "$keep"/backups/IV0000-PCSX*.pkg 2>/dev/null | tail -n +6 | while read -r old; do rm -f "$old"; done
        cp "$pkg" "$keep/" && echo "[build-ps4] copied to $keep (older ones in backups/, last 5 kept)"
    fi
    if [ "$mode" = upload ]; then
        [ -n "${PS4_HOST:-}" ] || { echo "[build-ps4] set PS4_HOST to the console's IP address (FTP on PS4_FTP_PORT, default 2121)" >&2; exit 1; }
        ftp="ftp://${PS4_HOST}:${PS4_FTP_PORT:-2121}"
        # This port's older packages (other versions, and the development identity's PCSX ones) leave /data/pkg first.
        for old in $(curl -sS --connect-timeout 10 --list-only "$ftp/data/pkg/" | tr -d '\r' | grep -E '^IV0000-(ZERU|PCSX).*\.pkg$' || true); do
            if [ "$old" != "$(basename "$pkg")" ]; then
                curl -sS --connect-timeout 10 -Q "DELE /data/pkg/$old" "$ftp/data/pkg/" -o /dev/null && echo "[build-ps4] removed old $old"
            fi
        done
        curl -sS --connect-timeout 10 -T "$pkg" "$ftp/data/pkg/"
        echo "[build-ps4] uploaded $(basename "$pkg") to ${PS4_HOST}"
    fi
fi
