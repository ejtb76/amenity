/* SPDX-License-Identifier: GPL-3.0-only */
/* JUNGLE engine. See jungle.h.
 *
 * Clock: an integer accumulator gains bpm_x10 * 4 * JUNGLE_TICKS per sample
 * and emits a tick at JUNGLE_RATE * 600, so tempo is exact with no drift.
 * Sequencer, rolls and one-shots act on ticks; voices render per sample.
 *
 * The break is SEA5 in the memory-mapped sample bank. Each voice decodes
 * 20-frame groups on demand and keeps the last two, so playback forward
 * costs one group decode per 20 source frames; a new start decodes from its
 * 320-frame chunk's seed.
 *
 * STRETCH hits: the voice's timeline moves at half speed while two grains
 * read the slice at full speed. Every `hop` samples a new grain starts at
 * the timeline and fades in over `fade` samples while the previous one
 * fades out (the two always sum to one), so each piece sounds twice. At a
 * trigger both grains start at the slice, which keeps the attack whole.
 * The defaults, 38 ms pieces and a 10 ms splice, are the user's pick: the
 * closest to an Akaizer-style 200% stretch. The splices bubble a little in
 * the lows; that is part of the sound (a band split did not help).
 *
 * No division by variables in audio code: everything it calls must stay
 * safe while Flash is being written. */
#include "jungle.h"
#include "jungle_clock.h"
#include "fm1_sea.h"
#include "jungle_libc.h"
#include "jungle_mul.h"

#ifndef FM1_AUDIO_DATA
#define FM1_AUDIO_DATA
#endif
#include "jungle_sea_dqt.inc"
#include "jungle_fx_tables.inc"

#ifndef JUNGLE_FADE_IN
#define JUNGLE_FADE_IN 48   /* samples, ~1.1 ms; equal fades keep a crossfade flat */
#endif
#ifndef JUNGLE_FADE_OUT
#define JUNGLE_FADE_OUT 48
#endif

#define TICK_THRESHOLD ((uint32_t)JUNGLE_RATE * 600U)
#define BAR_TICKS (JUNGLE_STEPS * JUNGLE_TICKS)
#define BEAT_TICKS (4 * JUNGLE_TICKS)
#define FULL 32767
#define SHOT_TICKS (JUNGLE_TICKS / 2)
#define SEA_BYTES 228U
#define NO_GROUP 0xffffffffU
#define ONE_Q24 (1U << 24)

/* 2^(n/12) in Q16 for n = -24..+24. */
static FM1_AUDIO_DATA const uint32_t pitch_q16[49] = {
    16384, 17358, 18390, 19484, 20643, 21870, 23170, 24548,
    26008, 27554, 29193, 30929, 32768, 34716, 36781, 38968,
    41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858,
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193,
    104032, 110218, 116772, 123715, 131072, 138866, 147123, 155872,
    165140, 174960, 185364, 196386, 208064, 220436, 233544, 247431,
    262144,
};
/* DJ filter: Chamberlin state-variable, run twice per sample (88.2 kHz)
 * to stay stable up to its 12 kHz top. Q15 frequency coefficients and the
 * cutoff in Hz per knob value; 64 is off. Q 1.4 gives a sweep some edge
 * (the level moves at most ~1 dB above the dry break); the panel uses it
 * only while the knob turns and rests at a flat Q 0.8. */
static FM1_AUDIO_DATA const uint16_t filter_f[128] = {
    350, 375, 402, 430, 460, 493, 528, 565, 606, 648, 694, 744,
    796, 853, 913, 978, 1047, 1121, 1201, 1286, 1377, 1475, 1579, 1691,
    1811, 1939, 2076, 2223, 2381, 2550, 2730, 2924, 3131, 3352, 3590, 3844,
    4116, 4407, 4719, 5053, 5410, 5792, 6202, 6640, 7108, 7610, 8147, 8721,
    9334, 9991, 10693, 11443, 12245, 13101, 14016, 14992, 16033, 17144, 18327, 19587,
    20928, 22353, 23865, 25469, 0, 70, 76, 82, 89, 96, 104, 112,
    122, 132, 142, 154, 167, 181, 195, 211, 229, 248, 268, 290,
    314, 339, 367, 397, 430, 465, 504, 545, 590, 638, 691, 747,
    809, 875, 947, 1025, 1109, 1200, 1298, 1405, 1520, 1645, 1780, 1926,
    2084, 2255, 2440, 2641, 2857, 3092, 3346, 3620, 3917, 4238, 4586, 4962,
    5368, 5808, 6283, 6797, 7353, 7954, 8604, 9306,
};
static const uint16_t filter_hz[128] = {
    150, 161, 172, 184, 197, 211, 226, 242, 259, 278, 297, 319,
    341, 365, 391, 419, 449, 480, 514, 551, 590, 632, 677, 724,
    776, 831, 890, 953, 1020, 1093, 1170, 1253, 1342, 1437, 1539, 1648,
    1764, 1889, 2023, 2167, 2320, 2485, 2661, 2849, 3051, 3267, 3499, 3747,
    4012, 4297, 4601, 4927, 5277, 5651, 6051, 6480, 6939, 7431, 7957, 8521,
    9125, 9772, 10464, 11206, 0, 30, 32, 35, 38, 41, 45, 48,
    52, 56, 61, 66, 71, 77, 84, 91, 98, 106, 115, 124,
    134, 145, 157, 170, 184, 199, 216, 233, 253, 273, 296, 320,
    346, 375, 406, 439, 475, 514, 556, 602, 651, 705, 763, 825,
    893, 966, 1046, 1132, 1225, 1325, 1434, 1552, 1679, 1817, 1966, 2128,
    2302, 2491, 2696, 2917, 3157, 3416, 3696, 4000,
};
#define FILTER_OPEN 64
/* Damping 1/Q in Q14; jungle_filter_resonance sets it (host listening tests). */
static volatile int32_t ctl_filter_damp = 20480; /* Q 0.8: flat; the panel raises it while sweeping */

enum { REPEAT, DOWN, UP, SHRINK, BUILD, ECHO, STRETCH_ALL };
enum { STRETCH_KEY = 2 };
#define BEAT (4 * JUNGLE_TICKS)
/* Per roll key: ticks between hits and what each hit does. */
static FM1_AUDIO_DATA const struct { uint8_t ticks, kind; } rolls[JUNGLE_ROLL_KEYS] = {
    { BEAT, REPEAT }, { BEAT, BUILD }, { 0, STRETCH_ALL }, { BEAT * 3 / 4, ECHO },
    { BEAT / 2, REPEAT }, { BEAT / 2, DOWN }, { BEAT / 2, UP }, { BEAT / 2, SHRINK },
    { BEAT / 4, REPEAT }, { BEAT / 4, DOWN }, { BEAT / 4, UP }, { BEAT / 4, SHRINK },
    { BEAT / 8, REPEAT }, { BEAT / 8, DOWN }, { BEAT / 8, UP }, { BEAT / 8, SHRINK },
};

