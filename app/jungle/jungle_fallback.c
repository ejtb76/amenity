/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The built-in Amen break: a 20 kHz SEA5 copy in the firmware image, played
 * while the sample bank holds no slot named AMEN (a firmware installed on its
 * own, without the installer page's sample bank). The bank's 32 kHz break
 * takes over once it is written.
 *
 * tools/jungle/make_amen_assets.bat generates the data into build/jungle;
 * it is third-party audio (see assets/third-party/amen/README.md). Built
 * without the sample, there is no built-in break and this returns 0. */
#include "jungle.h"

#if defined(__has_include)
#if __has_include("../../build/jungle/amen_fallback.inc")
#include "../../build/jungle/amen_fallback.inc"
#define JUNGLE_HAS_FALLBACK 1
#endif
#endif

int jungle_fallback(const uint8_t **data, unsigned *bytes, unsigned *frames)
{
#ifdef JUNGLE_HAS_FALLBACK
    *data = amen_fallback;
    *bytes = sizeof amen_fallback;
    *frames = AMEN_FALLBACK_FRAMES;
    return 1;
#else
    (void)data; (void)bytes; (void)frames;
    return 0;
#endif
}
