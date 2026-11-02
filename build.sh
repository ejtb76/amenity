#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build AMENITY with fm1-base (base/, a submodule: git submodule update --init --recursive).
#   ./build.sh                    app/build/amenity.fwsc (the firmware package)
#   ./build.sh --release X.Y      app/build/amenity-X.Y.fwsc
#   ./build.sh --host             the host simulator
#   ./build.sh --web              the browser emulator
# JIELI_TOOLCHAIN, AC79_SDK and PYTHON override the defaults (~/.jieli/toolchain, ~/fw-AC79_AIoT_SDK, python3).
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
exec "$PY" base/tools/build.py app "$@"
