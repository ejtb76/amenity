/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host test for the JUNGLE engine: renders scenarios to WAV and measures.
 *
 *   jumps  : sample-to-sample jumps larger than any in one uninterrupted
 *            playback of the loop (the reference), and how many sit right
 *            after a trigger, where a switching click would be
 *   timing : trigger frames of the straight pattern against the exact grid
 *   match  : straight pattern at the loop's own tempo against the reference
 *
 * Usage: jungle_host bank.bin out_dir  (build with JUNGLE_TRACE for the trigger log)
 * Controls are applied between 128-frame blocks, as on the device. */
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../app/jungle/jungle.h"

enum { BLOCK = 256, MAX_TRIGGERS = 4096 };   /* fm1-base's audio block */
extern uint32_t jungle_host_frame;

static struct { uint32_t frame; int slice, pitch, gain; } triggers[MAX_TRIGGERS];
static int trigger_count;
void jungle_host_trigger(uint32_t frame, unsigned slice, int pitch, int32_t gain)
{
    if (trigger_count < MAX_TRIGGERS) {
        triggers[trigger_count].frame = frame;
        triggers[trigger_count].slice = (int)slice;
        triggers[trigger_count].pitch = pitch;
        triggers[trigger_count].gain = gain;
        ++trigger_count;
    }
}

enum { EV_ROLL, EV_ROLL_OFF, EV_SHOT, EV_VARIATION, EV_AUTO, EV_FREEZE, EV_STRAIGHT, EV_TEMPO, EV_FILTER, EV_PITCH, EV_WARP, EV_STRETCH_HOLD, EV_STRETCH_GRAIN, EV_FEEDBACK, EV_FEEDBACK_DELAY, EV_STRETCH_SLOW, EV_EQ, EV_SATURATION, EV_COMPRESSION, EV_RESONANCE, EV_LOFI };
struct event { double step; int type, a, b; };
struct scenario {
    const char *name;
    unsigned bpm_x10, steps;
    const struct event *events;
    int count;
};

static const char *out_dir = "build/jungle-host";
static int src_max_jump, last_near;
/* MIDI out from the engine: counted per status. */
static unsigned midi_out_count[256];
static uint32_t midi_out_first_f8, midi_out_last_fa, midi_out_last_fc;
/* The timer's time of a frame (JUNGLE_TICK_HZ, wrapping as on the device). */
static uint32_t ticks_at(double frame) { return (uint32_t)(int64_t)floor(frame * ((double)JUNGLE_TICK_HZ / JUNGLE_RATE) + 0.5); }
void jungle_midi_out(unsigned status, uint32_t at)
{
    (void)at;
    if (status < 256) ++midi_out_count[status];
    if (status == 0xf8 && midi_out_count[0xf8] == 1) midi_out_first_f8 = jungle_host_frame;
    if (status == 0xfa) midi_out_last_fa = jungle_host_frame;
    if (status == 0xfc) midi_out_last_fc = jungle_host_frame;
}
/* Which white-key effect sounds, logged per block: frame and key (-1 none). */
static struct { uint32_t frame; int key; } effects[1024];
static int effect_count;
static int32_t *reference;
static unsigned reference_frames;

static uint8_t *load_bank(const char *path, unsigned *bytes, unsigned *frames)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bank = malloc((size_t)size);
    if (fread(bank, 1, (size_t)size, f) != (size_t)size) exit(1);
    fclose(f);
    uint32_t entry[3];
    memcpy(entry, bank + 32, sizeof entry); /* slot 1: offset, frames, bytes */
    *frames = entry[1];
    *bytes = entry[2];
    printf("bank %s: slot 1 \"%.15s\", %u frames\n", path, (const char *)bank + 44, *frames);
    return bank + entry[0];
}

