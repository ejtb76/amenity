# Amenity 2.1.1

Amenity 2.1.1 follows a MIDI clock over Bluetooth more reliably: it no longer
stops when the clock drops out for a moment, and it picks the clock up again in
time.

**Install or update:** https://ejtb76.github.io/amenity/ (Chrome or Edge, FM-1 on USB). Your own breaks and your settings stay.

## Fixed

- **Following a clock over Bluetooth no longer drops out.** Each Bluetooth
  MIDI message carries the time it was sent, and one message with a wrong time
  (seen: 1.5 seconds off) could make Amenity think the clock had stopped, so
  the break went silent while the other side played on. Amenity now spots a
  wrong time and uses the moment the message arrived instead.
- **A Stop from the device that leads always stops Amenity**, also when its
  clock went quiet just before.
- **After a short gap in the clock, the break is back in time at once.**
  Before, when a few pulses went missing (under half a second), the break
  waited about as long as the gap and then ran behind the sender.

## Changed

- **When the clock stops without a Stop, the break plays on.** A Bluetooth
  dropout or a computer that hangs no longer stops it: after half a second
  **EXT** turns orange, the screen says **NO CLOCK: ON** and the break carries
  on at the tempo it measured. When the clock comes back, Amenity picks it up
  where the sender is now, without a new Start. A Stop from the sender, a new
  Start or **PLAY** stops it. (In 2.1 the break stopped after half a second
  without clock and stayed silent until the next Start.)
  While Amenity plays on alone it sends no clock to the devices behind it
  (MIDI THRU): they wait for the clock to come back.

## Updating from 2.1

- Install over 2.1 (or 2.0) from the page above; **your own breaks, their
  order and your settings stay**.
- **Going back to stock (or another firmware) still erases your own breaks**:
  keep your WAV files. Your settings stay and come back when you install
  Amenity again.
- Amenity 2.1.1's identity is `FM-1_817020101` (pattern `FM-1_817XXYYZZ`).

Works on both FM-1 flash chips (Puya P25Q80 and UC25HQ80), as before.

## Third-party audio

The built-in breaks are third-party recordings and are **not** covered by
Amenity's licence; all rights belong to their owners:
"Amen, Brother", The Winstons (1969); "Apache", Incredible Bongo Band (1973);
"Think (About It)", Lyn Collins (1972). See `assets/third-party/`.

## Files

- `AMENITY-V21.fwsc`: the firmware, for The Firmware Hub and other installers
  (the same file as `amenity-2.1.1.fwsc`).
- `firmware.json`: the firmware described for hubs.
