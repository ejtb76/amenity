# Amenity

A live jungle firmware for the M-VAVE FM-1, by Presethead.

Amenity plays the Amen break, sliced into sixteen 8ths, and lets you tear it
up live from the panel. Hold a white key for a roll, a build, an echo or an
old-sampler stretch; play single hits on the black keys; let the pattern
vary itself and throw in a fill at the end of every phrase. One knob each for
a ringing feedback delay, variation, warp and a DJ filter, with lo-fi, EQ,
saturation and compression a button away.

- **Install:** [ejtb76.github.io/amenity](https://ejtb76.github.io/amenity/) (Chrome or Edge, FM-1 on USB); see [Installing](#installing)
- **Identity:** `FM-1_8170001`
- **Licence:** GPL-3.0 with the FM1 SDK linking exception (see [LICENSE.md](LICENSE.md)
  and [THIRD_PARTY.md](THIRD_PARTY.md)). The Amen break sample is **not**
  covered by it: see [Third-party audio](#third-party-audio).
- **Built on:** M-VAVE's FM1 Open V14 SDK. The M-VAVE update loader is included,
  so you can always go back to V15 or move to another firmware.

## Controls

| Control | What it does |
|---|---|
| **PLAY** | Start / stop. Lit green while the pattern plays, red while a white-key effect sounds. |
| **White keys** (hold) | Effects on the break, in four groups of four. The pattern keeps running underneath; a new effect starts on the next 16th, and letting go carries on to the pattern's next hit, so there is never a gap. |
| | Keys 1–4: **1/4 repeat**, **build** (1/4 → 1/8 → 1/16 → 1/32, a beat each), **STRETCH** (every hit of the pattern stretched 4× slower at its own pitch: the metallic sound of an old sampler's time-stretch), **echo** (dotted 1/8, softer and darker each time). |
| | Keys 5–8, 9–12, 13–16: **1/8**, **1/16**, **1/32** rolls: **repeat**, **down** and **up** (a whole tone per repeat), **shrink** (shorter and softer). |
| **Black keys** 1–8 | Single hits from the break: kick, snare, kick, snare, snare, snare, hat, ride (on the next 32nd). Each plays only its own 8th and stops at the end of it. |
| | **Hold a white key and turn SELECT** to tune it: on **1/4, 1/8, 1/16, 1/32 repeat/down/up/shrink** it scans through the 16 chops (slices); on **BUILD** it picks one of 16 stereo stutters (build, fast build, decelerating, steady 1/16, each centred, ping-pong, widening or random in the stereo field), which stays selected; on **STRETCH** it tunes the stretched hits up or down 24 semitones (stays); on **ECHO** it sets the echo's feedback, 75% to 200% (stays). Above 100% every repeat is louder than the last: the echo runs away and self-oscillates until you let go of the key (the output clips). |
| **F#5** (the rightmost black key) | Momentary **mute** of the break, ahead of the whole effects chain (delay and reverb tails ring on). |
| **K1 FEEDBACK** | A very short delay with feedback (0–90%) that rings metallic at a root note. |
| **FX** (hold) + any key | Sets that root note, C to B (lit keys show the current one). |
| **K2 VARIATION** | How much the pattern varies each phrase: 16th fills and swapped slices. |
| **K3 WARP** | How many hits the pattern warps: two in three STRETCH (purple on the screen), one in three half speed, an octave down (blue). |
| **K4 FILTER** | DJ filter: left low-pass, middle off, right high-pass. It rings a little while you turn it. |
| **SELECT** | LO-FI: sample rate (44.1 kHz down to 6 kHz) and bits (16 down to 6) together, like an early sampler. |
| **PRESETS** | Tempo: 1 BPM a click, 2 or 5 BPM a click when turned fast (hold SEL for 0.1 BPM steps). **OCT− / OCT+** halve / double it. |
| **ALGORITHM** | Transpose the whole break, -24 to +24 semitones. |
| **EDIT** | Chop editor: **SELECT** moves a green cursor over the 16 hits (8ths) on screen, **ALGORITHM** changes which chop (slice) plays there. **HOME** puts them back in order. Variation and warp work on top of your order. Edits live in RAM only: they are gone after a restart or a change of break or sample. EDIT again goes back. |
| **LFO** | LFO page: **K1** wave (sine, triangle, saw up, saw down, square, random), **K2** depth, **K3** time (32nd up to 8 bars, locked to the break), **K4** target: filter, delay send, reverb send, transpose, variation, warp, level (before the effects). LFO again goes back. |
| **SEQ** | Fill mode: every phrase ends with one random white-key effect, from 8, 4 or 2 steps before the end. |
| **REC** | Freeze: keep the current pattern instead of a new one each phrase. |
| **HOME** | Back to the straight break (variation and warp to zero, main page). |
| **GLO** | Break menu: every loop in the sample bank, then the built-in one. **SELECT** moves through the list and each break plays as you reach it, at its own tempo (PRESETS still changes it). GLO again, or HOME, goes back. SELECT is LO-FI on every other page. |
| **ENV** | EQ page: K1–K4 low (100 Hz shelf), low-mid (500 Hz), high-mid (2.5 kHz), high (8 kHz shelf), ±12 dB. ENV again to go back. |
| **FX** (tap) | FX page: K1 saturation, K2 compression, **K3 delay send**, **K4 reverb send** (both after the compressor). FX again: **delay page**, K1 time (SELECT switches between tempo divisions 1/32 to 1/2 and milliseconds), K2 feedback, K3 high-pass on the repeats, K4 on/off (what is in the delay rings out). FX again: **reverb page**, K1 type (ambience, room, hall, plate, cathedral), K2 size, K3 damping, K4 on/off (the tail rings out). FX again to go back. |
| **MASTER** | Volume. |

The sound chain: break (with stretch) → mute → LFO level → lo-fi → feedback → EQ →
saturation → compression → DJ filter, with the delay and reverb sends taken after
the compressor and returned after the filter, in stereo. The delay and reverb run at
half the sample rate (22.05 kHz), so their repeats and tails are a little dark. Each effect is level-matched, so turning it up
changes the character, not the loudness.

### MIDI clock

Amenity follows MIDI clock from any MIDI input. A **Start** from your
DAW or drum machine starts the break at step 1, **Continue** carries on where
it stopped, and **Stop** (or a second without clock) stops it. While it
follows, the screen shows **EXT**, the tempo shown is the one it measures,
and PLAY and the tempo controls rest.

Playing on its own clock, Amenity sends Start, clock (24 per beat) and Stop
to every MIDI output, so other gear can follow it.

## Installing

1. Open the installer, **https://ejtb76.github.io/amenity/**, in Chrome or Edge
   and connect the FM-1 by USB, directly (no hub). Use a USB cable that carries
   data. (The same page is attached to each release as
   **AMENITY-…-installer.html**, to open from your own computer.)
2. **Connect device**, tick the backup confirmation, **Start update**. From
   another firmware, the page may ask for its forced-flash route; that is
   M-VAVE's own migration path.
3. **The break works straight away.** The firmware carries a built-in copy
   (16 kHz). For the full-quality break (32 kHz), the page also writes it to
   the FM-1's sample bank when the FM-1 has no valid bank. If the FM-1 already
   has an Open sample bank (from FM1 Open or another firmware), the page
   leaves it alone and Amenity plays the built-in copy; for the full one, in
   the page's *8-slot sample bank*, click **Load built-in sample bank**, then
   **Write device sample bank**. Writing replaces all 8 sample slots, so back
   up samples you want to keep first. Installed from the `.fwsc` alone (for
   example through The Firmware Hub), Amenity plays the built-in copy. The
   screen shows **16K** next to AMENITY while it does.
4. To leave Amenity, use the same page: **Restore original V15**, or install
   any other firmware.

Projects and samples are not interchangeable between firmwares: back up first.

## Loading several breaks

The sample bank has 8 slots, and Amenity lists the loops among them in the
**GLO** menu. In the installer page's *8-slot sample bank*, import a WAV into
a slot, give it a name (up to 15 characters), repeat for the other breaks, then
**Write device sample bank** (this replaces all 8 slots).

- **Each break is a two-bar loop**, trimmed to the bar lines: Amenity cuts it
  into sixteen 8ths. The length sets the tempo it plays at its own speed
  (3 s is 160 BPM, 4 s is 120 BPM), which is what picking it from the menu
  sets, unless a MIDI clock leads.
- **The bank holds about 20 s in all** (it is shared by the slots), so around
  six breaks of 3 s, fewer if they are slower. The installer page shows what
  is left for a slot.
- A slot is listed as a break when it runs at least 1.5 s or its name starts
  with `AMEN`. Shorter slots are taken for one-shots and left out.
- At power-up Amenity starts on the first slot named `AMEN…`, else on the
  built-in break. The break picked in the menu is not stored.

## Third-party audio

Amenity is built around the Amen break, a drum break from "Amen, Brother" by
The Winstons (1969). The sample is third-party audio: it is **not** covered by
this project's licence, and all rights belong to the original rights holders.
It is included for convenience only; see
[assets/third-party/amen/README.md](assets/third-party/amen/README.md) for its
source and the processing applied.

Amenity starts on the sample bank's first slot named `AMEN…`; without one, on a
16 kHz copy of the break built into the firmware image. The installer page
carries the full 32 kHz break as its built-in sample bank. Both copies are
generated at build time from `assets/third-party/amen/amen.wav`.

### Building without the sample

Delete `assets/third-party/amen/`, or set `AMENITY_WITH_AMEN=0`, and build as
below. The firmware and installer build the same way, without a built-in
break; the installer then offers M-VAVE's synthesized demo bank, and Amenity
shows *NO BREAK* until a bank with a slot named `AMEN…` (a two-bar loop) is
written to the FM-1, for example from the workbench's WAV import.

## Building

Windows x64, with:

- **JieLi Windows toolchain 2.5.3** in `C:\JL` (`C:\JL\pi32\bin\clang.exe`;
  the build checks every toolchain file against `sdk/manifest.json`).
  [Installer](https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe).
- **Python 3** with numpy and scipy (sample conversion), and **Visual Studio**
  C tools (the sample-bank encoder and the host tests).
- The browser build downloads WASI SDK 27 on first use.

```
tools\jungle\build_release.bat https://github.com/<you>/<repo>/archive/refs/tags/<tag>.zip
```

The argument is the HTTPS URL of the matching source archive (the installer
links to it). The installer and `.fwsc` land in `build\install\`.

Host tests (render the break and every effect to WAV and measure levels,
timing and clicks; draw the screen to PNG):

```
tools\jungle\build_host.bat
tools\jungle\build_panel.bat
```

Most of Amenity lives in `app/jungle/jungle.c` (the sound engine: slicing,
sequencer, rolls, stretch and effects, integer-only, in the audio interrupt)
and `app/screen.c` (the panel and screen). The stock V14 README is kept in
[docs/FM1-OPEN-V14-README.md](docs/FM1-OPEN-V14-README.md).

## Credits

- FM-1 hardware and the FM1 Open SDK: M-VAVE.
- SEA codec for the sample bank: Dani Biró ([SEA](https://github.com/Daninet/sea-codec), MIT), via the FM1 Open SDK.
- Ported from Presethead's Jungle Choppa VST plugin.
- VESPER by Fat Cat Studios showed how to add a sound engine to FM1 Open's output stage.
- The Amen break: The Winstons, "Amen, Brother" (1969); drums by Gregory C. Coleman. See [Third-party audio](#third-party-audio).

## Licence

APP: [GPLv3 with the FM1 SDK linking exception](LICENSE.md). The device engine
is supplied as a binary SDK with separate terms. Retain the applicable
[SDK terms, third-party licenses and contributor notices](THIRD_PARTY.md) when
distributing builds. Files in `assets/third-party/` are excluded from the
licence.
