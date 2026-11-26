#!/usr/bin/env bash
# Renders the scripted Estratos tour at TV and baseline handheld-safe sizes.
# Output is written outside the repository unless CALIZA_SHOT_OUT is set.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
host=${CALIZA_HOST:-/tmp/caliza-build/caliza_host}
out=${CALIZA_SHOT_OUT:-/tmp/caliza-estratos-shots}
mkdir -p "$out/1080p" "$out/720p"
for size in 1080p 720p; do
  if [[ $size == 1080p ]]; then width=1920; height=1080; else width=1280; height=720; fi
  for script in main-tour first-run-and-picker empty-library; do
    sed "s|@OUT@|$out/$size|g" "$root/caliza/host/scripts/$script.script" >"/tmp/caliza-$script.script"
    (cd "$root" && "$host" --width "$width" --height "$height" --script "/tmp/caliza-$script.script")
  done
done
# The 200% text-size pair is rendered once, at TV size, into the output root.
sed "s|@OUT@|$out|g" "$root/caliza/host/scripts/text-200.script" >/tmp/caliza-text-200.script
(cd "$root" && "$host" --width 1920 --height 1080 --script /tmp/caliza-text-200.script)
printf 'captured Estratos preview shots in %s\n' "$out"
