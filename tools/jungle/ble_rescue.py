#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Reach an FM-1 over Bluetooth MIDI when its USB does not come up.

  scan              list Bluetooth MIDI devices nearby (sends nothing)
  rescue <address>  send the SDK's update SysEx F0 22 24 35 7D F7

The SDK handles that SysEx itself (fm1_midi_input_sysex), before any
firmware code, so a firmware whose USB is broken can still be switched to
the update path.

Usage: python -I tools/jungle/ble_rescue.py scan | rescue <address>
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

MIDI_SERVICE = '03b80e5a-ede8-4b33-a751-6ce34ec4c700'
MIDI_IO = '7772e5db-3868-4112-a1a9-f2669d106bf3'
# BLE-MIDI packet: header, timestamp, SysEx start..data, timestamp, F7.
RESCUE = bytes([0x80, 0x80, 0xF0, 0x22, 0x24, 0x35, 0x7D, 0x80, 0xF7])


async def scan():
    found = await BleakScanner.discover(timeout=10.0, return_adv=True)
    any_midi = False
    for address, (device, adv) in found.items():
        uuids = [u.lower() for u in adv.service_uuids]
        midi = MIDI_SERVICE in uuids
        any_midi |= midi
        if midi or (device.name and 'fm' in device.name.lower()):
            print(f'{address}  {device.name!r}  rssi {adv.rssi}  {"MIDI" if midi else ""}')
    if not any_midi:
        print('no Bluetooth MIDI device found (is the FM-1 on and nearby?)')


async def rescue(address):
    async with BleakClient(address, timeout=20.0) as client:
        print('connected:', client.is_connected)
        await client.write_gatt_char(MIDI_IO, RESCUE, response=False)
        print('sent F0 22 24 35 7D F7')
        await asyncio.sleep(2.0)


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'scan':
        asyncio.run(scan())
    elif len(sys.argv) >= 3 and sys.argv[1] == 'rescue':
        asyncio.run(rescue(sys.argv[2]))
    else:
        print(__doc__)
