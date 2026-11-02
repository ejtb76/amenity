#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the firmware's built-in break from the Amen sample:
#   build/jungle/amen_fallback.inc  the break as SEA5 at 32 kHz (third-party audio, git-ignored)
# Usage: tools/jungle/make_amen_assets.sh [amen.wav]
# AMENITY_WITH_AMEN=0, or no sample, builds without it (the firmware then has no break).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WAV="${1:-$ROOT/assets/third-party/amen/amen.wav}"
OUT="$ROOT/build/jungle"
PY="${PYTHON:-python3}"
mkdir -p "$OUT"
if [ "${AMENITY_WITH_AMEN:-1}" = 0 ] || [ ! -f "$WAV" ]; then
    echo "make_amen_assets: building without the Amen sample"
    rm -f "$OUT/amen_fallback.inc"
    exit 0
fi
CC="${CC:-$(command -v cc || command -v gcc || command -v clang || true)}"
if [ -z "$CC" ]; then
    # (WSL without build-essential: make it on Windows once, or apt install build-essential)
    [ -f "$OUT/amen_fallback.inc" ] && { echo "make_amen_assets: no C compiler, keeping $OUT/amen_fallback.inc"; exit 0; }
    echo "make_amen_assets: no C compiler (apt install build-essential)"; exit 1
fi
"$CC" -O2 -I"$ROOT/tools/jungle/sea" -I"$ROOT/app/jungle" -o "$OUT/make_bank" \
    "$ROOT/tools/jungle/make_bank.c" "$ROOT/app/jungle/sea_decode.c" -lm
"$PY" -I "$ROOT/tools/jungle/make_amen.py" "$WAV" "$OUT/amen-32k.raw" 32000
"$OUT/make_bank" "$OUT/amen-32k.raw" "$OUT/amen-32k.bin" "AMEN 160"
"$PY" -I "$ROOT/tools/jungle/bank_header.py" "$OUT/amen-32k.bin" "$OUT/amen_fallback.inc" --fallback
