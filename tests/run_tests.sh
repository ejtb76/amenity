#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# AMENITY's tests, as the release workflow runs them (and anyone, in WSL or on Linux):
#   - the host simulator runs every script in app/tests/ (screens, sound, lights, MIDI clock in and out);
#     a script fails on a FAIL line or a non-zero exit
#   - the breaks page's SEA5 encoder against the firmware's decoder, and its shaping against make_amen.py's
#     (only when node is there)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="$ROOT/build/tests"
mkdir -p "$OUT"
sh build.sh --host >"$OUT/host-build.log" 2>&1 || { cat "$OUT/host-build.log"; exit 1; }
fails=0
for t in app/tests/*.txt; do
    name="$(basename "$t" .txt)"
    mkdir -p "$OUT/$name"
    if app/build/host/amenity_host "$t" "$OUT/$name" >"$OUT/$name.log" 2>&1 && ! grep -q '^ *FAIL' "$OUT/$name.log"; then
        echo "ok    $name"
    else
        echo "FAIL  $name"
        grep '^ *FAIL' "$OUT/$name.log" || tail -5 "$OUT/$name.log"
        fails=$((fails + 1))
    fi
done
if command -v node >/dev/null 2>&1; then
    node tools/jungle/sea_check.mjs >"$OUT/sea_check.log" 2>&1 && echo "ok    sea_check" \
        || { echo "FAIL  sea_check"; tail -5 "$OUT/sea_check.log"; fails=$((fails + 1)); }
fi
[ "$fails" = 0 ] || { echo "$fails failed"; exit 1; }
echo "all passed"
