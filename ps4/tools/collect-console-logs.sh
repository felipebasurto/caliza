#!/usr/bin/env bash
# Read-only collection of PS4SX2 validation logs over the console's FTP server.
# It never writes, deletes, installs, or interrupts anything on the console.
#
#   PS4_HOST=192.168.1.20 bash ps4/tools/collect-console-logs.sh /tmp/ps4-logs
#
# Optional: PS4_FTP_PORT=2121. Then analyze, for example:
#   python3 ps4/tools/analyze_console_logs.py --expect-input --expect-wsi --expect-audio \
#     --expect-state --expect-gameplay /tmp/ps4-logs/*.log /tmp/ps4-logs/*.txt 2>/dev/null
set -euo pipefail

out="${1:-}"
if [ -z "$out" ] || [ -z "${PS4_HOST:-}" ]; then
    echo "usage: PS4_HOST=<console-ip> [PS4_FTP_PORT=2121] bash ps4/tools/collect-console-logs.sh <output-dir>" >&2
    exit 2
fi
mkdir -p "$out"
ftp="ftp://${PS4_HOST}:${PS4_FTP_PORT:-2121}"

fetch() {
    local remote="$1" local_name="$2" required="$3"
    local target="$out/$local_name"
    rm -f "$target"
    if curl -fsS --connect-timeout 10 --max-time 30 "$ftp$remote" -o "$target"; then
        echo "collected $remote -> $target"
    else
        rm -f "$target"
        if [ "$required" = required ]; then
            echo "missing required log $remote" >&2
            return 1
        fi
        echo "optional log absent: $remote"
    fi
}

status=0
fetch /data/PCSX2/boot.log boot.log required || status=1
fetch /data/PCSX2/emulog.txt emulog.txt required || status=1
fetch /data/PCSX2/settings.log settings.log optional || true
fetch /data/Crema/boot-trace.log boot-trace.log optional || true
fetch /data/Crema/crash.log crash.log optional || true

echo "console log collection complete in $out (read-only)"
exit "$status"
