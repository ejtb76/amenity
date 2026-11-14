# Amenity 2.1

Amenity 2.1 remembers your settings, plays in time with a second FM-1 over
Bluetooth, passes MIDI on like a classic MIDI THRU, and gets half time /
double time, a quieter screen if you want one, and its own boot screen.

**Install or update:** https://ejtb76.github.io/amenity/ (Chrome or Edge, FM-1 on USB). Your own breaks stay.

## New

- **Your settings are kept** over a power-off: the break, the tempo, every
  knob (EQ and FX pages too), the transpose, the feedback root, SEQ and the
  flashes setting. They are saved when you stop the sequencer, or two seconds
  after a change while it stands still, never while it plays. PLAY, REC and
  half / double time start fresh.
- **The connections page**: tap **EDIT** (hold it, as before, for the CPU
  load). SELECT moves the cursor, SAVE does what the bottom line says:
  - **BT**: the Bluetooth MIDI devices around. SAVE=CONNECT connects to one
    (another FM-1, a keyboard), SAVE=LET GO lets it go. When the other side
    connected to Amenity, the row says CONNECTED.
  - **USB**: DEVICE (plugged into a computer, as always) or HOST: Amenity
    runs a USB-MIDI device plugged into it (HOST TO its name), with no
    computer. The FM-1 gives no power to it: use a powered USB hub. After a
    power-on it is a DEVICE again. (Not yet tested on a real FM-1: tell us how
    it goes.)
  - **UI**: FLASHES ON / OFF (see below).
- **Two FM-1s in time over Bluetooth.** Connect from either one; one
  connection carries clock, Start and Stop both ways, so whichever you press
  PLAY on leads. Works with any fm1-base firmware on the other FM-1.
- **MIDI THRU (daisy chain).** Everything that comes in (notes, CC, clock,
  Start and Stop; not SysEx) goes straight on to the other outs, never back
  where it came from: the TRS jack to USB and Bluetooth, USB to Bluetooth,
  Bluetooth to USB. So your DAW, or a drum machine on the TRS jack, can lead
  Amenity and a second FM-1 over Bluetooth. While Amenity plays on its own
  clock, incoming clock is not passed on (no double clock downstream).
  (Tested from USB to Bluetooth; from the TRS jack not yet on a real FM-1.)
- **Half time / double time on OCT− / OCT+.** The break runs at half or twice
  the tempo from the next bar, its first step on a 1 (1/2X or 2X beside the
  tempo, amber until it comes in). The tempo, the MIDI clock out and an
  external clock stay as they are, so it works while following a clock too.
  SEL + OCT− / OCT+ still nudge the tempo by 0.1 BPM.
- **Flashes off**: some players find the screen's glow, pops, shakes and
  ripples too much. UI on the connections page turns them off: the grid
  stays still and what happens is only written. On by default.
- **HOME also turns the filter off**, beside variation and warp.
- **A boot screen of its own**: the Amenity logo with the version under it.

## Better

- **MIDI clock**: Amenity times the sender's clock before Start (most DAWs run
  it while stopped), so it starts at the right tempo at once, and settles on
  it sooner.
- The hints on the screen read SAVE=CONNECT, SELECT=MOVE, EDIT=BACK and so on.

## Crashes

Some players saw Amenity restart after a crash while it took MIDI (a clock,
Bluetooth). 2.1 keeps a record of every crash, across power-offs, so it can
be found: if it happens to you, see "When it crashes" in the README and send
what `fm1_remote.py log` prints. After such a restart Bluetooth may not
connect again until the FM-1 is switched off and on.

## Updating from 2.0

- Install over 2.0 from the page above; **your own breaks and their order
  stay**. The first time, the settings start as before (160 BPM, the Amen);
  from then on they are kept.
- **OCT− / OCT+ changed**: in 2.0 they halved or doubled the tempo; now they
  switch to half time / double time and leave the tempo alone.
- **EDIT changed**: a tap opens the connections page; hold it for the CPU load.
- **Going back to stock (or another firmware) still erases your own breaks**:
  keep your WAV files. Your settings stay and come back when you install
  Amenity again (a break that is gone falls back to the Amen).
- Amenity 2.1's identity is `FM-1_817020100` (pattern `FM-1_817XXYYZZ`).

Works on both FM-1 flash chips (Puya P25Q80 and UC25HQ80), as 2.0.

## Third-party audio

The built-in breaks are third-party recordings and are **not** covered by
Amenity's licence; all rights belong to their owners:
"Amen, Brother", The Winstons (1969); "Apache", Incredible Bongo Band (1973);
"Think (About It)", Lyn Collins (1972). See `assets/third-party/`.

## Files

- `AMENITY-V21.fwsc`: the firmware, for The Firmware Hub and other installers
  (the same file as `amenity-2.1.0.fwsc`).
- `firmware.json`: the firmware described for hubs.
