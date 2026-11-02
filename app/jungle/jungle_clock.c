/* SPDX-License-Identifier: GPL-3.0-only */
/* Following MIDI clock: the main loop's half (see jungle_clock.h).
 *
 * Each pulse comes in with the time it was read (the 24 MHz timer). Those
 * times jitter: USB delivers in frames, a DAW may send its clock once per
 * audio buffer, the main loop reads when it gets round to it. A delay-locked
 * loop turns them into a smooth time line, as JACK and Ardour do: Fons
 * Adriaensen, "Using a DLL to filter time" (2005). Per pulse, with e the
 * error against the time it predicted,
 *     t0 = t1;  t1 += b e + per;  per += c e;
 *     w = 2 pi B / F,  b = sqrt(2) w,  c = w^2   (critically damped)
 * where B / F, the bandwidth over the pulse rate, shifts down as the loop
 * settles: wide for the first beat (it locks quickly), Ardour's 1/200 up to
 * the eighth beat, then narrower still (a steady tempo, jitter falls by far
 * more than a hundredfold). When the error keeps leaning one way (a tempo
 * change, not jitter, which averages out) it goes back to the middle one.
 * The audio interrupt follows t0, per: it never sees a pulse.
 *
 * Pulses read together (a burst from a DAW, or a main loop held up by the
 * screen) were sent a period apart: the last is taken at the time read and
 * the others a period before each other, so a burst does not make the
 * break late by half a buffer.
 *
 * Around it: Start arms it and the first pulse after it is step 1; Continue
 * the same from where the break is; Stop stops; only the input that sent the
 * Start is followed; after the first beat a rough period from its 24 pulses
 * replaces the starting guess when that was more than 3% out; an error over
 * six pulses starts the loop again at that pulse; half a second without
 * pulses hands back to the internal clock. */
#include "jungle.h"
#include "jungle_clock.h"

#ifdef _MSC_VER                  /* (the host tests) */
#include <intrin.h>
#define BARRIER() _ReadWriteBarrier()
#else
#define BARRIER() __asm__ volatile("" ::: "memory")
#endif
#define TWO_PI 6.2831853f
enum { WIDE_PULSES = 24, MID_PULSES = 24 * 8, FAR_PULSES = 6 };
#ifndef CLOCK_BW_WIDE
#define CLOCK_BW_WIDE 0.02f      /* B / F for the first beat */
#endif
#ifndef CLOCK_BW_MID
#define CLOCK_BW_MID 0.005f      /* to the eighth beat */
#endif
#ifndef CLOCK_BW
#define CLOCK_BW 0.0015f         /* and from then on */
#endif
#ifndef CLOCK_LEAN
#define CLOCK_LEAN 0.25f         /* the error's average, in periods, that shifts back to the middle */
#endif
#ifndef CLOCK_LEAN_K
#define CLOCK_LEAN_K 0.03125f    /* its averaging, per pulse (about 32) */
#endif

static struct jungle_follow pub;
static uint32_t pulse, first_t, last_t, source;
static uint32_t t1;              /* the time predicted for the next pulse: t1 + t1_frac */
static float t1_frac, per, lean; /* lean: the error, averaged (CLOCK_LEAN_K) */
static uint32_t settled;         /* pulses since the loop (re)started, for its bandwidth */
static uint8_t state, starts, resume, have_pulse;
static uint32_t pending, pending_t;   /* pulses read at pending_t, not yet taken */

static void publish(uint32_t t0, float t0_frac)
{
    ++pub.seq;                   /* odd: being written */
    BARRIER();
    pub.t0 = t0;
    pub.t0_frac = t0_frac;
    pub.pulse = pulse;
    pub.per = per;
    pub.inv_per = per > 0.0f ? 1.0f / per : 0.0f;
    pub.state = state;
    pub.start = starts;
    pub.resume = resume;
    BARRIER();
    ++pub.seq;
}

