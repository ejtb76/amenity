#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Write build/jungle/amen_bank.inc: an FM-1 sample bank as a C array.

web/synth/demo_bank.h includes it when present, so the installer page
offers the break as its built-in bank (and writes it after a firmware
update when the FM-1 has no valid bank); the browser simulator plays it.

The output is a copy of third-party audio: it carries the Amen notice and
no license header. It lives in build/, which git ignores.

Usage: python -I tools/jungle/bank_header.py <bank.bin> [amen_bank.inc]
"""
import struct
import sys
from pathlib import Path

src = Path(sys.argv[1])
out = Path(sys.argv[2]) if len(sys.argv) > 2 else \
    Path(__file__).resolve().parents[2] / 'build' / 'jungle' / 'amen_bank.inc'
data = src.read_bytes()
assert struct.unpack_from('<III', data, 0) == (0x53314d46, 1, len(data)), 'not an FM-1 sample bank'

lines = [
    '/* Generated from assets/third-party/amen/amen.wav.',
    ' * Third-party audio: NOT covered by this project\'s license.',
    ' * See assets/third-party/amen/README.md. */',
    f'static const uint8_t amenity_bank[{len(data)}] = {{',
]
for i in range(0, len(data), 24):
    lines.append('    ' + ','.join(str(b) for b in data[i:i + 24]) + ',')
lines.append('};')
lines.append('')
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(lines), encoding='ascii', newline='\n')
print(f'wrote {out}: {len(data)} bytes')
