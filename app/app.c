/* SPDX-License-Identifier: GPL-3.0-only */
/* AMENITY on fm1-base: the panel, the screen, MIDI clock in and out, around the break engine (jungle/).
 *
 *   white keys (hold)  rolls and effects, last held wins     black keys 1-8  one-shot hits
 *   top three black keys (hold)  C# feedback at 75%, D# LO-FI at its most, F# an octave down: the knobs'
 *                                values again on release; the SEQ fill picks these too
 *   K1 FEEDBACK  K2 VARIATION  K3 WARP  K4 FILTER            FX held + key   the feedback's root
 *   PRESET the break (the built-in ones, then your own: user_breaks.c)  ALGORITHM transpose (down)
 *   SELECT tempo (1 BPM; SEL held: 0.1)  SEL + OCT- / OCT+ 0.1 BPM  OCT- / OCT+ halve / double tempo
 *   PLAY start/stop  REC freeze  SEQ fill  HOME straight break
 *   EDIT held: the audio's CPU load in the header
 *   ENV: EQ page (K1-K4: 100 Hz, 500 Hz, 2.5 kHz, 8 kHz)  FX tap: FX page (K1 saturation, K2 compression, K3 LO-FI)
 *   MIDI clock: a Start or Continue from an input leads (EXT; PLAY and tempo rest); on its own clock AMENITY
 *   sends Start, clock and Stop.
 *
 * Part of fm1-base's unity build, after the platform: gfx.c is in scope. */
#include "jungle.h"
#include "jungle_mul.h"

/* ------------------------------------------------------------------ audio --- */
/* What the render makes is heard one block later (the I2S buffer's other half): the engine works on that time, so
 * the break and the clock it sends line up with what comes out. */
#define HEARD_TICKS ((uint32_t)((uint64_t)APP_BLOCK * PLAT_TICKS_PER_US * 1000000u / 44100u))
static volatile uint8_t muted;
/* The time stamps' clock: the 24 MHz timer. The host simulator's plat_ticks24 is the computer's real time (for
 * measuring the render), which runs far behind the simulation: there the simulated milliseconds stand in. */
#ifdef PLAT_HOST
static uint32_t now_ticks(void) { return plat_ms() * (PLAT_TICKS_PER_US * 1000u); }
#else
static uint32_t now_ticks(void) { return plat_ticks24(); }
#endif

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    if (muted)
        return;
    jungle_render(lr, n, now_ticks() + HEARD_TICKS);
    for (i = 0; i < 2u * n; i++)
        lr[i] = mul_shr(lr[i], (int32_t)master_q12, 12);
}

void app_panic(void) { muted = 1; }

/* ------------------------------------------------------------- MIDI clock --- */
/* Clock out: the engine passes each message with its time (audio interrupt), the tick sends it then. */
enum { OUT_Q = 32 };
static struct { uint32_t at; uint8_t status; } out_q[OUT_Q];
static volatile uint32_t out_w, out_r;
void jungle_midi_out(unsigned status, uint32_t at)
{
    uint32_t w = out_w;
    if (w - out_r >= OUT_Q)
        return;
    out_q[w % OUT_Q].at = at;
    out_q[w % OUT_Q].status = (uint8_t)status;
    __asm__ volatile("" ::: "memory");
    out_w = w + 1u;
}

/* Clock in: every realtime message, stamped as the tick reads it (the tick runs right after the USB poll). */
static volatile uint8_t clock_ext;
static volatile uint16_t clock_bpm;
void app_tick(void)
{
    uint32_t pkt, now = now_ticks();
    unsigned bpm;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
            jungle_midi_realtime(st, (unsigned)port, now);
    }
    clock_ext = (uint8_t)jungle_clock(&bpm, now);
    clock_bpm = (uint16_t)bpm;
    while (out_r != out_w) {
        uint32_t r = out_r;
        if ((int32_t)(now - out_q[r % OUT_Q].at) < 0)
            break;
        plat_midi_send(0x0Fu | (uint32_t)out_q[r % OUT_Q].status << 8, TO_USB | TO_BLE);   /* CIN F: one byte */
        out_r = r + 1u;
    }
}

/* ------------------------------------------------------------------ state --- */
enum { PAGE_MAIN, PAGE_EQ, PAGE_FX };
enum { Q_SWEEP = 14, Q_FLAT = 8 };       /* the DJ filter rings only while K4 turns */
static struct {
    unsigned tempo, rhythm, filter, warp, feedback, root, lofi, saturation, compression, brk;
    int transpose, eq_db[4];
    uint8_t playing, frozen, auto_roll, page, fx_used, external, filter_q;
    uint32_t filter_turned, filter_q_step, clock_near;
} st = {.tempo = 1600, .filter = 64, .root = 4, .filter_q = Q_FLAT};

