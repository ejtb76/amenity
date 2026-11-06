#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Convert a break WAV (a two-bar loop) into 32 kHz mono int16 raw PCM.

Output: 32 kHz, for tools/jungle/make_bank.c to encode. Every break is made
as loud as the others: its loudness (the louder half of its 50 ms windows)
goes to LOUDNESS dBFS, and the few peaks that would then pass -1 dBFS are
held there by a look-ahead limiter (2 ms ahead, 60 ms release). The gain
and the most the limiter took off are printed.

Usage: python -I tools/jungle/make_amen.py <break.wav> [out.raw] [rate]
"""
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

OUT_RATE = 32000
SLICES = 16
LOUDNESS = -12.1   # dBFS: the Amen's, peak-normalised to -1 dBFS (it is left as it was)
CEILING = 10 ** (-1 / 20)


def loudness(y, rate):
    w = int(rate * 0.05)
    e = np.array([np.sqrt(np.mean(y[i:i + w] ** 2)) for i in range(0, len(y) - w, w)])
    return 20 * np.log10(np.sort(e)[len(e) // 2:].mean())


def limit(y, rate):
    """Hold peaks at CEILING: the gain each sample needs, at least over the 2 ms before it (look-ahead), and
    released back to 1 with a 60 ms time constant. The loop is circular: run twice, keep the second pass."""
    need = np.minimum(1.0, CEILING / np.maximum(np.abs(y), 1e-12))
    ahead = int(rate * 0.002)
    held = np.array([need[i:i + ahead + 1].min() for i in range(len(need))])
    g, k, out = 1.0, 1 - np.exp(-1 / (rate * 0.06)), np.empty_like(y)
    for _ in range(2):
        for i in range(len(y)):
            g = held[i] if held[i] < g else g + (1.0 - g) * k
            out[i] = g
    return y * out, 20 * np.log10(out.min())


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else \
        Path(__file__).resolve().parents[2] / 'build' / 'jungle' / 'amen-32k.raw'

    out_rate = int(sys.argv[3]) if len(sys.argv) > 3 else OUT_RATE
    rate, data = wavfile.read(src)
    x = data.astype(np.float64)
    if data.dtype == np.int32:
        x /= 2.0 ** 31
    elif data.dtype == np.int16:
        x /= 2.0 ** 15
    if x.ndim == 2:
        x = x.mean(axis=1)

    g = np.gcd(out_rate, rate)
    y = resample_poly(x, out_rate // g, rate // g)
    y *= CEILING / np.max(np.abs(y))
    gain = LOUDNESS - loudness(y, out_rate)
    took = 0.0
    if gain > 0.05:
        y, took = limit(y * 10 ** (gain / 20), out_rate)
    pcm = np.round(np.clip(y, -1, 32767 / 32768) * 32767).astype(np.int16)

    duration = len(x) / rate
    bpm = 480.0 / duration
    print(f'{src.name}: {rate} Hz, {duration:.4f} s, {bpm:.2f} BPM (2 bars) -> {len(pcm)} frames; '
          f'level {max(gain, 0):+.1f} dB to {LOUDNESS} dBFS loudness, limiter at most {took:.1f} dB')

    # Rough slice character, used to pick the one-shot hits.
    n = len(pcm) // SLICES
    for i in range(SLICES):
        s = y[i * n:i * n + 2048]
        spec = np.abs(np.fft.rfft(s * np.hanning(len(s))))
        freqs = np.fft.rfftfreq(len(s), 1 / out_rate)
        centroid = float((spec * freqs).sum() / max(spec.sum(), 1e-12))
        low = float(spec[freqs < 150].sum() / max(spec.sum(), 1e-12))
        print(f'  slice {i:2d}: peak {np.max(np.abs(s)):.2f}  centroid {centroid:6.0f} Hz  low {low:.2f}')

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(pcm.astype('<i2').tobytes())
    print(f'wrote {out}')


if __name__ == '__main__':
    main()
