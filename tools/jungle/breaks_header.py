#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write the firmware's built-in breaks as one C header: build/jungle/breaks.inc.

For each line of tools/jungle/breaks.txt whose FOLDER-32k.bin exists in the build directory (made by
tools/jungle/make_breaks.sh): the break's SEA5 data, its frame count, its name and the 8 slices its one-shots
play (kick snare kick snare snare snare hat ride). With "-" for the shots they are picked from the break itself,
from the first 60 ms of each of its 16 slices (FOLDER-32k.raw): the kicks have the most of their energy below
150 Hz, the snares are the loudest of the rest with their centre between 1.2 and 6 kHz, the hat and the ride the
brightest of what is left. Each pick is printed, to check by ear.

The output is a copy of third-party audio: it carries the notice and no license header. It lives in build/,
which git ignores.

Usage: python -I tools/jungle/breaks_header.py tools/jungle/breaks.txt build/jungle build/jungle/breaks.inc
"""
import struct
import sys
from pathlib import Path

import numpy as np

RATE = 32000


def features(raw):
    x = np.frombuffer(raw.read_bytes(), dtype='<i2').astype(np.float64) / 32768
    n = len(x) // 16
    out = []
    for i in range(16):
        s = x[i * n:i * n + int(RATE * 0.06)]
        spec = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2
        freqs = np.fft.rfftfreq(len(s), 1 / RATE)
        total = max(spec.sum(), 1e-12)
        out.append({'energy': float(np.sum(s * s)), 'low': float(spec[freqs < 150].sum() / total),
                    'centre': float((spec * freqs).sum() / total)})
    return out


def pick(f):
    left = set(range(16))

    def take(key, cond=lambda i: True, count=1):
        cand = sorted((i for i in left if cond(i)), key=key, reverse=True)[:count]
        if len(cand) < count:   # not enough that fit: the loudest of the rest
            cand += sorted((i for i in left if i not in cand), key=lambda i: f[i]['energy'], reverse=True)[:count - len(cand)]
        for i in cand:
            left.discard(i)
        return cand

    kicks = take(lambda i: f[i]['low'] * f[i]['energy'], count=2)
    snares = take(lambda i: f[i]['energy'] * (1 - f[i]['low']), lambda i: 1200 <= f[i]['centre'] <= 6000, count=4)
    hat, ride = take(lambda i: f[i]['centre'], count=2)
    return [kicks[0], snares[0], kicks[1], snares[1], snares[2], snares[3], hat, ride]


def main():
    listing, build, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    sources, arrays, rows = [], [], []
    for line in listing.read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        folder, name, *shots = line.split()
        bank = build / f'{folder}-32k.bin'
        if not bank.exists():
            continue
        data = bank.read_bytes()
        assert struct.unpack_from('<III', data, 0) == (0x53314d46, 1, len(data)), f'{bank}: not a sample bank'
        offset, frames, size = struct.unpack_from('<III', data, 32)
        sea = data[offset:offset + size]
        if shots == ['-'] or not shots:
            f = features(build / f'{folder}-32k.raw')
            shots = pick(f)
            print(f'{name}: one-shots picked ' + ' '.join(map(str, shots)) + '  (kick snare kick snare snare snare hat ride)')
            for i in sorted(set(shots)):
                print(f'    slice {i:2d}: energy {f[i]["energy"]:6.2f}  low {f[i]["low"]:.2f}  centre {f[i]["centre"]:5.0f} Hz')
        shots = [int(s) for s in shots]
        assert len(shots) == 8 and all(0 <= s < 16 for s in shots), f'{folder}: 8 slices 0..15'
        assert len(name) <= 11, f'{folder}: name over 11 characters'
        sym = f'break_{folder}'
        sources.append(f'assets/third-party/{folder}/{folder}.wav')
        arrays.append(f'static const uint8_t {sym}[{len(sea)}] = {{')
        for i in range(0, len(sea), 24):
            arrays.append('    ' + ','.join(str(b) for b in sea[i:i + 24]) + ',')
        arrays.append('};')
        rows.append(f'    {{"{name}", {sym}, sizeof {sym}, {frames}U, {{{", ".join(map(str, shots))}}}}},')
    lines = ['/* Generated from ' + ', '.join(sources) + '.',
             ' * Third-party audio: NOT covered by this project\'s license.',
             ' * See the README.md beside each of those files. */']
    lines += arrays
    lines.append('static const struct jungle_break BREAKS[] = {')
    lines += rows or ['    {0},']
    lines.append('};')
    lines.append(f'#define BREAK_COUNT {len(rows)}U')
    lines.append('')
    out.write_text('\n'.join(lines), encoding='ascii', newline='\n')
    print(f'wrote {out}: {len(rows)} breaks, ' + str(sum(len(a) for a in arrays)) + ' characters')


if __name__ == '__main__':
    main()
