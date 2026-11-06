/* SPDX-License-Identifier: GPL-3.0-only */
/* The breaks built into the firmware, so they play straight after installing, with nothing else to load.
 *
 * tools/jungle/make_breaks.sh generates their data into build/jungle from tools/jungle/breaks.txt; it is
 * third-party audio (see assets/third-party/). Built without the samples there are none. */
#include "jungle.h"

#if defined(__has_include)
#if __has_include("../../build/jungle/breaks.inc")
#include "../../build/jungle/breaks.inc"
#define JUNGLE_HAS_BREAKS 1
#endif
#endif

#ifdef JUNGLE_HAS_BREAKS
unsigned jungle_breaks(void) { return BREAK_COUNT; }
const struct jungle_break *jungle_break_at(unsigned i) { return i < BREAK_COUNT ? &BREAKS[i] : 0; }
#else
unsigned jungle_breaks(void) { return 0; }
const struct jungle_break *jungle_break_at(unsigned i) { (void)i; return 0; }
#endif
