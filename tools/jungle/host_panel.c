/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host test for the JUNGLE panel (app/screen.c): runs the real UI code with
 * scripted keys, buttons and encoders, renders audio alongside, and writes
 * screen captures (raw RGB565, 240x240) plus the LED state at each capture.
 *
 * Usage: jungle_panel bank.bin out_dir [nobank]
 *   nobank: no break bound and an empty bank, for the status line */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1.h"
#include "../../app/jungle/jungle.h"

/* ---- Device stubs -------------------------------------------------------- */

static struct fm1_controls panel;
static uint32_t clock_ms;
static uint16_t screen[240 * 240];
static uint8_t leds[41];
static unsigned submits, submitted_pixels;

void fm1_controls_init(void) {}
void fm1_controls_read(struct fm1_controls *state)
{
    *state = panel;
    memset(panel.encoders, 0, sizeof panel.encoders); /* detents since last read */
}
void fm1_led_set(unsigned key, int on) { if (key < 41) leds[key] = (uint8_t)(on != 0); }
static uint8_t play_green;
void fm1_led_matrix(unsigned column, unsigned row, int on) { if (column == 8 && row == 1) play_green = (uint8_t)(on != 0); }
uint32_t fm1_millis(void) { return clock_ms; }
void fm1_screen_init(void) {}
int fm1_screen_pending(void) { return 0; }
int fm1_screen_submit(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t *pixels, size_t count)
{
    if (x + w > 240 || y + h > 240 || count < (size_t)w * h) return -1;
    for (unsigned r = 0; r < h; ++r) memcpy(screen + (y + r) * 240 + x, pixels + r * w, w * 2U);
    ++submits;
    submitted_pixels += w * h;
    return 0;
}
void fm1_audio_stats_read(struct fm1_audio_stats *s) { memset(s, 0, sizeof *s); }
unsigned fm1_engine_output_level(void) { return 100; }
int groove_master_update(unsigned adc) { (void)adc; return 0; }
int fm1_ble_scan(void) { return 0; }
int fm1_ble_connect(unsigned i) { (void)i; return 0; }
int fm1_ble_disconnect(void) { return 0; }
void fm1_ble_get_status(struct fm1_ble_status *s) { memset(s, 0, sizeof *s); }
int fm1_ble_get_device(unsigned i, struct fm1_ble_device *d) { (void)i; (void)d; return -1; }
void fm1_bluetooth_get_status(struct fm1_bluetooth_status *s) { memset(s, 0, sizeof *s); }
static unsigned bank_state = 2; /* app/samples.c: 0 empty .. 2 ready */
unsigned groove_samples_status(void) { return bank_state; }
/* app/samples.c: three breaks for the GLO menu (one lowercase, the font only has capitals). */
static unsigned break_now;
static const char *const break_names[3] = {"AMEN", "funk break", "AMEN 20K"};
static const unsigned break_bpm[3] = {1600, 1030, 1600};
unsigned groove_breaks_count(void) { return bank_state == 2 ? 3 : 0; }
unsigned groove_break_current(void) { return break_now; }
const char *groove_break_name(unsigned i) { return i < 3 ? break_names[i] : NULL; }
unsigned groove_break_bpm_x10(unsigned i) { return i < 3 ? break_bpm[i] : 0; }
int groove_break_select(unsigned i) { if (i >= 3) return -1; break_now = i; return 0; }

void jungle_host_trigger(uint32_t frame, unsigned slice, int pitch, int32_t gain)
{
    (void)frame; (void)slice; (void)pitch; (void)gain;
}

void jungle_midi_out(unsigned status) { (void)status; }

void groove_screen_init(void);
void groove_screen_control(void);

/* ---- Script --------------------------------------------------------------- */

