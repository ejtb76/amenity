#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Convert host panel captures (raw RGB565, 240x240) to PNG, and the session
audio (raw 16-bit stereo 44.1 kHz) to WAV.  Usage: panel_png.py <dir>"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.io import wavfile

d = Path(sys.argv[1])
for raw in sorted(d.glob('*.rgb565')):
    v = np.fromfile(raw, dtype='<u2').reshape(240, 240).astype(np.uint32)
    rgb = np.stack([(v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31], -1)
    Image.fromarray(rgb.astype(np.uint8)).resize((480, 480), Image.NEAREST).save(raw.with_suffix('.png'))
    raw.unlink()
audio = d / 'panel-session.raw'
if audio.exists():
    wavfile.write(d / 'panel-session.wav', 44100, np.fromfile(audio, dtype='<i2').reshape(-1, 2))
    audio.unlink()
print('\n'.join(str(p) for p in sorted(d.glob('*.png'))))