/* Reads the SEA5 source, keeping the last two decoded 20-frame groups. */
struct reader {
    uint32_t pos, frac;              /* grain position, Q24 fraction */
    uint32_t cached[2], state_group; /* decoded group per slot; group the state follows */
    int32_t state[8];
    int16_t pcm[2][FM1_SEA_GROUP];
};
struct voice {
    uint32_t pos, frac, rate; /* timeline: source frame, Q24 fraction and step */
    uint32_t grain_rate;      /* stretch: grain step, Q24; 0 = read at the timeline */
    uint32_t grain_step;      /* crossfade rise per sample, Q24 */
    uint16_t grain_hop, grain_fade, grain_age;
    int32_t env, gain;        /* Q15 */
    int32_t tone, low;        /* one-pole low-pass: Q15 coefficient (FULL = open) and state */
    uint16_t gate;            /* ticks until release, 0 = play to the end */
    uint8_t active, releasing, rising; /* rising: the reader whose window goes up */
    struct reader read[2];
    const uint8_t *data;      /* the break it plays: a new one bound plays from the next hit */
    uint32_t frames;
};

/* Control task -> audio: single-word writes only. */
static volatile uint16_t ctl_bpm = JUNGLE_NATIVE_BPM_X10;
static volatile uint32_t ctl_reciprocal = 0xffffffffU / (JUNGLE_NATIVE_BPM_X10 * 4U * JUNGLE_TICKS);
static volatile uint8_t ctl_playing, ctl_rhythm, ctl_auto, ctl_freeze;
static volatile uint8_t ctl_roll;     /* 0, or 1 + rate * 4 + variant */
static volatile uint8_t ctl_straight; /* request counter */
static volatile uint16_t ctl_shot;    /* request counter << 4 | slice */
static volatile uint32_t ctl_seed;
static volatile int8_t ctl_transpose;
static volatile uint8_t ctl_filter = FILTER_OPEN;
static volatile uint8_t ctl_warp, ctl_stretch_hold;
static volatile uint32_t ctl_stretch_ratio = 16384; /* timeline / grain speed, Q16: 1 / slow; 4x, the user's pick */
/* Slower holds each slice's loud attack longer: -0.8, -1.4, -2.0 dB for
 * 2x, 3x, 4x keep stretch hits level with the rest (measured at 38/10 ms). */
static volatile int32_t ctl_stretch_gain = 26029;
/* Piece and splice in output samples; crossfade rise per sample, Q24. */
static volatile uint16_t ctl_grain_hop = 1675, ctl_grain_fade = 441; /* 38 ms, 10 ms */
static volatile uint32_t ctl_grain_step = ONE_Q24 / 441;
/* Sample binding: pointer last, so audio never sees new data with old sizes. */
static const uint8_t *volatile src_data;
static volatile uint32_t src_frames, src_slice;
static volatile uint32_t ctl_speed; /* source frames per output sample at the tempo, Q24 */
static volatile uint8_t src_generation;
/* The binding as this block found it (read once, consistent), for the hits it triggers. */
static const uint8_t *blk_data;
static uint32_t blk_frames, blk_slice;

/* Audio -> control task. */
static volatile int8_t view_step = -1, view_roll = -1;

/* Audio state. */
static struct voice voices[2];
static struct jungle_step steps[JUNGLE_STEPS];
static uint8_t fill_key;     /* SEQ fill this phrase: 0, 1 + white key, or 17 + instant effect (0..2) */
enum { FILL_INSTANT = 17 };
static uint8_t fill_sweep;   /* this phrase's fill sweeps the filter too */
#define SWEEP_DAMP 11702     /* Q 1.4, as when K4 turns */
static volatile uint8_t ctl_instant, view_instant;
static uint8_t instant;      /* this block's instant effects: held, and the fill's */
static int32_t lofi_max_rate;
static uint8_t lofi_max_shift;
#define INSTANT_FB 24576     /* 0.75, Q15 */
#define INSTANT_FB_GAIN 21674 /* sqrt(1 - 0.75^2), Q15: the level held, as the knob does */
static uint16_t fill_start;  /* its first tick in the phrase */
static uint8_t stretch_all;  /* the STRETCH key holds: every hit stretched */
static uint32_t tick_acc, tick_late, rng = 0x9e3779b9U; /* tick_late: Q16 samples since the tick */
static uint16_t bar_tick;
static uint8_t was_playing, pattern_ready, last_slice, seen_straight, seen_generation;
static volatile uint8_t view_made_rhythm, view_made_warp;   /* what the pattern playing was made with */
static volatile int8_t view_slice = -1;                      /* the slice last hit, -1 when none sounds */
static volatile uint8_t view_filter = FILTER_OPEN;           /* the filter's value this block, the sweep's too */
/* Following MIDI clock (jungle_clock.c): the n-th pulse after Start marks
 * tick ext_base + 8n. The break runs at the period the delay-locked loop has
 * found and is steered gently onto its time line, LOCK_GAIN faster per tick
 * behind (at most 25%); more than LOCK_FAR ticks out it jumps or waits. It
 * never sees a pulse itself, only the loop's smooth time. */
enum { LOCK_FAR = 48 };          /* six pulses */
#ifndef LOCK_GAIN
#define LOCK_GAIN 0.01f
#endif
static uint8_t ext, seen_start;
static uint32_t ext_ticks, ext_base;
/* Clock out: ticks since the internal clock started, F8 every 8th, each with
 * the time of its sample (block_now: the block's first frame). */
static uint32_t run_ticks, block_now, sample_now;
static volatile uint8_t view_playing;
static int8_t last_pitch;
static uint16_t seen_shot;
static int8_t shot_pending = -1;
static uint8_t roll_now, roll_slice; /* roll_now: 0, or 1 + key */
static int8_t roll_pitch;
static uint16_t roll_wait, roll_count, roll_elapsed; /* ticks to the next hit, hits, ticks since start */
static int32_t roll_gain, roll_tone;
static int32_t filter_low, filter_band, filter_coefficient;
static uint8_t filter_was_open = 1; /* leaving open jumps straight to the cutoff */
/* Comb: a feedback delay line of 23 ms, power of two for a mask. The output
 * is scaled by sqrt(1 - fb^2), which holds the average power of a broad
 * signal steady whatever the feedback. Feedback and scale glide per block. */
