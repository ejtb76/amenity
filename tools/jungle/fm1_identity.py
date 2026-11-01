# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Ask a connected FM-1 for its firmware identity, as the update page and The
Firmware Hub do (command 17 of the FM1 Open update protocol, sdk/WebUpdate.js),
and list the MIDI port names the computer sees.

Usage: python tools/jungle/fm1_identity.py [port name part]
Close other MIDI apps and installer tabs first: on Windows a MIDI port can be
open in one program at a time. Needs python-rtmidi.
"""
import sys
import time

import rtmidi


def pack7(data):
    out, bits, value = [], 0, 0
    for byte in data:
        value |= byte << bits
        bits += 8
        while bits >= 7:
            out.append(value & 127)
            value >>= 7
            bits -= 7
    if bits:
        out.append(value & 127)
    return out


def frame(command, payload=b""):
    total = sum(payload) & 255
    raw = [0, 89, command, len(payload) & 255, len(payload) >> 8, 0, *payload, ~total & 255]
    return [240, *pack7(raw), 247]


def parse(message):
    if len(message) < 10 or message[0] != 240 or message[-1] != 247:
        return None
    raw, bits, value = [], 0, 0
    for byte in message[1:-1]:
        if byte > 127:
            return None
        value |= byte << bits
        bits += 7
        if bits >= 8:
            raw.append(value & 255)
            value >>= 8
            bits -= 8
    if len(raw) < 7 or raw[0] != 0 or raw[1] != 89:
        return None
    if sum(raw[6:]) & 255 != 255:
        return None
    return raw[2], bytes(raw[6:-1])


def main():
    want = sys.argv[1].lower() if len(sys.argv) > 1 else ""
    midi_in, midi_out = rtmidi.MidiIn(), rtmidi.MidiOut()
    ins, outs = midi_in.get_ports(), midi_out.get_ports()
    print("MIDI in: ", ins or "(none)")
    print("MIDI out:", outs or "(none)")
    pairs = [(i, o) for i, a in enumerate(ins) for o, b in enumerate(outs)
             if want in a.lower() and want in b.lower() and a.split()[0] == b.split()[0]]
    if not pairs:
        print("No matching MIDI in/out pair; give part of the FM-1's port name.")
        return 1
    for i, o in pairs:
        midi_in.open_port(i)
        midi_in.ignore_types(sysex=False)
        midi_out.open_port(o)
        midi_out.send_message(frame(17))
        reply, until = None, time.time() + 1.5
        while time.time() < until and reply is None:
            message = midi_in.get_message()
            if message:
                parsed = parse(message[0])
                if parsed and parsed[0] == 17:
                    reply = parsed[1]
            else:
                time.sleep(0.01)
        midi_in.close_port()
        midi_out.close_port()
        name = ins[i]
        if reply is None:
            print(f"{name}: no identity reply")
        else:
            print(f"{name}: identity {reply.decode('ascii', 'replace').rstrip(chr(0)).strip()!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