static const uint8_t WHITE[16] = PLAT_WHITE_KEYS, BLACK[11] = PLAT_BLACK_KEYS;
static const char *const ROLL_NAME[16] = {
    "1/4 REPEAT", "BUILD", "STRETCH", "ECHO", "1/8 REPEAT", "1/8 DOWN", "1/8 UP", "1/8 SHRINK",
    "1/16 REPEAT", "1/16 DOWN", "1/16 UP", "1/16 SHRINK", "1/32 REPEAT", "1/32 DOWN", "1/32 UP", "1/32 SHRINK"};
static const char *const INSTANT_NAME[4] = {"FEEDBACK 75%", "LO-FI MAX", "OCTAVE DOWN", "FILTER SWEEP"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void tenths_text(unsigned v, char *b)  /* 1725 -> "172.5" */
{
    char t[8];
    int_text((int32_t)(v / 10u), b);
    t[0] = '.';
    t[1] = (char)('0' + v % 10u);
    t[2] = 0;
    text_cat(b, t);
}

/* ---------------------------------------------------------------- messages --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t held[16], held_n;
    uint8_t redraw;
    /* the little flashes when something starts (ANIM_MS each): the screen glowing in its colour, the state line's
     * name popping out of a block, a flip's ripple, a roll's shake, a warped step's move */
    uint32_t spark_t, pop_t, flip_t, glitch_t, warp_t;
    uint16_t spark_c, spark_lv;              /* spark_lv: how strong the screen's glow, /256 */
    uint8_t flip_bar, warp_kind, warp_step;  /* warp_kind: 1 a stretch step stretching, 2 an octave-down step sinking */
} ui;
#define ANIM_MS 260u
#define POP_MS 160u

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
}

/* ------------------------------------------------------------------- init --- */
static unsigned breaks_count(void);
static const struct jungle_break *break_at(unsigned i);
/* the break PRESET has picked: it plays from the next hit */
static void select_break(unsigned i)
{
    const struct jungle_break *b = break_at(i);
    if (!b)
        return;
    st.brk = i;
    (void)jungle_bind(b->data, b->bytes, b->frames);
    ui.redraw = 1;
}

#include "user_breaks.c"

void app_init(void)
{
    jungle_init();
    order_load();
    user_scan();
    select_break(0);
    jungle_tempo(st.tempo);
    jungle_variation(st.rhythm);
    jungle_filter(st.filter);
    jungle_filter_resonance(st.filter_q);
    jungle_warp(st.warp);
    jungle_feedback(st.feedback);
    jungle_feedback_note(st.root);
    jungle_lofi(st.lofi);
    jungle_pitch(st.transpose);
}

/* ------------------------------------------------------------------ input --- */
static void set_tempo(int bpm_x10)
{
    st.tempo = (unsigned)clamp(bpm_x10, JUNGLE_BPM_MIN_X10, JUNGLE_BPM_MAX_X10);
    jungle_tempo(st.tempo);
    ui.redraw = 1;
}

/* halve / double: from the next beat (shown amber until then) */
static void tempo_next(int bpm_x10)
{
    st.tempo = (unsigned)clamp(bpm_x10, JUNGLE_BPM_MIN_X10, JUNGLE_BPM_MAX_X10);
    jungle_tempo_next(st.tempo);
    ui.redraw = 1;
}

static void keys_input(uint32_t keys, uint32_t btn)
{
    uint32_t down = keys & ~ui.keys, up = ui.keys & ~keys, i;
    if (btn >> B_FX & 1u) {                  /* FX held: a key sets the feedback's root */
        for (i = 0; i < NKEYS; i++)
            if (down >> i & 1u) {
                st.root = (5u + i) % 12u;    /* key 0 is F3 */
                jungle_feedback_note(st.root);
                app_say("FEEDBACK ROOT", NOTE_NAME[st.root]);
            }
        if (down)
            st.fx_used = 1;                  /* FX was a modifier, not a tap */
        down = 0;
    }
    for (i = 0; i < 16u; i++) {              /* rolls: the last white key held wins, a release hands back */
        uint32_t bit = 1u << WHITE[i], k, n = 0;
        if (up & bit) {
            for (k = 0; k < ui.held_n; k++)
                if (ui.held[k] != i)
                    ui.held[n++] = ui.held[k];
            ui.held_n = (uint8_t)n;
        }
        if ((down & bit) && ui.held_n < 16u)
            ui.held[ui.held_n++] = (uint8_t)i;
    }
    if (down | up) {
        if (ui.held_n)
            jungle_roll(ui.held[ui.held_n - 1u]);
        else
            jungle_roll_off();
    }
    {                                        /* one-shots on the first eight black keys: the break's own */
        const struct jungle_break *b = break_at(st.brk);
        for (i = 0; b && i < 8u; i++)
            if (down >> BLACK[i] & 1u)
                jungle_one_shot(b->shots[i]);
    }
    {                                        /* the top three black keys: instant effects while held */
        uint32_t mask = 0;
        if (!(btn >> B_FX & 1u))
            for (i = 0; i < 3u; i++)
                if (keys >> BLACK[8u + i] & 1u)
                    mask |= 1u << i;
        jungle_instant(mask);
    }
}

