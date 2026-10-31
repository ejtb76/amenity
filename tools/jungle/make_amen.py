#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Convert the amen break WAV into 32 kHz mono int16 raw PCM.

The source is a two-bar loop. Output: 32 kHz (the FM-1 sample bank rate),
peak at -1 dBFS, for tools/jungle/make_bank.c to encode into a bank.

Usage: python -I tools/jungle/make_amen.py <amen.wav> [out.raw]
"""
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

OUT_RATE = 32000
SLICES = 16


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else \
        Path(__file__).resolve().parents[2] / 'build' / 'jungle' / 'amen-32k.raw'

    rate, data = wavfile.read(src)
    x = data.astype(np.float64)
    if data.dtype == np.int32:
        x /= 2.0 ** 31
    elif data.dtype == np.int16:
        x /= 2.0 ** 15
    if x.ndim == 2:
        x = x.mean(axis=1)

    g = np.gcd(OUT_RATE, rate)
    y = resample_poly(x, OUT_RATE // g, rate // g)
    y *= 10 ** (-1 / 20) / np.max(np.abs(y))
    pcm = np.round(y * 32767).astype(np.int16)

    duration = len(x) / rate
    bpm = 480.0 / duration
    print(f'{src.name}: {rate} Hz, {duration:.4f} s, {bpm:.2f} BPM (2 bars) -> {len(pcm)} frames')

    # Rough slice character, used to pick the one-shot hits.
    n = len(pcm) // SLICES
    for i in range(SLICES):
        s = y[i * n:i * n + 2048]
        spec = np.abs(np.fft.rfft(s * np.hanning(len(s))))
        freqs = np.fft.rfftfreq(len(s), 1 / OUT_RATE)
        centroid = float((spec * freqs).sum() / max(spec.sum(), 1e-12))
        low = float(spec[freqs < 150].sum() / max(spec.sum(), 1e-12))
        print(f'  slice {i:2d}: peak {np.max(np.abs(s)):.2f}  centroid {centroid:6.0f} Hz  low {low:.2f}')

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(pcm.astype('<i2').tobytes())
    print(f'wrote {out}')


if __name__ == '__main__':
    main()
