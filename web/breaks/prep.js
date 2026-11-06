// SPDX-License-Identifier: GPL-3.0-only
// A break for AMENITY, made in the browser as tools/jungle/make_amen.py and tools/jungle/breaks_header.py make the
// built-in ones: two bars, mono, 32 kHz (lower for a break over 3 s, so that it fits its slot), as loud as the Amen
// (peaks limited), its one-shots picked from its slices.

export const SLOT_FRAMES = Math.floor((17 * 4096 - 256) / 228) * 320;   // the most frames a slot holds (app/user_breaks.c)
const LOUDNESS = -12.1;            // dBFS: the Amen's
const CEILING = 10 ** (-1 / 20);

// the louder half of the 50 ms windows, as an RMS level in dBFS
function loudness(y, rate) {
  const w = Math.floor(rate * 0.05), e = [];
  for (let i = 0; i + w < y.length; i += w) {
    let s = 0;
    for (let k = i; k < i + w; k++) s += y[k] * y[k];
    e.push(Math.sqrt(s / w));
  }
  e.sort((a, b) => a - b);
  const top = e.slice(e.length >> 1);
  return 20 * Math.log10(top.reduce((a, b) => a + b, 0) / top.length);
}

// hold peaks at CEILING: each sample's gain at least what the next 2 ms need, released over 60 ms; the loop is a
// circle, so twice round, the second kept. -> the most it took off, dB
function limit(y, rate) {
  const n = y.length, need = new Float64Array(n), ahead = Math.floor(rate * 0.002);
  for (let i = 0; i < n; i++) need[i] = Math.min(1, CEILING / Math.max(Math.abs(y[i]), 1e-12));
  const held = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    let m = need[i];
    for (let k = i + 1; k <= i + ahead && k < n; k++) if (need[k] < m) m = need[k];
    held[i] = m;
  }
  const k = 1 - Math.exp(-1 / (rate * 0.06)), g = new Float64Array(n);
  let gain = 1, least = 1;
  for (let pass = 0; pass < 2; pass++)
    for (let i = 0; i < n; i++) {
      gain = held[i] < gain ? held[i] : gain + (1 - gain) * k;
      g[i] = gain;
    }
  for (let i = 0; i < n; i++) { y[i] *= g[i]; if (g[i] < least) least = g[i]; }
  return 20 * Math.log10(least);
}

// each slice's first 60 ms: its energy, its share below 150 Hz and its centre (Hz)
function features(y, rate) {
  const n = Math.floor(y.length / 16), m = Math.floor(rate * 0.06), out = [];
  const win = new Float64Array(m);
  for (let i = 0; i < m; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (m - 1));
  for (let s = 0; s < 16; s++) {
    const x = y.subarray(s * n, s * n + m);
    let energy = 0, total = 0, low = 0, centre = 0;
    for (let i = 0; i < x.length; i++) energy += x[i] * x[i];
    for (let f = 0; f <= m >> 1; f++) {     // the power spectrum, as numpy's rfft gives it
      let re = 0, im = 0;
      const w = -2 * Math.PI * f / m;
      for (let i = 0; i < x.length; i++) { re += x[i] * win[i] * Math.cos(w * i); im += x[i] * win[i] * Math.sin(w * i); }
      const p = re * re + im * im, hz = f * rate / m;
      total += p; centre += p * hz;
      if (hz < 150) low += p;
    }
    total = Math.max(total, 1e-12);
    out.push({ energy, low: low / total, centre: centre / total });
  }
  return out;
}

// kick snare kick snare snare snare hat ride, as breaks_header.py picks them
function pick(f) {
  const left = new Set([...Array(16).keys()]);
  const take = (key, cond, count) => {
    let cand = [...left].filter(cond).sort((a, b) => key(b) - key(a)).slice(0, count);
    if (cand.length < count)
      cand = cand.concat([...left].filter(i => !cand.includes(i)).sort((a, b) => f[b].energy - f[a].energy)
        .slice(0, count - cand.length));
    cand.forEach(i => left.delete(i));
    return cand;
  };
  const kicks = take(i => f[i].low * f[i].energy, () => true, 2);
  const snares = take(i => f[i].energy * (1 - f[i].low), i => f[i].centre >= 1200 && f[i].centre <= 6000, 4);
  const [hat, ride] = take(i => f[i].centre, () => true, 2);
  return [kicks[0], snares[0], kicks[1], snares[1], snares[2], snares[3], hat, ride];
}

// an AudioBuffer (any rate, any channels) -> {pcm: Int16Array, rate, seconds, bpm, gain, limited, shots}: mono at the
// slot's rate (the browser resamples), then shaped
export async function prepare(buffer) {
  const seconds = buffer.duration, rate = Math.min(32000, Math.floor(SLOT_FRAMES / seconds));
  const frames = Math.min(SLOT_FRAMES, Math.round(seconds * rate));
  const ctx = new OfflineAudioContext(1, frames, rate);      // mono (the channels mixed) at the slot's rate
  const src = ctx.createBufferSource();
  src.buffer = buffer;
  src.connect(ctx.destination);
  src.start();
  const y = Float64Array.from((await ctx.startRendering()).getChannelData(0));
  return { ...shape(y, rate), rate, seconds, bpm: 480 / seconds };
}

// mono samples at rate (changed in place) -> {pcm: Int16Array, gain, limited, shots}: peak to -1 dBFS, then to
// the Amen's loudness (peaks limited when raised), the one-shots picked (tools/jungle/shape_check.mjs checks it
// against make_amen.py and breaks_header.py)
export function shape(y, rate) {
  let peak = 0;
  for (const v of y) peak = Math.max(peak, Math.abs(v));
  if (!peak) throw new Error('the file is silent');
  for (let i = 0; i < y.length; i++) y[i] *= CEILING / peak;
  const gain = LOUDNESS - loudness(y, rate);
  let limited = 0;
  if (Math.abs(gain) > 0.05) {
    const g = 10 ** (gain / 20);
    for (let i = 0; i < y.length; i++) y[i] *= g;
    if (gain > 0) limited = limit(y, rate);
  }
  const pcm = new Int16Array(y.length);
  for (let i = 0; i < y.length; i++) pcm[i] = Math.round(Math.max(-1, Math.min(32767 / 32768, y[i])) * 32767);
  return { pcm, gain, limited, shots: pick(features(y, rate)) };
}
