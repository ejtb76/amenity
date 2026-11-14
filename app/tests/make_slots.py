# SPDX-License-Identifier: GPL-3.0-only
"""Two own-break slot images for app/tests/breakname.txt (user_breaks.c's layout: an 'AMB1' head, the data at
+256). The data is silence-ish filler of the right size: the test checks where the settings find a break, not its sound.
usage: python -I app/tests/make_slots.py   (writes app/tests/slot-one.bin, app/tests/slot-two.bin)"""
import struct, zlib
from pathlib import Path

FRAMES = 16 * 320                       # the least a slot takes (JUNGLE_SLICES x 320)
BYTES = (FRAMES + 319) // 320 * 228
for name, fill in (("ONE", 1), ("TWO", 2)):
    data = bytes([fill]) * BYTES
    head = struct.pack("<5I", 0x31424D41, 1, FRAMES, BYTES, zlib.crc32(data)) + name.encode().ljust(12, b"\0") + \
        bytes(range(8)) + struct.pack("<I", 32000) + bytes(16)
    assert len(head) == 60             # (sizeof user_head_t; the data at +256 either way)
    Path(__file__).with_name(f"slot-{name.lower()}.bin").write_bytes(head.ljust(256, b"\xff") + data)
