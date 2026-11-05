/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* AMENITY panel: keys, knobs, buttons, LEDs and screen.
 *
 * Replaces the groovebox UI. The engine still calls groove_screen_init and
 * groove_screen_control from the control task; everything here runs there.
 *
 *   white keys   hold for a roll (see jungle.h); the third holds STRETCH.
 *                Holding one and turning SELECT changes it: the repeats, down,
 *                up and shrink pick the slice; BUILD picks one of 16 stereo
 *                stutters (kept); STRETCH tunes its pitch (kept); ECHO sets its
 *                feedback (back to default on release)
 *   black keys   eight one-shot hits (next 32nd); the ninth (C#5) is a
 *                momentary MUTE of the break, ahead of the effects chain
 *   K1..K4       feedback (short metallic delay), variation, warp amount, DJ filter
 *   FX held      a key sets the feedback's root note (C..B)
 *   PRESETS      tempo (SEL held: fine)
 *   SELECT       LO-FI (sample rate and bits down)   ALGORITHM   transpose -12..+12
 *   ENV          EQ page: K1..K4 low, low-mid, high-mid, high (-12..+12 dB)
 *   FX tap       FX page: K1 saturation, K2 compression, K3 delay send, K4
 *                reverb send (both after the compressor); FX again: DELAY page
 *                (time as a division or in ms, feedback, high-pass, wet/dry;
 *                SELECT picks division or ms); FX again: REVERB page (type,
 *                size, damping, wet/dry); FX again, ENV or HOME: back to the
 *                main knobs
 *   PLAY start/stop   REC freeze   SEQ auto-roll   HOME straight break
 *   GLO          BREAKS menu: SELECT picks the break (it plays at once, at its
 *                own tempo); GLO again or HOME: back to the main page
 *   MIDI clock: a Start or Continue from any input leads (EXT on screen,
 *   tempo measured, PLAY and tempo knobs rest); the internal clock sends
 *   Start, clock and Stop out.
 *   OCT- / OCT+  tempo halve / double
 *
 * Drawing: a page is composed in 16-row bands and sent without waiting
 * (fm1_screen_submit); only bands whose content changed are sent. */
#include "instrument.h"
#include "fm1_engine_control.h"
#include "jungle/jungle.h"
#include <string.h>

enum {
    BACK = 0x0000,
    PANEL = 0x18e3,
    TEXT = 0xef5d,
    ACCENT = 0x07ff,
    DIM = 0x738e,
    GREEN = 0x07e0,
    AMBER = 0xfd20,
    RED = 0xf8a2,
    STRETCH = 0xb81f
};
enum { OCT_DOWN, OCT_UP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC };
#define BUTTON(b) (1U << (b))
enum { ENC_SELECT, ENC_ALGORITHM, ENC_K1, ENC_K2, ENC_K3, ENC_K4, ENC_PRESETS };
void fm1_led_matrix(unsigned column, unsigned row, int on); /* app/board/panel.c */
/* LEDs no key owns, by matrix place. The first is PLAY's green (found on
 * the device with the test below); SEL + OCT+ steps a test light through
 * all three. */
static const uint8_t spare_leds[3][2] = {{8, 1}, {9, 1}, {10, 1}};
static unsigned led_test; /* 0 off, 1..3: that spare LED lit alone */
static uint8_t green_lit;

static const uint8_t white_keys[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t black_keys[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
/* One-shots on the first eight black keys: kick, snare, kick, snare, snare,
 * snare, hat, ride. The ninth, C#5 (panel key 20), is the MUTE; the last two
 * black keys are kept for effects. */
static const uint8_t shot_slices[8] = {0, 2, 9, 7, 10, 15, 3, 12};
enum { MUTE_BLACK = 8 };
static const char *const roll_labels[JUNGLE_ROLL_KEYS] = {
    "1/4 REPEAT", "BUILD", "STRETCH", "ECHO",
    "1/8 REPEAT", "1/8 DOWN", "1/8 UP", "1/8 SHRINK",
    "1/16 REPEAT", "1/16 DOWN", "1/16 UP", "1/16 SHRINK",
    "1/32 REPEAT", "1/32 DOWN", "1/32 UP", "1/32 SHRINK",
};

/* ---- Settings and live state ---------------------------------------------- */

static unsigned tempo = 1600, rhythm, filter = 64, warp, feedback, root = 4, playing, frozen, auto_roll;
/* Following MIDI clock. */
static unsigned external;
static uint32_t clock_near; /* when the locked tempo last matched the one shown */
enum { PAGE_MAIN, PAGE_EQ, PAGE_FX, PAGE_BREAKS, PAGE_DELAY, PAGE_REVERB, PAGE_EDIT, PAGE_LFO };
static unsigned page, saturation, compression, fx_used, lofi;
/* Hold a white key and turn SELECT (see apply_encoders). */
enum { ECHO_DEFAULT = 75 };
static int slice_pick = -1, stretch_pitch;
static unsigned stutter, echo_fb = ECHO_DEFAULT, muted;
static const char *const stutter_names[16] = {
    "BUILD", "BUILD PING", "BUILD WIDE", "BUILD RAND",
    "FAST", "FAST PING", "FAST WIDE", "FAST RAND",
    "DECEL", "DECEL PING", "DECEL WIDE", "DECEL RAND",
    "1/16", "1/16 PING", "1/16 WIDE", "1/16 RAND",
};
/* FX page sends, the delay page and the reverb page. The delay time is a
 * division (dly_sync) of the tempo or milliseconds; the engine takes it in
 * half-rate samples (22.05 kHz), at most JUNGLE_DELAY_MAX. */
enum { DLY_DIVS = 12, DLY_MS_MIN = 10, DLY_MS_MAX = JUNGLE_DELAY_MAX * 20 / 441 };
static unsigned dly_send, rev_send, dly_fb = 45, dly_hp = 25, dly_mix = 127, dly_sync = 1, dly_div = 7, dly_ms = 250;
static unsigned dly_samples;
static unsigned rev_type = 2, rev_size = 70, rev_damp = 40, rev_mix = 127;
static const char *const div_names[DLY_DIVS] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8",
                                                "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"};
/* Each division in ticks (48 per 16th note, 192 a beat). */
static const uint16_t div_ticks[DLY_DIVS] = {24, 32, 48, 64, 72, 96, 128, 144, 192, 256, 288, 384};
static const char *const rev_names[JUNGLE_REVERB_TYPES] = {"AMBIENCE", "ROOM", "HALL", "PLATE", "CATHEDRAL"};
/* EDIT: the cursor over the sixteen 8th-note hits (the lit cells of the grid). */
static unsigned edit_pos;
/* LFO page: wave, depth, time (a clock division, in ticks) and target. */
enum { LFO_TIMES = 12 };
static unsigned lfo_wave, lfo_depth, lfo_time = 8, lfo_dest;
static const char *const lfo_wave_names[JUNGLE_LFO_WAVES] = {"SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "RANDOM"};
static const char *const lfo_time_names[LFO_TIMES] = {"1/32", "1/16", "1/8T", "1/8", "1/8.", "1/4",
                                                       "1/4.", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
static const uint16_t lfo_time_ticks[LFO_TIMES] = {24, 48, 64, 96, 144, 192, 288, 384, 768, 1536, 3072, 6144};
static const char *const lfo_dest_names[JUNGLE_LFO_DESTS] = {"FILTER", "DLY SEND", "REV SEND", "TRANSPOSE",
                                                              "VARIATION", "WARP", "LEVEL"};
/* The filter rings (Q 1.4) only while K4 turns: half a second after the
 * last detent its Q glides back to flat (0.8), a tenth per 30 ms. */
enum { Q_SWEEP = 14, Q_FLAT = 8 };
static unsigned filter_q = Q_FLAT;
static uint32_t filter_turned, filter_q_step;
static int eq_db[4];
static const char *const eq_labels[4] = {"LOW", "LO MID", "HI MID", "HIGH"};
static const char *const note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/* Panel key 0 is F3: its pitch class. */
static unsigned key_pitch_class(unsigned key) { return (key + 5U) % 12U; }
static int transpose;
static uint32_t previous_keys, previous_buttons, led_state, button_leds;
static uint8_t held[16], held_count; /* white keys in press order */
static const char *notice;
static char notice_text[32];
static uint32_t notice_until, health_at, health_overruns;
static uint16_t notice_serial; /* new text in the same buffer still repaints */

/* Everything the screen shows; a change marks its region for repaint. */
struct view {
    struct jungle_step steps[JUNGLE_STEPS];
    int8_t step, roll;
    uint16_t tempo;
    uint8_t rhythm, filter, warp, feedback, root, playing, frozen, auto_roll, external;
    uint8_t page, saturation, compression, lofi;
    int8_t eq[4];
    int8_t transpose;
    const char *notice, *status;
    uint8_t builtin; /* the firmware's own 20 kHz break plays (no AMEN slot in the bank) */
    uint16_t notice_serial;
    uint8_t bound;
    uint8_t break_index, break_count; /* GLO menu: the playing break, and how many there are */
    uint8_t stutter, echo_fb, slice, muted; /* slice: the one a sounding roll repeats, else 0 */
    int8_t stretch_pitch;
    uint8_t dly_send, rev_send, dly_fb, dly_hp, dly_mix, dly_sync, dly_div;
    uint8_t rev_type, rev_size, rev_damp, rev_mix;
    uint16_t dly_ms;
    uint8_t edit_pos, lfo_wave, lfo_depth, lfo_time, lfo_dest;
};
static struct view shown, now_view;
static unsigned full_repaint;

/* ---- Drawing ------------------------------------------------------------- */

/* Five columns per glyph, bit 0 at the top. Digits followed by uppercase. */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const uint8_t punctuation[][6] = {
    {'-', 8, 8, 8, 8, 8}, {'/', 0x60, 0x18, 6, 1, 0}, {'+', 8, 8, 0x3e, 8, 8},
    {'%', 0x63, 0x13, 8, 0x64, 0x63}, {'.', 0, 0x60, 0x60, 0, 0}, {':', 0, 0x36, 0x36, 0, 0},
    {'=', 0x14, 0x14, 0x14, 0x14, 0x14}, {'>', 0x41, 0x22, 0x14, 8, 0},
};

enum { PAINT_ROWS = 16, PAINT_BANDS = FM1_SCREEN_HEIGHT / PAINT_ROWS };
_Static_assert(FM1_SCREEN_WIDTH * PAINT_ROWS <= FM1_SCREEN_DMA_PIXELS, "LCD band capacity");
static uint16_t paint_pixels[FM1_SCREEN_WIDTH * PAINT_ROWS];
static uint8_t paint_left[PAINT_BANDS], paint_right[PAINT_BANDS];
static unsigned paint_top, paint_next, paint_x, paint_end;

static void paint_mark(unsigned x, unsigned y, unsigned w, unsigned h)
{
    if (!w || !h || x >= FM1_SCREEN_WIDTH || y >= FM1_SCREEN_HEIGHT) return;
    if (w > FM1_SCREEN_WIDTH - x) w = FM1_SCREEN_WIDTH - x;
    if (h > FM1_SCREEN_HEIGHT - y) h = FM1_SCREEN_HEIGHT - y;
    for (unsigned b = y / PAINT_ROWS; b <= (y + h - 1) / PAINT_ROWS; ++b) {
        if (x < paint_left[b]) paint_left[b] = (uint8_t)x;
        if (x + w > paint_right[b]) paint_right[b] = (uint8_t)(x + w);
    }
}
/* Fill and text draw into the current band, clipped to it. */
static __attribute__((noinline)) void fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color)
{
    unsigned right = x + w < paint_end ? x + w : paint_end;
    unsigned first = y < paint_top ? paint_top : y;
    unsigned end = y + h < paint_top + PAINT_ROWS ? y + h : paint_top + PAINT_ROWS;
    if (x < paint_x) x = paint_x;
    for (unsigned row = first; row < end; ++row)
        for (unsigned col = x; col < right; ++col)
            paint_pixels[(row - paint_top) * (paint_end - paint_x) + col - paint_x] = color;
}
static void text(unsigned x, unsigned y, const char *s, unsigned scale, uint16_t color)
{
    unsigned w = 6U * scale, h = 7U * scale;
    if (y >= paint_top + PAINT_ROWS || y + h <= paint_top) return;
    for (; *s && x + w <= FM1_SCREEN_WIDTH; ++s, x += w) {
        if (x >= paint_end) break;
        if (x + w <= paint_x) continue;
        static const uint8_t blank[5] = {0};
        const uint8_t *shape = blank;
        if (*s >= '0' && *s <= '9') shape = font[*s - '0'];
        else if (*s >= 'A' && *s <= 'Z') shape = font[10 + *s - 'A'];
        else
            for (unsigned i = 0; i < sizeof punctuation / sizeof punctuation[0]; ++i)
                if (*s == (char)punctuation[i][0]) { shape = punctuation[i] + 1; break; }
        unsigned first_row = y < paint_top ? paint_top : y;
        unsigned end_row = y + h < paint_top + PAINT_ROWS ? y + h : paint_top + PAINT_ROWS;
        unsigned first_col = x < paint_x ? paint_x : x;
        unsigned end_col = x + w < paint_end ? x + w : paint_end;
        for (unsigned row = first_row; row < end_row; ++row) {
            unsigned mask = 1U << ((row - y) / scale);
            uint16_t *pixel = paint_pixels + (row - paint_top) * (paint_end - paint_x) + first_col - paint_x;
            for (unsigned col = first_col; col < end_col; ++col, ++pixel) {
                unsigned column = (col - x) / scale;
                if (column < 5U && (shape[column] & mask)) *pixel = color;
            }
        }
    }
}
static char *decimal(char *out, int value, unsigned tenths)
{
    char digits[12];
    unsigned n = 0, v = (unsigned)(value < 0 ? -value : value);
    if (value < 0) *out++ = '-';
    do { digits[n++] = (char)('0' + v % 10U); v /= 10U; if (tenths && n == 1) digits[n++] = '.'; }
    while (v || (tenths && n < 3));
    while (n) *out++ = digits[--n];
    *out = 0;
    return out;
}
static unsigned percent(unsigned v) { return (v * 100U + 63U) / 127U; }
/* "NN%" for a 0..127 value. */
static void percent_text(char *out, unsigned v)
{
    char *end = decimal(out, (int)percent(v), 0);
    end[0] = '%';
    end[1] = 0;
}

/* Layout. */
enum {
    HEADER_Y = 0, HEADER_H = 32,
    GRID_Y = 36, CELL_H = 26, ROW_GAP = 34, CELL_W = 15,
    ROLL_Y = 110, ROLL_H = 24,
    TAGS_Y = 140, TAGS_H = 10,
    KNOBS_Y = 158, KNOBS_H = 46, KNOB_W = 60,
    FOOT_Y = 212, FOOT_H = 12,
    NOTICE_Y = 228, NOTICE_H = 12
};

static void step_rect(unsigned step, unsigned *x, unsigned *y)
{
    *x = (step & 15U) * CELL_W;
    *y = GRID_Y + (step >> 4) * ROW_GAP;
}
static void draw_cell(unsigned step)
{
    unsigned x, y;
    step_rect(step, &x, &y);
    const struct jungle_step *s = &now_view.steps[step];
    /* EDIT: the cursor cell is green. */
    int cursor = now_view.page == PAGE_EDIT && !(step & 1U) && now_view.edit_pos == (step >> 1);
    uint16_t color = cursor ? GREEN : !s->active ? PANEL : s->stretch ? STRETCH : s->pitch > 0 ? AMBER : s->pitch < 0 ? ACCENT : TEXT;
    fill(x + 1, y, CELL_W - 2, CELL_H, color);
    if (s->active) {
        char digit[2] = {(char)(s->slice < 10 ? '0' + s->slice : 'A' + s->slice - 10), 0};
        text(x + 5, y + 10, digit, 1, BACK);
    }
    if (now_view.step == (int)step)
        fill(x + 1, y + CELL_H + 1, CELL_W - 2, 3, now_view.roll >= 0 ? RED : GREEN);
}
static void draw_knob(unsigned column, const char *label, const char *value, unsigned amount, int centered)
{
    unsigned x = column * KNOB_W + 3;
    text(x, KNOBS_Y, label, 1, DIM);
    text(x, KNOBS_Y + 14, value, strlen(value) <= 4 ? 2 : 1, TEXT);
    fill(x, KNOBS_Y + 36, 54, 4, PANEL);
    if (centered) {
        unsigned middle = x + 27, pos = x + amount * 54U / 127U;
        if (pos < middle) fill(pos, KNOBS_Y + 36, middle - pos, 4, ACCENT);
        else fill(middle, KNOBS_Y + 36, pos - middle + 1, 4, ACCENT);
    } else fill(x, KNOBS_Y + 36, amount * 54U / 127U, 4, ACCENT);
}
static void filter_label(char *out, unsigned value)
{
    if (value == 64) { strcpy(out, "OFF"); return; }
    unsigned hz = jungle_filter_hz(value);
    memcpy(out, value < 64 ? "LP " : "HP ", 3);
    char *end = hz >= 1000 ? decimal(out + 3, (int)(hz / 100U), 1) : decimal(out + 3, (int)hz, 0);
    if (hz >= 1000) { end[0] = 'K'; end[1] = 0; }
}
/* GLO: the breaks on offer, one per row, the playing one marked. Names come
 * from the sample bank in any case; the font only has capitals. */
static void draw_breaks(void)
{
    text(6, 38, "BREAKS", 1, DIM);
    unsigned count = now_view.break_count;
    if (!count) text(6, 60, "NO BREAKS", 2, AMBER);
    for (unsigned i = 0; i < count; ++i) {
        unsigned y = 50U + 16U * i;
        int current = i == now_view.break_index;
        const char *source = groove_break_name(i);
        char name[16], bpm[8];
        unsigned n = 0;
        for (; source && source[n] && n < 15U; ++n)
            name[n] = source[n] >= 'a' && source[n] <= 'z' ? (char)(source[n] - 32) : source[n];
        name[n] = 0;
        if (current) text(6, y, ">", 2, GREEN);
        text(24, y, name, 2, current ? GREEN : TEXT);
        decimal(bpm, (int)groove_break_bpm_x10(i), 1);
        text(206, y + 4U, bpm, 1, DIM);
    }
    text(6, FOOT_Y, "SELECT = CHOOSE   GLO = BACK", 1, AMBER);
}
static void draw_page(void)
{
    char line[32];
    fill(paint_x, paint_top, paint_end - paint_x, PAINT_ROWS, BACK);
    /* Header: name and tempo. */
    text(6, 9, "AMENITY", 2, ACCENT);
    if (now_view.external) text(96, 3, "EXT", 1, GREEN);
    if (now_view.builtin) text(96, 13, "20K", 1, AMBER);
    decimal(line, (int)now_view.tempo, 1);
    text(234 - 18U * (unsigned)strlen(line), 5, line, 3, TEXT);
    if (now_view.page == PAGE_BREAKS) {
        draw_breaks();
        if (now_view.notice || now_view.status)
            text(6, NOTICE_Y, now_view.notice ? now_view.notice : now_view.status, 1, AMBER);
        return;
    }
    for (unsigned step = 0; step < JUNGLE_STEPS; ++step) draw_cell(step);
    /* The roll that sounds, or the transport state. */
    if (!now_view.bound) text(6, ROLL_Y, "NO BREAK", 2, AMBER);
    else if (now_view.roll >= 0) {
        /* The label, then what SELECT set on it: BUILD's stutter replaces it. */
        unsigned r = (unsigned)now_view.roll;
        const char *label = r == 1U ? stutter_names[now_view.stutter & 15U] : roll_labels[r];
        text(6, ROLL_Y, label, 2, RED);
        unsigned x = 6U + 12U * ((unsigned)strlen(label) + 1U);
        if (r == 2U) {
            if (now_view.stretch_pitch) {
                char *end = line;
                if (now_view.stretch_pitch > 0) *end++ = '+';
                decimal(end, now_view.stretch_pitch, 0);
                text(x, ROLL_Y, line, 2, AMBER);
            }
        } else if (r == 3U) {
            memcpy(line, "FB ", 3);
            char *end = decimal(line + 3, now_view.echo_fb, 0);
            end[0] = '%';
            end[1] = 0;
            text(x, ROLL_Y, line, 2, AMBER);
        } else if (r != 1U) {
            memcpy(line, "SL ", 3);
            line[3] = (char)(now_view.slice < 10U ? '0' + now_view.slice : 'A' + now_view.slice - 10U);
            line[4] = 0;
            text(x, ROLL_Y, line, 2, AMBER);
        }
    } else text(6, ROLL_Y, now_view.playing ? "PLAYING" : "STOPPED - PLAY", 2, now_view.playing ? GREEN : DIM);
    if (now_view.frozen) text(6, TAGS_Y, "FREEZE", 1, AMBER);
    if (now_view.auto_roll) text(54, TAGS_Y, "SEQ FILL", 1, GREEN);
    if (now_view.muted) text(210, TAGS_Y, "MUTE", 1, RED);
    if (now_view.transpose) {
        char *end = line;
        memcpy(end, "TRANSPOSE ", 10); end += 10;
        if (now_view.transpose > 0) *end++ = '+';
        decimal(end, now_view.transpose, 0);
        text(126, TAGS_Y, line, 1, TEXT);
    }
    if (now_view.page == PAGE_EQ) {
        for (unsigned b = 0; b < 4; ++b) {
            char *end = line;
            if (now_view.eq[b] > 0) *end++ = '+';
            memcpy(decimal(end, now_view.eq[b], 0), "DB", 3);
            draw_knob(b, eq_labels[b], line, (unsigned)(now_view.eq[b] + 12) * 127U / 24U, 1);
        }
        text(6, FOOT_Y, "EQ: ENV = BACK", 1, AMBER);
    } else if (now_view.page == PAGE_FX) {
        decimal(line, (int)percent(now_view.saturation), 0);
        draw_knob(0, "SATURATE", line, now_view.saturation, 0);
        decimal(line, (int)percent(now_view.compression), 0);
        draw_knob(1, "COMPRESS", line, now_view.compression, 0);
        percent_text(line, now_view.dly_send);
        draw_knob(2, "DLY SEND", line, now_view.dly_send, 0);
        percent_text(line, now_view.rev_send);
        draw_knob(3, "REV SEND", line, now_view.rev_send, 0);
        text(6, FOOT_Y, "FX: FX = DELAY PAGE", 1, AMBER);
    } else if (now_view.page == PAGE_DELAY) {
        /* K1 is the time, in a division of the tempo or in milliseconds; SELECT picks which. */
        unsigned amount;
        if (now_view.dly_sync) {
            strcpy(line, div_names[now_view.dly_div % DLY_DIVS]);
            amount = now_view.dly_div * 127U / (DLY_DIVS - 1U);
        } else {
            char *end = decimal(line, (int)now_view.dly_ms, 0);
            strcpy(end, "MS");
            amount = (now_view.dly_ms - DLY_MS_MIN) * 127U / (DLY_MS_MAX - DLY_MS_MIN);
        }
        draw_knob(0, now_view.dly_sync ? "TIME DIV" : "TIME MS", line, amount, 0);
        char *fb_end = decimal(line, (int)((now_view.dly_fb * 90U + 63U) / 127U), 0);
        fb_end[0] = '%';
        fb_end[1] = 0;
        draw_knob(1, "FEEDBACK", line, now_view.dly_fb, 0);
        unsigned hz = jungle_filter_hz(65U + now_view.dly_hp * 62U / 127U); /* the DJ filter's high-pass steps */
        if (hz >= 1000U) {
            char *end = decimal(line, (int)(hz / 100U), 1);
            end[0] = 'K';
            end[1] = 0;
        } else {
            char *end = decimal(line, (int)hz, 0);
            strcpy(end, "HZ");
        }
        draw_knob(2, "HI-PASS", line, now_view.dly_hp, 0);
        percent_text(line, now_view.dly_mix);
        draw_knob(3, "WET/DRY", line, now_view.dly_mix, 0);
        text(6, FOOT_Y, "DELAY: SEL = DIV/MS", 1, AMBER);
    } else if (now_view.page == PAGE_REVERB) {
        draw_knob(0, "TYPE", rev_names[now_view.rev_type % JUNGLE_REVERB_TYPES],
                  now_view.rev_type * 127U / (JUNGLE_REVERB_TYPES - 1U), 0);
        percent_text(line, now_view.rev_size);
        draw_knob(1, "SIZE", line, now_view.rev_size, 0);
        percent_text(line, now_view.rev_damp);
        draw_knob(2, "DAMPING", line, now_view.rev_damp, 0);
        percent_text(line, now_view.rev_mix);
        draw_knob(3, "WET/DRY", line, now_view.rev_mix, 0);
        text(6, FOOT_Y, "REVERB: FX = BACK", 1, AMBER);
    } else if (now_view.page == PAGE_LFO) {
        draw_knob(0, "WAVE", lfo_wave_names[now_view.lfo_wave % JUNGLE_LFO_WAVES],
                  now_view.lfo_wave * 127U / (JUNGLE_LFO_WAVES - 1U), 0);
        percent_text(line, now_view.lfo_depth);
        draw_knob(1, "DEPTH", line, now_view.lfo_depth, 0);
        draw_knob(2, "TIME", lfo_time_names[now_view.lfo_time % LFO_TIMES], now_view.lfo_time * 127U / (LFO_TIMES - 1U), 0);
        draw_knob(3, "TARGET", lfo_dest_names[now_view.lfo_dest % JUNGLE_LFO_DESTS],
                  now_view.lfo_dest * 127U / (JUNGLE_LFO_DESTS - 1U), 0);
        text(6, FOOT_Y, "LFO: LFO = BACK", 1, AMBER);
    } else {
        char *fb_end = decimal(line, (int)((now_view.feedback * 90U + 63U) / 127U), 0);
        *fb_end++ = '%';
        *fb_end++ = ' ';
        strcpy(fb_end, note_names[now_view.root]);
        draw_knob(0, "FEEDBACK", line, now_view.feedback, 0);
        decimal(line, (int)percent(now_view.rhythm), 0);
        draw_knob(1, "VARIATION", line, now_view.rhythm, 0);
        decimal(line, (int)percent(now_view.warp), 0);
        draw_knob(2, "WARP", line, now_view.warp, 0);
        filter_label(line, now_view.filter);
        draw_knob(3, "FILTER", line, now_view.filter, 1);
        if (now_view.lofi) {
            unsigned hz, bits;
            jungle_lofi_view(now_view.lofi, &hz, &bits);
            memcpy(line, "LO-FI ", 6);
            char *end = decimal(line + 6, (int)((hz + 500U) / 1000U), 0);
            *end++ = 'K';
            *end++ = ' ';
            memcpy(decimal(end, (int)bits, 0), "BIT", 4);
        } else strcpy(line, "LO-FI OFF");
        if (now_view.page == PAGE_EDIT) text(6, FOOT_Y, "SEL=STEP ALGO=CHOP", 1, GREEN);
        else text(6, FOOT_Y, line, 1, now_view.lofi ? AMBER : DIM);
    }
    text(126, FOOT_Y, "HOLD WHITE = ROLL", 1, DIM);
    if (now_view.notice || now_view.status)
        text(6, NOTICE_Y, now_view.notice ? now_view.notice : now_view.status, 1, AMBER);
}
/* Mark what changed since the screen last showed it. */
static void mark_changes(void)
{
    struct view *a = &shown, *b = &now_view;
    if (full_repaint) {
        paint_mark(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT);
        full_repaint = 0;
        *a = *b;
        return;
    }
    if (a->tempo != b->tempo || a->builtin != b->builtin || a->external != b->external) paint_mark(0, HEADER_Y, FM1_SCREEN_WIDTH, HEADER_H);
    if (b->page == PAGE_BREAKS) {
        /* The menu has no grid, knobs or transport line to keep up with. */
        if (a->page != PAGE_BREAKS || a->break_index != b->break_index || a->break_count != b->break_count)
            paint_mark(0, HEADER_H, FM1_SCREEN_WIDTH, NOTICE_Y - HEADER_H);
        if (a->notice != b->notice || a->notice_serial != b->notice_serial || a->status != b->status)
            paint_mark(0, NOTICE_Y, FM1_SCREEN_WIDTH, NOTICE_H);
        *a = *b;
        return;
    }
    /* Leaving the menu: the whole body below the header is the menu's. */
    if (a->page == PAGE_BREAKS) paint_mark(0, HEADER_H, FM1_SCREEN_WIDTH, NOTICE_Y - HEADER_H);
    if (a->warp != b->warp) paint_mark(2 * KNOB_W, KNOBS_Y, KNOB_W, KNOBS_H);
    if (a->feedback != b->feedback || a->root != b->root) paint_mark(0, KNOBS_Y, KNOB_W, KNOBS_H);
    if (a->page != b->page || a->saturation != b->saturation || a->compression != b->compression || a->lofi != b->lofi ||
        memcmp(a->eq, b->eq, sizeof b->eq) ||
        a->dly_send != b->dly_send || a->rev_send != b->rev_send || a->dly_fb != b->dly_fb || a->dly_hp != b->dly_hp ||
        a->dly_mix != b->dly_mix || a->dly_sync != b->dly_sync || a->dly_div != b->dly_div || a->dly_ms != b->dly_ms ||
        a->rev_type != b->rev_type || a->rev_size != b->rev_size || a->rev_damp != b->rev_damp || a->rev_mix != b->rev_mix ||
        a->lfo_wave != b->lfo_wave || a->lfo_depth != b->lfo_depth || a->lfo_time != b->lfo_time || a->lfo_dest != b->lfo_dest) {
        paint_mark(0, KNOBS_Y, FM1_SCREEN_WIDTH, KNOBS_H);
        paint_mark(0, FOOT_Y, FM1_SCREEN_WIDTH / 2, FOOT_H);
    }
    for (unsigned step = 0; step < JUNGLE_STEPS; ++step) {
        int playhead = (a->step == (int)step) != (b->step == (int)step) ||
                       (b->step == (int)step && (a->roll >= 0) != (b->roll >= 0));
        /* EDIT's cursor: its old and new cells, and every cell when the page changes. */
        int cursor = a->page != b->page ||
                     (b->page == PAGE_EDIT && a->edit_pos != b->edit_pos && !(step & 1U) &&
                      ((step >> 1) == a->edit_pos || (step >> 1) == b->edit_pos));
        if (playhead || cursor || memcmp(&a->steps[step], &b->steps[step], sizeof b->steps[step])) {
            unsigned x, y;
            step_rect(step, &x, &y);
            paint_mark(x, y, CELL_W, CELL_H + 4);
        }
    }
    if (a->roll != b->roll || a->playing != b->playing || a->bound != b->bound || a->stutter != b->stutter ||
        a->echo_fb != b->echo_fb || a->slice != b->slice || a->stretch_pitch != b->stretch_pitch)
        paint_mark(0, ROLL_Y, FM1_SCREEN_WIDTH, ROLL_H);
    if (a->frozen != b->frozen || a->auto_roll != b->auto_roll || a->transpose != b->transpose || a->muted != b->muted)
        paint_mark(0, TAGS_Y, FM1_SCREEN_WIDTH, TAGS_H);
    if (a->rhythm != b->rhythm) paint_mark(KNOB_W, KNOBS_Y, KNOB_W, KNOBS_H);
    if (a->filter != b->filter) paint_mark(3 * KNOB_W, KNOBS_Y, KNOB_W, KNOBS_H);
    if (a->notice != b->notice || a->notice_serial != b->notice_serial || a->status != b->status) paint_mark(0, NOTICE_Y, FM1_SCREEN_WIDTH, NOTICE_H);
    *a = *b;
}
/* Send changed bands while the LCD is free, at most ~4 ms per poll. */
static void paint_flush(void)
{
    uint32_t started = fm1_millis();
    for (unsigned visited = 0; visited < PAINT_BANDS; ++visited) {
        int pending = fm1_screen_pending();
        if (pending) {
            if (pending < 0) full_repaint = 1;
            break;
        }
        unsigned band = paint_next;
        paint_next = (band + 1U) % PAINT_BANDS;
        if (paint_left[band] >= paint_right[band]) continue;
        paint_top = band * PAINT_ROWS;
        paint_x = paint_left[band];
        paint_end = paint_right[band];
        draw_page();
        unsigned width = paint_end - paint_x;
        if (fm1_screen_submit(paint_x, paint_top, width, PAINT_ROWS, paint_pixels, width * PAINT_ROWS)) {
            full_repaint = 1;
            break;
        }
        paint_left[band] = FM1_SCREEN_WIDTH;
        paint_right[band] = 0;
        /* The DMA borrows paint_pixels until fm1_screen_pending says idle. */
        if (fm1_millis() - started >= 4U) break;
    }
}

/* ---- Controls ------------------------------------------------------------ */

/* Why no break plays, as the installer page words it; NULL when it plays. */
static const char *sample_status(void)
{
    if (jungle_native_bpm_x10()) return NULL;
    switch (groove_samples_status()) {
    case 1: return "READING SAMPLE BANK";
    case 2: return "NO BREAK: INSTALLER > BUILT-IN BANK";
    case 3: return "WRITING SAMPLE BANK";
    default: return "NO SAMPLE BANK: USE AMENITY INSTALLER";
    }
}
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void show_notice(const char *text_line, uint32_t now, uint32_t ms)
{
    notice = text_line;
    notice_until = now + ms;
    ++notice_serial;
}
static void update_roll(void)
{
    if (held_count) jungle_roll(held[held_count - 1]);
    else jungle_roll_off();
}
/* The delay time in the engine's half-rate samples (22.05 per ms): a division
 * of the tempo (a beat is 192 ticks, 13230000 / BPM-x10 samples) or the
 * milliseconds. Control task only: it divides. */
static void delay_apply(void)
{
    unsigned samples = dly_sync ? (unsigned)div_ticks[dly_div % DLY_DIVS] * 68906U / (tempo ? tempo : 1U)
                                : dly_ms * 441U / 20U;
    if (samples < 4U) samples = 4U;
    if (samples > JUNGLE_DELAY_MAX) samples = JUNGLE_DELAY_MAX;
    if (samples != dly_samples) {
        dly_samples = samples;
        jungle_delay_time(samples);
    }
}
/* EDIT: the slice at each of the sixteen places (RAM only, like the engine's). */
static uint8_t edit_slice[16];
static const uint8_t *edit_bound;
static void edit_reset(void)
{
    for (unsigned i = 0; i < 16; ++i) edit_slice[i] = (uint8_t)i;
    jungle_edit_reset();
}
static unsigned wrap16(int v) { return (unsigned)(((v % 16) + 16) % 16); }
static void apply_keys(uint32_t keys, uint32_t buttons, uint32_t now)
{
    uint32_t down = keys & ~previous_keys, up = previous_keys & ~keys;
    if (buttons & BUTTON(FX)) {
        /* FX held: a key sets the root; releases still end rolls. */
        for (unsigned key = 0; key < 27; ++key)
            if (down & (1U << key)) {
                root = key_pitch_class(key);
                jungle_feedback_note(root);
                memcpy(notice_text, "FEEDBACK ROOT ", 14);
                strcpy(notice_text + 14, note_names[root]);
                show_notice(notice_text, now, 1200U);
            }
        if (down) fx_used = 1; /* FX was a modifier, not a tap */
        down = 0;
    }
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t bit = 1U << white_keys[i];
        if (up & bit) {
            /* Remove from the held list; the previous key takes over. */
            unsigned k = 0;
            for (unsigned j = 0; j < held_count; ++j)
                if (held[j] != i) held[k++] = held[j];
            held_count = (uint8_t)k;
        }
        if ((down & bit) && held_count < 16) held[held_count++] = (uint8_t)i;
    }
    if (down | up) {
        update_roll();
        /* A new key, or a release, hands back what SELECT set on the last one:
         * the slice and the echo's feedback (BUILD's stutter and STRETCH's pitch stay). */
        slice_pick = -1;
        jungle_roll_slice_pick(-1);
        echo_fb = ECHO_DEFAULT;
        jungle_echo_feedback(echo_fb);
    }
    for (unsigned i = 0; i < 8; ++i)
        if (down & (1U << black_keys[i])) jungle_one_shot(shot_slices[i]);
    /* C#5: mute while held, even with FX held (before the whole effects chain). */
    unsigned mute_now = (keys >> black_keys[MUTE_BLACK]) & 1U;
    if (mute_now != muted) {
        muted = mute_now;
        jungle_mute(muted);
    }
    previous_keys = keys;
}
static void set_tempo(int bpm_x10)
{
    tempo = (unsigned)clamp(bpm_x10, JUNGLE_BPM_MIN_X10, JUNGLE_BPM_MAX_X10);
    jungle_tempo(tempo);
}
static void apply_buttons(uint32_t buttons, uint32_t now)
{
    uint32_t pressed = buttons & ~previous_buttons, released = previous_buttons & ~buttons;
    if (pressed & BUTTON(FX)) fx_used = 0;
    /* FX taps: FX page -> delay page -> reverb page -> back. */
    if ((released & BUTTON(FX)) && !fx_used)
        page = page == PAGE_FX ? PAGE_DELAY : page == PAGE_DELAY ? PAGE_REVERB : page == PAGE_REVERB ? PAGE_MAIN : PAGE_FX;
    if (pressed & BUTTON(ENV)) page = page == PAGE_EQ ? PAGE_MAIN : PAGE_EQ;
    if (pressed & BUTTON(GLO)) page = page == PAGE_BREAKS ? PAGE_MAIN : PAGE_BREAKS;
    if (pressed & BUTTON(EDIT)) page = page == PAGE_EDIT ? PAGE_MAIN : PAGE_EDIT;
    if (pressed & BUTTON(LFO)) page = page == PAGE_LFO ? PAGE_MAIN : PAGE_LFO;
    if ((pressed & BUTTON(PLAY)) && external) show_notice("EXTERNAL MIDI CLOCK", now, 1500U);
    else if (pressed & BUTTON(PLAY)) {
        playing = !playing;
        if (playing) jungle_seed(now);
        jungle_play(playing);
    }
    if (pressed & BUTTON(REC)) jungle_freeze(frozen = !frozen);
    if (pressed & BUTTON(SEQ)) jungle_auto_roll(auto_roll = !auto_roll);
    if ((pressed & BUTTON(HOME)) && page == PAGE_EDIT) {
        /* In EDIT, HOME puts the chops back in order and stays on the page. */
        edit_reset();
        show_notice("CHOPS IN ORDER", now, 1200U);
    } else if (pressed & BUTTON(HOME)) {
        /* Back to the plain break: no variation or warp until K2/K3 turn again. */
        rhythm = warp = 0;
        page = PAGE_MAIN;
        jungle_variation(0);
        jungle_warp(0);
        jungle_straight();
        show_notice("STRAIGHT BREAK", now, 1200U);
    }
    if ((pressed & BUTTON(OCT_DOWN)) && !external) set_tempo((int)tempo / 2);
    if ((pressed & BUTTON(OCT_UP)) && (buttons & BUTTON(SCL))) {
        led_test = (led_test + 1U) % 4U;
        for (unsigned i = 0; i < 3; ++i) fm1_led_matrix(spare_leds[i][0], spare_leds[i][1], led_test == i + 1U);
        if (led_test) {
            memcpy(notice_text, "LED TEST COLUMN ", 16);
            char *end = decimal(notice_text + 16, spare_leds[led_test - 1U][0], 0);
            memcpy(end, " ROW 1", 7);
            show_notice(notice_text, now, 4000U);
        } else show_notice("LED TEST OFF", now, 1200U);
        green_lit = 2; /* unknown: set again on the next update */
    } else if ((pressed & BUTTON(OCT_UP)) && !external) set_tempo((int)tempo * 2);
    previous_buttons = buttons;
}
static void apply_encoders(const struct fm1_controls *c)
{
    const int16_t *e = c->encoders;
    if (e[ENC_PRESETS] && !external) {
        int detents = e[ENC_PRESETS], step = (c->buttons & BUTTON(SCL)) ? 1 : 10;
        if (detents > 2 || detents < -2) step *= 2; /* fast turn */
        set_tempo((int)tempo + detents * step);
    }
    if (page == PAGE_EQ) {
        /* One dB per detent. */
        for (unsigned b = 0; b < 4; ++b)
            if (e[ENC_K1 + b]) {
                eq_db[b] = clamp(eq_db[b] + e[ENC_K1 + b], -12, 12);
                jungle_eq(b, eq_db[b]);
            }
    }
    if (page == PAGE_FX) {
        if (e[ENC_K1]) {
            saturation = (unsigned)clamp((int)saturation + 2 * e[ENC_K1], 0, 127);
            jungle_saturation(saturation);
        }
        if (e[ENC_K2]) {
            compression = (unsigned)clamp((int)compression + 2 * e[ENC_K2], 0, 127);
            jungle_compression(compression);
        }
        /* The sends are taken after the compressor. */
        if (e[ENC_K3]) {
            dly_send = (unsigned)clamp((int)dly_send + 2 * e[ENC_K3], 0, 127);
            jungle_delay_send(dly_send);
        }
        if (e[ENC_K4]) {
            rev_send = (unsigned)clamp((int)rev_send + 2 * e[ENC_K4], 0, 127);
            jungle_reverb_send(rev_send);
        }
    }
    if (page == PAGE_DELAY) {
        if (e[ENC_K1]) {
            if (dly_sync) dly_div = (unsigned)clamp((int)dly_div + e[ENC_K1], 0, DLY_DIVS - 1);
            else dly_ms = (unsigned)clamp((int)dly_ms + 5 * e[ENC_K1], DLY_MS_MIN, DLY_MS_MAX);
            delay_apply();
        }
        if (e[ENC_K2]) {
            dly_fb = (unsigned)clamp((int)dly_fb + 2 * e[ENC_K2], 0, 127);
            jungle_delay_feedback(dly_fb);
        }
        if (e[ENC_K3]) {
            dly_hp = (unsigned)clamp((int)dly_hp + 2 * e[ENC_K3], 0, 127);
            jungle_delay_highpass(dly_hp);
        }
        if (e[ENC_K4]) {
            dly_mix = (unsigned)clamp((int)dly_mix + 2 * e[ENC_K4], 0, 127);
            jungle_delay_mix(dly_mix);
        }
    }
    if (page == PAGE_REVERB) {
        if (e[ENC_K1]) rev_type = (unsigned)clamp((int)rev_type + e[ENC_K1], 0, JUNGLE_REVERB_TYPES - 1);
        if (e[ENC_K2]) rev_size = (unsigned)clamp((int)rev_size + 2 * e[ENC_K2], 0, 127);
        if (e[ENC_K3]) rev_damp = (unsigned)clamp((int)rev_damp + 2 * e[ENC_K3], 0, 127);
        if (e[ENC_K1] || e[ENC_K2] || e[ENC_K3]) jungle_reverb(rev_type, rev_size, rev_damp);
        if (e[ENC_K4]) {
            rev_mix = (unsigned)clamp((int)rev_mix + 2 * e[ENC_K4], 0, 127);
            jungle_reverb_mix(rev_mix);
        }
    }
    if (page == PAGE_LFO) {
        if (e[ENC_K1]) {
            lfo_wave = (unsigned)clamp((int)lfo_wave + e[ENC_K1], 0, JUNGLE_LFO_WAVES - 1);
            jungle_lfo_wave(lfo_wave);
        }
        if (e[ENC_K2]) {
            lfo_depth = (unsigned)clamp((int)lfo_depth + 2 * e[ENC_K2], 0, 127);
            jungle_lfo_depth(lfo_depth);
        }
        if (e[ENC_K3]) {
            lfo_time = (unsigned)clamp((int)lfo_time + e[ENC_K3], 0, LFO_TIMES - 1);
            jungle_lfo_time(lfo_time_ticks[lfo_time]);
        }
        if (e[ENC_K4]) {
            lfo_dest = (unsigned)clamp((int)lfo_dest + e[ENC_K4], 0, JUNGLE_LFO_DESTS - 1);
            jungle_lfo_dest(lfo_dest);
        }
    }
    int main_knobs = page == PAGE_MAIN || page == PAGE_EDIT;
    if (main_knobs && e[ENC_K1]) {
        feedback = (unsigned)clamp((int)feedback + 2 * e[ENC_K1], 0, 127);
        jungle_feedback(feedback);
    }
    if (main_knobs && e[ENC_K2]) {
        rhythm = (unsigned)clamp((int)rhythm + 2 * e[ENC_K2], 0, 127);
        jungle_variation(rhythm);
    }
    if (main_knobs && e[ENC_K3]) {
        /* Chance per 8th of a warped hit (stretch or an octave down) in the coming patterns. */
        warp = (unsigned)clamp((int)warp + 2 * e[ENC_K3], 0, 127);
        jungle_warp(warp);
    }
    if (main_knobs && e[ENC_K4]) {
        /* Two values per detent; crossing the middle stops at open first. */
        int next = clamp((int)filter + 2 * e[ENC_K4], 0, 127);
        if (((int)filter < 64 && next > 64) || ((int)filter > 64 && next < 64)) next = 64;
        filter = (unsigned)next;
        jungle_filter(filter);
        filter_turned = fm1_millis();
        if (filter_q != Q_SWEEP) jungle_filter_resonance(filter_q = Q_SWEEP);
    }
    if (e[ENC_SELECT] && page == PAGE_BREAKS) {
        /* The menu: SELECT walks the list and each break plays as it is reached. */
        int count = (int)groove_breaks_count(), now_at = (int)groove_break_current();
        int next = count ? clamp(now_at + e[ENC_SELECT], 0, count - 1) : now_at;
        if (count && next != now_at && groove_break_select((unsigned)next) == 0) {
            /* The loop's own tempo, unless a MIDI clock sets it. */
            if (!external) set_tempo((int)groove_break_bpm_x10((unsigned)next));
            const char *name = groove_break_name((unsigned)next);
            memcpy(notice_text, "BREAK ", 6);
            unsigned n = 0;
            for (; name && name[n] && n < 15U; ++n)
                notice_text[6 + n] = name[n] >= 'a' && name[n] <= 'z' ? (char)(name[n] - 32) : name[n];
            notice_text[6 + n] = 0;
            show_notice(notice_text, fm1_millis(), 1200U);
        }
    } else if (e[ENC_SELECT] && held_count) {
        /* A white key held: SELECT tunes that key's effect. */
        unsigned key = held[held_count - 1];
        int d = e[ENC_SELECT];
        if (key == 1U) {
            /* BUILD: one of 16 stereo stutters; it stays. */
            stutter = wrap16((int)stutter + d);
            jungle_stutter(stutter);
        } else if (key == 2U) {
            /* STRETCH: pitch of the stretched hits; it stays. */
            stretch_pitch = clamp(stretch_pitch + d, -12, 12);
            jungle_stretch_pitch(stretch_pitch);
        } else if (key == 3U) {
            /* ECHO: feedback, 5% a detent; back to default on release. */
            echo_fb = (unsigned)clamp((int)echo_fb + 5 * d, 75, 100);
            jungle_echo_feedback(echo_fb);
        } else {
            /* Repeats, downs, ups, shrinks: scan the chops from the one repeating. */
            int base = slice_pick >= 0 ? slice_pick : jungle_roll_slice();
            slice_pick = (int)wrap16(base + d);
            jungle_roll_slice_pick(slice_pick);
        }
    } else if (e[ENC_SELECT] && page == PAGE_EDIT) {
        edit_pos = wrap16((int)edit_pos + e[ENC_SELECT]);
    } else if (e[ENC_SELECT] && page == PAGE_DELAY) {
        dly_sync = !dly_sync; /* divisions <-> milliseconds */
        delay_apply();
    } else if (e[ENC_SELECT]) {
        lofi = (unsigned)clamp((int)lofi + 2 * e[ENC_SELECT], 0, 127);
        jungle_lofi(lofi);
    }
    if (e[ENC_ALGORITHM] && page == PAGE_EDIT) {
        /* The chop at the cursor, scrolled through the 16 slices. */
        edit_slice[edit_pos] = (uint8_t)wrap16((int)edit_slice[edit_pos] + e[ENC_ALGORITHM]);
        jungle_edit_slice(edit_pos, edit_slice[edit_pos]);
    } else if (e[ENC_ALGORITHM]) {
        transpose = clamp(transpose + e[ENC_ALGORITHM], -12, 12);
        jungle_pitch(transpose);
    }
}
/* LEDs: a running light over the white keys (one per 8th), held roll key
 * and pressed black keys lit; PLAY, REC and SEQ show their state. */
static void update_leds(uint32_t keys, uint32_t buttons, int step, int effect)
{
    uint32_t state = 0;
    if (buttons & BUTTON(FX)) {
        /* Choosing the root: the keys of the current one light up. */
        for (unsigned key = 0; key < 27; ++key)
            if (key_pitch_class(key) == root) state |= 1U << key;
        step = -1;
        keys = 0;
    }
    if (step >= 0) state |= 1U << white_keys[(unsigned)step >> 1];
    if (held_count) state |= 1U << white_keys[held[held_count - 1]];
    for (unsigned i = 0; i < 11; ++i) state |= keys & (1U << black_keys[i]);
    for (unsigned key = 0; key < 27; ++key)
        if ((state ^ led_state) & (1U << key)) fm1_led_set(14 + key, (state >> key) & 1U);
    led_state = state;
    /* PLAY: green while the pattern plays, red while a white-key effect
     * sounds (held or from SEQ). */
    unsigned running = (unsigned)jungle_playing();
    uint32_t lit = (running && effect >= 0 ? BUTTON(PLAY) : 0) | (frozen ? BUTTON(REC) : 0) | (auto_roll ? BUTTON(SEQ) : 0) |
                   (page == PAGE_EQ ? BUTTON(ENV) : 0) |
                   (page == PAGE_FX || page == PAGE_DELAY || page == PAGE_REVERB ? BUTTON(FX) : 0) |
                   (page == PAGE_BREAKS ? BUTTON(GLO) : 0) | (page == PAGE_EDIT ? BUTTON(EDIT) : 0) |
                   (page == PAGE_LFO ? BUTTON(LFO) : 0);
    uint8_t green = running && effect < 0;
    if (!led_test && green != green_lit) fm1_led_matrix(spare_leds[0][0], spare_leds[0][1], green);
    green_lit = green;
    for (unsigned b = 0; b < 14; ++b)
        if ((lit ^ button_leds) & BUTTON(b)) fm1_led_set(b, (lit >> b) & 1U);
    button_leds = lit;
}

/* sdk/Build.ps1 requires Bluetooth MIDI pairing to stay in the firmware; the
 * groovebox reached it from its BLE page, which AMENITY does not have yet. */
static void (*const keep_bluetooth[])(void) = {
    (void (*)(void))fm1_ble_scan, (void (*)(void))fm1_ble_connect,
    (void (*)(void))fm1_ble_disconnect, (void (*)(void))fm1_ble_get_status,
    (void (*)(void))fm1_ble_get_device, (void (*)(void))fm1_bluetooth_get_status,
};
static void (*const *volatile keep_linked)(void);

void groove_screen_init(void)
{
    keep_linked = keep_bluetooth;
    /* The engine copies the delay and reverb settings when it starts. */
    jungle_delay_send(dly_send);
    jungle_delay_feedback(dly_fb);
    jungle_delay_highpass(dly_hp);
    jungle_delay_mix(dly_mix);
    dly_samples = 0;
    delay_apply();
    jungle_reverb_send(rev_send);
    jungle_reverb(rev_type, rev_size, rev_damp);
    jungle_reverb_mix(rev_mix);
    jungle_init();
    edit_reset();
    edit_bound = jungle_bound();
    jungle_lfo_wave(lfo_wave);
    jungle_lfo_depth(lfo_depth);
    jungle_lfo_time(lfo_time_ticks[lfo_time]);
    jungle_lfo_dest(lfo_dest);
    jungle_stutter(stutter);
    jungle_stretch_pitch(stretch_pitch);
    jungle_echo_feedback(echo_fb);
    jungle_mute(0);
    jungle_tempo(tempo);
    jungle_variation(rhythm);
    jungle_filter(filter);
    jungle_warp(warp);
    jungle_stretch_hold(0);
    jungle_filter_resonance(filter_q);
    jungle_lofi(lofi);
    jungle_feedback(feedback);
    jungle_feedback_note(root);
    jungle_pitch(transpose);
    previous_keys = previous_buttons = led_state = button_leds = 0;
    held_count = 0;
    notice = NULL;
    fm1_controls_init();
    for (unsigned key = 0; key < 41; ++key) fm1_led_set(key, 0);
    fm1_screen_init();
    memset(paint_left, FM1_SCREEN_WIDTH, sizeof paint_left);
    memset(paint_right, 0, sizeof paint_right);
    paint_next = 0;
    full_repaint = 1;
    struct fm1_audio_stats dma;
    fm1_audio_stats_read(&dma);
    health_overruns = dma.overruns;
    health_at = fm1_millis();
}

void groove_screen_control(void)
{
    struct fm1_controls controls;
    fm1_controls_read(&controls);
    uint32_t now = fm1_millis();
    if (groove_master_update(controls.knob)) {
        unsigned level = fm1_engine_output_level();
        if (level) {
            memcpy(notice_text, "MASTER ", 7);
            memcpy(decimal(notice_text + 7, (int)percent(level), 0), " / 100", 7);
            show_notice(notice_text, now, 1200U);
        } else show_notice("MASTER MUTED", now, 1200U);
    }
    if (now - health_at >= 500U) {
        struct fm1_audio_stats dma;
        fm1_audio_stats_read(&dma);
        if (dma.overruns != health_overruns) show_notice("AUDIO OVERLOAD", now, 2500U);
        health_overruns = dma.overruns;
        health_at = now;
    }
    /* MIDI clock: while it leads, the internal transport stands down and
     * the tempo shown follows the one the engine locked to; a 0.1 BPM
     * difference only once it has lasted a second, so it does not flicker. */
    unsigned locked;
    external = (unsigned)jungle_clock(&locked);
    if (external) {
        if (playing) jungle_play(playing = 0);
        if (locked + 1U >= tempo && locked <= tempo + 1U) {
            if (locked == tempo) clock_near = now;
            else if (now - clock_near >= 1000U) set_tempo((int)locked);
        } else {
            set_tempo((int)locked);
            clock_near = now;
        }
    }
    apply_keys(controls.keys, controls.buttons, now);
    apply_buttons(controls.buttons, now);
    /* A new break or sample: the engine dropped the chop edits, so does the panel. */
    if (jungle_bound() != edit_bound) {
        edit_bound = jungle_bound();
        edit_reset();
    }
    apply_encoders(&controls);
    if (dly_sync) delay_apply(); /* follows the tempo */
    if (filter_q > Q_FLAT && now - filter_turned >= 500U && now - filter_q_step >= 30U) {
        filter_q_step = now;
        jungle_filter_resonance(--filter_q);
    }
    if (notice && (int32_t)(now - notice_until) >= 0) notice = NULL;

    jungle_pattern(now_view.steps);
    now_view.step = (int8_t)jungle_current_step();
    now_view.roll = (int8_t)jungle_roll_active();
    now_view.tempo = (uint16_t)tempo;
    now_view.rhythm = (uint8_t)rhythm;
    now_view.filter = (uint8_t)filter;
    now_view.warp = (uint8_t)warp;
    now_view.feedback = (uint8_t)feedback;
    now_view.page = (uint8_t)page;
    now_view.lofi = (uint8_t)lofi;
    now_view.saturation = (uint8_t)saturation;
    now_view.compression = (uint8_t)compression;
    for (unsigned b = 0; b < 4; ++b) now_view.eq[b] = (int8_t)eq_db[b];
    now_view.root = (uint8_t)root;
    now_view.playing = (uint8_t)jungle_playing();
    now_view.external = (uint8_t)external;
    now_view.frozen = (uint8_t)frozen;
    now_view.auto_roll = (uint8_t)auto_roll;
    now_view.transpose = (int8_t)transpose;
    now_view.notice = notice;
    now_view.notice_serial = notice_serial;
    now_view.status = sample_status();
    now_view.bound = now_view.status == NULL;
    now_view.break_index = (uint8_t)groove_break_current();
    now_view.break_count = (uint8_t)groove_breaks_count();
    now_view.stutter = (uint8_t)stutter;
    now_view.echo_fb = (uint8_t)echo_fb;
    now_view.slice = (uint8_t)(jungle_roll_slice() & 15);
    now_view.muted = (uint8_t)muted;
    now_view.stretch_pitch = (int8_t)stretch_pitch;
    now_view.dly_send = (uint8_t)dly_send;
    now_view.rev_send = (uint8_t)rev_send;
    now_view.dly_fb = (uint8_t)dly_fb;
    now_view.dly_hp = (uint8_t)dly_hp;
    now_view.dly_mix = (uint8_t)dly_mix;
    now_view.dly_sync = (uint8_t)dly_sync;
    now_view.dly_div = (uint8_t)dly_div;
    now_view.dly_ms = (uint16_t)dly_ms;
    now_view.rev_type = (uint8_t)rev_type;
    now_view.rev_size = (uint8_t)rev_size;
    now_view.rev_damp = (uint8_t)rev_damp;
    now_view.rev_mix = (uint8_t)rev_mix;
    now_view.edit_pos = (uint8_t)edit_pos;
    now_view.lfo_wave = (uint8_t)lfo_wave;
    now_view.lfo_depth = (uint8_t)lfo_depth;
    now_view.lfo_time = (uint8_t)lfo_time;
    now_view.lfo_dest = (uint8_t)lfo_dest;
    const uint8_t *builtin = NULL;
    unsigned builtin_bytes, builtin_frames;
    now_view.builtin = (uint8_t)(jungle_fallback(&builtin, &builtin_bytes, &builtin_frames) && jungle_bound() == builtin);
    update_leds(controls.keys, controls.buttons, now_view.playing ? now_view.step : -1, now_view.roll);
    mark_changes();
    paint_flush();
}

/* MIDI Machine Control from the transfer link: play and stop. */
void groove_screen_mmc(unsigned command)
{
    if (command == 2 || command == 3) { playing = 1; jungle_play(1); }
    else if (command == 1 || command == 9) { playing = 0; jungle_play(0); }
}

#ifdef FM1_HOST_TEST
/* The browser simulator (web/engine/Simulator.c) was written for the
 * groovebox pages. AMENITY has one page: these keep its hooks inert. */
void groove_screen_tap(uint32_t now) { (void)now; }
void groove_screen_snapshot(uint8_t state[8]) { memset(state, 0, 8); }
int groove_screen_open(unsigned target) { (void)target; return -1; }
int groove_screen_preset(unsigned slot) { (void)slot; return -1; }
unsigned groove_screen_preset_position(void) { return 0; }
const char *groove_screen_web_knobs(void) { return "FEEDBACK|VARIATION|WARP|FILTER"; }
unsigned groove_screen_web_first(void) { return 0; }
int groove_screen_web_step(unsigned step, unsigned toggle) { (void)step; (void)toggle; return -1; }
const void *groove_screen_web_targets(void) { return NULL; }
unsigned groove_screen_web_target_count(void) { return 0; }
unsigned groove_screen_web_context(void) { return 0; }
unsigned groove_screen_web_mask(void) { return 0; }
int groove_screen_web_adjust(unsigned context, unsigned action, unsigned index, unsigned owner, int delta)
{
    (void)context; (void)action; (void)index; (void)owner; (void)delta;
    return -1;
}
void groove_screen_web_fine(unsigned fine) { (void)fine; }
#endif