void jungle_follow_read(struct jungle_follow *f)
{
    static struct jungle_follow last;
    uint32_t seq = pub.seq;
    BARRIER();
    if (!(seq & 1U)) {
        struct jungle_follow copy = pub;
        BARRIER();
        if (pub.seq == seq) last = copy;   /* else the main loop was writing it: keep the last */
    }
    *f = last;
}

/* t + frac, carried so that 0 <= frac < 1 */
static void advance(uint32_t *t, float *frac, float by)
{
    float sum = *frac + by;
    int32_t whole = (int32_t)sum;
    if (sum < (float)whole) --whole;
    *t += (uint32_t)whole;
    *frac = sum - (float)whole;
}

static void restart_at(uint32_t t)
{
    settled = 0;
    lean = 0.0f;
    t1 = t;
    t1_frac = 0.0f;
    advance(&t1, &t1_frac, per);
    publish(t, 0.0f);
}

static void take(uint32_t t);

/* The pulses read together, a period apart, the last at the time read. */
static void flush(void)
{
    uint32_t n = pending;
    pending = 0;
    for (uint32_t i = n; i; --i)
        take(pending_t - (uint32_t)((float)(i - 1U) * per + 0.5f));
}

void jungle_midi_realtime(unsigned status, unsigned from, uint32_t t)
{
    if (status == 0xf8U && (state == CLOCK_ARMED || state == CLOCK_RUNNING) && from == source) {
        if (pending && t != pending_t) flush();
        pending_t = t;
        ++pending;
        last_t = t;
        return;
    }
    if (pending) flush();
    if (status == 0xfaU || status == 0xfbU) {
        if (status == 0xfbU && state == CLOCK_RUNNING) return;
        source = from;
        state = CLOCK_ARMED;
        resume = status == 0xfbU;
        ++starts;
        pulse = 0;
        have_pulse = 0;
        last_t = t;
        publish(t, 0.0f);
        return;
    }
    if (state == CLOCK_OFF || from != source) return;
    if (status == 0xfcU) {
        state = CLOCK_STOPPED;
        publish(pub.t0, pub.t0_frac);
        return;
    }
}

/* One pulse at time t. */
static void take(uint32_t t)
{
    if (state == CLOCK_ARMED) {
        /* The first pulse: step 1 (or where Continue left the break). The
         * period starts from the last one followed, else from the tempo. */
        if (!have_pulse || per <= 0.0f) per = 6.0e8f / (float)jungle_tempo_x10();
        state = CLOCK_RUNNING;
        have_pulse = 1;
        pulse = 0;
        first_t = t;
        restart_at(t);
        return;
    }
    ++pulse;
    float e = (float)(int32_t)(t - t1) - t1_frac;
    if (e > per * FAR_PULSES || e < -per * FAR_PULSES) {
        restart_at(t);           /* far out: start again from this pulse */
        return;
    }
    if (pulse == WIDE_PULSES) {
        float rough = (float)(t - first_t) / WIDE_PULSES;
        if (rough > per * 1.03f || rough < per * 0.97f) {
            per = rough;
            restart_at(t);
            return;
        }
    }
    lean += (e - lean) * CLOCK_LEAN_K;
    if ((lean > per * CLOCK_LEAN || lean < -per * CLOCK_LEAN) && settled >= MID_PULSES) settled = WIDE_PULSES;
    ++settled;
    float w = TWO_PI * (settled < WIDE_PULSES ? CLOCK_BW_WIDE : settled < MID_PULSES ? CLOCK_BW_MID : CLOCK_BW);
    uint32_t t0 = t1;
    float t0_frac = t1_frac;
    advance(&t1, &t1_frac, 1.41421356f * w * e + per);
    per += w * w * e;
    publish(t0, t0_frac);
}

int jungle_clock(unsigned *bpm_x10, uint32_t now)
{
    if (pending) flush();
    if (state != CLOCK_OFF && now - last_t > JUNGLE_TICK_HZ / 2U) {
        state = CLOCK_OFF;       /* half a second without pulses */
        publish(pub.t0, pub.t0_frac);
    }
    *bpm_x10 = per > 0.0f ? (unsigned)(6.0e8f / per + 0.5f) : 0U;
    return state != CLOCK_OFF;
}