static void buttons_input(uint32_t btn, uint32_t now)
{
    uint32_t pressed = btn & ~ui.btn, released = ui.btn & ~btn;
#define PRESSED(b) (pressed >> (b) & 1u)
    if (PRESSED(B_FX))
        st.fx_used = 0;
    if ((released >> B_FX & 1u) && !st.fx_used)
        st.page = st.page == PAGE_FX ? PAGE_MAIN : PAGE_FX;
    if (PRESSED(B_ENV))
        st.page = st.page == PAGE_EQ ? PAGE_MAIN : PAGE_EQ;
    if (PRESSED(B_PLAY)) {
        if (st.external)
            app_say("EXTERNAL", "MIDI CLOCK");
        else {
            st.playing = !st.playing;
            if (st.playing)
                jungle_seed(now);
            jungle_play(st.playing);
        }
    }
    if (PRESSED(B_REC))
        jungle_freeze(st.frozen = !st.frozen);
    if (PRESSED(B_SEQ))
        jungle_auto_roll(st.auto_roll = !st.auto_roll);
    if (PRESSED(B_HOME)) {                   /* the plain break: no variation or warp until K2 / K3 turn again */
        st.rhythm = st.warp = 0;
        st.page = PAGE_MAIN;
        jungle_variation(0);
        jungle_warp(0);
        jungle_straight();
        app_say("STRAIGHT", "BREAK");
    }
    if (PRESSED(B_OCTDN) && !st.external)    /* halve from the next beat, or with SEL held 0.1 BPM down */
        (btn >> B_SEL & 1u) ? set_tempo((int)st.tempo - 1) : tempo_next((int)st.tempo / 2);
    if (PRESSED(B_OCTUP) && !st.external)
        (btn >> B_SEL & 1u) ? set_tempo((int)st.tempo + 1) : tempo_next((int)st.tempo * 2);
#undef PRESSED
    if (pressed | released)
        ui.redraw = 1;
}

static unsigned turn(unsigned v, int32_t detents) { return (unsigned)clamp((int32_t)v + 2 * detents, 0, 127); }

static void encoders_input(uint32_t btn, uint32_t now)
{
    int32_t e[NE], k;
    for (k = 0; k < NE; k++)
        if ((e[k] = plat_enc(k)) != 0)
            ui.redraw = 1;
    if (e[EN_SELECT] && !st.external)        /* SELECT: the tempo, 1 BPM a detent (2 turned fast), 0.1 with SEL */
        set_tempo((int)st.tempo + e[EN_SELECT] * ((btn >> B_SEL & 1u) ? 1 : e[EN_SELECT] > 2 || e[EN_SELECT] < -2 ? 20 : 10));
    if (e[EN_PRESET] && breaks_count()) {        /* PRESET: the next or the previous break, round */
        int n = (int)breaks_count(), next = ((int)st.brk + e[EN_PRESET]) % n;
        select_break((unsigned)(next < 0 ? next + n : next));
        app_say("BREAK", break_at(st.brk)->name);
    }
    if (st.page == PAGE_EQ) {
        for (k = 0; k < 4; k++)
            if (e[EN_K1 + k]) {
                st.eq_db[k] = clamp(st.eq_db[k] + e[EN_K1 + k], -12, 12);
                jungle_eq((unsigned)k, st.eq_db[k]);
            }
    } else if (st.page == PAGE_FX) {
        if (e[EN_K1])
            jungle_saturation(st.saturation = turn(st.saturation, e[EN_K1]));
        if (e[EN_K2])
            jungle_compression(st.compression = turn(st.compression, e[EN_K2]));
        if (e[EN_K3])
            jungle_lofi(st.lofi = turn(st.lofi, e[EN_K3]));
    } else {
        if (e[EN_K1])
            jungle_feedback(st.feedback = turn(st.feedback, e[EN_K1]));
        if (e[EN_K2])
            jungle_variation(st.rhythm = turn(st.rhythm, e[EN_K2]));
        if (e[EN_K3])
            jungle_warp(st.warp = turn(st.warp, e[EN_K3]));
        if (e[EN_K4]) {                      /* crossing the middle stops at open first */
            int next = (int)turn(st.filter, e[EN_K4]);
            if (((int)st.filter < 64 && next > 64) || ((int)st.filter > 64 && next < 64))
                next = 64;
            jungle_filter(st.filter = (unsigned)next);
            st.filter_turned = now;
            if (st.filter_q != Q_SWEEP)
                jungle_filter_resonance(st.filter_q = Q_SWEEP);
        }
    }
    if (e[EN_ALGO]) {                        /* down only: pitched up sounded thin */
        st.transpose = clamp(st.transpose + e[EN_ALGO], -12, 0);
        jungle_pitch(st.transpose);
    }
}

