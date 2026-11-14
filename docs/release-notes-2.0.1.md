# Amenity 2.0.1

**Install or update:** https://ejtb76.github.io/amenity/ (Chrome or Edge, FM-1 on USB). Your own breaks stay.

## Changed

- **OCT− / OCT+ are now half time / double time.** The break runs at half or
  twice the tempo from the next bar (1/2X or 2X beside the tempo, amber until
  it comes in), while the tempo, the MIDI clock out and an external clock stay
  as they are. It works while following a MIDI clock too, where OCT− / OCT+
  did nothing before. SEL + OCT− / OCT+ still nudge the tempo by 0.1 BPM.
- **HOME also turns the filter off**, beside variation and warp.

## Crashes

Some players saw Amenity restart after a crash while it took MIDI (a clock,
Bluetooth). 2.0.1 keeps a record of every crash, across power-offs, so it can
be found: if it happens to you, see "When it crashes" in the README and send
what `fm1_remote.py log` prints. After such a restart Bluetooth may not
connect again until the FM-1 is switched off and on.

Works on both FM-1 flash chips (Puya P25Q80 and UC25HQ80), as 2.0.0.