enum { COMB_SIZE = 1024 };
static int32_t comb[COMB_SIZE];
static uint32_t comb_write;
static int32_t comb_fb, comb_gain = FULL;
static volatile int32_t ctl_comb_fb, ctl_comb_gain = FULL;
static volatile uint16_t ctl_comb_delay = 535; /* E2, ~12 ms */
/* EQ: the control task writes a band's coefficients, then bumps eq_change;
 * audio copies all four at its next block. A band at 0 dB is skipped. */
static volatile int32_t ctl_eq[4][5];
static volatile uint8_t ctl_eq_on, eq_change;
static int32_t eq_c[4][5], eq_z[4][4]; /* coefficients; x1, x2, y1, y2 */
static uint8_t eq_on, eq_seen;
/* LO-FI: sample-and-hold at `rate` (Q16 of 44.1 kHz) and quantization to
 * a step of 1 << shift; 0 rate = off. */
static volatile uint32_t ctl_lofi_rate;
static volatile uint8_t ctl_lofi_shift;
static uint32_t lofi_phase;
static int32_t lofi_held;
/* Saturation: drive and make-up gain, Q12; 0 drive = off. */
static volatile int32_t ctl_sat_drive, ctl_sat_makeup = 4096;
/* Compression: gain per envelope level (index = envelope / 128), Q12,
 * written by the control task. A peak envelope (0.5 ms attack, 120 ms
 * release) picks the gain, which drops at once and recovers smoothly; the
 * signal is delayed 64 samples (1.5 ms) so the gain is down before a hit's
 * attack arrives. */
enum { COMP_AHEAD = 64 };
static int16_t comp_table[256];
static volatile uint8_t ctl_comp_on;
static int32_t comp_env, comp_gain = 4096; /* envelope Q8 */
static int32_t comp_delay[COMP_AHEAD];
static uint32_t comp_write;
/* Delay in samples for C2..B2: 44100 / f. */
static const uint16_t comb_notes[12] = {674, 636, 601, 567, 535, 505, 477, 450, 425, 401, 378, 357};
#ifdef JUNGLE_TRACE
uint32_t jungle_host_frame;
void jungle_host_trigger(uint32_t frame, unsigned slice, int pitch, int32_t gain);
#endif

void jungle_init(void)
{
    memset(voices, 0, sizeof voices);
    fill_key = 0;
    {
        unsigned hz, bits;   /* LO-FI at its last step, for the instant effect */
        jungle_lofi_view(127U, &hz, &bits);
        lofi_max_rate = (int32_t)(((uint64_t)hz << 16) / 44100U);
        lofi_max_shift = (uint8_t)(16U - bits);
    }
    for (unsigned i = 0; i < JUNGLE_STEPS; ++i) {
        steps[i].active = !(i & 1U);
        steps[i].slice = (uint8_t)(i / 2U % JUNGLE_SLICES);
        steps[i].pitch = 0;
    }
    was_playing = pattern_ready = roll_now = 0;
    shot_pending = -1;
    seen_shot = ctl_shot;
    seen_straight = ctl_straight;
    view_step = view_roll = -1;
}
/* The loop is two bars: its own tempo is 480 s / duration, so at bpm the
 * source advances frames * bpm_x10 / (44100 * 4800) frames per sample. */
static void update_speed(void)
{
    ctl_speed = (uint32_t)(((umul64(src_frames, ctl_bpm) << 24) + 105840000U) / 211680000U);
}
int jungle_bind(const uint8_t *sea, unsigned bytes, unsigned frames)
{
    int ok = sea && frames >= JUNGLE_SLICES * FM1_SEA_FRAMES &&
             bytes >= (frames + FM1_SEA_FRAMES - 1U) / FM1_SEA_FRAMES * SEA_BYTES;
    /* src_generation is odd while the binding is written: the audio keeps the last one meanwhile. */
    src_generation = (uint8_t)(src_generation + 1U);
    JUNGLE_BARRIER();
    src_data = ok ? sea : NULL;
    src_frames = ok ? frames : 0U;
    src_slice = ok ? frames / JUNGLE_SLICES : 0U;
    JUNGLE_BARRIER();
    src_generation = (uint8_t)(src_generation + 1U);
    if (ok) update_speed();
    return ok ? 0 : -1;
}
const uint8_t *jungle_bound(void) { return src_data; }
unsigned jungle_native_bpm_x10(void)
{
    uint32_t frames = src_frames;
    return frames ? (unsigned)((153600000U + frames / 2U) / frames) : 0U;
}
void jungle_play(unsigned on) { ctl_playing = on ? 1U : 0U; }
void jungle_seed(uint32_t seed) { ctl_seed = seed; }
void jungle_tempo(unsigned bpm_x10)
{
    unsigned bpm = bpm_x10 < JUNGLE_BPM_MIN_X10 ? JUNGLE_BPM_MIN_X10 :
                   bpm_x10 > JUNGLE_BPM_MAX_X10 ? JUNGLE_BPM_MAX_X10 : bpm_x10;
    ctl_reciprocal = 0xffffffffU / (bpm * 4U * JUNGLE_TICKS);
    ctl_bpm = (uint16_t)bpm;
    update_speed();
}
static uint8_t clamp127(unsigned v) { return (uint8_t)(v > 127U ? 127U : v); }
void jungle_variation(unsigned rhythm) { ctl_rhythm = clamp127(rhythm); }
void jungle_pitch(int semitones)
{
    ctl_transpose = (int8_t)(semitones < -12 ? -12 : semitones > 12 ? 12 : semitones);
}
void jungle_filter(unsigned value) { ctl_filter = clamp127(value); }
void jungle_warp(unsigned amount) { ctl_warp = clamp127(amount); }
static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0;
    for (uint32_t bit = 1U << 30; bit; bit >>= 2) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
    }
    return r;
}
void jungle_feedback(unsigned amount)
{
    /* 0..99%, Q15: the top of the knob reads 100% and rings on for seconds (at 100% it would never stop, and
     * the level held would take all of the break away). sqrt(1 - fb^2) keeps the level within about 2 dB all the
     * way up, depending on the root (measured on the Amen). */
    int32_t fb = (int32_t)(clamp127(amount) * 32440U / 127U);
    ctl_comb_gain = (int32_t)isqrt((1U << 30) - (uint32_t)(fb * fb));
    ctl_comb_fb = fb;
}
#ifdef JUNGLE_TRACE
void jungle_feedback_raw(int32_t fb, int32_t gain) { ctl_comb_fb = fb; ctl_comb_gain = gain; }   /* (host tests) */
#endif
void jungle_eq(unsigned band, int db)
{
    if (band > 3U) return;
    db = db < -12 ? -12 : db > 12 ? 12 : db;
    for (unsigned k = 0; k < 5; ++k) ctl_eq[band][k] = eq_coef[band][db + 12][k];
    ctl_eq_on = (uint8_t)(db ? ctl_eq_on | (1U << band) : ctl_eq_on & ~(1U << band));
    eq_change = (uint8_t)(eq_change + 1U);
}
/* Gain for a level in half-dB (-96..+48), Q12. */
static unsigned half_db_gain(int half_db)
{
    return db_gain[(half_db < -96 ? -96 : half_db > 48 ? 48 : half_db) + 96];
}
/* Rate falls by up to 17.3 dB (44.1 kHz -> 6 kHz), so the gain table
 * serves as the exponential; bits go 16 -> 6. */
