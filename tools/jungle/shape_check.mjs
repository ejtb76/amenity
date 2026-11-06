// SPDX-License-Identifier: GPL-3.0-only
// Check the breaks page's shaping (web/breaks/prep.js shape) against the build's (make_amen.py, breaks_header.py):
// the same resampled input (tools/jungle/shape_check.py writes it), the samples and the one-shots compared.
//   python -I tools/jungle/shape_check.py && node tools/jungle/shape_check.mjs
import { readFileSync } from 'node:fs';
import { shape } from '../../web/breaks/prep.js';
const dir = new URL('../../build/jungle/', import.meta.url);
const shots = JSON.parse(readFileSync(new URL('shape_check.json', dir)));
let bad = 0;
for (const name of Object.keys(shots)) {
  const f = readFileSync(new URL(`${name}-resampled.f64`, dir));
  const y = new Float64Array(f.buffer.slice(f.byteOffset, f.byteOffset + f.length));
  const r = readFileSync(new URL(`${name}-32k.raw`, dir)), want = new Int16Array(r.buffer.slice(r.byteOffset, r.byteOffset + r.length));
  const got = shape(y, 32000);
  let worst = 0;
  for (let i = 0; i < want.length; i++) worst = Math.max(worst, Math.abs(got.pcm[i] - want[i]));
  const same = got.shots.join() === shots[name].join();
  if (worst > 2 || !same || got.pcm.length !== want.length) bad++;
  console.log(`${name}: samples differ by at most ${worst} (of 32767), level ${got.gain.toFixed(1)} dB, ` +
              `limited ${got.limited.toFixed(1)} dB; one-shots ${got.shots.join(' ')} ${same ? '= breaks_header' : '!= ' + shots[name].join(' ')}`);
}
process.exit(bad ? 1 : 0);
