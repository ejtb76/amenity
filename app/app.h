/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; AMENITY's changes (C) 2026 Presethead */
/* AMENITY's half of the contract with the platform (fm1-base: device/unity.c, host/plat_host.c), from fm1-base's
 * template. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "AMENITY"
#define APP_SPLASH_RGB RGB(60, 200, 235)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TICK                         /* app_tick: MIDI clock in and out, 1 kHz */
#define APP_USB_CMD                      /* app_usb_cmd: your own breaks from the breaks page (user_breaks.c) */

/* the store: the app's objects, the panel calibration (the platform's) and Bluetooth's address and pairing among
 * them, and their flash offsets */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine and its break */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_tick(void);                     /* 1 kHz, in the timer interrupt that takes in USB and TRS MIDI */
void app_usb_cmd(const uint8_t *d, uint32_t n);   /* a frame for the app in the update's framing: main loop */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */
