# Apache break sample

Source: "Apache", Incredible Bongo Band (1973, from the album "Bongo Rock").
Composition: Jerry Lordan.

This file is NOT covered by this project's license.
All rights belong to the original rights holders.

It is included for convenience only. If you redistribute this project,
you are responsible for your own use of this sample.
To build without it, see "Building without the samples" in the main README.

Processing applied here: `apache.wav` is a two-bar loop of the break (48 kHz,
16-bit stereo, 3.0 s, 160 BPM), stored as received. At build time
`tools/jungle/make_breaks.sh` converts it to 32 kHz mono 16-bit (peak
at -1 dBFS), encodes it in the SEA5 format and writes it as a C array that is
built into the firmware image. Those copies stay in `build/`, which is not
part of the repository.