static void write_wav(const char *name, const int32_t *q23, unsigned frames)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    uint32_t data = frames * 4U, riff = 36U + data, rate = JUNGLE_RATE, bytes = rate * 4U;
    uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
    uint32_t sixteen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&sixteen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&bytes, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (unsigned i = 0; i < frames * 2U; ++i) {
        int32_t v = q23[i] >> 8;
        int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

static void apply(const struct event *e)
{
    switch (e->type) {
    case EV_ROLL: jungle_roll((unsigned)e->a); break;
    case EV_ROLL_OFF: jungle_roll_off(); break;
    case EV_SHOT: jungle_one_shot((unsigned)e->a); break;
    case EV_VARIATION: jungle_variation((unsigned)e->a); break;
    case EV_AUTO: jungle_auto_roll((unsigned)e->a); break;
    case EV_FREEZE: jungle_freeze((unsigned)e->a); break;
    case EV_STRAIGHT: jungle_straight(); break;
    case EV_TEMPO: jungle_tempo((unsigned)e->a); break;
    case EV_FILTER: jungle_filter((unsigned)e->a); break;
    case EV_PITCH: jungle_pitch(e->a); break;
    case EV_WARP: jungle_warp((unsigned)e->a); break;
    case EV_STRETCH_HOLD: jungle_stretch_hold((unsigned)e->a); break;
    case EV_STRETCH_GRAIN: jungle_stretch_grain((unsigned)e->a, (unsigned)e->b); break;
    case EV_FEEDBACK: jungle_feedback((unsigned)e->a); break;
    case EV_FEEDBACK_DELAY: jungle_feedback_delay((unsigned)e->a); break;
    case EV_STRETCH_SLOW: jungle_stretch_slow((unsigned)e->a); break;
    case EV_EQ: jungle_eq((unsigned)e->a, e->b); break;
    case EV_SATURATION: jungle_saturation((unsigned)e->a); break;
    case EV_COMPRESSION: jungle_compression((unsigned)e->a); break;
    case EV_RESONANCE: jungle_filter_resonance((unsigned)e->a); break;
    case EV_LOFI: jungle_lofi((unsigned)e->a); break;
    }
}

/* Renders one scenario; returns the buffer (mono view in amen units in *mono). */
static int32_t *render(const struct scenario *sc, unsigned *frames_out, int verbose)
{
    double frames_per_step = JUNGLE_RATE * 60.0 / (sc->bpm_x10 / 10.0 * 4.0);
    unsigned frames = (unsigned)(sc->steps * frames_per_step) / BLOCK * BLOCK + BLOCK;
    int32_t *buf = calloc((size_t)frames * 2U, sizeof *buf);
    jungle_init();
    jungle_tempo(sc->bpm_x10);
    jungle_variation(0);
    jungle_auto_roll(0);
    jungle_freeze(0);
    jungle_roll_off();
    jungle_pitch(0);
    jungle_filter(64);
    jungle_warp(0);
    jungle_stretch_hold(0);
    jungle_stretch_grain(38, 100);
    jungle_stretch_slow(4);
    jungle_feedback(0);
    jungle_feedback_note(4);
    for (unsigned b = 0; b < 4; ++b) jungle_eq(b, 0);
    jungle_saturation(0);
    jungle_compression(0);
    jungle_filter_resonance(8);
    jungle_lofi(0);
    jungle_seed(12345);
    jungle_host_frame = 0;
    trigger_count = 0;
    effect_count = 0;
    int last_effect = -1;
    int next = 0;
    /* Settings at step 0 apply before playback starts. */
    while (next < sc->count && sc->events[next].step <= 0.0) apply(&sc->events[next++]);
    jungle_play(1);
    for (unsigned at = 0; at < frames; at += BLOCK) {
        while (next < sc->count && sc->events[next].step * frames_per_step <= at)
            apply(&sc->events[next++]);
        jungle_render(buf + 2U * at, BLOCK, ticks_at(at));
        int effect = jungle_roll_active();
        if (effect != last_effect && effect_count < 1024) {
            effects[effect_count].frame = at;
            effects[effect_count].key = effect;
            ++effect_count;
            last_effect = effect;
        }
    }
    jungle_play(0);
    *frames_out = frames;
    (void)verbose;
    return buf;
}

/* The whole loop once, uninterrupted, at bpm; its largest jump is the
 * baseline for the scenarios at that tempo. */
static int32_t *render_reference(unsigned bpm_x10, unsigned source_frames, unsigned *frames, int *max_jump)
{
    jungle_init();
    jungle_tempo(bpm_x10);
    jungle_play(0);
    jungle_one_shot(0);
    *frames = (unsigned)((uint64_t)source_frames * JUNGLE_RATE / 32000U * jungle_native_bpm_x10() / bpm_x10) / BLOCK * BLOCK;
    int32_t *buf = calloc((size_t)*frames * 2U, sizeof *buf);
    for (unsigned at = 0; at < *frames; at += BLOCK) jungle_render(buf + 2U * at, BLOCK, ticks_at(at));
    *max_jump = 0;
    for (unsigned i = 1; i < *frames; ++i) {
        int jump = abs((buf[2 * i] >> 7) - (buf[2 * i - 2] >> 7));
        if (jump > *max_jump) *max_jump = jump;
    }
    return buf;
}

static void measure_clicks(const char *name, const int32_t *buf, unsigned frames, int *count, double *worst)
{
    int clicks = 0, near = 0;
    double max_ratio = 0;
    for (unsigned i = 1; i < frames; ++i) {
        int jump = abs((buf[2 * i] >> 7) - (buf[2 * i - 2] >> 7));
        double ratio = (double)jump / src_max_jump;
        if (ratio > max_ratio) max_ratio = ratio;
        if (jump > src_max_jump) {
            ++clicks;
            /* A switching click sits within a fade of a trigger. */
            for (int t = 0; t < trigger_count; ++t)
                if (i >= triggers[t].frame && i < triggers[t].frame + 64U) { ++near; break; }
        }
    }
    (void)name;
    *count = clicks;
    last_near = near;
    *worst = max_ratio;
}

static const char *roll_names[JUNGLE_ROLL_KEYS] = {
    "1-4-repeat", "1-4-build", "1-8T-repeat", "1-8D-echo",
    "1-8-repeat", "1-8-down", "1-8-up", "1-8-shrink",
    "1-16-repeat", "1-16-down", "1-16-up", "1-16-shrink",
    "1-32-repeat", "1-32-down", "1-32-up", "1-32-shrink",
};

/* MIDI clock in: Start, then pulses on a steady grid at 172.5 BPM and from
 * 30 s at 175, each up to 1 ms early or late (as USB MIDI delivers them).
 * With burst, the sender only sends once per audio buffer of that many
 * frames, as some DAWs do: every pulse waits for the end of its buffer. As on
 * fm1-base, the main loop reads what has come in now and then (every 0.3 to
 * 3 ms, and one pass in twelve held up 8 ms by the screen), and stamps it
 * with the time it read it; the audio runs in blocks of 256 frames. The
 * panel's rule shows the tempo the clock reports.
 * Reports the straight pattern's hits against the grid once settled (from
 * 10 s, and not in the 8 s after the tempo change): mean, spread and the
 * worst change from one 8th to the next (what the ear hears as a shaky
 * groove); how soon it locks; the tempo readings and their spread; and that
 * Stop stops. */
static void clock_follow(const char *name, unsigned burst)
{
    enum { SECONDS = 60, CHANGE_PULSE = 24 * 86 }; /* 86 beats at 172.5 = 29.9 s */
    const double p1 = JUNGLE_RATE * 60.0 / (172.5 * 24.0), p2 = JUNGLE_RATE * 60.0 / (175.0 * 24.0);
    unsigned frames = JUNGLE_RATE * SECONDS / BLOCK * BLOCK;
    int32_t *buf = calloc((size_t)frames * 2U, sizeof *buf);
    jungle_init();
    jungle_tempo(1600);
    jungle_variation(0);
    jungle_roll_off();
    jungle_play(0);
    jungle_host_frame = 0;
    trigger_count = 0;
    srand(7);
#define PULSE_AT(i) ((i) < CHANGE_PULSE ? 2000.0 + (i) * p1 : 2000.0 + CHANGE_PULSE * p1 + ((i) - CHANGE_PULSE) * p2)
#define DELIVER(t) (burst ? ceil((t) / burst) * burst : (t))
    unsigned pulse_index = 0; /* Start at frame 1900, first pulse at 2000 */
    double next_pulse = DELIVER(PULSE_AT(0)), poll = 0.0;
    unsigned started = 0, stopped = 0, tempo = 1600, reading[4] = {0};
    unsigned lo[2] = {9999, 9999}, hi[2] = {0, 0};
    uint32_t stop_at = (uint32_t)(JUNGLE_RATE * 58), change_at = (uint32_t)PULSE_AT(CHANGE_PULSE);
    for (unsigned at = 0; at < frames; at += BLOCK) {
        /* The main loop's passes before this block's interrupt. */
        for (; poll < at; poll += (rand() % 12 ? 0.3 + (rand() % 1000) * 0.0027 : 8.0) * JUNGLE_RATE / 1000.0) {
            uint32_t t = ticks_at(poll);
            if (!started && poll >= 1900) { jungle_midi_realtime(0xfa, 0, t); started = 1; }
            while (started && !stopped && next_pulse <= poll && poll < stop_at) {
                jungle_midi_realtime(0xf8, 0, t);
                ++pulse_index;
                next_pulse = DELIVER(PULSE_AT(pulse_index) + (rand() % 89 - 44));
            }
            if (!stopped && poll >= stop_at) { jungle_midi_realtime(0xfc, 0, t); stopped = 1; }
            unsigned ignore;
            (void)jungle_clock(&ignore, t);   /* the end of the pass */
        }
        jungle_render(buf + 2U * at, BLOCK, ticks_at(at));
        unsigned locked;
        if (jungle_clock(&locked, ticks_at(at)) && locked != tempo) {
            tempo = locked;
            jungle_tempo(tempo);
            if (getenv("CLOCK_DUMP")) printf("  t %5.2f s tempo %u\n", at / 44100.0, tempo);
        }
        static const unsigned when[4] = {5, 29, 32, 57};
        for (unsigned k = 0; k < 4; ++k) if (!reading[k] && at >= JUNGLE_RATE * when[k]) reading[k] = tempo;
        for (unsigned k = 0; k < 2; ++k)
            if (at >= JUNGLE_RATE * (20U + 28U * k) && at < JUNGLE_RATE * (29U + 28U * k)) {
                if (tempo < lo[k]) lo[k] = tempo;
                if (tempo > hi[k]) hi[k] = tempo;
            }
    }
    /* Every 8th of the straight pattern is a hit, 12 pulses apart. */
    double sum = 0, low = 1e9, high = -1e9, step_worst = 0, last_off = 0;
    int n = 0, have_last = 0;
    for (int t = 0; t < trigger_count; ++t) {
        double ideal = PULSE_AT(12.0 * t), off = triggers[t].frame - ideal;
        if (triggers[t].frame > stop_at) break;
        if (getenv("CLOCK_DUMP")) printf("  hit %3d at %7u off %+5.0f\n", t, triggers[t].frame, off);
        int settled = triggers[t].frame > JUNGLE_RATE * 10U &&
                      (triggers[t].frame < change_at || triggers[t].frame > change_at + JUNGLE_RATE * 8U);
        if (!settled) { have_last = 0; continue; }
        if (off < low) low = off;
        if (off > high) high = off;
        if (have_last && fabs(off - last_off) > step_worst) step_worst = fabs(off - last_off);
        last_off = off;
        have_last = 1;
        sum += off;
        ++n;
    }
    /* Locked: from the last hit more than 5 ms from the settled mean, after
     * the start and after the change. */
    double locked_at[2] = {0, 0};
    for (int t = 0; n && t < trigger_count && triggers[t].frame <= stop_at; ++t)
        if (fabs(triggers[t].frame - PULSE_AT(12.0 * t) - sum / n) > 0.005 * JUNGLE_RATE) {
            int part = triggers[t].frame > change_at;
            locked_at[part] = (triggers[t].frame - (part ? change_at : 2000.0)) / JUNGLE_RATE;
        }
#undef DELIVER
#undef PULSE_AT
    int after_stop = 0;
    for (int t = 0; t < trigger_count; ++t) after_stop += triggers[t].frame > stop_at + 2 * BLOCK;
    const double ms = 1000.0 / JUNGLE_RATE;
    printf("\n%-22s clock 172.5 then 175 BPM, +-1 ms: first hit at frame %u (first pulse 2000); settled, off the grid: "
           "mean %+.1f ms, %+.1f..%+.1f ms, worst change 8th to 8th %.1f ms\n",
           name, trigger_count ? triggers[0].frame : 0,
           n ? sum / n * ms : 0.0, low * ms, high * ms, step_worst * ms);
    printf("%-22s tempo at 5 s %.1f, 29 s %.1f (20-29 s: %.1f..%.1f), 32 s %.1f, 57 s %.1f (48-57 s: %.1f..%.1f)\n", "",
           reading[0] / 10.0, reading[1] / 10.0, lo[0] / 10.0, hi[0] / 10.0, reading[2] / 10.0, reading[3] / 10.0,
           lo[1] / 10.0, hi[1] / 10.0);
    printf("%-22s within 5 ms of that from %.1f s after Start and %.1f s after the change\n", "",
           locked_at[0], locked_at[1]);
    printf("%-22s hits after Stop: %d; following at the end: %d\n", "", after_stop,
           jungle_clock(&(unsigned){0}, ticks_at(frames)));
    write_wav(name, buf, frames);
    free(buf);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: jungle_host bank.bin out_dir\n"); return 2; }
    out_dir = argv[2];
    unsigned bytes, frames_in_bank;
    const uint8_t *sea = load_bank(argv[1], &bytes, &frames_in_bank);
    if (jungle_bind(sea, bytes, frames_in_bank)) { fprintf(stderr, "bind failed\n"); return 1; }
    unsigned native = jungle_native_bpm_x10();

    reference = render_reference(native, frames_in_bank, &reference_frames, &src_max_jump);
    write_wav("reference", reference, reference_frames);
    printf("loop at %.1f BPM, largest sample jump in the reference %d\n\n", native / 10.0, src_max_jump);

    /* 1. Straight pattern at the native tempo must reproduce the loop. */
    {
        static const struct event none[1];
        struct scenario sc = { "straight-160", native, 64, none, 0 };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        /* Per slice, the best of -1/0/+1 samples: triggers land on whole
         * samples, slice starts are 8268.75 frames apart. */
        double err = 0, sig = 0;
        unsigned slice = reference_frames / JUNGLE_SLICES;
        for (unsigned k = 1; k < JUNGLE_SLICES - 1U; ++k) {
            double best = -1;
            for (int lag = -1; lag <= 1; ++lag) {
                double e = 0;
                for (unsigned i = k * slice + 2U; i < (k + 1U) * slice - 2U; ++i) {
                    double want = reference[2 * (i + lag)] >> 7, got = buf[2 * i] >> 7;
                    e += (got - want) * (got - want);
                }
                if (best < 0 || e < best) best = e;
            }
            err += best;
            for (unsigned i = k * slice + 2U; i < (k + 1U) * slice - 2U; ++i)
                sig += (double)(reference[2 * i] >> 7) * (reference[2 * i] >> 7);
        }
        int clicks; double worst;
        measure_clicks(sc.name, buf, frames, &clicks, &worst);
        printf("%-22s match to the reference: %.1f dB difference, jumps %d (worst %.2fx)\n",
               sc.name, 10 * log10(err / sig), clicks, worst);
        write_wav(sc.name, buf, frames);
        free(buf);
    }

    /* Everything below plays at 170 BPM: the baseline follows. */
    {
        unsigned frames;
        free(render_reference(1700, frames_in_bank, &frames, &src_max_jump));
        printf("\nat 170 BPM the reference's largest sample jump is %d\n", src_max_jump);
    }

    /* 2. Timing: straight pattern at 170 BPM, every 8th on the exact grid. */
    {
        static const struct event none[1];
        struct scenario sc = { "straight-170", 1700, 256, none, 0 };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        double frames_per_step = JUNGLE_RATE * 60.0 / (170.0 * 4.0), worst = 0;
        for (int t = 0; t < trigger_count; ++t) {
            double want = 2 * t * frames_per_step;
            double off = fabs(triggers[t].frame - want);
            if (off > worst) worst = off;
        }
        int clicks; double jw;
        measure_clicks(sc.name, buf, frames, &clicks, &jw);
        printf("%-22s %d triggers over 8 loops, worst timing error %.2f samples, jumps %d (worst %.2fx)\n",
               sc.name, trigger_count, worst, clicks, jw);
        write_wav(sc.name, buf, frames);
        free(buf);
    }

    /* 3. Variation with swing, 8 loops. */
    {
        static const struct event ev[] = {
            { 0, EV_VARIATION, 90, 0 },
        };
        struct scenario sc = { "variation-170", 1700, 256, ev, 2 };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        int clicks; double worst;
        measure_clicks(sc.name, buf, frames, &clicks, &worst);
        printf("%-22s %d triggers, jumps %d (worst %.2fx, %d at a trigger)\n", sc.name, trigger_count, clicks, worst, last_near);
        write_wav(sc.name, buf, frames);
        free(buf);
    }

    /* 4. Every roll: pattern, roll held from step 16.3 to 36.3 (a bit late, to
     *    test the start grid, and long enough for build), then the pattern. */
    printf("\nrolls at 170 BPM (held from step 16.3 to 36.3):\n");
    for (int key = 0; key < JUNGLE_ROLL_KEYS; ++key) {
        struct event ev[] = { { 16.3, EV_ROLL, key, 0 }, { 36.3, EV_ROLL_OFF, 0, 0 } };
        char name[64];
        snprintf(name, sizeof name, "roll-%s", roll_names[key]);
        struct scenario sc = { name, 1700, 44, ev, 2 };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        double fps = JUNGLE_RATE * 60.0 / (170.0 * 4.0);
        int first = -1, rolls = 0;
        for (int t = 0; t < trigger_count; ++t)
            if (triggers[t].frame >= 16.3 * fps && triggers[t].frame < 36.3 * fps) {
                if (first < 0) first = t;
                ++rolls;
            }
        int clicks; double worst;
        measure_clicks(name, buf, frames, &clicks, &worst);
        printf("  %-18s %2d hits at steps", name, rolls);
        for (int t = first, k = 0; first >= 0 && t < first + rolls && k < 10; ++t, ++k)
            printf(" %.2f", triggers[t].frame / fps);
        printf("%s\n  %-18s pitches", rolls > 10 ? " ..." : "", "");
        for (int t = first, k = 0; first >= 0 && t < first + rolls && k < 10; ++t, ++k)
            printf(" %+d", triggers[t].pitch);
        printf("%s, jumps %d (worst %.2fx, %d at a trigger)\n", rolls > 10 ? " ..." : "", clicks, worst, last_near);
        write_wav(name, buf, frames);
        free(buf);
    }

    /* 5. A demo of all sixteen: 8 steps pattern, 16 steps roll, each. */
    {
        struct event ev[64];
        int n = 0;
        for (int k = 0; k < 16; ++k) {
            ev[n++] = (struct event){ 24.0 * k + 8, EV_ROLL, k, 0 };
            ev[n++] = (struct event){ 24.0 * k + 24, EV_ROLL_OFF, 0, 0 };
        }
        struct scenario sc = { "demo-all-rolls", 1700, 16 * 24 + 8, ev, n };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        int clicks; double worst;
        measure_clicks(sc.name, buf, frames, &clicks, &worst);
        printf("\n%-22s 4 groups x 4 variants, jumps %d (worst %.2fx, %d at a trigger)\n", sc.name, clicks, worst, last_near);
        write_wav(sc.name, buf, frames);
        free(buf);
    }

    /* 6. SEQ fill: 16 phrases, one effect at the end of each. Every fill
     *    must start 8, 4 or 2 steps before the phrase ends (on the grid,
     *    within a 32nd), and 1/32 rolls only in the last 2 steps. */
    {
        static const struct event ev[] = { { 0, EV_VARIATION, 60, 0 }, { 0, EV_AUTO, 1, 0 } };
        struct scenario sc = { "seq-fill-170", 1700, 32 * 16, ev, 2 };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        double fps = JUNGLE_RATE * 60.0 / (170.0 * 4.0);
        int fills = 0, wrong = 0;
        printf("%-22s fills (phrase step: key)", sc.name);
        for (int k = 0; k < effect_count; ++k) {
            if (effects[k].key < 0) continue;
            double at = fmod(effects[k].frame / fps, 32.0);
            int key = effects[k].key, length = (int)(32.0 - at + 0.75);
            int ok = (length == 8 || length == 4 || length == 2) && !(key >= 12 && length != 2) &&
                     !(length == 2 && key < 8 && key != 2);
            wrong += !ok;
            ++fills;
            printf(" %.1f:%s%d", at, ok ? "" : "!", key);
        }
        int clicks; double worst;
        measure_clicks(sc.name, buf, frames, &clicks, &worst);
        printf("\n%-22s %d fills in 16 phrases, %d breaking the rules; jumps %d (worst %.2fx)\n", "", fills, wrong, clicks, worst);
        write_wav(sc.name, buf, frames);
        free(buf);
    }

    /* 8. DJ filter: a bar at each setting, level per bar against the open
     *    filter, plus a knob turned one detent per 16th from open to closed. */
    {
        static const int settings[] = { 64, 56, 40, 20, 0, 64, 72, 90, 110, 127, 64 };
        enum { N = sizeof settings / sizeof settings[0] };
        struct event ev[N + 64];
        int n = 0;
        for (int k = 0; k < N; ++k) ev[n++] = (struct event){ 16.0 * k + 0.01, EV_FILTER, settings[k], 0 };
        for (int k = 0; k < 64; ++k) ev[n++] = (struct event){ 16.0 * N + k, EV_FILTER, 64 - k, 0 };
        struct scenario sc = { "filter-170", 1700, 16 * N + 64, ev, n };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        int clicks; double worst;
        measure_clicks(sc.name, buf, frames, &clicks, &worst);
        double fps = JUNGLE_RATE * 60.0 / (170.0 * 4.0), open = 0;
        printf("\n%-22s level per bar:", sc.name);
        for (int k = 0; k < N; ++k) {
            double sum = 0;
            unsigned a = (unsigned)(16.0 * k * fps), b = (unsigned)(16.0 * (k + 1) * fps);
            for (unsigned i = a; i < b; ++i) sum += (double)(buf[2 * i] >> 7) * (buf[2 * i] >> 7);
            double db = 10 * log10(sum / (b - a) + 1e-9);
            if (k == 0) open = db;
            printf(" %d:%+.1f", settings[k], db - open);
        }
        printf(" dB\n%-22s %u Hz .. %u Hz, jumps %d (worst %.2fx, %d at a trigger)\n", "",
               jungle_filter_hz(0), jungle_filter_hz(127), clicks, worst, last_near);
        write_wav(sc.name, buf, frames);
        free(buf);
    }

    /* 9. STRETCH. Two bars plain, two bars with every hit stretch, for each
     *    setting; level per two bars against the plain ones. */
    {
        struct stretch_ab { const char *name; int piece[4], fade[4], slow[4]; };
        static const struct stretch_ab abs[] = {
            { "stretch-slow-175", { 38, 38, 38, 38 }, { 100, 100, 100, 100 }, { 2, 3, 4, 4 } },
        };
        for (unsigned m = 0; m < 1; ++m) {
            struct event ev[20];
            int n = 0;
            for (int k = 0; k < 4; ++k) {
                ev[n++] = (struct event){ 64.0 * k + 32 - 0.5, EV_STRETCH_GRAIN, abs[m].piece[k], abs[m].fade[k] };
                ev[n++] = (struct event){ 64.0 * k + 32 - 0.5, EV_STRETCH_SLOW, abs[m].slow[k], 0 };
                ev[n++] = (struct event){ 64.0 * k + 32 - 0.5, EV_STRETCH_HOLD, 1, 0 };
                ev[n++] = (struct event){ 64.0 * k + 64 - 0.5, EV_STRETCH_HOLD, 0, 0 };
            }
            struct scenario sc = { abs[m].name, 1750, 256, ev, n };
            unsigned frames;
            int32_t *buf = render(&sc, &frames, 0);
            int clicks; double worst;
            measure_clicks(sc.name, buf, frames, &clicks, &worst);
            double fps = JUNGLE_RATE * 60.0 / (175.0 * 4.0), plain = 0;
            printf("%s%-22s", m ? "" : "\n", sc.name);
            for (int k = 0; k < 8; ++k) {
                double sum = 0;
                unsigned a = (unsigned)(32.0 * k * fps), b = (unsigned)(32.0 * (k + 1) * fps);
                for (unsigned i = a; i < b && i < frames; ++i) sum += (double)(buf[2 * i] >> 7) * (buf[2 * i] >> 7);
                double db = 10 * log10(sum / (b - a) + 1e-9);
                if (k == 0) plain = db;
                if (k & 1) printf(" %dx slower: %+.1f dB", abs[m].slow[k / 2], db - plain);
            }
            printf("\n%-22s jumps %d (worst %.2fx)\n", "", clicks, worst);
            write_wav(sc.name, buf, frames);
            free(buf);
        }

        /* As a variation: stretch hits picked by the pattern. */
        static const struct event var[] = { { 0, EV_VARIATION, 50, 0 }, { 0, EV_WARP, 70, 0 } };
        struct scenario sv = { "warp-variation-175", 1750, 256, var, 2 };
        unsigned frames;
        int32_t *bv = render(&sv, &frames, 0);
        int clicks; double worst;
        measure_clicks(sv.name, bv, frames, &clicks, &worst);
        printf("%-22s 8 loops, variation 50, warp 70; jumps %d (worst %.2fx)\n", sv.name, clicks, worst);
        write_wav(sv.name, bv, frames);
        free(bv);

        /* Stretch held over rolls: a 1/8 down roll, then a 1/16 repeat. */
        static const struct event mr[] = {
            { 0, EV_STRETCH_GRAIN, 38, 100 }, { 8, EV_STRETCH_HOLD, 1, 0 },
            { 8, EV_ROLL, 5, 0 }, { 16, EV_ROLL, 8, 0 }, { 24, EV_ROLL_OFF, 0, 0 }, { 24, EV_STRETCH_HOLD, 0, 0 } };
        struct scenario sr = { "stretch-rolls-175", 1750, 40, mr, 6 };
        int32_t *br = render(&sr, &frames, 0);
        measure_clicks(sr.name, br, frames, &clicks, &worst);
        printf("%-22s jumps %d (worst %.2fx)\n", sr.name, clicks, worst);
        write_wav(sr.name, br, frames);
        free(br);

        /* Cost: the same 64 bars plain and all stretch. */
        static const struct event cost_on[] = { { 0, EV_STRETCH_HOLD, 1, 0 }, { 0, EV_VARIATION, 90, 60 } };
        static const struct event cost_off[] = { { 0, EV_VARIATION, 90, 60 } };
        clock_t c0 = clock();
        struct scenario s4 = { "cost", 1750, 1024, cost_off, 1 };
        free(render(&s4, &frames, 0));
        clock_t c1 = clock();
        struct scenario s5 = { "cost", 1750, 1024, cost_on, 2 };
        free(render(&s5, &frames, 0));
        clock_t c2 = clock();
        printf("%-22s render time all stretch / plain: %.2fx\n", "stretch-cost", (double)(c2 - c1) / (double)(c1 - c0));

        /* The worst case: every hit stretched, feedback, all four EQ bands,
         * saturation and compression at once. */
        static const struct event all_on[] = {
            { 0, EV_STRETCH_HOLD, 1, 0 }, { 0, EV_VARIATION, 90, 60 }, { 0, EV_FEEDBACK, 90, 0 },
            { 0, EV_EQ, 0, 6 }, { 0, EV_EQ, 1, -6 }, { 0, EV_EQ, 2, 6 }, { 0, EV_EQ, 3, -6 },
            { 0, EV_SATURATION, 64, 0 }, { 0, EV_COMPRESSION, 64, 0 } };
        clock_t c3 = clock();
        struct scenario s6 = { "cost", 1750, 1024, all_on, 9 };
        free(render(&s6, &frames, 0));
        clock_t c4 = clock();
        printf("%-22s render time everything on / plain: %.2fx\n", "full-cost", (double)(c4 - c3) / (double)(c1 - c0));
    }

    /* 10. Feedback comb: two bars plain, two at 70% for 3, 6 and 12 ms;
     *     then a sweep 0..90% at 6 ms, a step per bar. Level against plain. */
    {
        static const int delays[] = { 30, 60, 120 };
        struct event ev[16];
        int n = 0;
        for (int k = 0; k < 3; ++k) {
            ev[n++] = (struct event){ 64.0 * k + 32 - 0.5, EV_FEEDBACK_DELAY, delays[k], 0 };
            ev[n++] = (struct event){ 64.0 * k + 32 - 0.5, EV_FEEDBACK, 99, 0 };
            ev[n++] = (struct event){ 64.0 * k + 64 - 0.5, EV_FEEDBACK, 0, 0 };
        }
        struct scenario sc = { "feedback-175", 1750, 192, ev, n };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        double fps = JUNGLE_RATE * 60.0 / (175.0 * 4.0), plain = 0;
        printf("\n%-22s", sc.name);
        for (int k = 0; k < 6; ++k) {
            double sum = 0;
            unsigned a = (unsigned)(32.0 * k * fps), b = (unsigned)(32.0 * (k + 1) * fps);
            for (unsigned i = a; i < b && i < frames; ++i) sum += (double)(buf[2 * i] >> 7) * (buf[2 * i] >> 7);
            double db = 10 * log10(sum / (b - a) + 1e-9);
            if (k == 0) plain = db;
            if (k & 1) printf(" 70%% at %.0f ms: %+.1f dB", delays[k / 2] / 10.0, db - plain);
        }
        int clicks; double worst;
        measure_clicks(sc.name, buf, frames, &clicks, &worst);
        printf("\n%-22s jumps %d (worst %.2fx)\n", "", clicks, worst);
        write_wav(sc.name, buf, frames);
        free(buf);

        struct event sw[16];
        n = 0;
        for (int k = 0; k < 8; ++k) sw[n++] = (struct event){ 16.0 * k + 0.01, EV_FEEDBACK, k * 127 / 7, 0 };
        struct scenario ss = { "feedback-sweep-175", 1750, 128, sw, n };
        buf = render(&ss, &frames, 0);
        printf("%-22s level per bar, 0 to 100%%:", ss.name);
        for (int k = 0; k < 8; ++k) {
            double sum = 0;
            unsigned a = (unsigned)(16.0 * k * fps), b = (unsigned)(16.0 * (k + 1) * fps);
            for (unsigned i = a; i < b && i < frames; ++i) sum += (double)(buf[2 * i] >> 7) * (buf[2 * i] >> 7);
            double db = 10 * log10(sum / (b - a) + 1e-9);
            if (k == 0) plain = db;
            printf(" %+.1f", db - plain);
        }
        measure_clicks(ss.name, buf, frames, &clicks, &worst);
        printf(" dB\n%-22s jumps %d (worst %.2fx)\n", "", clicks, worst);
        write_wav(ss.name, buf, frames);
        free(buf);
    }

    /* 11. Letting go of a roll a little early: the roll carries on to the
     *     pattern's next hit, so the hits stay a 16th apart throughout. */
    {
        static const struct event ev[] = { { 16.3, EV_ROLL, 8, 0 }, { 22.9, EV_ROLL_OFF, 0, 0 } };
        struct scenario sc = { "roll-release-early", 1750, 32, ev, 2 };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        double fps = JUNGLE_RATE * 60.0 / (175.0 * 4.0), widest = 0;
        printf("\n%-22s 1/16 roll released at step 22.9, hits at steps", sc.name);
        for (int t = 0; t < trigger_count; ++t) {
            double at = triggers[t].frame / fps;
            if (at > 20.5 && at < 28.5) printf(" %.2f", at);
            if (t && at > 16.5 && at < 28.5 && at - triggers[t - 1].frame / fps > widest)
                widest = at - triggers[t - 1].frame / fps;
        }
        printf("; widest gap %.2f steps\n", widest);
        write_wav(sc.name, buf, frames);
        free(buf);
    }

    /* 12. EQ, saturation, compression: two bars plain, then two bars per
     *     setting. Level (RMS) and crest (peak / RMS) against plain; the
     *     EQ's spectrum change goes to fx_analysis.py. */
    {
        struct fx_run { const char *name; int type, band; int value[8]; int count; };
        static const struct fx_run runs[] = {
            { "fx-eq-175", EV_EQ, -1, { 0 }, 8 },
            { "fx-saturation-175", EV_SATURATION, 0, { 0, 32, 64, 96, 127 }, 5 },
            { "fx-compression-175", EV_COMPRESSION, 0, { 0, 32, 64, 96, 127 }, 5 },
            { "fx-lofi-175", EV_LOFI, 0, { 0, 32, 64, 96, 127 }, 5 },
        };
        /* EQ run: bands 0..3 at +12 then -12. */
        static const int eq_band[8] = { 0, 0, 1, 1, 2, 2, 3, 3 }, eq_db[8] = { 12, -12, 12, -12, 12, -12, 12, -12 };
        for (unsigned r = 0; r < 4; ++r) {
            const struct fx_run *fx = &runs[r];
            struct event ev[24];
            int n = 0;
            for (int k = 0; k < fx->count; ++k) {
                double at = 64.0 * k + 32 - 0.5;
                if (fx->type == EV_EQ) {
                    ev[n++] = (struct event){ at, EV_EQ, eq_band[k], eq_db[k] };
                    ev[n++] = (struct event){ at + 32, EV_EQ, eq_band[k], 0 };
                } else {
                    ev[n++] = (struct event){ at, fx->type, fx->value[k], 0 };
                    ev[n++] = (struct event){ at + 32, fx->type, 0, 0 };
                }
            }
            struct scenario sc = { fx->name, 1750, 64 * fx->count, ev, n };
            unsigned frames;
            int32_t *buf = render(&sc, &frames, 0);
            double fps = JUNGLE_RATE * 60.0 / (175.0 * 4.0), plain_rms = 0, plain_crest = 0;
            printf("%s%-22s", r ? "" : "\n", sc.name);
            for (int k = 0; k < 2 * fx->count; ++k) {
                double sum = 0, peak = 0;
                unsigned a = (unsigned)(32.0 * k * fps), b = (unsigned)(32.0 * (k + 1) * fps);
                for (unsigned i = a; i < b && i < frames; ++i) {
                    double v = buf[2 * i] >> 7;
                    sum += v * v;
                    if (fabs(v) > peak) peak = fabs(v);
                }
                double rms = sqrt(sum / (b - a)), db = 20 * log10(rms + 1e-9), crest = 20 * log10(peak / (rms + 1e-9));
                if (k == 0) { plain_rms = db; plain_crest = crest; }
                if (!(k & 1)) continue;
                if (fx->type == EV_EQ) printf(" b%d%+d:%+.1f", eq_band[k / 2], eq_db[k / 2], db - plain_rms);
                else if (fx->type == EV_LOFI) {
                    unsigned hz, bits;
                    jungle_lofi_view((unsigned)fx->value[k / 2], &hz, &bits);
                    printf(" %d (%u Hz %u bit): %+.1f dB", fx->value[k / 2], hz, bits, db - plain_rms);
                } else printf(" %d: %+.1f dB crest %+.1f", fx->value[k / 2], db - plain_rms, crest - plain_crest);
            }
            int clicks; double worst;
            measure_clicks(sc.name, buf, frames, &clicks, &worst);
            printf("\n%-22s jumps %d (worst %.2fx)\n", "", clicks, worst);
            write_wav(sc.name, buf, frames);
            free(buf);
        }
    }

    /* 13. Filter sweeps for three Q values: a bar plain, then the knob two
     *     values per 16th from off down to 0 (low-pass), back to off, up to
     *     127 (high-pass) and back. Level per bar against the plain one. */
    {
        static const int qs[] = { 8, 12, 14 };
        for (unsigned q = 0; q < 3; ++q) {
            struct event ev[300];
            int n = 0;
            ev[n++] = (struct event){ 0, EV_RESONANCE, qs[q], 0 };
            double at = 16.0;
            for (int v = 64; v >= 0; v -= 2) ev[n++] = (struct event){ at++, EV_FILTER, v, 0 };
            for (int v = 0; v <= 64; v += 2) ev[n++] = (struct event){ at++, EV_FILTER, v, 0 };
            for (int v = 64; v <= 127; v += 2) ev[n++] = (struct event){ at++, EV_FILTER, v, 0 };
            for (int v = 127; v >= 64; v -= 2) ev[n++] = (struct event){ at++, EV_FILTER, v, 0 };
            char name[64];
            snprintf(name, sizeof name, "filter-sweep-q%02d-175", qs[q]);
            struct scenario sc = { name, 1750, (unsigned)at + 16, ev, n };
            unsigned frames;
            int32_t *buf = render(&sc, &frames, 0);
            double fps = JUNGLE_RATE * 60.0 / (175.0 * 4.0), plain = 0, top = -99, bottom = 99;
            for (int k = 0; k * 16.0 < at; ++k) {
                double sum = 0;
                unsigned a = (unsigned)(16.0 * k * fps), b = (unsigned)(16.0 * (k + 1) * fps);
                for (unsigned i = a; i < b && i < frames; ++i) sum += (double)(buf[2 * i] >> 7) * (buf[2 * i] >> 7);
                double db = 10 * log10(sum / (b - a) + 1e-9);
                if (k == 0) plain = db;
                else { if (db - plain > top) top = db - plain; if (db - plain < bottom) bottom = db - plain; }
            }
            int clicks; double worst;
            measure_clicks(name, buf, frames, &clicks, &worst);
            printf("%s%-22s Q %.1f: level per bar %+.1f .. %+.1f dB; jumps %d (worst %.2fx)\n", q ? "" : "\n", name,
                   qs[q] / 10.0, bottom, top, clicks, worst);
            write_wav(name, buf, frames);
            free(buf);
        }
    }

    /* 14. MIDI clock in: see clock_follow. Plain USB delivery, then sent the
     *     way a DAW that sends clock once per audio buffer does. */
    clock_follow("midi-clock-in", 0);
    clock_follow("midi-clock-burst-512", 512);
    clock_follow("midi-clock-burst-1024", 1024);
    clock_follow("midi-clock-burst-2048", 2048);
    {
        /* Clock out: the internal clock for 8 beats at 170 BPM. */
        memset(midi_out_count, 0, sizeof midi_out_count);
        static const struct event none[1];
        struct scenario sc = { "midi-clock-out", 1700, 32, none, 0 };
        unsigned out_frames;
        free(render(&sc, &out_frames, 0));
        jungle_render(calloc(BLOCK * 2U, sizeof(int32_t)), BLOCK, 0); /* lets the Stop go out */
        printf("%-22s 8 beats played: FA %u, F8 %u (expect 24 a beat: %u), FC %u; first F8 at frame %u after FA at %u\n",
               "midi-clock-out", midi_out_count[0xfa], midi_out_count[0xf8], 24U * 8U, midi_out_count[0xfc],
               midi_out_first_f8, midi_out_last_fa);
    }

    /* 7. One-shots over the pattern: kick, snare, snare, kick on the off-beats. */
    {
        static const struct event ev[] = {
            { 33.1, EV_SHOT, 0, 0 }, { 37.1, EV_SHOT, 2, 0 }, { 41.6, EV_SHOT, 7, 0 }, { 45.1, EV_SHOT, 9, 0 },
        };
        struct scenario sc = { "one-shots-170", 1700, 64, ev, 4 };
        unsigned frames;
        int32_t *buf = render(&sc, &frames, 0);
        int clicks; double worst;
        measure_clicks(sc.name, buf, frames, &clicks, &worst);
        printf("%-22s jumps %d (worst %.2fx)\n", sc.name, clicks, worst);
        write_wav(sc.name, buf, frames);
        free(buf);
    }
    return 0;
}