enum { NONE, KEY, UNKEY, PRESS, RELEASE, TURN, PANEL_KEY, PANEL_UNKEY }; /* PANEL_KEY: any key by its panel number */
struct action { unsigned ms; int type, a, b; const char *capture; };
/* White key index -> panel key bit, as app/screen.c maps them. */
static const uint8_t white_bits[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const struct action script[] = {
    { 200, 0, 0, 0, "01-stopped" },
    { 300, PRESS, 12, 0, NULL }, { 400, RELEASE, 12, 0, NULL },            /* PLAY */
    { 1500, 0, 0, 0, "02-playing" },
    { 1600, TURN, 3, 40, NULL }, { 1650, TURN, 4, 20, NULL },             /* K2, K3 */
    { 1700, TURN, 5, -10, NULL }, { 1750, TURN, 0, 8, NULL },             /* K4 low-pass, swing */
    { 1800, TURN, 1, -3, NULL },                                           /* transpose -3 */
    { 1820, TURN, 2, 10, NULL },                                           /* K1 stretch */
    { 1840, TURN, 6, 5, NULL },                                            /* PRESETS tempo +5 */
    { 1860, TURN, 0, 30, NULL },                                           /* SELECT lo-fi */
    { 1900, PRESS, 11, 0, NULL }, { 1950, RELEASE, 11, 0, NULL },         /* SEQ auto-roll */
    { 4800, 0, 0, 0, "03-variation" },
    { 5000, KEY, 9, 0, NULL },                                             /* hold 1/16 down */
    { 5600, 0, 0, 0, "04-roll" },
    { 5800, UNKEY, 9, 0, NULL },
    { 6000, PRESS, 13, 0, NULL }, { 6050, RELEASE, 13, 0, NULL },         /* REC freeze */
    { 6100, TURN, 5, 20, NULL },                                           /* K4 past open: stops at open */
    { 6200, TURN, 5, 10, NULL },                                           /* then high-pass */
    { 6300, PRESS, 8, 0, NULL }, { 6350, RELEASE, 8, 0, NULL },           /* HOME */
    { 6500, 0, 0, 0, "05-home-freeze-hp" },
    { 6600, PRESS, 2, 0, NULL }, { 6650, KEY, 4, 0, NULL },             /* FX + 5th white key (C4): root C */
    { 6700, 0, 0, 0, "06-fx-root-c" },
    { 6750, UNKEY, 4, 0, NULL }, { 6800, RELEASE, 2, 0, NULL },
    { 6900, PRESS, 4, 0, NULL }, { 6950, RELEASE, 4, 0, NULL },           /* ENV: EQ page */
    { 7000, TURN, 2, 4, NULL }, { 7020, TURN, 3, -3, NULL }, { 7040, TURN, 5, 6, NULL },
    { 7200, 0, 0, 0, "07-eq-page" },
    { 7300, PRESS, 2, 0, NULL }, { 7350, RELEASE, 2, 0, NULL },           /* FX tap: FX page */
    { 7400, TURN, 2, 20, NULL }, { 7420, TURN, 3, 30, NULL },
    { 7600, 0, 0, 0, "08-fx-page" },
    { 7700, PRESS, 7, 0, NULL }, { 7750, RELEASE, 7, 0, NULL },           /* GLO: breaks menu */
    { 7800, TURN, 0, 1, NULL },                                            /* SELECT: next break */
    { 8000, 0, 0, 0, "08-breaks-menu" },
    { 8100, PRESS, 7, 0, NULL }, { 8150, RELEASE, 7, 0, NULL },           /* GLO again: back */
    { 8300, 0, 0, 0, "08-breaks-closed" },
    /* FX pages: sends, delay page (divisions, then ms), reverb page */
    { 8400, PRESS, 2, 0, NULL }, { 8450, RELEASE, 2, 0, NULL },           /* FX: FX page */
    { 8500, TURN, 4, 25, NULL }, { 8520, TURN, 5, 20, NULL },             /* K3 delay send, K4 reverb send */
    { 8700, 0, 0, 0, "09-fx-sends" },
    { 8800, PRESS, 2, 0, NULL }, { 8850, RELEASE, 2, 0, NULL },           /* FX: delay page */
    { 8900, TURN, 2, -2, NULL }, { 8920, TURN, 3, 10, NULL }, { 8940, TURN, 4, 15, NULL }, { 8960, TURN, 5, -10, NULL },
    { 9100, 0, 0, 0, "09-delay-div" },
    { 9200, TURN, 0, 1, NULL }, { 9250, TURN, 2, 30, NULL },              /* SELECT: milliseconds */
    { 9400, 0, 0, 0, "09-delay-ms" },
    { 9500, PRESS, 2, 0, NULL }, { 9550, RELEASE, 2, 0, NULL },           /* FX: reverb page */
    { 9600, TURN, 2, 1, NULL }, { 9620, TURN, 3, -10, NULL }, { 9640, TURN, 4, 20, NULL }, { 9660, TURN, 5, -5, NULL },
    { 9800, 0, 0, 0, "09-reverb" },
    { 9900, PRESS, 2, 0, NULL }, { 9950, RELEASE, 2, 0, NULL },           /* FX: back to main */
    /* LFO page */
    { 10000, PRESS, 5, 0, NULL }, { 10050, RELEASE, 5, 0, NULL },
    { 10100, TURN, 2, 4, NULL }, { 10120, TURN, 3, 60, NULL }, { 10140, TURN, 4, 3, NULL }, { 10160, TURN, 5, 6, NULL },
    { 10300, 0, 0, 0, "10-lfo-page" },
    { 10400, PRESS, 5, 0, NULL }, { 10450, RELEASE, 5, 0, NULL },
    /* EDIT page: move the cursor, change its chop */
    { 10500, PRESS, 6, 0, NULL }, { 10550, RELEASE, 6, 0, NULL },
    { 10600, TURN, 0, 3, NULL }, { 10620, TURN, 1, 5, NULL },
    { 10800, 0, 0, 0, "11-edit-page" },
    { 10900, PRESS, 8, 0, NULL }, { 10950, RELEASE, 8, 0, NULL },         /* HOME in EDIT: chops in order */
    { 11000, PRESS, 6, 0, NULL }, { 11050, RELEASE, 6, 0, NULL },
    /* Hold keys and turn SELECT: BUILD stutter, STRETCH pitch, ECHO feedback, repeat slice; C#5 mute */
    { 11100, KEY, 1, 0, NULL }, { 11300, TURN, 0, 3, NULL }, { 11500, 0, 0, 0, "12-build-stutter" }, { 11550, UNKEY, 1, 0, NULL },
    { 11600, KEY, 2, 0, NULL }, { 11800, TURN, 0, 4, NULL }, { 12000, 0, 0, 0, "12-stretch-pitch" }, { 12050, UNKEY, 2, 0, NULL },
    { 12100, KEY, 3, 0, NULL }, { 12300, TURN, 0, 4, NULL }, { 12500, 0, 0, 0, "12-echo-fb" }, { 12550, UNKEY, 3, 0, NULL },
    { 12560, 0, 0, 0, "12-echo-released" },
    { 12600, KEY, 9, 0, NULL }, { 12800, TURN, 0, 3, NULL }, { 13000, 0, 0, 0, "12-slice-scan" }, { 13050, UNKEY, 9, 0, NULL },
    { 13100, PANEL_KEY, 20, 0, NULL }, { 13300, 0, 0, 0, "13-mute" }, { 13350, PANEL_UNKEY, 20, 0, NULL },
    { 13400, TURN, 1, 20, NULL }, { 13500, 0, 0, 0, "13-transpose-up" },
};

/* ---- Bank and main -------------------------------------------------------- */

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
    memcpy(entry, bank + 32, sizeof entry);
    *frames = entry[1];
    *bytes = entry[2];
    return bank + entry[0];
}

