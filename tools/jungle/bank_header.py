#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Write an FM-1 sample bank as a C array for the build:

  default      build/jungle/amen_bank.inc: the whole bank. web/synth/demo_bank.h
               includes it when present, so the installer page offers the
               break as its built-in bank (and writes it after a firmware
               update when the FM-1 has no valid bank).
  --fallback   build/jungle/amen_fallback.inc: slot 1's SEA5 data alone and
               its frame count: the firmware's built-in break
               (app/jungle/jungle_fallback.c).

The output is a copy of third-party audio: it carries the Amen notice and
no license header. It lives in build/, which git ignores.

Usage: python -I tools/jungle/bank_header.py <bank.bin> [out.inc] [--fallback]
"""
import struct
import sys
from pathlib import Path

args = [a for a in sys.argv[1:] if a != '--fallback']
fallback = '--fallback' in sys.argv
src = Path(args[0])
default = 'amen_fallback.inc' if fallback else 'amen_bank.inc'
out = Path(args[1]) if len(args) > 1 else Path(__file__).resolve().parents[2] / 'build' / 'jungle' / default
data = src.read_bytes()
assert struct.unpack_from('<III', data, 0) == (0x53314d46, 1, len(data)), 'not an FM-1 sample bank'
name, frames = 'amenity_bank', 0
if fallback:
    offset, frames, size = struct.unpack_from('<III', data, 32)
    data = data[offset:offset + size]
    name = 'amen_fallback'

lines = [
    '/* Generated from assets/third-party/amen/amen.wav.',
    ' * Third-party audio: NOT covered by this project\'s license.',
    ' * See assets/third-party/amen/README.md. */',
    f'static const uint8_t {name}[{len(data)}] = {{',
]
for i in range(0, len(data), 24):
    lines.append('    ' + ','.join(str(b) for b in data[i:i + 24]) + ',')
lines.append('};')
if fallback:
    lines.append(f'#define AMEN_FALLBACK_FRAMES {frames}U')
lines.append('')
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(lines), encoding='ascii', newline='\n')
print(f'wrote {out}: {len(data)} bytes')