void jungle_lofi_view(unsigned amount, unsigned *hz, unsigned *bits)
{
    unsigned a = clamp127(amount);
    *hz = a ? (44100U * half_db_gain(-(int)(a * 346U / 127U) / 10) + 2048U) / 4096U : 44100U;
    *bits = 16U - a * 10U / 127U;
}
void jungle_instant(unsigned mask) { ctl_instant = (uint8_t)(mask & 7U); }
unsigned jungle_instant_active(void) { return view_instant; }
void jungle_lofi(unsigned amount)
{
    unsigned hz, bits;
    jungle_lofi_view(amount, &hz, &bits);
    ctl_lofi_shift = (uint8_t)(16U - bits);
    ctl_lofi_rate = amount ? (uint32_t)(((uint64_t)hz << 16) / 44100U) : 0U;
}
void jungle_saturation(unsigned amount)
{
    /* Drive up to +18 dB. The clipper's 3/2 scale adds 3.5 dB to quiet
     * signals and the clipping takes back about 47% of the drive, so the
     * make-up is -3.5 dB - 53% of the drive (measured on the break). */
    int drive = (int)clamp127(amount) * 36 / 127; /* half-dB */
    ctl_sat_makeup = (int32_t)half_db_gain(-7 - drive * 53 / 100);
    ctl_sat_drive = amount ? (int32_t)half_db_gain(drive) : 0;
}
void jungle_compression(unsigned amount)
{
    /* Threshold 0..-24 dB, ratio 1..6 (Q8). Make-up: half the reduction a
     * full-scale signal gets, plus 0.00036 dB * amount^2, which the break
     * measured as the rest (the peak envelope keeps the gain down between
     * hits). */
    int a = (int)clamp127(amount), threshold = -a * 48 / 127; /* half-dB */
    int ratio = 256 + a * 1280 / 127;
    int makeup = (-threshold) * (ratio - 256) / ratio / 2 + a * a * 72 / 100000;
    for (unsigned i = 0; i < 256; ++i) {
        int over = level_db[i] - threshold, reduce = over > 0 ? over * (ratio - 256) / ratio : 0;
        comp_table[i] = (int16_t)half_db_gain(makeup - reduce);
    }
    ctl_comp_on = a ? 1U : 0U;
}
void jungle_feedback_note(unsigned pitch_class) { ctl_comb_delay = comb_notes[pitch_class % 12U]; }
void jungle_feedback_delay(unsigned tenths)
{
    unsigned samples = (tenths < 1U ? 1U : tenths > 230U ? 230U : tenths) * 441U / 100U;
    ctl_comb_delay = (uint16_t)(samples ? samples : 1U);
}
void jungle_stretch_hold(unsigned on) { ctl_stretch_hold = on ? 1U : 0U; }
void jungle_stretch_slow(unsigned times)
{
    static const uint16_t gains[3] = {29889, 27853, 26029};
    times = times < 2U ? 2U : times > 4U ? 4U : times;
    ctl_stretch_gain = gains[times - 2U];
    ctl_stretch_ratio = 65536U / times;
}
void jungle_stretch_grain(unsigned piece_ms, unsigned fade_tenths)
{
    unsigned hop = (piece_ms < 3U ? 3U : piece_ms > 80U ? 80U : piece_ms) * 441U / 10U;
    unsigned fade = (fade_tenths < 1U ? 1U : fade_tenths > 300U ? 300U : fade_tenths) * 441U / 100U;
    if (fade > hop) fade = hop;
    /* A single hop write switches; the fields are read once per hit. */
    ctl_grain_fade = (uint16_t)fade;
    ctl_grain_step = ONE_Q24 / fade;
    ctl_grain_hop = (uint16_t)hop;
}
void jungle_filter_resonance(unsigned q_tenths) { ctl_filter_damp = (int32_t)(163840U / (q_tenths < 5U ? 5U : q_tenths > 30U ? 30U : q_tenths)); }
unsigned jungle_filter_hz(unsigned value) { return filter_hz[value > 127U ? 127U : value]; }
void jungle_auto_roll(unsigned on) { ctl_auto = on ? 1U : 0U; }
void jungle_freeze(unsigned on) { ctl_freeze = on ? 1U : 0U; }
void jungle_straight(void) { ctl_straight = (uint8_t)(ctl_straight + 1U); }
void jungle_roll(unsigned key)
{
    if (key < JUNGLE_ROLL_KEYS) ctl_roll = (uint8_t)(1U + key);
}
void jungle_roll_off(void) { ctl_roll = 0; }
void jungle_one_shot(unsigned slice)
{
    if (slice < JUNGLE_SLICES)
        ctl_shot = (uint16_t)(((ctl_shot >> 4) + 1U) << 4 | slice);
}
int jungle_current_step(void) { return view_step; }
unsigned jungle_filter_now(void) { return view_filter; }
int jungle_current_slice(void) { return voices[0].active || voices[1].active ? view_slice : -1; }
void jungle_pattern_made(unsigned *rhythm, unsigned *warp)
{
    *rhythm = view_made_rhythm;
    *warp = view_made_warp;
}
int jungle_playing(void) { return view_playing; }
unsigned jungle_tempo_x10(void) { return ctl_bpm; }
int jungle_roll_active(void) { return view_roll; }
void jungle_pattern(struct jungle_step out[JUNGLE_STEPS]) { memcpy(out, steps, sizeof steps); }

/* ---- Audio interrupt ---------------------------------------------------- */

