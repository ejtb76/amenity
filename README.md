# Amenity

A live jungle firmware for the M-VAVE FM-1, by Presethead.

Amenity plays a break, sliced into sixteen 8ths, and lets you tear it up live
from the panel. Hold a white key for a roll, a build, an echo or an
old-sampler stretch; play single hits on the black keys and throw in feedback,
lo-fi or an octave drop on the top three; let the pattern vary and flip itself
and end every phrase with a fill. Three classic breaks are built in, and you
can add four of your own from a web page.

- **Install:** [ejtb76.github.io/amenity](https://ejtb76.github.io/amenity/) (Chrome or Edge, FM-1 on USB); see [Installing](#installing)
- **Your breaks:** [ejtb76.github.io/amenity/breaks](https://ejtb76.github.io/amenity/breaks/); see [Your own breaks](#your-own-breaks)
- **Identity:** `FM-1_817XXYYZZ` (2.0 is `FM-1_817020000`)
- **Licence:** GPL-3.0-only (see [LICENSE.md](LICENSE.md)). The break samples
  are **not** covered by it: see [Third-party audio](#third-party-audio).
- **Built on:** [fm1-base](https://github.com/charlesvestal/fm1-base) and
  felucca-bsp. Its installer can always put M-VAVE's own firmware back.
- **Both flash chips:** runs, saves and updates on the original FM-1 (Puya
  P25Q80 flash) and on the newer units (UC25HQ80 flash, sold since autumn 2026).

## Controls

| Control | What it does |
|---|---|
| **PLAY** | Start / stop. Green while the pattern plays, red while an effect sounds. |
| **White keys** (hold) | Effects on the break, in four groups of four. The pattern keeps running underneath; a new effect starts on the next 16th, and letting go carries on to the pattern's next hit, so there is never a gap. |
| | Keys 1–4: **1/4 repeat**, **build** (1/4 → 1/8 → 1/16 → 1/32, a beat each), **STRETCH** (every hit stretched 4× slower at its own pitch: the metallic sound of an old sampler's time-stretch), **echo** (dotted 1/8, softer and darker each time). |
| | Keys 5–8, 9–12, 13–16: **1/8**, **1/16**, **1/32** rolls: **repeat**, **down** and **up** (a whole tone per repeat), **shrink** (shorter and softer). |
| **Black keys** 1–8 | Single hits from the break that plays: kick, snare, kick, snare, snare, snare, hat, ride. |
| **Black keys** 9–11 (hold) | Instant effects while held: **feedback** at 75%, **lo-fi** all the way, **an octave down**. |
| **K1 FEEDBACK** | A very short delay with feedback (0–100%) that rings metallic at a root note. |
| **FX** (hold) + any key | Sets that root note, C to B (lit keys show the current one). |
| **K2 VARIATION** | How much the pattern varies each phrase: 16th fills, swapped slices, and the **flip**: the break restarting on the "and" of beat 3. |
| **K3 WARP** | How many hits the pattern warps: two in three STRETCH (purple on the screen), one in three half speed, an octave down (blue). |
| **K4 FILTER** | DJ filter: left low-pass, middle off, right high-pass. |
| **SELECT** | Tempo, 1 BPM a detent (2 when turned fast); hold **SEL** for 0.1 BPM. 40 to 320 BPM. |
| **OCT− / OCT+** | **Half time / double time**: the break runs at half or twice the tempo from the next bar, its first step on a 1 (1/2X or 2X beside the tempo, amber until then), while the tempo, the MIDI clock out and an external clock stay as they are. With **SEL** held: 0.1 BPM down / up. |
| **PRESETS** | The break: the built-in ones, then yours. A new break takes over from the next hit. |
| **ALGORITHM** | Transpose the whole break down, up to an octave. |
| **SEQ** | Fill mode: every phrase ends with a random effect from the white keys or the top three black keys, from 8, 4 or 2 steps before the end; one fill in four also sweeps the filter. |
| **REC** | Freeze: keep the current pattern instead of a new one each phrase. |
| **HOME** | Back to the straight break (variation and warp to zero, the filter off, main page). |
| **ENV** | EQ page: K1–K4 low (100 Hz shelf), low-mid (500 Hz), high-mid (2.5 kHz), high (8 kHz shelf), ±12 dB. ENV again to go back. |
| **FX** (tap) | FX page: K1 saturation, K2 compression, K3 LO-FI (sample rate and bits together, like an early sampler). FX again to go back. |
| **EDIT** (tap) | The connections page: see [Connections](#connections). |
| **EDIT** (hold) | How busy the audio is (CPU). |
| **MASTER** | Volume. |

The keys light with the slice that sounds, over a running light. VARIATION
and WARP show **NEXT** (or **FROZEN** with REC) until the next pattern takes
the new value.

The sound chain: break (with stretch) → lo-fi → feedback → EQ → saturation →
compression → DJ filter. Each effect is level-matched, so turning it up
changes the character, not the loudness.

Your settings are kept over a power-off: the break, the tempo, every knob
(EQ and FX pages too), the transpose, the feedback root, SEQ and the
flashes on or off. They are saved when you stop the sequencer, or two
seconds after a change while it stands still (never while it plays: a save
pauses the sound for a moment). PLAY, REC and half / double time start
fresh. Your own breaks and their order are kept too.

### MIDI clock

Amenity follows MIDI clock from USB, Bluetooth or the TRS input. A **Start**
from your DAW or drum machine starts the break at step 1, **Continue** carries
on where it stopped, and **Stop** stops it. It follows only the device that
sent Start. While it follows, the screen shows **EXT**, the tempo shown is the
one it measures, and PLAY and the tempo controls rest; OCT− / OCT+ still set
half time or double time against it.

If the clock stops without a Stop (a Bluetooth dropout, a computer that
hangs), the break does not stop: after half a second **EXT** turns orange and
the screen says **NO CLOCK: ON**, and the break goes on at the measured tempo.
When the clock comes back, Amenity picks it up again where the sender is now,
without a new Start. A Stop from the sender, a new Start or **PLAY** stops it.

Playing on its own clock, Amenity sends Start, clock (24 per beat) and Stop
over USB and Bluetooth, so other gear can follow it.

**MIDI THRU (daisy chain):** everything that comes in (notes, CC, clock,
Start and Stop; not SysEx) goes straight on to the other outs, never back
where it came from: the TRS jack to USB and Bluetooth, USB to Bluetooth,
Bluetooth to USB. So a drum machine on the TRS jack, or your DAW over USB,
can lead Amenity and a second FM-1 over Bluetooth at the same time. While
Amenity plays on its own clock, incoming clock pulses are not passed on (its
own go out instead), so nothing downstream gets a double clock.

### Connections

Tap **EDIT** for the connections page, a list: the Bluetooth MIDI devices
around (it looks for them while the page is up), then the USB port. **SELECT**
moves the cursor, **SAVE** does what the bottom line says (SAVE=...):

- on a Bluetooth device: **CONNECT**, or **LET GO** of the one connected;
- on USB: **HOST** or **DEVICE** (see below).
- on UI: **FLASHES OFF** for a still screen (no glow, pops, shakes or ripples when
  something starts), **FLASHES ON** again. Kept with the other settings.

The MIDI clock goes to everything connected, and a Start from any of them is
followed. Whichever one you press PLAY on leads, and the others follow.

**Two FM-1s over Bluetooth:** connect from one of them only. One connection
carries clock, Start and Stop both ways, so either FM-1 can lead. This works
with any fm1-base firmware on the other FM-1. While the FM-1 is connected to a
Bluetooth device, a computer cannot connect to it over Bluetooth (USB still
works).

**USB DEVICE or HOST:** this is only about the USB port, not Bluetooth.

- **DEVICE** (the normal setting): the FM-1 is plugged into a computer, for
  your DAW, the installer and the breaks page.
- **HOST**: the FM-1 takes the computer's place for whatever is plugged into
  it, such as a USB-MIDI keyboard, a drum machine or a second FM-1. They
  share clock and notes over the cable without a computer, which can be
  steadier than Bluetooth. The screen shows **HOST TO** and the device's name.
  The FM-1 gives no power to a USB device, so put a powered hub between them.
  A second FM-1 must run firmware that follows USB-MIDI clock.

After a power-on USB is a DEVICE again. Tap EDIT again to go back.

## Installing

1. Open **https://ejtb76.github.io/amenity/** in Chrome or Edge and connect
   the FM-1 by USB with a cable that carries data.
2. Choose **Install** and follow the page. Don't unplug the FM-1 until the
   page says Done; if the install stops, plug it back in and press Install
   again: it carries on.
3. Coming from Amenity 1.0 or another firmware built on M-VAVE's FM1 Open,
   the page may ask you to put M-VAVE's firmware back first: choose **Back to
   the stock firmware**, then **Install**.
4. To leave Amenity, use the same page: **Back to the stock firmware**, or
   install any other firmware.

The firmware is also attached to each release as `AMENITY-V….fwsc`, for
other installers such as The Firmware Hub.

## Your own breaks

Open **https://ejtb76.github.io/amenity/breaks/** in Chrome or Edge with
Amenity running on the FM-1 (stopped, and not following a clock). Drop a WAV
of one, two, four or eight bars on one of the four slots: the page guesses
how many (from its length, or a tempo in its name such as `90 SMALL18`),
makes it mono at 32 kHz (lower for a slow break, so it fits), as loud as the
built-in breaks, picks its hits for the black keys and sends it. Amenity plays
two bars: a one-bar file is played twice, of four or eight bars the first two
are used. Drag the slots to change the order PRESETS plays them in; delete
them from the page.

Installing another firmware, or going back to M-VAVE's, erases your breaks.

The same from the command line: `python tools/jungle/fm1_breaks.py list`,
`send BREAK.wav [--slot N] [--name NAME] [--bars 1|2|4|8]`, `delete N`.

## Third-party audio

The built-in breaks are drum breaks from records:

- **AMEN**: "Amen, Brother", The Winstons (1969); drums by Gregory C. Coleman.
- **APACHE**: "Apache", Incredible Bongo Band (1973).
- **THINK**: "Think (About It)", Lyn Collins (1972).

They are third-party audio: they are **not** covered by this project's
licence, and all rights belong to the original rights holders. They are
included for convenience only. Each folder in
[assets/third-party/](assets/third-party/) has a README with its source and
the processing applied. The firmware carries copies generated from them at
build time; those copies never go into git.

### Building without the samples

Delete any of the folders in `assets/third-party/` and build as below: the
missing breaks are left out (`tools/jungle/breaks.txt` lists them). With none,
Amenity has no built-in break and plays only breaks you send it.

### When it crashes

Amenity keeps a record of its crashes, also across power-offs. To send it
in, connect the FM-1 by USB and run, in a copy of this repository with its
submodules (`git clone --recursive`, Python with mido and python-rtmidi):

```
python base/tools/fm1_remote.py log
```

and send what it prints with the version you run and what you were doing
(MIDI clock, Bluetooth, which controls). Right after a crash, before
switching off, it also has the registers of the last crashes.

## Building

Linux or WSL, with the JieLi toolchain and AC79 SDK as fm1-base's README
describes, and Python 3 with numpy and scipy:

```
git submodule update --init --recursive
sh build.sh                       # app/build/amenity.fwsc (identity FM-1_81700)
sh build.sh --release 2.0         # app/build/amenity-2.0.fwsc (FM-1_817020000)
```

Host simulator and tests (screens to PNG, sound to WAV, MIDI clock in and out):

```
sh build.sh --host
app/build/host/amenity_host app/tests/screens.txt build/screens
```

The sound engine is `app/jungle/jungle.c` (slicing, sequencer, rolls, stretch
and effects, integer-only, in the audio interrupt) with the MIDI clock in
`app/jungle/jungle_clock.c`; the panel and screen are `app/app.c`, your own
breaks `app/user_breaks.c` and `web/breaks/`.

## Credits

- FM-1 hardware: M-VAVE. Amenity is not affiliated with M-VAVE.
- [fm1-base](https://github.com/charlesvestal/fm1-base) by Charles Vestal and
  felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0.
- SEA codec: Dani Biró ([SEA](https://github.com/Daninet/sea-codec), MIT).
- The MIDI clock follows Fons Adriaensen's delay-locked loop.
- Ported from Presethead's Jungle Choppa VST plugin.
- The breaks: see [Third-party audio](#third-party-audio).

## Licence

Amenity's own code is [GPL-3.0-only](LICENSE.md). fm1-base and felucca-bsp
(`base/`) are MPL-2.0, the SEA codec files MIT; each file names its licence.
Files in `assets/third-party/` are excluded from the licence.
