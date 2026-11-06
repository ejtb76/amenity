// SPDX-License-Identifier: MIT
// SEA5 encoder for AMENITY's breaks page: a port of the FM1 Open SDK's offline encoder (sea_encode.h), which is
// Copyright (c) 2025 Dani Biro (the SEA codec), with modifications (c) 2026 M-VAVE; port (c) 2026 Presethead.
// It makes exactly what tools/jungle/make_bank.c makes (checked by tools/jungle/sea_check.mjs), for the decoder in
// the firmware (app/jungle/sea_decode.c): 320 frames a chunk, 228 bytes, 5 bits a frame, the fixed table below.
// Every number here stays below 2^53, so the C's 64-bit sums are exact as JavaScript numbers.

export const SEA_FRAMES = 320, SEA_GROUP = 20, SEA_CHUNK = 228;

// the quantizer table the firmware decodes with (app/jungle/jungle_sea_dqt.inc, Copyright (c) 2025 Dani Biro, MIT)
const DQT = new Int16Array([
  1,-1,3,-3,5,-5,7,-7,9,-9,11,-11,13,-13,15,-15,
  17,-17,19,-19,21,-21,23,-23,25,-25,27,-27,29,-29,31,-31,
  4,-4,13,-13,23,-23,33,-33,43,-43,53,-53,63,-63,73,-73,
  83,-83,93,-93,103,-103,113,-113,123,-123,133,-133,143,-143,155,-155,
  11,-11,35,-35,63,-63,91,-91,119,-119,147,-147,175,-175,203,-203,
  231,-231,259,-259,287,-287,315,-315,343,-343,371,-371,399,-399,434,-434,
  21,-21,70,-70,126,-126,182,-182,238,-238,294,-294,350,-350,406,-406,
  462,-462,518,-518,574,-574,630,-630,686,-686,742,-742,798,-798,868,-868,
  36,-36,120,-120,216,-216,312,-312,408,-408,504,-504,600,-600,696,-696,
  792,-792,888,-888,984,-984,1080,-1080,1176,-1176,1272,-1272,1368,-1368,1488,-1488,
  56,-56,188,-188,338,-338,488,-488,638,-638,788,-788,938,-938,1088,-1088,
  1238,-1238,1388,-1388,1538,-1538,1688,-1688,1838,-1838,1988,-1988,2138,-2138,2325,-2325,
  81,-81,270,-270,486,-486,702,-702,918,-918,1134,-1134,1350,-1350,1566,-1566,
  1782,-1782,1998,-1998,2214,-2214,2430,-2430,2646,-2646,2862,-2862,3078,-3078,3348,-3348,
  113,-113,375,-375,675,-675,975,-975,1275,-1275,1575,-1575,1875,-1875,2175,-2175,
  2475,-2475,2775,-2775,3075,-3075,3375,-3375,3675,-3675,3975,-3975,4275,-4275,4650,-4650,
  149,-149,498,-498,896,-896,1294,-1294,1692,-1692,2090,-2090,2488,-2488,2886,-2886,
  3284,-3284,3682,-3682,4080,-4080,4478,-4478,4876,-4876,5274,-5274,5672,-5672,6169,-6169,
  193,-193,643,-643,1157,-1157,1671,-1671,2185,-2185,2699,-2699,3213,-3213,3727,-3727,
  4241,-4241,4755,-4755,5269,-5269,5783,-5783,6297,-6297,6811,-6811,7325,-7325,7967,-7967,
  242,-242,808,-808,1454,-1454,2100,-2100,2746,-2746,3392,-3392,4038,-4038,4684,-4684,
  5330,-5330,5976,-5976,6622,-6622,7268,-7268,7914,-7914,8560,-8560,9206,-9206,10013,-10013,
  299,-299,995,-995,1791,-1791,2587,-2587,3383,-3383,4179,-4179,4975,-4975,5771,-5771,
  6567,-6567,7363,-7363,8159,-8159,8955,-8955,9751,-9751,10547,-10547,11343,-11343,12338,-12338,
  362,-362,1208,-1208,2174,-2174,3140,-3140,4106,-4106,5072,-5072,6038,-6038,7004,-7004,
  7970,-7970,8936,-8936,9902,-9902,10868,-10868,11834,-11834,12800,-12800,13766,-13766,14973,-14973,
  434,-434,1445,-1445,2601,-2601,3757,-3757,4913,-4913,6069,-6069,7225,-7225,8381,-8381,
  9537,-9537,10693,-10693,11849,-11849,13005,-13005,14161,-14161,15317,-15317,16473,-16473,17918,-17918,
  512,-512,1705,-1705,3069,-3069,4433,-4433,5797,-5797,7161,-7161,8525,-8525,9889,-9889,
  11253,-11253,12617,-12617,13981,-13981,15345,-15345,16709,-16709,18073,-18073,19437,-19437,21142,-21142,
  598,-598,1993,-1993,3587,-3587,5181,-5181,6775,-6775,8369,-8369,9963,-9963,11557,-11557,
  13151,-13151,14745,-14745,16339,-16339,17933,-17933,19527,-19527,21121,-21121,22715,-22715,24707,-24707
]);

