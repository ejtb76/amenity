/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* JUNGLE: live amen break slicer, sequencer and roll engine.
 * The sound engine of AMENITY, a port of the Jungle Choppa VST plugin. Integer-only; renders in the APP
 * output stage. Control functions belong to the control task and only write
 * single words, which the audio interrupt picks up at its next block. */
#ifndef JUNGLE_H
#define JUNGLE_H
#include <stdint.h>

/* The delay line holds 2^JUNGLE_DELAY_BITS half-rate samples of 16 bits: 14
 * is 743 ms for 32 KiB of RAM. If the build stops with "runtime heap ... need
 * at least 49152", lower this by one (371 ms, 16 KiB). */
#ifndef JUNGLE_DELAY_BITS
#define JUNGLE_DELAY_BITS 14
#endif

#ifndef JUNGLE_AUDIO_CODE
#ifdef FM1_HOST_TEST
#define JUNGLE_AUDIO_CODE
#else
#include "fm1.h"
#define JUNGLE_AUDIO_CODE FM1_AUDIO_CODE
#endif
#endif

enum {
    JUNGLE_RATE = 44100,         /* output */
    JUNGLE_NATIVE_BPM_X10 = 1600, /* the amen; the real value follows the bound loop */
    JUNGLE_STEPS = 32,           /* 16ths over the two bars */
    JUNGLE_SLICES = 16,          /* 8ths, each plays to the end of the loop */
    JUNGLE_TICKS = 48,           /* clock ticks per 16th step */
    JUNGLE_BPM_MIN_X10 = 400,
    JUNGLE_BPM_MAX_X10 = 3200
};

/* Rolls, one per white key, in four groups of four:
 *   keys 0..3    1/4 repeat, build (1/4 -> 1/8 -> 1/16 -> 1/32, a beat
 *                each), STRETCH (no roll: every hit of the pattern plays
 *                stretched while held), echo (dotted 1/8, softer and darker
 *                on every repeat)
 *   keys 4..7    1/8  repeat, down, up, shrink
 *   keys 8..11   1/16 repeat, down, up, shrink
 *   keys 12..15  1/32 repeat, down, up, shrink
 * Repeat: same slice and pitch. Down/up: a whole tone per repeat.
 * Shrink: every repeat shorter and softer. */
enum { JUNGLE_ROLL_KEYS = 16 };

struct jungle_step { uint8_t active, slice, stretch; int8_t pitch; };

void jungle_init(void);
/* Control task. Bind the break: SEA5 data from the sample bank (32 kHz mono,
 * 228 bytes per 320 frames), a two-bar loop. NULL unbinds and silences.
 * Returns 0, or -1 for data too short for its frame count. */
int jungle_bind(const uint8_t *sea, unsigned bytes, unsigned frames);
/* The built-in break (jungle_fallback.c): 1 with its data, or 0 when the
 * firmware was built without the sample. */
int jungle_fallback(const uint8_t **data, unsigned *bytes, unsigned *frames);
/* The SEA5 data the engine plays now, or NULL. */
const uint8_t *jungle_bound(void);
/* Tempo of the bound loop at its own speed if it is a 32 kHz bank slot;
 * 0 when nothing is bound. (Playback speed follows the frame count and
 * needs no sample rate: the loop is always two bars.) */
unsigned jungle_native_bpm_x10(void);
/* Control task. Stopping releases the sound; starting begins at step 1. */
void jungle_play(unsigned on);
/* Mixed into the random generator at the next start (e.g. a millisecond clock). */
void jungle_seed(uint32_t seed);
void jungle_tempo(unsigned bpm_x10);
/* 0..127. Variation and the SEQ fill apply from the next pattern. */
/* Transpose everything, -12..+12 semitones, from the next hit. */
void jungle_pitch(int semitones);
/* STRETCH: a hit stretched slower at its own pitch, the way an old sampler's
 * time-stretch did it: short pieces of the slice, each played `slow` times
 * (2..4), spliced with a short crossfade. The repeats ring metallic.
 * WARP amount 0..127: the chance per 8th that the pattern warps a hit, from
 * the next pattern (never on the first four 8ths): two in three become
 * STRETCH, one in three plays at half speed, an octave down. Hold makes every
 * hit stretch, rolls and one-shots too. Grain: the piece length in ms (3..80)
 * and the splice crossfade in tenths of a ms (1..300, at most the piece). */