static JUNGLE_AUDIO_CODE unsigned rnd(void)
{
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x;
    return x >> 16; /* 0..65535 */
}
/* 0..n-1 without division. */
static JUNGLE_AUDIO_CODE unsigned rnd_below(unsigned n) { return (rnd() * n) >> 16; }

static JUNGLE_AUDIO_CODE void straight_pattern(void)
{
    for (unsigned i = 0; i < JUNGLE_STEPS; ++i) {
        steps[i].active = !(i & 1U);
        steps[i].slice = (uint8_t)((i >> 1) & (JUNGLE_SLICES - 1));
        steps[i].pitch = 0;
        steps[i].stretch = 0;
        steps[i].flip = 0;
    }
    fill_key = 0;
}

/* The plugin's generator: 8th-note base, 16th retrigger fills and slice
 * swaps, plus STRETCH hits; the first four 8ths stay plain to anchor the
 * groove. Probabilities are Q16: 0.55 and 0.2 of the 0..127 amount. */
static JUNGLE_AUDIO_CODE void generate(void)
{
    unsigned rhythm = ctl_rhythm, warp = ctl_warp;
    view_made_rhythm = (uint8_t)rhythm;
    view_made_warp = (uint8_t)warp;
    straight_pattern();
    for (unsigned i = 0; i < JUNGLE_STEPS; i += 2) {
        unsigned early = i < 8;
        if (rnd() < rhythm * 284U) {
            steps[i + 1].active = 1;
            steps[i + 1].slice = steps[i].slice;
        }
        if (rnd() < rhythm * 103U)
            steps[i].slice = (uint8_t)rnd_below(JUNGLE_SLICES);
        /* Warp: up to a one-in-two chance per 8th at the top of the knob;
         * two in three stretch, one in three half speed (an octave down). */
        if (!early && rnd() < warp * 258U) {
            if (rnd() < 43691U) steps[i].stretch = 1;
            else steps[i].pitch = -12;
        }
    }
    /* The flip: on the "and" of beat 3 the break starts again from the top (its kick) and plays on to the
     * end of the bar; the next bar is itself again. A chance per bar that follows the variation: never at
     * 0 (the straight break stays straight), one in four at the middle, one in two at the top. */
    for (unsigned bar = 0; bar < JUNGLE_STEPS; bar += 16U)
        if (rnd() < rhythm * 258U)
            for (unsigned i = 10; i < 16U; ++i) {
                struct jungle_step *st = &steps[bar + i];
                st->active = !(i & 1U);
                st->slice = (uint8_t)((i - 10U) / 2U);
                st->pitch = 0;
                st->stretch = 0;
                st->flip = 1;
            }
    /* SEQ fill: the phrase ends with one effect, from 8, 4 or 2 steps before
     * the end. 1/32 rolls only fit the last 2 steps, so 2 steps choose among
     * the 1/16 and 1/32 rolls and STRETCH; longer fills among the other keys. */
    if (ctl_auto) {
        /* (and the three instant effects, in fills of any length) */
        unsigned length = 8U >> rnd_below(3), key;
        if (length == 2U) {
            key = rnd_below(12);
            key = key >= 9U ? FILL_INSTANT - 1U + key - 9U : key == 8U ? STRETCH_KEY : 8U + key;
        } else {
            key = rnd_below(15);
            key = key >= 12U ? FILL_INSTANT - 1U + key - 12U : key;
        }
        fill_key = (uint8_t)(1U + key);
        fill_start = (uint16_t)((JUNGLE_STEPS - length) * JUNGLE_TICKS);
        fill_sweep = rnd_below(4) == 0U;
    }
    pattern_ready = 1;
}

/* A slice pitched up runs fast into the next one, so it stops after
 * `ticks`; a stretch slice runs at half speed and needs no cut. */
static JUNGLE_AUDIO_CODE unsigned pitch_gate(int pitch, unsigned stretch, unsigned ticks)
{
    return pitch > 0 && !stretch ? ticks : 0U;
}

static JUNGLE_AUDIO_CODE void trigger(unsigned slice, int pitch, int32_t gain, unsigned gate, int32_t tone, unsigned stretch)
{
    pitch += (instant & JUNGLE_FX_OCTAVE) ? -12 : ctl_transpose;
    if (pitch < -24) pitch = -24;
    if (pitch > 24) pitch = 24;
    stretch |= ctl_stretch_hold | stretch_all;
    if (!blk_data) return;
    uint32_t tempo = ctl_speed; /* Q24 */
    /* Two voices: the new one fades in while the old one fades out. */
    struct voice *v = !voices[0].active ? &voices[0] : !voices[1].active ? &voices[1] :
                      voices[0].env <= voices[1].env ? &voices[0] : &voices[1];
    for (unsigned i = 0; i < 2; ++i)
        if (voices[i].active) voices[i].releasing = 1;
    uint32_t pitched = umul_shr(tempo, pitch_q16[pitch + 24], 16);
    v->rate = stretch ? umul_shr(pitched, ctl_stretch_ratio, 16) : pitched;
    v->grain_rate = stretch ? pitched : 0U;
    v->grain_hop = ctl_grain_hop;
    v->grain_fade = ctl_grain_fade;
    v->grain_step = ctl_grain_step;
    v->grain_age = 0;
    v->rising = 1;
    /* The tick fell between samples: start where the slice would be by now. */
    uint32_t ahead = umul_shr(tick_late, v->rate, 16);
    v->data = blk_data;
    v->frames = blk_frames;
    view_slice = (int8_t)slice;
    v->pos = slice * blk_slice + (ahead >> 24);
    v->frac = ahead & 0xffffffU;
    for (unsigned r = 0; r < 2; ++r) {
        struct reader *g = &v->read[r];
        g->pos = v->pos;
        g->frac = v->frac;
        g->cached[0] = g->cached[1] = g->state_group = NO_GROUP;
    }
    v->env = 0;
    v->gain = stretch ? (gain * ctl_stretch_gain) >> 15 : gain;
    v->tone = tone;
    v->low = 0;
    v->gate = (uint16_t)gate;
    v->active = 1;
    v->releasing = 0;
#ifdef JUNGLE_TRACE
    jungle_host_trigger(jungle_host_frame, slice, pitch, gain);
#endif
}

static JUNGLE_AUDIO_CODE unsigned auto_request(void)
{
    return ctl_auto && fill_key && fill_key < FILL_INSTANT && bar_tick >= fill_start ? fill_key : 0U;
}