const RECIPROCAL = [65536, 13107, 4681, 2340, 1365, 873, 606, 436, 329, 255, 202, 164, 135, 113, 96, 82];
const sat = x => (x > 32767 ? 32767 : x < -32768 ? -32768 : x);
const predict = s => Math.floor((s[0] * s[4] + s[1] * s[5] + s[2] * s[6] + s[3] * s[7]) / 8192);
function update(s, pcm, residual) {
  const delta = residual >> 4;
  for (let i = 0; i < 4; i++) s[4 + i] = sat(s[4 + i] + (s[i] < 0 ? -delta : delta));
  s[0] = s[1]; s[1] = s[2]; s[2] = s[3]; s[3] = pcm;
}
function penalty(s) {
  const v = Math.floor((s[4] * s[4] + s[5] * s[5] + s[6] * s[6] + s[7] * s[7]) / 262144) - 0x8ff;
  return v > 0 ? v * v : 0;
}
function divide(v, reciprocal) {
  const n = Math.floor((v * reciprocal + 32768) / 65536);
  return n + Math.sign(v) - Math.sign(n);
}

// Int16Array of mono samples -> Uint8Array of SEA5 chunks
export function seaEncode(input) {
  const bits = 5, limit = 1 << bits, frames = input.length;
  const blocks = Math.ceil(frames / SEA_FRAMES), data = new Uint8Array(blocks * SEA_CHUNK);
  const quant = new Uint8Array(65);
  let x = limit - 1;
  quant[0] = x;
  for (let i = 1; i < limit; i += 2) { quant[i] = quant[i + 1] = x; x -= 2; }
  x = 0;
  for (let i = limit; i < 2 * limit; i += 2) { quant[i] = quant[i + 1] = x; x += 2; }
  quant[2 * limit] = x - 2;
  let state = [0, 0, 0, 0, 0, 0, -8192, 16384], previous = 0;
  const codes = new Uint8Array(SEA_GROUP);
  for (let block = 0; block < blocks; block++) {
    const c = block * SEA_CHUNK;
    data[c] = 1; data[c + 1] = 0x40 | bits; data[c + 2] = 20; data[c + 3] = 0x5a;
    for (let i = 0; i < 8; i++) { data[c + 4 + 2 * i] = state[i] & 0xff; data[c + 5 + 2 * i] = (state[i] >> 8) & 0xff; }
    for (let group = 0; group < SEA_FRAMES / SEA_GROUP; group++) {
      let bestRank = Infinity, bestState = null, bestSf = 0;
      const bestCodes = new Uint8Array(SEA_GROUP);
      for (let search = 0; search < 16; search++) {
        const sf = (search + previous) & 15, trial = state.slice(), reciprocal = RECIPROCAL[sf];
        let rank = 0, i;
        for (i = 0; i < SEA_GROUP; i++) {
          const position = block * SEA_FRAMES + group * SEA_GROUP + i;
          const pcm = position < frames ? input[position] : 0;
          const prediction = predict(trial);
          let scaled = divide(pcm - prediction, reciprocal);
          if (scaled > limit) scaled = limit;
          if (scaled < -limit) scaled = -limit;
          const q = quant[limit + scaled], residual = DQT[(sf << bits) + q];
          const reconstructed = sat(prediction + residual), error = pcm - reconstructed;
          rank += error * error + penalty(trial);
          if (rank > bestRank) break;
          codes[i] = q;
          update(trial, reconstructed, residual);
        }
        if (i === SEA_GROUP && rank < bestRank) { bestRank = rank; bestSf = sf; bestState = trial; bestCodes.set(codes); }
      }
      state = bestState; previous = bestSf;
      data[c + 20 + (group >> 1)] |= bestSf << ((group & 1) ? 0 : 4);
      for (let i = 0; i < SEA_GROUP; i++) {
        const at = (group * SEA_GROUP + i) * bits, packed = bestCodes[i] << (11 - (at & 7));
        data[c + 28 + (at >> 3)] |= (packed >> 8) & 0xff;
        if ((at & 7) > 3) data[c + 29 + (at >> 3)] |= packed & 0xff;
      }
    }
  }
  return data;
}