void jungle_warp(unsigned amount);
void jungle_stretch_hold(unsigned on);
void jungle_stretch_grain(unsigned piece_ms, unsigned fade_tenths);
void jungle_stretch_slow(unsigned times);
/* Short feedback delay (a comb) on the whole break, before the DJ filter:
 * it rings metallic at the delay's pitch. Feedback 0..127 is off to 90%;
 * the average level stays put as it rises. The pitch is a root note, 0..11
 * from C, in the octave C2..B2 (delays 15.3..8.1 ms); E by default.
 * jungle_feedback_delay sets any delay in tenths of a ms (1..230). */
void jungle_feedback(unsigned amount);
/* After the feedback delay, in this order: a 4-band EQ (band 0 low shelf
 * 100 Hz, 1 peak 500 Hz, 2 peak 2.5 kHz, 3 high shelf 8 kHz; -12..+12 dB),
 * saturation (0..127: drive 0..+18 dB into a soft clipper, level held) and
 * compression (0..127: threshold 0..-24 dB, ratio 1..6, level held). */
/* LO-FI, first in the chain, like an early sampler: 0 is off; 1..127
 * lowers the sample rate (44.1 kHz down to ~6 kHz, exponentially) and the
 * bits (16 down to 6) together, without anti-aliasing. jungle_lofi_view
 * gives the rate in Hz and the bits for the panel. */
void jungle_lofi(unsigned amount);
void jungle_lofi_view(unsigned amount, unsigned *hz, unsigned *bits);
void jungle_eq(unsigned band, int db);
void jungle_saturation(unsigned amount);
void jungle_compression(unsigned amount);
void jungle_feedback_note(unsigned pitch_class);
void jungle_feedback_delay(unsigned tenths);
/* Delay and reverb, fed from the signal after the compressor and added after
 * the DJ filter (so the filter does not sweep the returns). Both run at half
 * the sample rate (22.05 kHz): their repeats and tails are a little dark, and
 * the buffers stay small. A send 0..127 is the level fed in; the tails of
 * either ring on when the send is turned down.
 * Delay: one tap with feedback and a high-pass on what is written. Time is in
 * half-rate samples (1 = 45 us), 4..JUNGLE_DELAY_MAX; feedback 0..127 is 0 to
 * 90%; high-pass 0..127 is 30 Hz to 4 kHz; mix 0..127 is how much of the
 * delay is returned (the dry stays at full level).
 * Reverb: a four-line feedback delay network with Hadamard mixing, two input
 * allpass diffusers and a low-pass in each line. type 0..JUNGLE_REVERB_TYPES-1
 * (ambience, room, hall, plate, cathedral) sets the line lengths, decay and
 * diffusion; size 0..127 moves along that type's range of both; damping 0..127
 * is how dark the tail gets as it decays; mix 0..127 is the return level. */
enum {
    JUNGLE_DELAY_MAX = (1 << JUNGLE_DELAY_BITS) - 8,
    JUNGLE_REVERB_TYPES = 5
};
void jungle_delay_send(unsigned amount);
void jungle_delay_time(unsigned half_rate_samples);
void jungle_delay_feedback(unsigned amount);
void jungle_delay_highpass(unsigned amount);
void jungle_delay_mix(unsigned amount);
void jungle_reverb_send(unsigned amount);
void jungle_reverb(unsigned type, unsigned size, unsigned damping);
void jungle_reverb_mix(unsigned amount);
/* DJ filter, 0..127: below 64 low-pass (150 Hz at 0), 64 open, above 64
 * high-pass (4 kHz at 127). jungle_filter_hz gives the cutoff for a value. */
void jungle_filter(unsigned value);
unsigned jungle_filter_hz(unsigned value);
/* The DJ filter's Q in tenths (5..30); 8 (flat) by default. */
void jungle_filter_resonance(unsigned q_tenths);
void jungle_variation(unsigned rhythm);
/* SEQ fill: each phrase ends with one white-key effect chosen at random,
 * from 8, 4 or 2 steps before the end (1/32 rolls only in the last 2). */
void jungle_auto_roll(unsigned on);
/* Freeze keeps the current pattern instead of generating a new one. */
void jungle_freeze(unsigned on);
/* Replace the pattern with the straight break at the next step. */
void jungle_straight(void);
/* Hold a roll key as above. Release with jungle_roll_off. A new roll starts
 * on the next 16th (32nd for 1/32 rolls) and repeats the slice that was
 * playing; the pattern keeps running underneath. After a release the roll
 * goes on until the pattern's next hit (at most the next 8th), so letting
 * go a little early never leaves a gap. */
