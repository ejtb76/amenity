#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Write app/fm_factory_one.inc: the first voice of M-VAVE's factory DX7
bank (app/fm_factory.inc), unpacked. Amenity plays no DX7 voices, so it
keeps this one instead of the whole packed bank and spends the flash on the
built-in Amen break. The output keeps the bank's Apache-2.0 notice.

Usage: python -I tools/jungle/make_factory_one.py
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
src = (root / 'app' / 'fm_factory.inc').read_text(encoding='utf-8')
packed = [int(x) for x in re.findall(r'\d+', src.split('factory_packed[] = {')[1].split('};')[0])]

# Same decoder as app/fm_factory.c: literals below 128, else a copy run.
out, at = [], 0
while len(out) < 128:
    token = packed[at]
    at += 1
    if token < 128:
        out.append(token)
        continue
    low = packed[at]
    at += 1
    distance = low & 127
    if low & 128:
        distance |= packed[at] << 7
        at += 1
    for _ in range(token - 128 + 3):
        out.append(out[-distance])
voice = out[:128]

lines = [
    '/* SPDX-License-Identifier: Apache-2.0',
    ' * M-VAVE original V15 factory DX7 bank: its first voice ("' + bytes(voice[118:128]).decode('ascii').rstrip() + '"),',
    ' * unpacked from fm_factory.inc by tools/jungle/make_factory_one.py.',
    ' * VMEM SHA-256 of the bank: ecad0b8620f9d784d46c8da1e3c5de02c40214649ed8dec218026431046aad25',
    ' * Source and license: THIRD_PARTY.md (FM-1_015.fwsc). */',
    'static const uint8_t factory_voice[128] = {',
]
for i in range(0, 128, 16):
    lines.append('    ' + ', '.join(str(v) for v in voice[i:i + 16]) + ',')
lines += ['};', '']
(root / 'app' / 'fm_factory_one.inc').write_text('\n'.join(lines), encoding='ascii', newline='\n')
print('wrote app/fm_factory_one.inc')
