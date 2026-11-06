// SPDX-License-Identifier: GPL-3.0-only
// AMENITY's breaks over Web MIDI (Chrome, Edge): the commands of app/user_breaks.c in the update's SysEx framing,
// F0 pack7(00 59 cmd len24 body ~sum) F7; for WRITE the FM-1 reads the break from us with read requests (cmd 0x30),
// 512 bytes at a time, as an update does. tools/jungle/fm1_breaks.py is the same from the command line.

const CMD_LIST = 0x60, CMD_WRITE = 0x61, CMD_DELETE = 0x62, CMD_ORDER = 0x63, READ = 0x30;
export const STATUS = ['done', 'bad request', 'AMENITY is playing or following a clock: stop it first',
  'the transfer stopped', 'writing the flash failed', 'the check after writing failed'];

function pack7(bytes) {
  const out = [];
  let acc = 0, bits = 0;
  for (const b of bytes) {
    acc |= b << bits;
    bits += 8;
    while (bits >= 7) { out.push(acc & 0x7f); acc >>= 7; bits -= 7; }
  }
  if (bits) out.push(acc & 0x7f);
  return out;
}
function unpack7(s) {
  const out = [];
  let acc = 0, bits = 0;
  for (const b of s) {
    acc |= (b & 0x7f) << bits;
    bits += 7;
    while (bits >= 8) { out.push(acc & 0xff); acc >>= 8; bits -= 8; }
  }
  return Uint8Array.from(out);
}
const sum = a => a.reduce((s, b) => s + b, 0);
function frame(cmd, body) {
  const n = body.length;
  return Uint8Array.from([0xf0, ...pack7([0x00, 0x59, cmd, n & 0xff, (n >> 8) & 0xff, 0, ...body, ~sum(body) & 0xff]), 0xf7]);
}
function response(addr, data, flags) {
  const n = data.length, msg = [0x00, 0x59, READ, (n + 8) & 0xff, ((n + 8) >> 8) & 0xff, 0, flags,
    addr & 0xff, (addr >> 8) & 0xff, (addr >> 16) & 0xff, (addr >>> 24) & 0xff, n & 0xff, (n >> 8) & 0xff, 0, ...data];
  msg.push(~sum(msg.slice(6)) & 0xff);
  return Uint8Array.from([0xf0, ...pack7(msg), 0xf7]);
}
const le32 = (v) => [v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >>> 24) & 0xff];

export class Amenity {
  // -> an Amenity on the first MIDI port pair named AMENITY, or throws
  static async connect() {
    if (!navigator.requestMIDIAccess) throw new Error('this browser has no Web MIDI: use Chrome or Edge');
    const access = await navigator.requestMIDIAccess({ sysex: true });
    const input = [...access.inputs.values()].find(p => /amenity/i.test(p.name));
    const output = [...access.outputs.values()].find(p => /amenity/i.test(p.name));
    if (!input || !output) throw new Error('no FM-1 running AMENITY on USB (connect it, close other MIDI apps)');
    const a = new Amenity(input, output);
    await a.list();                                   // it answers: AMENITY with own breaks
    return a;
  }
  constructor(input, output) {
    this.input = input;
    this.output = output;
    this.waiting = null;
    input.onmidimessage = e => this.receive(e.data);
  }
  receive(data) {
    if (data[0] !== 0xf0 || data[data.length - 1] !== 0xf7 || !this.waiting) return;
    const u = unpack7(data.subarray(1, -1));
    if (u.length < 7 || u[0] !== 0x00 || u[1] !== 0x59) return;
    this.waiting(u);
  }
  // send cmd, answer the FM-1's read requests from data, -> the body of its answer
  command(cmd, body = [], data = null, progress = null, timeout = 5000) {
    return new Promise((resolve, reject) => {
      let timer;
      const arm = () => { clearTimeout(timer); timer = setTimeout(() => { this.waiting = null; reject(new Error(
        'the FM-1 did not answer: is it running AMENITY 2 or later, and nothing else (another tab) using it?')); }, timeout); };
      this.waiting = u => {
        arm();
        if (u[2] === READ && u.length === 15 && data) {
          const flags = u[6], addr = u[7] | u[8] << 8 | u[9] << 16 | u[10] << 24, n = u[11] | u[12] << 8 | u[13] << 16;
          const chunk = new Uint8Array(n);
          chunk.set(data.subarray(addr, addr + n));
          this.output.send(response(addr, chunk, flags));
          if (progress) progress(Math.min(addr + n, data.length) / data.length);
        } else if (u[2] === cmd) {
          clearTimeout(timer);
          this.waiting = null;
          resolve(u.subarray(6, 6 + (u[3] | u[4] << 8)));
        }
      };
      arm();
      this.output.send(frame(cmd, body));
    });
  }
  // -> {busy, slots: [{used, name, frames, rate}], order: the slots in the order PRESET plays them}
  async list() {
    const b = await this.command(CMD_LIST), slots = [];
    const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
    for (let s = 0; s < b[1]; s++) {
      const at = 7 + 21 * s, name = String.fromCharCode(...b.subarray(at + 1, at + 13)).replace(/\0.*$/, '');
      slots.push({ used: !!b[at], name, frames: v.getUint32(at + 13, true), rate: v.getUint32(at + 17, true) });
    }
    const at = 7 + 21 * b[1], order = b.length >= at + b[1] ? [...b.subarray(at, at + b[1])] : slots.map((_, i) => i);
    return { busy: !!b[6], slots, order };
  }
  async order(order) { return (await this.command(CMD_ORDER, order))[0]; }
  // sea: Uint8Array of SEA5 data; -> status (0 done)
  async write(slot, name, sea, frames, rate, shots, crc, progress) {
    const n = [...name.padEnd(12, '\0')].slice(0, 12).map(c => c.charCodeAt(0) & 0x7f);
    const body = [slot, ...le32(sea.length), ...le32(crc), ...le32(frames), ...n, ...shots, ...le32(rate)];
    return (await this.command(CMD_WRITE, body, sea, progress, 10000))[0];
  }
  async remove(slot) { return (await this.command(CMD_DELETE, [slot]))[0]; }
  close() { this.input.onmidimessage = null; }
}

// CRC-32, as zlib's (the FM-1 checks what it wrote against it)
export function crc32(bytes) {
  let c = ~0;
  for (const b of bytes) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}