static JUNGLE_AUDIO_CODE void fire_roll(void)
{
    unsigned key = (unsigned)(roll_now - 1U), variant = rolls[key].kind;
    unsigned every = rolls[key].ticks, gate = 0, n = roll_count < 12 ? roll_count : 12;
    /* Build: a beat each of 1/4, 1/8 and 1/16, then 1/32 until release. */
    if (variant == BUILD)
        every = roll_elapsed < BEAT ? BEAT : roll_elapsed < 2 * BEAT ? BEAT / 2 :
                roll_elapsed < 3 * BEAT ? BEAT / 4 : BEAT / 8;
    int pitch = roll_pitch;
    int32_t gain = FULL, tone = FULL;
    if (variant == DOWN) pitch -= 2 * (int)n;
    else if (variant == UP) pitch += 2 * (int)n;
    else if (variant == SHRINK) {
        gain = roll_gain;
        roll_gain = (roll_gain * 27853) >> 15; /* -1.4 dB per repeat */
        gate = (every * (8U - (n < 7 ? n : 7U))) >> 3;
        if (!gate) gate = 1;
    } else if (variant == ECHO) {
        /* -2.5 dB and a lower cutoff per repeat: ~5 kHz, 2 kHz, 900 Hz ... */
        gain = roll_gain;
        tone = roll_tone;
        roll_gain = (roll_gain * 24576) >> 15;
        roll_tone = (roll_tone * 22938) >> 15;
    }
    if (!gate) gate = pitch_gate(pitch, 0, every);
    trigger(roll_slice, pitch, gain, gate, tone, 0);
    ++roll_count;
    roll_wait = (uint16_t)every;
}

static JUNGLE_AUDIO_CODE void on_tick(void)
{
    unsigned step = bar_tick / JUNGLE_TICKS, offset = bar_tick % JUNGLE_TICKS;

    for (unsigned i = 0; i < 2; ++i) {
        struct voice *v = &voices[i];
        if (v->active && v->gate && --v->gate == 0) v->releasing = 1;
    }
    if (bar_tick == 0 && (!ctl_freeze || !pattern_ready)) generate();
    if (offset == 0) {
        uint8_t request = ctl_straight;
        if (request != seen_straight) {
            seen_straight = request;
            straight_pattern();
            view_made_rhythm = view_made_warp = 0;
        }
        view_step = (int8_t)step;
    }

    unsigned seq_fire = offset == 0;
    const struct jungle_step *s = &steps[step];

    /* Manual rolls win over auto-rolls. A new roll waits for the next 16th
     * (32nd for 1/32 rolls); until then the previous roll or the pattern
     * keeps playing. Switching keys keeps the repeated slice. */
    unsigned request = ctl_roll;
    if (!request) request = auto_request();
    if (!request) {
        /* Released: carry on to the pattern's next hit, or the next 8th. */
        if (roll_now && ((seq_fire && s->active) || (offset == 0 && !(step & 1U)))) roll_now = 0;
    } else if (request != roll_now) {
        unsigned on_grid = rolls[request - 1U].ticks <= JUNGLE_TICKS / 2 ? bar_tick % (JUNGLE_TICKS / 2) == 0 : offset == 0;
        if (on_grid) {
            if (!roll_now) {
                unsigned here = seq_fire && s->active;
                roll_slice = here ? s->slice : last_slice;
                roll_pitch = here ? s->pitch : last_pitch;
            }
            roll_now = (uint8_t)request;
            roll_wait = 0;
            roll_count = 0;
            roll_elapsed = 0;
            roll_gain = roll_tone = FULL;
        }
    }
    stretch_all = roll_now && rolls[roll_now - 1U].kind == STRETCH_ALL;
    if (roll_now && !stretch_all) {
        if (roll_wait == 0) fire_roll();
        --roll_wait;
        if (roll_elapsed < 0xffffU) ++roll_elapsed;
    } else if (seq_fire && s->active) {
        /* With STRETCH held the pattern itself plays, every hit stretched. */
        trigger(s->slice, s->pitch, FULL, pitch_gate(s->pitch, s->stretch | ctl_stretch_hold | stretch_all, JUNGLE_TICKS), FULL, s->stretch);
        last_slice = s->slice;
        last_pitch = s->pitch;
    }
    view_roll = (int8_t)(roll_now ? roll_now - 1 : -1);

    if (shot_pending >= 0 && bar_tick % SHOT_TICKS == 0) {
        trigger((unsigned)shot_pending, 0, FULL, 0, FULL, 0);
        last_slice = (uint8_t)shot_pending;
        last_pitch = 0;
        shot_pending = -1;
    }
    if (++bar_tick >= BAR_TICKS) bar_tick = 0;
    if (!ext && !(run_ticks++ & 7U)) {   /* 24 per beat, timed by the tick's own sample */
        float late = (float)sample_now - (float)tick_late * (1.0f / 65536.0f);
        jungle_midi_out(0xf8U, block_now + (uint32_t)(int32_t)(late * ((float)JUNGLE_TICK_HZ / JUNGLE_RATE)));
    }
}

/* One decoded source frame. Groups after the one the state follows continue
 * from it; anything else restarts at the chunk's seed. */
static JUNGLE_AUDIO_CODE int32_t fetch(struct reader *r, const uint8_t *data, uint32_t frame)
{
    uint32_t group = frame / FM1_SEA_GROUP, slot = group & 1U;
    if (r->cached[slot] != group) {
        unsigned g = group & 15U;
        unsigned first = g && r->state_group + 1U == group ? g : 0U;
        if (fm1_sea_decode_groups(data + (group >> 4) * SEA_BYTES, SEA_BYTES, 5, jungle_dqt,
                                  first, g, r->state, r->pcm[slot]))
            return 0;
        r->state_group = group;
        r->cached[slot] = group;
    }
    return r->pcm[slot][frame - group * FM1_SEA_GROUP];
}
/* Interpolated source at the reader, then a step forward; silent past the end. */
static JUNGLE_AUDIO_CODE int32_t read_step(struct reader *r, const uint8_t *data, uint32_t frames, uint32_t rate)
{
    int32_t s = 0;
    if (r->pos + 1U < frames) {
        int32_t a = fetch(r, data, r->pos), b = fetch(r, data, r->pos + 1U);
        s = a + (((b - a) * (int32_t)(r->frac >> 9)) >> 15);
    }
    r->frac += rate;
    r->pos += r->frac >> 24;
    r->frac &= 0xffffffU;
    return s;
}

