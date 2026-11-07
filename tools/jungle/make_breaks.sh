#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the firmware's built-in breaks (tools/jungle/breaks.txt) from the samples in assets/third-party:
#   build/jungle/breaks.inc  every break as SEA5 at 32 kHz, with its name and one-shot slices
#                            (third-party audio, git-ignored)
# AMENITY_WITH_SAMPLES=0, or no samples, builds without them (the firmware then has no break).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/jungle"
LIST="$ROOT/tools/jungle/breaks.txt"
PY="${PYTHON:-python3}"
mkdir -p "$OUT"
if [ "${AMENITY_WITH_SAMPLES:-1}" = 0 ]; then
    echo "make_breaks: building without the samples"
    rm -f "$OUT/breaks.inc"
    exit 0
fi
# up to date: nothing to do
if [ -f "$OUT/breaks.inc" ] && [ -z "$(find "$ROOT/assets/third-party" "$ROOT/tools/jungle" "$ROOT/app/jungle/jungle_sea_dqt.inc" -newer "$OUT/breaks.inc" \( -name '*.wav' -o -name '*.txt' -o -name '*.py' -o -name '*.c' -o -name '*.h' -o -name '*.sh' -o -name '*.inc' \) | head -1)" ]; then
    exit 0
fi
CC="${CC:-$(command -v cc || command -v gcc || command -v clang || true)}"
[ -n "$CC" ] || { echo "make_breaks: no C compiler (apt install build-essential)"; exit 1; }
"$CC" -O2 -I"$ROOT/tools/jungle/sea" -I"$ROOT/app/jungle" -o "$OUT/make_bank" \
    "$ROOT/tools/jungle/make_bank.c" "$ROOT/app/jungle/sea_decode.c" -lm
grep -v '^#' "$LIST" | while read -r folder name rest; do
    [ -n "$folder" ] || continue
    wav="$ROOT/assets/third-party/$folder/$folder.wav"
    if [ ! -f "$wav" ]; then echo "make_breaks: no $folder.wav, left out"; continue; fi
    "$PY" -I "$ROOT/tools/jungle/make_amen.py" "$wav" "$OUT/$folder-32k.raw" 32000 | head -1
    "$OUT/make_bank" "$OUT/$folder-32k.raw" "$OUT/$folder-32k.bin" "$name"
done
"$PY" -I "$ROOT/tools/jungle/breaks_header.py" "$LIST" "$OUT" "$OUT/breaks.inc"
