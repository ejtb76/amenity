# Third-party audio asset: Amen break

Instructions for handling the Amen break sample in this firmware repository.
Read this before adding, moving, converting or referencing the sample.

## Background

The Amen break is a drum break from "Amen, Brother" by The Winstons (1969).
It is NOT public domain. The composition and the sound recording are still
under copyright (US: until 2067, EU: until at least end of 2039). This project
ships the sample for convenience, but does not own it and cannot license it.

The goal of these rules: the project's open source license must never appear
to cover this audio, and the sample must be removable without breaking the
build.

## Hard rules

1. Never place the sample under the project license (MIT/GPL/etc.).
2. Never label the sample as CC0, public domain, royalty-free or "free to use".
   Other projects do this (e.g. Sonic Pi lists it as CC0); do not copy that.
3. Never add the project's copyright header or SPDX license identifier to the
   audio file or to any file derived from it.
4. Never claim authorship of the recording in metadata, commit messages,
   README text or release notes.

## Directory layout

Keep everything related to the sample in one isolated directory:

```
assets/third-party/amen/
  amen.wav           # the sample (original or trimmed/resampled)
  README.md          # source + rights notice (template below)
```

Adapt the path to the repo's existing structure if `assets/` does not exist,
but keep the `third-party/amen/` part so the exclusion is obvious.

### `assets/third-party/amen/README.md` template

```markdown
# Amen break sample

Source: "Amen, Brother", The Winstons (1969). Drums: Gregory C. Coleman.
Composition: Richard L. Spencer.

This file is NOT covered by this project's license.
All rights belong to the original rights holders.

It is included for convenience only. If you redistribute this project,
you are responsible for your own use of this sample.
To build without it, see "Building without the sample" in the main README.

Processing applied here: <describe: trimmed to N bars, resampled to X kHz,
converted to mono 16-bit, etc.>
```

## Root LICENSE and README

- Add a section at the end of the root `LICENSE` file (below the license text,
  clearly separated) stating that `assets/third-party/` is excluded:

  ```
  ---
  Exclusions: files in assets/third-party/ are not covered by this license.
  See the README.md in each subdirectory for source and rights information.
  ```

- Add a short "Third-party audio" section to the main `README.md` that links
  to `assets/third-party/amen/README.md` and explains how to build without
  the sample.

## Build integration

Firmware usually embeds audio as a C array. That array is a copy of the
recording and falls under the same rules as the `.wav`.

- Prefer generating the array at build time from the `.wav` (script in
  `tools/`), and add the generated output to `.gitignore`.
- If the generated file must be committed, put it in
  `assets/third-party/amen/generated/`, not in `src/`, and start it with:

  ```c
  /* Generated from assets/third-party/amen/amen.wav.
   * Third-party audio: NOT covered by this project's license.
   * See assets/third-party/amen/README.md. */
  ```

  Do not add an SPDX header to this file.

- Add a build option (e.g. `AMEN_SAMPLE=0` / `-DWITH_AMEN_SAMPLE=OFF`) that
  builds the firmware without the sample. With the option off, the feature
  must either use a fallback (e.g. a synthesized/re-sequenced break) or be
  disabled cleanly. The build must succeed if `assets/third-party/amen/` is
  deleted entirely.

## Git hygiene

- Add the sample and its README in a dedicated commit, separate from code
  changes, with a message like:
  `Add third-party Amen break sample (excluded from project license)`
- Do not mix sample changes into unrelated commits.

## If a takedown request arrives

1. Delete `assets/third-party/amen/`.
2. Set the default of the build option to "without sample".
3. Update the README section to say the sample must be supplied by the user.
4. Commit and push. Ask the maintainer before rewriting git history.

## Checklist before committing

- [ ] Sample lives only in `assets/third-party/amen/` (and generated files
      only in its `generated/` subfolder or in ignored build output).
- [ ] `assets/third-party/amen/README.md` exists and names the source.
- [ ] Root `LICENSE` has the exclusion note.
- [ ] Main `README.md` has the "Third-party audio" section.
- [ ] No SPDX header, project copyright line or CC0 label on any
      sample-derived file.
- [ ] Build passes with the sample option off and with the directory removed.