static JUNGLE_AUDIO_CODE int32_t render_voice(struct voice *v)
{
    const uint8_t *data = v->data;
    uint32_t frames = v->frames;
    if (v->releasing) {
        v->env -= FULL / JUNGLE_FADE_OUT;
        if (v->env <= 0) { v->active = 0; return 0; }
    } else if (v->env < FULL) {
        v->env += FULL / JUNGLE_FADE_IN;
        if (v->env > FULL) v->env = FULL;
    }
    if (v->pos + 1U >= frames) { v->active = 0; return 0; }
    uint32_t left = frames - 1U - v->pos;
    if (!v->releasing && left <= ((JUNGLE_FADE_OUT * v->rate) >> 24) + 1U) v->releasing = 1;
    int32_t s;
    if (!v->grain_rate) {
        /* Turntable: read at the timeline. */
        v->read[0].pos = v->pos;
        v->read[0].frac = v->frac;
        s = read_step(&v->read[0], data, frames, 0);
    } else {
        /* Stretch: the newest grain alone, after the splice crossfade. */
        s = read_step(&v->read[v->rising], data, frames, v->grain_rate);
        if (v->grain_age < v->grain_fade) {
            uint32_t w = v->grain_age * v->grain_step;
            int32_t b = read_step(&v->read[v->rising ^ 1U], data, frames, v->grain_rate);
            s = (int32_t)((mul64(s, w) + mul64(b, ONE_Q24 - w)) >> 24);
        }
    }
    if (v->tone < FULL) {
        v->low += ((s - v->low) * v->tone + 16384) >> 15; /* rounded: no stuck offset */
        s = v->low;
    }
    s = (((s * v->env) >> 15) * v->gain) >> 15;
    v->frac += v->rate;
    v->pos += v->frac >> 24;
    v->frac &= 0xffffffU;
    if (v->grain_rate && ++v->grain_age >= v->grain_hop) {
        /* The older grain is done: it restarts, rising, at the timeline
         * (after this sample's step); the current one fades out. */
        struct reader *done = &v->read[v->rising ^ 1U];
        v->grain_age = 0;
        v->rising ^= 1U;
        done->pos = v->pos;
        done->frac = v->frac;
    }
    return s;
}