/* MIDI clock: while it leads, PLAY rests and the tempo shown is the clock's (a 0.1 BPM difference only once it has
 * lasted a second, so it does not flicker) */
static void clock_follow(uint32_t now)
{
    unsigned locked = clock_bpm;
    if (clock_ext != st.external) {
        st.external = clock_ext;
        ui.redraw = 1;
    }
    if (!st.external)
        return;
    if (st.playing)
        jungle_play(st.playing = 0);
    if (locked + 1u >= st.tempo && locked <= st.tempo + 1u) {
        if (locked == st.tempo)
            st.clock_near = now;
        else if (now - st.clock_near >= 1000u)
            set_tempo((int)locked);
    } else {
        set_tempo((int)locked);
        st.clock_near = now;
    }
}

/* Lights: lit (100%), what you hold and the running light (one white key an 8th, where the pattern is); at
 * felucca-bsp's mid level (about 12%), what plays by itself: the slice sounding on its white key and on the black
 * keys whose one-shot it is, a roll and the instant effects from a fill. (The panel has these two levels and a glow: no 75%.) */
static void lights(void)
{
    uint32_t btn = 0, keys = ui.keys, mid = 0, i;
    int step = jungle_current_step(), slice = jungle_current_slice(), roll = jungle_roll_active();
    if (jungle_playing())
        btn |= 1u << B_PLAY;
    if (st.frozen)
        btn |= 1u << B_REC;
    if (st.auto_roll)
        btn |= 1u << B_SEQ;
    if (st.page == PAGE_FX)
        btn |= 1u << B_FX;
    if (st.page == PAGE_EQ)
        btn |= 1u << B_ENV;
    if (step >= 0)
        keys |= 1u << WHITE[(unsigned)step / 2u];
    if (slice >= 0) {
        const struct jungle_break *b = break_at(st.brk);
        mid |= 1u << WHITE[slice];
        for (i = 0; b && i < 8u; i++)        /* and the black one-shot keys that play this slice */
            if (b->shots[i] == (uint8_t)slice)
                mid |= 1u << BLACK[i];
    }
    if (roll >= 0)
        mid |= 1u << WHITE[roll];
    for (i = 0; i < 3u; i++)
        if (jungle_instant_active() >> i & 1u)
            mid |= 1u << BLACK[8u + i];
    if (ui.btn >> B_FX & 1u)                 /* FX held: the keys of the feedback's root */
        for (i = 0; i < NKEYS; i++)
            if ((5u + i) % 12u == st.root)
                keys |= 1u << i;
    plat_play_red(roll >= 0 || jungle_instant_active());   /* red while an effect sounds, held or from a fill */
    plat_glow(1);
    plat_leds(btn, keys);
    plat_lights_mid(0, mid & ~keys);
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms();
    clock_follow(now);
    keys_input(keys, btn);
    buttons_input(btn, now);
    encoders_input(btn, now);
    if (st.filter_q > Q_FLAT && now - st.filter_turned >= 500u && now - st.filter_q_step >= 30u) {
        st.filter_q_step = now;
        jungle_filter_resonance(--st.filter_q);
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    lights();
}

void app_input_only(void) { input(); }

/* ----------------------------------------------------------------- screen --- */
#define C_CYAN RGB(60, 200, 235)
#define C_GREEN RGB(80, 220, 120)
#define C_ORANGE RGB(240, 150, 40)
#define C_PURPLE RGB(170, 90, 230)
#define C_BLUE RGB(70, 120, 235)
#define C_RED RGB(235, 60, 50)

/* a knob's label, value and bar: filled from the left, or (centred) from the middle, as v1 drew them; with a
 * note (amber), the value waits for something: "NEXT" pattern, or "FROZEN" */
static void value_note(int32_t x, int32_t y, const char *label, const char *v, unsigned of127, int centred,
                       const char *note)
{
    int32_t w = 50, at = (int32_t)(of127 > 127u ? 127u : of127) * w / 127, mid = w / 2;
    cv_text(x, y, &FONT_XS, label, note ? C_ORANGE : C_GRAY);
    cv_text(x, y + 13, &FONT_M, v, note ? C_ORANGE : C_WHITE);
    if (note)
        cv_text(x, y + 45, &FONT_T, note, C_ORANGE);
    cv_rect(x, y + 40, w, 3, C_LINE);
    if (!centred)
        cv_rect(x, y + 40, at, 3, C_CYAN);
    else if (at == mid)
        cv_rect(x + mid - 1, y + 39, 2, 5, C_CYAN);   /* at the middle: a mark */
    else
        cv_rect(x + (at < mid ? at : mid), y + 40, at < mid ? mid - at : at - mid, 3, C_CYAN);
}
static void value(int32_t x, int32_t y, const char *label, const char *v, unsigned of127, int centred)
{
    value_note(x, y, label, v, of127, centred, 0);
}

/* between two colours: a at 0, b at 256 */
static uint16_t mix(uint16_t a, uint16_t b, uint32_t t)
{
    uint32_t u = 256u - t;
    return (uint16_t)((((a >> 11) * u + (b >> 11) * t) >> 8) << 11 | (((a >> 5 & 63u) * u + (b >> 5 & 63u) * t) >> 8) << 5 |
                      (((a & 31u) * u + (b & 31u) * t) >> 8));
}
/* a colour at level/256 of itself */
static uint16_t fade(uint16_t c, uint32_t level)
{
    uint32_t r = (c >> 11) * level >> 8, g = (c >> 5 & 63u) * level >> 8, b = (c & 31u) * level >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}
/* the state line's red name (rolls, effects): for POP_MS after something started by itself, black on a block */
static void state_name(const char *name, const char *more, uint32_t now)
{
    if (ui.pop_t && now - ui.pop_t < POP_MS) {
        int32_t w = text_w(&FONT_M, name) + 8;
        cv_rect(2, 0, w, 26, C_RED);
        cv_text(6, 0, &FONT_M, name, C_BLACK);
        cv_text(w + 6, 0, &FONT_M, more, C_RED);
    } else {
        int32_t x = cv_text(6, 0, &FONT_M, name, C_RED);
        cv_text(x + 4, 0, &FONT_M, more, C_RED);
    }
}

static void draw(void)
{
    char t[24];
    uint32_t now = plat_ms(), i;
    int step = jungle_current_step(), roll = jungle_roll_active();
    struct jungle_step pat[JUNGLE_STEPS];
    /* the header: the name, EXT, the tempo; or a message for a moment */
    /* the whole screen glows in the colour of what started by itself, fading out */
    uint32_t glow_p = ui.spark_t && now - ui.spark_t < ANIM_MS ? (now - ui.spark_t) * 256u / ANIM_MS : 256u;
    uint16_t bg = glow_p < 256u ? fade(ui.spark_c, (256u - glow_p) * ui.spark_lv / 256u) : C_BLACK;
    cv_begin(240, 38, bg);                   /* (down to the grid: no black seam between them) */
    if (now < ui.msg_until) {
        int32_t x = cv_text(6, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_CYAN);
    } else {
        int32_t x = cv_text(6, 2, &FONT_M, APP_NAME, C_CYAN);
        if (st.external)
            cv_text(x + 6, 4, &FONT_XS, "EXT", C_GREEN);
        if (FM1B_BLE)
            cv_text(x + 6, 17, &FONT_XS, "BLE", plat_ble_connected() ? C_HI : C_DIM);
        if (ui.btn >> B_EDIT & 1u) {         /* EDIT held: how busy the audio is */
            t[0] = 0;
            text_cat(t, "CPU ");
            int_text((int32_t)plat_cpu_pct(), t + 4);
            text_cat(t, "%");
            cv_text(234 - text_w(&FONT_M, t), 4, &FONT_M, t, C_ORANGE);
        } else {
            tenths_text(st.tempo, t);
            cv_text(234 - text_w(&FONT_L, t), 1, &FONT_L, t, jungle_tempo_pending() ? C_ORANGE : C_WHITE);
        }
    }
    cv_blit(0, 0);
    /* the pattern: 32 steps in two rows, the step playing marked below it; a hit white (stretch purple, octave
     * down blue), an empty step nothing, as in v1. What plays by itself or is held moves it about a little: a flip
     * sends a cyan ripple out from its first step, an automatic roll shakes the rows, a sweep darkens the hits as
     * the filter closes, feedback gives every hit an echo, LO-FI lays scan lines over it, an octave down sinks it */
    jungle_pattern(pat);
    cv_begin(240, 76, bg);
    {
        unsigned fx = jungle_instant_active();
        uint32_t ripple = ui.flip_t && now - ui.flip_t < ANIM_MS ? (now - ui.flip_t) * 24u / ANIM_MS + 1u : 0u;
        int shake = ui.glitch_t && now - ui.glitch_t < ANIM_MS ? ((now / 33u) & 1u ? 3 : -3) : 0;
        uint32_t closed = (fx & JUNGLE_FX_SWEEP) && jungle_filter_now() < 64u ? (64u - jungle_filter_now()) * 200u / 64u : 0u;
        int32_t sink = (fx & JUNGLE_FX_OCTAVE) ? 3 : 0;
        /* a warped step as it plays: a stretch one stretches out and back, an octave-down one sinks the grid */
        uint32_t wp = ui.warp_t && now - ui.warp_t < ANIM_MS ? (now - ui.warp_t) * 256u / ANIM_MS : 256u;
        uint32_t swell = wp < 256u ? (wp < 64u ? wp * 4u : (256u - wp) * 256u / 192u) : 0u;   /* 0..256 and back */
        int32_t wx = -99, wy = 0;
        if (ui.warp_kind == 2u)
            sink += (int32_t)(swell * 7u / 256u);
        for (i = 0; i < JUNGLE_STEPS; i++) {
            int32_t x = 4 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u / 4u) * 2 + (i / 16u ? -shake : shake);
            int32_t y = (int32_t)(i / 16u) * 36 + sink;
            int32_t d = ripple ? (int32_t)i - (int32_t)(ui.flip_bar * 16u + 10u) : 99;
            int lit = ripple && (d < 0 ? -d : d) + 1 >= (int32_t)ripple - 1 && (d < 0 ? -d : d) + 1 <= (int32_t)ripple;
            uint16_t c = pat[i].stretch ? C_PURPLE : pat[i].pitch < 0 || sink ? C_BLUE : C_WHITE;
            if (closed)
                c = mix(c, RGB(20, 70, 90), closed);
            if (pat[i].active && (fx & JUNGLE_FX_FEEDBACK))
                cv_rect(x + 3, y + 3, 12, 26, RGB(40, 90, 110));   /* the echo, behind */
            if (pat[i].active && ui.warp_kind == 1u && swell && i == ui.warp_step) {   /* (drawn last, on top) */
                wx = x;
                wy = y;
            }
            if (pat[i].active)
                cv_rect(x, y, 12, 26, lit ? C_CYAN : c);
            else if (lit)
                cv_rect(x, y + 8, 12, 10, fade(C_CYAN, 140u));
            if (pat[i].active && pat[i].flip && !lit) {   /* a flip's steps: a thin cyan rim */
                cv_rect(x, y, 12, 1, C_CYAN);
                cv_rect(x, y + 25, 12, 1, C_CYAN);
                cv_rect(x, y, 1, 26, C_CYAN);
                cv_rect(x + 11, y, 1, 26, C_CYAN);
            }
            if (pat[i].active) {
                char h[2] = {"0123456789ABCDEF"[pat[i].slice & 15u], 0};
                cv_text(x + 3, y + 6, &FONT_XS, h, C_BLACK);
            }
            if ((int)i == step)
                cv_rect(x, y + 28, 12, 3, C_GREEN);
        }
        if (wx > -99) {                      /* the stretch step stretching out and back */
            int32_t grow = (int32_t)(swell * 28u / 256u);
            char h[2] = {"0123456789ABCDEF"[pat[ui.warp_step].slice & 15u], 0};
            cv_rect(wx - grow, wy, 12 + 2 * grow, 26, C_PURPLE);
            cv_text(wx + 3, wy + 6, &FONT_XS, h, C_BLACK);
        }
        if (fx & JUNGLE_FX_LOFI)             /* scan lines */
            for (i = 1; i < 76u; i += 4u)
                cv_rect(0, (int32_t)i, 240, 1, bg);
    }
    cv_blit(0, 38);
    /* what is happening, the modes, the knobs of the page */
    cv_begin(240, 126, bg);
    if (roll >= 0)
        state_name(ROLL_NAME[roll], "", now);
    else if (jungle_instant_active()) {      /* the first of them by name, + if there are more */
        unsigned fx = jungle_instant_active(), k = fx & 1u ? 0u : fx & 2u ? 1u : fx & 4u ? 2u : 3u;
        state_name(INSTANT_NAME[k], fx & ~(1u << k) ? "+" : "", now);
    } else if (st.external && !jungle_playing())
        cv_text(6, 0, &FONT_M, "EXT: WAITING", C_GREEN);
    else
        cv_text(6, 0, &FONT_M, jungle_playing() ? "PLAYING" : "STOPPED", jungle_playing() ? C_GREEN : C_GRAY);
    {
        const struct jungle_break *b = break_at(st.brk);
        const char *name = b ? b->name : "NO BREAK";
        cv_text(234 - text_w(&FONT_B, name), 6, &FONT_B, name, b ? C_WHITE : C_RED);
    }
    {
        int32_t x = 6;
        if (st.frozen)
            x = cv_text(x, 28, &FONT_XS, "FREEZE", C_ORANGE) + 10;
        if (st.auto_roll)
            x = cv_text(x, 28, &FONT_XS, "SEQ FILL", C_GREEN) + 10;
        if (st.transpose) {
            int_text(st.transpose, t);
            x = cv_text(x, 28, &FONT_XS, "TRANSPOSE", C_GRAY) + 4;
            cv_text(x, 28, &FONT_XS, t, C_WHITE);
        }
    }
    if (st.page == PAGE_EQ) {
        static const char *const BAND[4] = {"LOW", "LO-MID", "HI-MID", "HIGH"};
        for (i = 0; i < 4u; i++) {
            t[0] = 0;
            if (st.eq_db[i] > 0)
                text_cat(t, "+");
            int_text(st.eq_db[i], t + (st.eq_db[i] > 0));
            text_cat(t, "DB");
            value(6 + (int32_t)i * 58, 46, BAND[i], t, (unsigned)(st.eq_db[i] + 12) * 127u / 24u, 1);
        }
    } else if (st.page == PAGE_FX) {
        int_text((int32_t)st.saturation, t);
        value(6, 46, "SATURATE", t, st.saturation, 0);
        int_text((int32_t)st.compression, t);
        value(64, 46, "COMPRESS", t, st.compression, 0);
        if (st.lofi) {                       /* LO-FI: its rate in kHz */
            unsigned hz, bits;
            jungle_lofi_view(st.lofi, &hz, &bits);
            int_text((int32_t)((hz + 500u) / 1000u), t);
            text_cat(t, "K");
        } else
            t[0] = 0, text_cat(t, "OFF");
        value(122, 46, "LO-FI", t, st.lofi, 0);
    } else {
        int_text((int32_t)((st.feedback * 100u + 63u) / 127u), t);
        text_cat(t, "%");
        value(6, 46, "FEEDBACK", t, st.feedback, 0);
        cv_text(6 + text_w(&FONT_M, t) + 4, 59, &FONT_XS, NOTE_NAME[st.root], C_GRAY);
        /* turned since the pattern playing was made: it takes the new value from the next one */
        unsigned made_rhythm, made_warp;
        const char *wait = st.frozen ? "FROZEN" : "NEXT";
        jungle_pattern_made(&made_rhythm, &made_warp);
        int_text((int32_t)st.rhythm, t);
        value_note(64, 46, "VARIATION", t, st.rhythm, 0, jungle_playing() && st.rhythm != made_rhythm ? wait : 0);
        int_text((int32_t)st.warp, t);
        value_note(122, 46, "WARP", t, st.warp, 0, jungle_playing() && st.warp != made_warp ? wait : 0);
        unsigned flt = (jungle_instant_active() & JUNGLE_FX_SWEEP) ? jungle_filter_now() : st.filter;   /* sweeping: live */
        if (flt == 64)
            t[0] = 0, text_cat(t, "OFF");
        else {
            unsigned hz = jungle_filter_hz(flt);
            t[0] = 0;
            text_cat(t, flt < 64 ? "LP" : "HP");
            int_text((int32_t)(hz >= 1000u ? hz / 1000u : hz), t + 2);
            if (hz >= 1000u)
                text_cat(t, "K");
        }
        value(180, 46, "FILTER", t, flt, 1);
    }
    if (st.lofi) {
        unsigned hz, bits;
        jungle_lofi_view(st.lofi, &hz, &bits);
        t[0] = 0;
        text_cat(t, "LO-FI ");
        int_text((int32_t)(hz / 1000u), t + 6);
        text_cat(t, "K ");
        {
            char b[8];
            int_text((int32_t)bits, b);
            text_cat(t, b);
        }
        text_cat(t, "BIT");
        cv_text(6, 104, &FONT_XS, t, C_ORANGE);
    }
    cv_text(234 - text_w(&FONT_XS, st.page == PAGE_EQ ? "EQ: ENV = BACK" : st.page == PAGE_FX ? "FX: FX = BACK" :
                                                                                               "HOLD WHITE = ROLL"),
            104, &FONT_XS,
            st.page == PAGE_EQ ? "EQ: ENV = BACK" : st.page == PAGE_FX ? "FX: FX = BACK" : "HOLD WHITE = ROLL", C_GRAY);
    cv_blit(0, 114);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static int last_step = -2, last_roll = -2, last_playing = -1, last_instant = -1;
    static unsigned last_made = 0xFFFFu;
    static uint32_t last_ble, last_xruns, last_cpu;
    uint32_t now = plat_ms(), xruns = plat_xruns();
    int step = jungle_current_step(), roll = jungle_roll_active(), playing = jungle_playing(),
        instant = (int)jungle_instant_active();
    input();
    if (xruns != last_xruns) {               /* the render ran late: say so, as v1 did */
        if (last_xruns)
            app_say("AUDIO", "OVERLOAD");
        last_xruns = xruns;
    }
    if ((ui.btn >> B_EDIT & 1u) && now - last_cpu >= 250u) {
        last_cpu = now;
        ui.redraw = 1;
    }
    {                                        /* a new pattern took the knobs' values: the notes go */
        unsigned r, w;
        jungle_pattern_made(&r, &w);
        if ((r << 8 | w) != last_made) {
            last_made = r << 8 | w;
            ui.redraw = 1;
        }
    }
    {                                        /* what started (by itself, or a white key): a spark, a pop, a flash */
        uint32_t fx = (uint32_t)instant & 7u, was = last_instant < 0 ? 0u : (uint32_t)last_instant & 7u;   /* held or not */
        ui.spark_lv = ui.spark_lv ? ui.spark_lv : 75u;
        if (roll >= 0 && roll != last_roll) {    /* a roll: by itself, or a white key (each new one) */
            ui.spark_t = ui.pop_t = ui.glitch_t = now | 1u;
            ui.spark_c = C_RED;
            ui.spark_lv = 75u;
        } else if (fx & ~was) {
            ui.spark_t = ui.pop_t = ui.glitch_t = now | 1u;
            ui.spark_c = C_ORANGE;
            ui.spark_lv = 75u;
        } else if ((uint32_t)instant & JUNGLE_FX_SWEEP & ~(uint32_t)(last_instant < 0 ? 0 : last_instant)) {
            ui.spark_t = now | 1u;
            ui.spark_c = C_CYAN;
            ui.spark_lv = 75u;
        }
        if (step != last_step && step >= 0) {
            struct jungle_step pat[JUNGLE_STEPS];
            jungle_pattern(pat);
            if (step % 16 == 10 && pat[step].flip) {
                ui.flip_t = ui.spark_t = now | 1u;
                ui.flip_bar = (uint8_t)(step / 16);
                ui.spark_c = C_WHITE;
                ui.spark_lv = 75u;
            } else if (pat[step].active && (pat[step].stretch || pat[step].pitch < 0) && roll < 0) {
                ui.warp_t = ui.spark_t = now | 1u;   /* a warped step plays: its own move, a softer glow */
                ui.warp_kind = pat[step].stretch ? 1u : 2u;
                ui.warp_step = (uint8_t)step;
                ui.spark_c = pat[step].stretch ? C_PURPLE : C_BLUE;
                ui.spark_lv = 40u;
            }
        }
        if ((ui.spark_t && now - ui.spark_t < ANIM_MS + 40u) || (ui.flip_t && now - ui.flip_t < ANIM_MS + 40u) ||
            (ui.glitch_t && now - ui.glitch_t < ANIM_MS + 40u) || (ui.warp_t && now - ui.warp_t < ANIM_MS + 40u))
            ui.redraw = 1;                   /* (and one frame after, to clear it) */
    }
    {                                        /* the sweep: the filter's bar follows it */
        static unsigned last_filter = 64u;
        unsigned f = jungle_filter_now();
        if (((uint32_t)instant & JUNGLE_FX_SWEEP) && f != last_filter)
            ui.redraw = 1;
        last_filter = f;
    }
    if (instant != last_instant) {
        last_instant = instant;
        ui.redraw = 1;
    }
    {                                        /* a halved / doubled tempo came in: white again */
        static unsigned last_pending;
        unsigned p = jungle_tempo_pending();
        if (p != last_pending) {
            last_pending = p;
            ui.redraw = 1;
        }
    }
    if (step != last_step || roll != last_roll || playing != last_playing ||
        (uint32_t)plat_ble_connected() != last_ble || (ui.msg_until && now >= ui.msg_until)) {
        last_step = step;
        last_roll = roll;
        last_playing = playing;
        last_ble = (uint32_t)plat_ble_connected();
        if (ui.msg_until && now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}
