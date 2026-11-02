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

#endif
