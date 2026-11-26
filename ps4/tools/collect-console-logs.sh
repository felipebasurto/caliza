#!/usr/bin/env bash
# Read-only collection of Caliza validation logs over the console's FTP server.
# It never writes, deletes, installs, or interrupts anything on the console.
#
#   PS4_HOST=192.168.1.20 bash ps4/tools/collect-console-logs.sh /tmp/ps4-logs
#
# Optional: PS4_FTP_PORT=2121, PS4_LOG_ROTATIONS=2, PS4_LOG_ROOT=legacy for old PCSX2 builds.
# Then analyze, for example:
#   python3 ps4/tools/analyze_console_logs.py --expect-input --expect-wsi --expect-audio \
#     --expect-state --expect-gameplay /tmp/ps4-logs/*.log /tmp/ps4-logs/*.txt 2>/dev/null
set -euo pipefail

out="${1:-}"
if [ -z "$out" ] || [ -z "${PS4_HOST:-}" ]; then
    echo "usage: PS4_HOST=<console-ip> [PS4_FTP_PORT=2121] bash ps4/tools/collect-console-logs.sh <output-dir>" >&2
    exit 2
fi
case "${PS4_LOG_ROOT:-caliza}" in
    caliza) log_root=/data/caliza ;;
    legacy) log_root=/data/PCSX2 ;;
    *) echo "PS4_LOG_ROOT must be caliza or legacy" >&2; exit 2 ;;
esac
mkdir -p "$out" "$out/rotated"
ftp="ftp://${PS4_HOST}:${PS4_FTP_PORT:-2121}"

fetch() {
    local local_name="$1" required="$2"
    shift 2
    local target="$out/$local_name"
    rm -f "$target"
    local remote
    for remote in "$@"; do
        if curl -fsS --noproxy "*" --connect-timeout 10 --max-time 30 "$ftp$remote" -o "$target"; then
            echo "collected $remote -> $target"
            return 0
        fi
    done
    rm -f "$target"
    if [ "$required" = required ]; then
        echo "missing required log: $*" >&2
        return 1
    fi
    echo "optional log absent: $*"
}

fetch_app_log() {
    local name="$1" required="$2" local_name="${3:-$1}"
    fetch "$local_name" "$required" "$log_root/logs/$name" "$log_root/$name"
}

status=0
fetch_app_log boot.log required || status=1
fetch_app_log emulog.txt required || status=1
fetch_app_log stderr.log optional || true
fetch_app_log settings.log optional || true
fetch boot-trace.log optional /data/Crema/boot-trace.log || true
fetch crash.log optional /data/Crema/crash.log || true

rotations="${PS4_LOG_ROTATIONS:-2}"
case "$rotations" in ''|*[!0-9]*) rotations=2 ;; esac
for stream in boot.log stderr.log emulog.txt; do
    stem="${stream%.*}"
    ext="${stream##*.}"
    for i in $(seq 1 "$rotations"); do
        fetch_app_log "$stem.$i.$ext" optional "rotated/$stem.$i.$ext" || true
    done
done

echo "console log collection complete in $out (read-only)"
exit "$status"
