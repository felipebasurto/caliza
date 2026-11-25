#!/usr/bin/env bash
# PCSX2 PS4 port (AI-assisted): compile (Makefile.ps4), link (link/CMakeLists.txt) and package PCSX2 for the PS4.
#   ps4/build-ps4.sh [imports|package|upload]
#     imports: after the link, list the imported names no PS4 system library provides (PS5-only calls)
#     package: also make the pkg (PCSX00002) in ~/pcsx2ps4-build/out
#     upload:  package, then put the pkg in the console's /data/pkg over FTP (PS4_HOST, PS4_FTP_PORT)
# Uses CEMUPS4's toolchain and runtime (~/cemups4) and its sysroot and Mesa build (~/cemups4-build), read only.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PCSX2="$(cd "$HERE/.." && pwd)"
PORT="${PS4_PORT_DIR:-$HOME/cemups4}"
PSB="${PCSX2PS4_BUILD:-$HOME/pcsx2ps4-build}"
export PS4_SYSROOT="${PS4_SYSROOT:-$HOME/cemups4-build/sysroot}"
OPENORBIS="${OPENORBIS:-/opt/pacbrew/ps4/openorbis}"
mode="${1:-}"

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
make -C "$HERE" -f Makefile.ps4 -j"${JOBS:-20}" PGS=$pgs NARROW="$narrow" LTO="$lto" all objects.txt > "$PSB/compile.log" 2>&1 \
    || { grep -E "error" "$PSB/compile.log" | head -40; exit 1; }
tail -1 "$PSB/compile.log"

echo "[build-ps4] linking"
source "$PORT/scripts/ps4-env.sh"
link="$PSB/build/link"
# Reconfigure every time: CMake caches PS4_LTO and ORBIS_ORDER_FILE, so an existing link tree would otherwise keep
# the previous experiment's values. This does not recompile the port objects; Makefile.ps4 owns those.
ps4_cmake -S "$HERE/link" -B "$link" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$PORT/toolchain/ps4-cemu.cmake" -DPS4_PORT_DIR="$PORT" \
    -DPCSX2_OBJECTS_FILE="$HERE/objects.txt" -DPS4_PAID=0x3800000000000035 \
    -DPS4_NARROW="$narrow" -DPS4_LTO="$lto" -DORBIS_ORDER_FILE="$order" > "$PSB/link-configure.log" 2>&1 \
    || { tail -30 "$PSB/link-configure.log"; exit 1; }
elf="$link/pcsx2"
eboot="$link/pcsx2_eboot/eboot.bin"
rm -f "$eboot" # a failed link must not leave the last eboot to be packaged
make -C "$link" -j"${JOBS:-20}" > "$PSB/link.log" 2>&1 || true
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
    llvm-readelf-21 --dyn-syms "$elf" 2>/dev/null | awk '$7=="UND" {print $8}' | sed 's/@.*//' | sort -u > "$PSB/undefined.txt"
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
    else
        echo "[build-ps4] no known PS5-only dynamic imports"
    fi
    [ -f "$eboot" ] || exit 1
}
echo "[build-ps4] eboot: $eboot ($(du -h "$eboot" | cut -f1))"

if [ "$mode" = package ] || [ "$mode" = upload ]; then
    stage="$PSB/build/stage"
    rm -rf "$stage"
    mkdir -p "$stage/sce_sys"
    cp "$eboot" "$stage/"
    cp "$PCSX2/ps4/sce_sys/icon0.png" "$stage/sce_sys/" # PS4SX2 icon, 512x512 RGB, as the PS4 wants
    cp -r "$PCSX2/bin/resources" "$stage/resources"    # /app0/resources (main-boot.cpp, ORBIS_PS4)
    # GPL conveyance and bundled-asset notices travel in the package itself.
    cp "$PCSX2/COPYING.GPLv3" "$stage/COPYING.GPLv3"
    cp "$PCSX2/ps5/frontend/assets/SOURCES.txt" "$stage/ASSET-SOURCES.txt"
    cp "$PCSX2/ps5/frontend/assets/fonts/FontAwesome-LICENSE.txt" "$stage/FontAwesome-LICENSE.txt"
    cat > "$stage/SOURCE.txt" <<'EOF'
