// SPDX-License-Identifier: GPL-3.0-only
// Check that the breaks page's SEA5 encoder (web/breaks/sea.js) makes what tools/jungle/make_bank.c makes:
// for each build/jungle/NAME-32k.raw, the encoder's bytes against the SEA data in NAME-32k.bin.
//   node tools/jungle/sea_check.mjs
import { readFileSync, readdirSync } from 'node:fs';
import { seaEncode } from '../../web/breaks/sea.js';
const dir = new URL('../../build/jungle/', import.meta.url);
let bad = 0;
for (const f of readdirSync(dir).filter(n => n.endsWith('-32k.raw'))) {
  const raw = readFileSync(new URL(f, dir)), pcm = new Int16Array(raw.buffer, raw.byteOffset, raw.length >> 1);
  const bank = readFileSync(new URL(f.replace('.raw', '.bin'), dir)), v = new DataView(bank.buffer, bank.byteOffset);
  const off = v.getUint32(32, true), size = v.getUint32(40, true), want = bank.subarray(off, off + size);
  const t0 = performance.now(), got = seaEncode(pcm), ms = performance.now() - t0;
  const same = got.length === want.length && got.every((b, i) => b === want[i]);
  if (!same) bad++;
  console.log(`${f}: ${got.length} bytes, ${same ? 'identical to make_bank' : 'DIFFERENT'} (${ms.toFixed(0)} ms)`);
}
process.exit(bad ? 1 : 0);