JUNGLE_AUDIO_CODE void jungle_render(int32_t *out, unsigned frames, uint32_t now)
{
    /* The break bound now, read whole (the generation even and the same before and after), else the last
     * one. Breaks are in the firmware and never go away: a voice of the one bound before plays on until the
     * next hit. Unbound (NULL) silences. */
    uint8_t generation = src_generation;
    if (!(generation & 1U) && generation != seen_generation) {
        const uint8_t *d = src_data;
        uint32_t f = src_frames, sl = src_slice;
        if (src_generation == generation) {
            seen_generation = generation;
            blk_data = d;
            blk_frames = f;
            blk_slice = sl;
            if (!d) voices[0].active = voices[1].active = 0;
        }
    }
    const uint8_t *data = blk_data;
    /* MIDI clock in: the loop's state and time line. Start restarts at
     * step 1, Continue goes on from the break's next pulse, Stop or a lost
     * clock stops. */
    struct jungle_follow f;
    jungle_follow_read(&f);
    block_now = now;
    unsigned resume = 0;
    if (f.state != CLOCK_OFF && f.start != seen_start) {
        seen_start = f.start;
        if (!ext && was_playing) jungle_midi_out(0xfcU, now); /* the internal clock gives way */
        if (f.resume) {
            resume = 1;
            ext_base = (ext_ticks + 7U) & ~7U;
        } else {
            ext_ticks = ext_base = 0;
            was_playing = 0;     /* restart below at step 1 */
        }
    }
    ext = f.state != CLOCK_OFF;
    unsigned playing = data ? (ext ? f.state == CLOCK_ARMED || f.state == CLOCK_RUNNING : ctl_playing) : 0U;
    view_playing = (uint8_t)playing;
    uint32_t increment = (uint32_t)ctl_bpm * 4U * JUNGLE_TICKS, reciprocal = ctl_reciprocal;
    if (playing != was_playing && resume && playing) was_playing = 1; /* no restart */
    if (playing != was_playing) {
        if (!ext) jungle_midi_out(playing ? 0xfaU : 0xfcU, now);
        run_ticks = 0;
        was_playing = (uint8_t)playing;
        for (unsigned i = 0; i < 2; ++i) if (voices[i].active) voices[i].releasing = 1;
        roll_now = 0;
        view_roll = -1;
        view_step = -1;
        if (playing) {
            rng ^= ctl_seed * 2654435761U;
            if (!rng) rng = 1;
            bar_tick = 0;
            pattern_ready = 0;
            tick_acc = TICK_THRESHOLD - increment;
        }
    }
    /* Locked to a MIDI clock: how far behind the loop's time line the break
     * is at this block's start (in ticks; the whole part exact), and the rate
     * for the block. */
    uint32_t rate = increment;
    unsigned wait = 0;
    if (ext && playing) {
        if (f.state != CLOCK_RUNNING) wait = 1;   /* armed: until the first pulse */
        else {
            float since = ((float)(int32_t)(now - f.t0) - f.t0_frac) * f.inv_per;   /* pulses since pulse f.pulse */
            float behind = (float)(int32_t)(ext_base + 8U * f.pulse + 1U - ext_ticks) + 8.0f * since -
                           (float)tick_acc * (1.0f / (float)TICK_THRESHOLD);
            if (behind > (float)LOCK_FAR) {
                for (int32_t n = (int32_t)behind; n > 0; --n) { tick_late = 0; sample_now = 0; on_tick(); ++ext_ticks; }
                tick_acc = 0;
            } else if (behind < -(float)LOCK_FAR) wait = 1;
            else {
                float steer = behind * LOCK_GAIN;
                steer = steer > 0.25f ? 0.25f : steer < -0.25f ? -0.25f : steer;
                rate = (uint32_t)(8.0f * f.inv_per * ((float)JUNGLE_TICK_HZ / JUNGLE_RATE) * (float)TICK_THRESHOLD *
                                  (1.0f + steer));
            }
        }
    }
    uint16_t shot = ctl_shot;
    if (shot != seen_shot && data) {
        seen_shot = shot;
        if (playing) shot_pending = (int8_t)(shot & 15U);
        else {
            tick_late = 0;
            trigger(shot & 15U, 0, FULL, 0, FULL, 0);
            last_slice = (uint8_t)(shot & 15U);
        }
    }
    /* The fill's filter sweep: from the knob's value (or from open, when the knob is on high-pass) down to
     * the lowest over the fill, linearly in the knob's steps; the knob's value again at the phrase's start. */
    unsigned filter = ctl_filter;
    int32_t filter_damp = ctl_filter_damp;
    unsigned sweeping = ctl_auto && fill_key && fill_sweep && was_playing && bar_tick >= fill_start;
    if (sweeping) {
        unsigned from = filter > FILTER_OPEN ? FILTER_OPEN : filter;
        unsigned span = BAR_TICKS - fill_start, done = bar_tick - fill_start;
        filter = from - from * done / span;
        if (filter == FILTER_OPEN) filter = FILTER_OPEN - 1U;   /* (open is a bypass: sweep from just below) */
        filter_damp = SWEEP_DAMP;
    }
    /* Glide the cutoff a quarter of the way per block: no zipper steps. */
    if (filter != FILTER_OPEN)
        filter_coefficient = filter_was_open ? (int32_t)filter_f[filter] :
            filter_coefficient + ((int32_t)filter_f[filter] - filter_coefficient) / 4;
    filter_was_open = filter == FILTER_OPEN;
    /* Instant effects: held, or the SEQ fill's from its first tick (taken a block at a time). */
    instant = ctl_instant;
    if (ctl_auto && fill_key >= FILL_INSTANT && bar_tick >= fill_start && was_playing)
        instant |= (uint8_t)(1U << (fill_key - FILL_INSTANT));
    view_instant = (uint8_t)(instant | (sweeping ? JUNGLE_FX_SWEEP : 0U));
    view_filter = (uint8_t)filter;
    int32_t fb_to = (instant & JUNGLE_FX_FEEDBACK) ? INSTANT_FB : ctl_comb_fb;
    int32_t fb_gain_to = (instant & JUNGLE_FX_FEEDBACK) ? INSTANT_FB_GAIN : ctl_comb_gain;
    comb_fb += (fb_to - comb_fb) / 4;
    comb_gain += (fb_gain_to - comb_gain) / 4;
    uint32_t comb_delay = ctl_comb_delay;
    if (eq_seen != eq_change) {
        eq_seen = eq_change;
        for (unsigned b = 0; b < 4; ++b)
            for (unsigned k = 0; k < 5; ++k) eq_c[b][k] = ctl_eq[b][k];
        eq_on = ctl_eq_on;
    }
    int32_t sat_drive = ctl_sat_drive, sat_makeup = ctl_sat_makeup;
    uint32_t lofi_rate = ctl_lofi_rate, lofi_shift = ctl_lofi_shift;
    if (instant & JUNGLE_FX_LOFI) {
        lofi_rate = (uint32_t)lofi_max_rate;
        lofi_shift = lofi_max_shift;
    }
    unsigned comp_on = ctl_comp_on;
    for (unsigned i = 0; i < frames; ++i) {
        sample_now = i;
        if (playing && !wait) {
            tick_acc += rate;
            while (tick_acc >= TICK_THRESHOLD) {
                tick_acc -= TICK_THRESHOLD;
                tick_late = umul_shr(tick_acc, reciprocal, 16);
                on_tick();
                ++ext_ticks;
            }
        }
        int32_t mix = 0;
        for (unsigned k = 0; k < 2; ++k)
            if (voices[k].active) mix += render_voice(&voices[k]);
        /* LO-FI: take a new sample only when the slow clock ticks, rounded
         * to the coarse step; hold it in between. */
        if (lofi_rate) {
            lofi_phase += lofi_rate;
            if (lofi_phase >= 65536U) {
                lofi_phase -= 65536U;
                int32_t half = lofi_shift ? 1 << (lofi_shift - 1U) : 0;
                lofi_held = ((mix + half) >> lofi_shift) << lofi_shift;
            }
            mix = lofi_held;
        }
        /* Comb. The line always runs, so raising the feedback starts clean. */
        int32_t y = mix + mul_shr(comb[(comb_write - comb_delay) & (COMB_SIZE - 1)], comb_fb, 15);
        y = y > (1 << 20) ? 1 << 20 : y < -(1 << 20) ? -(1 << 20) : y;
        comb[comb_write] = y;
        comb_write = (comb_write + 1U) & (COMB_SIZE - 1);
        mix = mul_shr(y, comb_gain, 15);
        /* EQ: direct form I, Q28 coefficients, rounded. */
        for (unsigned b = 0; eq_on && b < 4; ++b) {
            if (!(eq_on & (1U << b))) continue;
            int32_t *c = eq_c[b], *z = eq_z[b];
            int64_t acc = mul64(c[0], mix) + mul64(c[1], z[0]) + mul64(c[2], z[1])
                        - mul64(c[3], z[2]) - mul64(c[4], z[3]) + (1 << 27);
            int32_t out_b = (int32_t)(acc >> 28);
            z[1] = z[0]; z[0] = mix; z[3] = z[2]; z[2] = out_b;
            mix = out_b;
        }
        /* Saturation: a cubic soft clipper, full scale at +-32768. */
        if (sat_drive) {
            int32_t u = mul_shr(mix, sat_drive, 12);
            u = u > 32768 ? 32768 : u < -32768 ? -32768 : u;
            int32_t c3 = (int32_t)((mul64(u * u, u) >> 30) / 3); /* |u| <= 32768: u * u fits */
            mix = mul_shr((u - c3) * 3 / 2, sat_makeup, 12);
        }
        /* Compression: peak envelope, gain from the table, smoothed. */
        if (comp_on) {
            int32_t level = (mix < 0 ? -mix : mix) << 8;
            comp_env += mul_shr(level - comp_env, level > comp_env ? 1468 : 6, 15);
            int32_t index = comp_env >> 15;
            int32_t target = comp_table[index > 255 ? 255 : index];
            comp_gain = target < comp_gain ? target : comp_gain + (target - comp_gain) / 64;
            int32_t ahead = comp_delay[comp_write];
            comp_delay[comp_write] = mix;
            comp_write = (comp_write + 1U) & (COMP_AHEAD - 1U);
            mix = mul_shr(ahead, comp_gain, 12);
        }
        /* Half level inside the filter; 64-bit products, since EQ boosts,
         * feedback and resonance can all lift the band state. */
        int32_t x = mix >> 1;
        for (unsigned k = 0; k < 2; ++k) {
            filter_low += mul_add_shr(filter_coefficient, filter_band, 16384, 15);
            int32_t high = x - filter_low - mul_add_shr(filter_damp, filter_band, 8192, 14);
            filter_band += mul_add_shr(filter_coefficient, high, 16384, 15);
            if (k) x = filter < FILTER_OPEN ? filter_low : filter > FILTER_OPEN ? high : x;
        }
        mix = x << 1;
        out[2 * i] += mix << 7;
        out[2 * i + 1] += mix << 7;
#ifdef JUNGLE_TRACE
        ++jungle_host_frame;
#endif
    }
}
