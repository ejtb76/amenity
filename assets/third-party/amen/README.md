# Amen break sample

Source: "Amen, Brother", The Winstons (1969). Drums: Gregory C. Coleman.
Composition: Richard L. Spencer.

This file is NOT covered by this project's license.
All rights belong to the original rights holders.

It is included for convenience only. If you redistribute this project,
you are responsible for your own use of this sample.
To build without it, see "Building without the samples" in the main README.

Processing applied here: `amen.wav` is a two-bar loop of the break (48 kHz,
24-bit stereo, 3.0 s, 160 BPM), stored as received. At build time
`tools/jungle/make_amen_assets.sh` converts it to 32 kHz mono 16-bit (peak
at -1 dBFS), encodes it in the SEA5 format and writes it as a C array that is
built into the firmware image. Those copies stay in `build/`, which is not
part of the repository.
