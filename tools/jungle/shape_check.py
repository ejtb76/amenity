# SPDX-License-Identifier: GPL-3.0-only
"""Reference data for tools/jungle/shape_check.mjs: each build/jungle/NAME-32k.raw's input as make_amen.py has it
just before shaping (mono, resampled to 32 kHz, float64) and the one-shots breaks_header.py picks.
Usage: python -I tools/jungle/shape_check.py"""
import json
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'jungle'))
import breaks_header  # noqa: E402

out = {}
for raw in sorted((ROOT / 'build' / 'jungle').glob('*-32k.raw')):
    name = raw.name[:-len('-32k.raw')]
    rate, data = wavfile.read(ROOT / 'assets' / 'third-party' / name / f'{name}.wav')
    x = data.astype(np.float64)
    x /= 2.0 ** 31 if data.dtype == np.int32 else 2.0 ** 15 if data.dtype == np.int16 else 1.0
    if x.ndim == 2:
        x = x.mean(axis=1)
    g = np.gcd(32000, rate)
    resample_poly(x, 32000 // g, rate // g).astype('<f8').tofile(raw.with_name(f'{name}-resampled.f64'))
    out[name] = breaks_header.pick(breaks_header.features(raw))
(ROOT / 'build' / 'jungle' / 'shape_check.json').write_text(json.dumps(out))
print(out)