static void capture(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.rgb565", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fwrite(screen, 2, 240 * 240, f);
    fclose(f);
    /* LEDs as a keyboard: white keys on the bottom row, black keys above. */
    char top[40], bottom[40];
    memset(top, ' ', sizeof top);
    memset(bottom, ' ', sizeof bottom);
    unsigned column = 0;
    for (unsigned key = 0; key < 27; ++key) {
        int white = 0;
        for (unsigned w = 0; w < 16; ++w) if (white_bits[w] == key) white = 1;
        if (white) bottom[column++] = leds[14 + key] ? '#' : '.';
        else if (column) top[column - 1] = leds[14 + key] ? '#' : '^';
    }
    top[column] = bottom[column] = 0;
    printf("%-20s LEDs  %s  PLAY %s REC %c SEQ %c\n%-20s       %s   (%u band transfers, %u pixels)\n",
           name, top, leds[12] ? "red" : play_green ? "green" : "off", leds[13] ? '#' : '.', leds[11] ? '#' : '.', "", bottom,
           submits, submitted_pixels);
    submits = submitted_pixels = 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: jungle_panel bank.bin out_dir\n"); return 2; }
    unsigned bytes, frames;
    const uint8_t *sea = load_bank(argv[1], &bytes, &frames);
    if (argc > 3 && !strcmp(argv[3], "nobank")) {
        bank_state = 0;
        groove_screen_init();
        groove_screen_control();
        groove_screen_control();
        capture(argv[2], "00-no-sample-bank");
        bank_state = 2; /* a bank with no usable slot */
        groove_screen_control();
        groove_screen_control();
        capture(argv[2], "00-no-break-in-bank");
        return 0;
    }
    if (jungle_bind(sea, bytes, frames)) return 1;
    groove_screen_init();

    static int32_t block[FM1_BLOCK_WORDS];
    unsigned next = 0, frame = 0, end = script[sizeof script / sizeof script[0] - 1].ms + 100;
    FILE *wav = NULL;
    char path[512];
    snprintf(path, sizeof path, "%s/panel-session.raw", argv[2]);
    wav = fopen(path, "wb");
    while (clock_ms < end) {
        /* One audio block, then the control task, as on the device (~3 ms). */
        memset(block, 0, sizeof block);
        jungle_render(block, FM1_BLOCK_FRAMES);
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
            int32_t v = block[i] >> 8;
            int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            fwrite(&s, 2, 1, wav);
        }
        frame += FM1_BLOCK_FRAMES;
        clock_ms = (uint32_t)((uint64_t)frame * 1000U / 44100U);
        while (next < sizeof script / sizeof script[0] && script[next].ms <= clock_ms) {
            const struct action *a = &script[next++];
            switch (a->type) {
            case KEY: panel.keys |= 1U << white_bits[a->a]; break;
            case UNKEY: panel.keys &= ~(1U << white_bits[a->a]); break;
            case PANEL_KEY: panel.keys |= 1U << a->a; break;
            case PANEL_UNKEY: panel.keys &= ~(1U << a->a); break;
            case PRESS: panel.buttons |= 1U << a->a; break;
            case RELEASE: panel.buttons &= ~(1U << a->a); break;
            case TURN: panel.encoders[a->a] = (int16_t)(panel.encoders[a->a] + a->b); break;
            }
            if (a->capture) {
                groove_screen_control();
                groove_screen_control(); /* let every pending band go out */
                capture(argv[2], a->capture);
            }
        }
        groove_screen_control();
    }
    fclose(wav);
    /* The firmware's own break, as when the bank holds no AMEN slot. */
    const uint8_t *builtin;
    unsigned builtin_bytes, builtin_frames;
    if (jungle_fallback(&builtin, &builtin_bytes, &builtin_frames) &&
        !jungle_bind(builtin, builtin_bytes, builtin_frames)) {
        groove_screen_control();
        groove_screen_control();
        capture(argv[2], "09-builtin-break");
    }
    /* Following a MIDI clock at 174 BPM for eight seconds: EXT and its tempo. */
    jungle_midi_realtime(0xfa);
    for (unsigned at = 0, pulses = 0; at < 8U * 44100U; at += FM1_BLOCK_FRAMES) {
        while (pulses * 44100ULL * 60U <= (uint64_t)at * 174U * 24U) { jungle_midi_realtime(0xf8); ++pulses; }
        memset(block, 0, sizeof block);
        jungle_render(block, FM1_BLOCK_FRAMES);
        clock_ms += 3;
        groove_screen_control();
    }
    groove_screen_control();
    capture(argv[2], "10-external-clock");
    return 0;
}
