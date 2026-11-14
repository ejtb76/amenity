/* SPDX-License-Identifier: GPL-3.0-only */
/* Following MIDI clock, between jungle_clock.c (the main loop: the pulses
 * with their time stamps, filtered by a delay-locked loop) and jungle.c (the
 * audio interrupt: the break follows the loop's smooth time). */
#ifndef JUNGLE_CLOCK_H
#define JUNGLE_CLOCK_H
#include <stdint.h>

enum { CLOCK_OFF, CLOCK_ARMED, CLOCK_RUNNING, CLOCK_STOPPED };

/* What the loop has found, published with a sequence count (odd while it is
 * written): the time of pulse `pulse` (0: the first after Start, step 1) is
 * t0 + t0_frac ticks, pulses come `per` ticks apart (inv_per = 1 / per).
 * `start` counts Starts and Continues; `resume` is 1 after a Continue. */
struct jungle_follow {
    uint32_t seq, t0, pulse;
    float t0_frac, per, inv_per;
    uint8_t state, start, resume;
};
/* The audio interrupt's copy: the latest consistent one. */
void jungle_follow_read(struct jungle_follow *f);

/* For measuring a sender's clock (tools/jungle/fm1_clocklog.py, through app/user_breaks.c's CMD_CLOCK): the time
 * each pulse was read, from any input, and per audio block while following how far the break was behind the
 * loop's time line (in 1/16 ticks, + behind). Rings of JUNGLE_LOG_N; the counts run on (index = count % N). */
enum { JUNGLE_LOG_N = 512 };
extern volatile uint32_t jungle_pulse_log[JUNGLE_LOG_N], jungle_pulse_count;
extern volatile int16_t jungle_behind_log[JUNGLE_LOG_N];
extern volatile uint32_t jungle_behind_count;
/* the loop's period now (ticks of JUNGLE_TICK_HZ a pulse; 0 when not following) */
float jungle_clock_period(void);

#endif