void jungle_roll(unsigned key);
void jungle_roll_off(void);
/* While a repeat, down, up or shrink roll is held: repeat this slice (0..15)
 * from its next hit instead of the one it started on; -1 hands back. BUILD,
 * STRETCH and ECHO ignore it. jungle_roll_slice is the slice a roll repeats,
 * or the last one played when no roll sounds, for the panel to step from and
 * show. */
void jungle_roll_slice_pick(int slice);
int jungle_roll_slice(void);
/* BUILD's stereo stutter, 0..15 = rate * 4 + pan; it stays until changed.
 * Rate: 0 build (1/4 -> 1/8 -> 1/16 -> 1/32, a beat each), 1 fast build
 * (1/8 -> 1/64, half a beat each), 2 slowing (1/32 -> 1/4), 3 steady 1/16.
 * Pan of each hit: 0 centre, 1 ping-pong, 2 widening ping-pong, 3 random.
 * Mode 0 is the plain build. */
void jungle_stutter(unsigned mode);
/* STRETCH: tune the stretched hits (the held key and warped hits), -12..+12
 * semitones. It stays until changed. */
void jungle_stretch_pitch(int semitones);
/* ECHO: how much of each repeat comes back, as the next repeat's level, in
 * percent (10..95; 75 by default). */
void jungle_echo_feedback(unsigned percent);
/* Chop editor. The pattern's base is the sixteen 8th-note hits, one per
 * slice in order; this moves one: position 0..15 (the hit's place in the two
 * bars) plays slice 0..15. It changes the pattern at once and every pattern
 * after it (variation and warp work on top of it). RAM only: it is forgotten
 * at power-up and when a break is bound (the sample changes), and
 * jungle_edit_reset puts the order back. */
void jungle_edit_slice(unsigned position, unsigned slice);
void jungle_edit_reset(void);
/* LFO, one step per clock tick and in time with the break (it starts at step
 * 1 with the play). Wave: 0 sine, 1 triangle, 2 saw up, 3 saw down, 4 square,
 * 5 random (a new value each cycle). Depth 0..127 (0 is off). Time is the
 * cycle length in ticks (a 16th is JUNGLE_TICKS, a beat four of them), 8..
 * 24576. The destination (below) is moved by up to the depth around where its
 * own control sits: the filter by +-63 steps, the sends by +-100%, the
 * transposition by +-12 semitones (for the next hits), variation and warp by
 * +-127 (for the next pattern), and the level is turned down by up to the
 * depth, following the wave. */
enum {
    JUNGLE_LFO_FILTER, JUNGLE_LFO_DELAY, JUNGLE_LFO_REVERB, JUNGLE_LFO_TRANSPOSE,
    JUNGLE_LFO_VARIATION, JUNGLE_LFO_WARP, JUNGLE_LFO_LEVEL,
    JUNGLE_LFO_DESTS, JUNGLE_LFO_WAVES = 6
};
void jungle_lfo_wave(unsigned wave);
void jungle_lfo_depth(unsigned amount);
void jungle_lfo_time(unsigned ticks);
void jungle_lfo_dest(unsigned dest);
/* Momentary mute of the break, ahead of the whole effects chain: the effects'
 * tails ring on. A 1 ms fade each way. */
void jungle_mute(unsigned on);
/* Play one slice on the next 32nd. */
void jungle_one_shot(unsigned slice);

/* MIDI clock in: the MIDI routing task passes every realtime status here
 * (F8 clock, FA start, FB continue, FC stop). After a Start or Continue the
 * break follows the clock: 24 pulses a beat, the first pulse after Start on
 * step 1; a second without pulses stops it and hands back to the internal
 * clock. While it follows, jungle_clock reports 1 and the tempo it has
 * locked to (BPM x 10), for the control task to show and to set the
 * break's speed with (jungle_tempo). */
void jungle_midi_realtime(unsigned status);
int jungle_clock(unsigned *bpm_x10);
/* MIDI clock out, the other way round: while the internal clock plays, the
 * audio interrupt sends Start, 24 pulses a beat and Stop through this
 * (app/midi_router.c queues them for the MIDI outputs). */
void jungle_midi_out(unsigned status);

/* Views for the control task. */
int jungle_playing(void);
int jungle_current_step(void);        /* -1 while stopped */
int jungle_roll_active(void);         /* roll key while sounding, else -1 */
void jungle_pattern(struct jungle_step out[JUNGLE_STEPS]);

/* Audio interrupt: add frames of interleaved stereo Q23 to out. */
JUNGLE_AUDIO_CODE void jungle_render(int32_t *out, unsigned frames);
#endif