PS4SX2 is a GPL-3.0-or-later derivative of PCSX2, built on PS5SX2.
Corresponding source: https://github.com/felipebasurto/PS4SX2
Upstream PCSX2 source: https://github.com/PCSX2/pcsx2
EOF
    # BIOS files are never bundled by default. Setting PCSX2_PS4_BIOS_DIR is an explicit personal-build
    # opt-in; the implicit ~/pcsx2ps4-build/bios fallback was too easy to include accidentally.
    bios="${PCSX2_PS4_BIOS_DIR:-}"
    if [ -n "$bios" ] && [ -d "$bios" ] && [ -n "$(ls -A "$bios")" ]; then
        mkdir -p "$stage/bios"
        cp "$bios"/* "$stage/bios/"
        cat > "$stage/PERSONAL-BUILD.txt" <<'EOF'
This package contains console BIOS files explicitly added by its builder for personal use.
Do not redistribute it. Public PS4SX2 packages never include BIOS files.
EOF
        echo "[build-ps4] BIOS files in the package: $(ls "$stage/bios" | wc -l) from $bios (personal build: don't share it)"
    else
        echo "[build-ps4] BIOS files in the package: none (redistributable build)"
    fi
    # Plain homebrew launch values (CEMUPS4's), plus PS4 Pro mode and the seventh CPU core. No Sony modules: no Piglet.
    out="$PSB/out"
    mkdir -p "$out"
    rm -f "$out"/IV0000-PCSX*.pkg # only the newest package is kept
    pkg="$(SFO_STYLE=plain NO_RIGHT_SPRX=1 SFO_ATTRIBUTE="${SFO_ATTRIBUTE:-0x00810000}" \
        DOLPHIN_PS4_MODULES_DIR=/nonexistent "$HERE/make-pkg.sh" "$stage" PCSX00002 "PS4SX2" \
        "${PCSX2_PS4_VERSION:-00.01}" PCSX2PS4 "$out" | tail -1)"
    echo "[build-ps4] package: $pkg ($(du -h "$pkg" | cut -f1))"
    # Optionally a copy elsewhere (PCSX2_PS4_PKG_DIR, e.g. a Windows folder): the current package, and the one it replaces
    # moved into backups/ with its date, keeping the last 5. Personal builds carry your BIOS: keep them to yourself.
    keep="${PCSX2_PS4_PKG_DIR:-}"
    if [ -n "$keep" ] && mkdir -p "$keep/backups" 2>/dev/null; then
        for prev in "$keep"/IV0000-PCSX*.pkg; do
            [ -f "$prev" ] || continue
            mv "$prev" "$keep/backups/$(basename "$prev" .pkg)_$(date -r "$prev" +%Y-%m-%d_%H%M).pkg"
        done
        ls -1t "$keep"/backups/IV0000-PCSX*.pkg 2>/dev/null | tail -n +6 | while read -r old; do rm -f "$old"; done
        cp "$pkg" "$keep/" && echo "[build-ps4] copied to $keep (older ones in backups/, last 5 kept)"
    fi
    if [ "$mode" = upload ]; then
        [ -n "${PS4_HOST:-}" ] || { echo "[build-ps4] set PS4_HOST to the console's IP address (FTP on PS4_FTP_PORT, default 2121)" >&2; exit 1; }
        ftp="ftp://${PS4_HOST}:${PS4_FTP_PORT:-2121}"
        # This port's older packages (other PCSX titles and versions) leave the console's /data/pkg first.
        for old in $(curl -sS --connect-timeout 10 --list-only "$ftp/data/pkg/" | tr -d '\r' | grep '^IV0000-PCSX.*\.pkg$' || true); do
            if [ "$old" != "$(basename "$pkg")" ]; then
                curl -sS --connect-timeout 10 -Q "DELE /data/pkg/$old" "$ftp/data/pkg/" -o /dev/null && echo "[build-ps4] removed old $old"
            fi
        done
        curl -sS --connect-timeout 10 -T "$pkg" "$ftp/data/pkg/"
        echo "[build-ps4] uploaded $(basename "$pkg") to ${PS4_HOST}"
    fi
fi
