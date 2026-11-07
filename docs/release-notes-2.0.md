# Amenity 2.0

Amenity 2 is rebuilt on [fm1-base](https://github.com/charlesvestal/fm1-base)
and felucca-bsp instead of M-VAVE's FM1 Open SDK. Same break engine, more
breaks, your own breaks, a steadier MIDI clock and a livelier screen.

**Install:** https://ejtb76.github.io/amenity/ (Chrome or Edge, FM-1 on USB).
**Your breaks:** https://ejtb76.github.io/amenity/breaks/

## New

- **Three built-in breaks**: Amen, Apache and Think. PRESETS picks the break;
  the new one takes over from the next hit.
- **Your own breaks**: four slots, filled from a web page. Drop a WAV of one,
  two, four or eight bars (of four or eight, the first two are used); the page
  guesses how many, makes it as loud as the built-in breaks and picks its hits
  for the black keys. Drag to reorder, delete from the page.
- **Instant effects** on the top three black keys while held: feedback, lo-fi,
  an octave down. SEQ's fills use them too, and one fill in four sweeps the
  filter.
- **The flip**: with VARIATION up, the break sometimes restarts on the "and"
  of beat 3.
- **Tempo on SELECT** (1 BPM a detent, SEL held for 0.1). OCT− / OCT+ halve
  or double it on the next beat, and only within 40–320 BPM.
- **MIDI clock** follows with a delay-locked loop, from USB, Bluetooth or
  TRS, and only from the device that sent Start.
- **The screen**: hits flash, fills and rolls glow and shake, flips ripple,
  stretch and octave-down steps move; the keys light with the slice that
  sounds. VARIATION and WARP show NEXT until the next pattern takes them.
- FEEDBACK goes to 100%. LO-FI moved to K3 on the FX page; EDIT held shows
  the CPU load.
- **Installing** uses fm1-base's installer: an install that stops carries on
  when you press Install again, and "Back to the stock firmware" puts M-VAVE's
  V15 or V16 back.

## Coming from Amenity 1.0

- **Nothing of 1.0 carries over.** Amenity 2 keeps no settings from 1.0, and
  does not use 1.0's sample bank: the breaks are built into the firmware now.
- The installer may ask you to go **back to the stock firmware first**, then
  install Amenity 2.
- Settings are not saved between power-ons: Amenity starts at 160 BPM on the
  Amen. Your own breaks and their order are kept.
- **Installing any other firmware, or going back to stock, erases your own
  breaks.** Keep your WAV files.
- Amenity 2's identity is `FM-1_817020000` (pattern `FM-1_817XXYYZZ`); 1.0's
  was `FM-1_8170001`.

## Third-party audio

The built-in breaks are third-party recordings and are **not** covered by
Amenity's licence; all rights belong to their owners:
"Amen, Brother", The Winstons (1969); "Apache", Incredible Bongo Band (1973);
"Think (About It)", Lyn Collins (1972). See `assets/third-party/`.

## Files

- `AMENITY-V20.fwsc`: the firmware, for The Firmware Hub and other installers
  (the same file as `amenity-2.0.fwsc`).
- `firmware.json`: the firmware described for hubs.
