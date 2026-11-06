#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Your own breaks on an FM-1 running AMENITY, over USB-MIDI: what web/breaks/ does, from the command line.

  python tools/jungle/fm1_breaks.py list
  python tools/jungle/fm1_breaks.py send BREAK.wav [--slot N] [--name NAME]
  python tools/jungle/fm1_breaks.py delete N

A break is a WAV of exactly two bars (any rate, mono or stereo). It is made as the built-in ones are
(tools/jungle/make_amen.py: 32 kHz mono, as loud as the Amen, peaks limited; a break over 3 s at a lower rate so
that it fits its slot), encoded to SEA5 by tools/jungle/make_bank (build it with make_breaks.sh, or on Windows
build/jungle/make_bank.exe), its one-shots picked by tools/jungle/breaks_header.py. Stop AMENITY first.

The protocol (app/user_breaks.c): frames in the update's SysEx framing, the FM-1 reading the data with read
requests. Needs mido with python-rtmidi.
"""
import argparse
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

import mido
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'jungle'))
import breaks_header  # noqa: E402  (its pick and features)
import make_amen      # noqa: E402  (its loudness and limiter)

CMD_LIST, CMD_WRITE, CMD_DELETE = 0x60, 0x61, 0x62
STATUS = {0: 'done', 1: 'bad request', 2: 'busy: stop AMENITY (and the external clock) first',
          3: 'the transfer', 4: 'the flash', 5: 'the check after writing'}
ROOM_FRAMES = (17 * 4096 - 256) // 228 * 320    # the most frames a slot holds


def pack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return out


def unpack7(s):
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def frame(cmd, body):
    n = len(body)
    return pack7(bytes([0x00, 0x59, cmd, n & 0xFF, n >> 8 & 0xFF, 0]) + body + bytes([~sum(body) & 0xFF]))


def response(addr, data, fl):
    n = len(data)
    msg = bytearray([0x00, 0x59, 0x30, (n + 8) & 0xFF, (n + 8) >> 8 & 0xFF, 0, fl])
    msg += addr.to_bytes(4, 'little') + bytes([n & 0xFF, n >> 8 & 0xFF, 0]) + data
    msg.append(~sum(msg[6:]) & 0xFF)
    return pack7(msg)


class Link:
    def __init__(self):
        ins = [p for p in mido.get_input_names() if 'AMENITY' in p.upper()]
        outs = [p for p in mido.get_output_names() if 'AMENITY' in p.upper()]
        if not ins or not outs:
            sys.exit('no FM-1 running AMENITY on USB (close other MIDI apps and the breaks page first)')
        self.inp, self.out = mido.open_input(ins[0]), mido.open_output(outs[0])

    def send(self, data7):
        self.out.send(mido.Message('sysex', data=data7))

    def read(self, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for m in self.inp.iter_pending():
                if m.type == 'sysex':
                    u = unpack7(m.data)
                    if len(u) >= 7 and u[:2] == b'\x00\x59':
                        return u
            time.sleep(0.001)
        return None

    def command(self, cmd, body=b'', data=b'', timeout=5.0, progress=None):
        """send a command, answer the FM-1's read requests from data, -> the body of its answer"""
        self.send(frame(cmd, body))
        last = time.monotonic()
        while time.monotonic() - last < timeout:
            u = self.read(0.2)
            if u is None:
                continue
            last = time.monotonic()
            if u[2] == 0x30 and len(u) == 15:      # a read request: flags, addr, len
                fl, addr, n = u[6], int.from_bytes(u[7:11], 'little'), int.from_bytes(u[11:14], 'little')
                self.send(response(addr, data[addr:addr + n].ljust(n, b'\0'), fl))
                if progress:
                    progress(min(addr + n, len(data)), len(data))
            elif u[2] == cmd:
                n = u[3] | u[4] << 8
                return u[6:6 + n]
        sys.exit('the FM-1 did not answer (is it running AMENITY v2 with own breaks?)')


def slots(link):
    b = link.command(CMD_LIST)
    count, busy, out = b[1], b[6], []
    for s in range(count):
        e = b[7 + 21 * s:7 + 21 * (s + 1)]
        name = e[1:13].split(b'\0')[0].decode('ascii', 'replace')
        frames, rate = struct.unpack_from('<II', e, 13)
        out.append((bool(e[0]), name, frames, rate))
    return out, busy


def prepare(path):
    """WAV -> (sea bytes, frames, rate, shots)"""
    from scipy.io import wavfile
    rate, data = wavfile.read(path)
    duration = len(data) / rate
    out_rate = min(32000, int(ROOM_FRAMES / duration))
    with tempfile.TemporaryDirectory() as tmp:
        raw, bank = Path(tmp) / 'b.raw', Path(tmp) / 'b.bin'
        subprocess.run([sys.executable, '-I', str(ROOT / 'tools/jungle/make_amen.py'), str(path), str(raw),
                        str(out_rate)], check=True, stdout=subprocess.DEVNULL)
        exe = ROOT / 'build/jungle' / ('make_bank.exe' if sys.platform == 'win32' else 'make_bank')
        subprocess.run([str(exe), str(raw), str(bank), 'MY BREAK'], check=True, stdout=subprocess.DEVNULL)
        b = bank.read_bytes()
        off, frames, size = struct.unpack_from('<III', b, 32)
        old = breaks_header.RATE
        breaks_header.RATE = out_rate
        shots = breaks_header.pick(breaks_header.features(raw))
        breaks_header.RATE = old
        return b[off:off + size], frames, out_rate, shots


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='what', required=True)
    sub.add_parser('list')
    s = sub.add_parser('send')
    s.add_argument('wav')
    s.add_argument('--slot', type=int)
    s.add_argument('--name')
    d = sub.add_parser('delete')
    d.add_argument('slot', type=int)
    a = ap.parse_args()
    link = Link()
    have, busy = slots(link)
    if a.what == 'list':
        for i, (used, name, frames, rate) in enumerate(have):
            print(f'slot {i + 1}: ' + (f'{name:11s} {frames / rate:.2f} s, {rate} Hz, {480 * rate / frames:.1f} BPM'
                                       if used and rate else 'empty'))
        if busy:
            print('(playing: stop it before sending or deleting)')
        return
    if a.what == 'delete':
        rc = link.command(CMD_DELETE, bytes([a.slot - 1]))[0]
        print(f'slot {a.slot}: {STATUS.get(rc, rc)}')
        sys.exit(1 if rc else 0)
    slot = a.slot - 1 if a.slot else next((i for i, h in enumerate(have) if not h[0]), None)
    if slot is None:
        sys.exit('every slot holds a break: delete one, or give --slot')
    name = (a.name or Path(a.wav).stem).upper()[:11]
    sea, frames, rate, shots = prepare(a.wav)
    print(f'{name}: {frames} frames at {rate} Hz ({frames / rate:.2f} s, {480 * rate / frames:.1f} BPM), '
          f'{len(sea)} bytes, one-shots {shots}')
    body = bytes([slot]) + struct.pack('<III', len(sea), zlib.crc32(sea), frames) + \
        name.encode('ascii', 'replace').ljust(12, b'\0') + bytes(shots) + struct.pack('<I', rate)
    rc = link.command(CMD_WRITE, body, sea, timeout=10.0,
                      progress=lambda at, n: print(f'\rsending {at * 100 // n:3d}%', end='', flush=True))
    print(f'\rslot {slot + 1}: {STATUS.get(rc[0], rc[0])}')
    sys.exit(1 if rc[0] else 0)


if __name__ == '__main__':
    main()
