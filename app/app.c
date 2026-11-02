/* SPDX-License-Identifier: GPL-3.0-only */
/* AMENITY on fm1-base: the panel, the screen, MIDI clock in and out, around the break engine (jungle/).
 *
 *   white keys (hold)  rolls and effects, last held wins     black keys 1-8  one-shot hits
 *   K1 FEEDBACK  K2 VARIATION  K3 WARP  K4 FILTER            FX held + key   the feedback's root
 *   SELECT LO-FI  ALGORITHM transpose (down)  PRESET tempo (SEL held: 0.1 BPM)  OCT- / OCT+ halve / double
 *   PLAY start/stop  REC freeze  SEQ fill  HOME straight break
 *   ENV: EQ page (K1-K4: 100 Hz, 500 Hz, 2.5 kHz, 8 kHz)  FX tap: FX page (K1 saturation, K2 compression)
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

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    if (muted)
        return;
    jungle_render(lr, n, plat_ticks24() + HEARD_TICKS);
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
    uint32_t pkt, now = plat_ticks24();
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
    unsigned tempo, rhythm, filter, warp, feedback, root, lofi, saturation, compression;
    int transpose, eq_db[4];
    uint8_t playing, frozen, auto_roll, page, fx_used, external, filter_q;
    uint32_t filter_turned, filter_q_step, clock_near;
} st = {.tempo = 1600, .filter = 64, .root = 4, .filter_q = Q_FLAT};

static const uint8_t WHITE[16] = PLAT_WHITE_KEYS, BLACK[11] = PLAT_BLACK_KEYS;
/* one-shots on the first eight black keys: kick, snare, kick, snare, snare, snare, hat, ride */
static const uint8_t SHOT_SLICES[8] = {0, 2, 9, 7, 10, 15, 3, 12};
static const char *const ROLL_NAME[16] = {
    "1/4 REPEAT", "BUILD", "STRETCH", "ECHO", "1/8 REPEAT", "1/8 DOWN", "1/8 UP", "1/8 SHRINK",
    "1/16 REPEAT", "1/16 DOWN", "1/16 UP", "1/16 SHRINK", "1/32 REPEAT", "1/32 DOWN", "1/32 UP", "1/32 SHRINK"};
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
} ui;

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
void app_init(void)
{
    const uint8_t *data;
    unsigned bytes, frames;
    jungle_init();
    if (jungle_fallback(&data, &bytes, &frames))
        (void)jungle_bind(data, bytes, frames);
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
    for (i = 0; i < 8u; i++)
        if (down >> BLACK[i] & 1u)
            jungle_one_shot(SHOT_SLICES[i]);
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
    if (PRESSED(B_OCTDN) && !st.external)
        set_tempo((int)st.tempo / 2);
    if (PRESSED(B_OCTUP) && !st.external)
        set_tempo((int)st.tempo * 2);
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
    if (e[EN_PRESET] && !st.external) {
        int step = (btn >> B_SEL & 1u) ? 1 : 10;
        if (e[EN_PRESET] > 2 || e[EN_PRESET] < -2)
            step *= 2;                       /* a fast turn */
        set_tempo((int)st.tempo + e[EN_PRESET] * step);
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
    if (e[EN_SELECT])
        jungle_lofi(st.lofi = turn(st.lofi, e[EN_SELECT]));
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
    st.external = clock_ext;
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

static void lights(void)
{
    uint32_t btn = 0, keys = ui.keys, i;
    int step = jungle_current_step(), roll = jungle_roll_active();
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
        keys |= 1u << WHITE[(unsigned)step / 2u];   /* a running light, one white key an 8th */
    if (roll >= 0)
        keys |= 1u << WHITE[roll];
    if (ui.btn >> B_FX & 1u)                 /* FX held: the keys of the feedback's root */
        for (i = 0; i < NKEYS; i++)
            if ((5u + i) % 12u == st.root)
                keys |= 1u << i;
    plat_play_red(roll >= 0);
    plat_glow(1);
    plat_leds(btn, keys);
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

static void value(int32_t x, int32_t y, const char *label, const char *v, unsigned of127)
{
    cv_text(x, y, &FONT_XS, label, C_GRAY);
    cv_text(x, y + 13, &FONT_M, v, C_WHITE);
    if (of127 <= 127u) {
        cv_rect(x, y + 40, 50, 3, C_LINE);
        cv_rect(x, y + 40, (int32_t)(of127 * 50u / 127u), 3, C_CYAN);
    }
}

static void draw(void)
{
    char t[24];
    uint32_t now = plat_ms(), i;
    int step = jungle_current_step(), roll = jungle_roll_active();
    struct jungle_step pat[JUNGLE_STEPS];
    /* the header: the name, EXT, the tempo; or a message for a moment */
    cv_begin(240, 34, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(6, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_CYAN);
    } else {
        int32_t x = cv_text(6, 2, &FONT_M, APP_NAME, C_CYAN);
        if (st.external)
            cv_text(x + 6, 4, &FONT_XS, "EXT", C_GREEN);
        if (FM1B_BLE)
            cv_text(x + 6, 17, &FONT_XS, "BLE", plat_ble_connected() ? C_HI : C_DIM);
        if (ui.btn >> B_SEL & 1u) {          /* SEL held: how busy the audio is */
            t[0] = 0;
            text_cat(t, "CPU ");
            int_text((int32_t)plat_cpu_pct(), t + 4);
            text_cat(t, "%");
            cv_text(234 - text_w(&FONT_M, t), 4, &FONT_M, t, C_ORANGE);
        } else {
            tenths_text(st.tempo, t);
            cv_text(234 - text_w(&FONT_L, t), 1, &FONT_L, t, C_WHITE);
        }
    }
    cv_blit(0, 0);
    /* the pattern: 32 steps in two rows, the step playing marked below it */
    jungle_pattern(pat);
    cv_begin(240, 76, C_BLACK);
    for (i = 0; i < JUNGLE_STEPS; i++) {
        int32_t x = 4 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u / 4u) * 2, y = (int32_t)(i / 16u) * 36;
        uint16_t c = C_LINE;
        if (pat[i].active)
            c = pat[i].stretch ? C_PURPLE : pat[i].pitch < 0 ? C_BLUE : C_GRAY;
        cv_rect(x, y, 12, 26, c);
        if (pat[i].active) {
            char h[2] = {"0123456789ABCDEF"[pat[i].slice & 15u], 0};
            cv_text(x + 3, y + 6, &FONT_XS, h, C_BLACK);
        }
        if ((int)i == step)
            cv_rect(x, y + 28, 12, 3, C_GREEN);
    }
    cv_blit(0, 38);
    /* what is happening, the modes, the knobs of the page */
    cv_begin(240, 126, C_BLACK);
    if (roll >= 0)
        cv_text(6, 0, &FONT_M, ROLL_NAME[roll], C_RED);
    else if (st.external && !jungle_playing())
        cv_text(6, 0, &FONT_M, "EXT: WAITING", C_GREEN);
    else
        cv_text(6, 0, &FONT_M, jungle_playing() ? "PLAYING" : "STOPPED", jungle_playing() ? C_GREEN : C_GRAY);
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
            int_text(st.eq_db[i], t);
            value(6 + (int32_t)i * 58, 46, BAND[i], t, 200u);
        }
    } else if (st.page == PAGE_FX) {
        int_text((int32_t)st.saturation, t);
        value(6, 46, "SATURATE", t, st.saturation);
        int_text((int32_t)st.compression, t);
        value(64, 46, "COMPRESS", t, st.compression);
    } else {
        int_text((int32_t)(st.feedback * 90u / 127u), t);
        text_cat(t, "%");
        value(6, 46, "FEEDBACK", t, st.feedback);
        cv_text(6 + text_w(&FONT_M, t) + 4, 59, &FONT_XS, NOTE_NAME[st.root], C_GRAY);
        int_text((int32_t)st.rhythm, t);
        value(64, 46, "VARIATION", t, st.rhythm);
        int_text((int32_t)st.warp, t);
        value(122, 46, "WARP", t, st.warp);
        if (st.filter == 64)
            t[0] = 0, text_cat(t, "OFF");
        else {
            unsigned hz = jungle_filter_hz(st.filter);
            t[0] = 0;
            text_cat(t, st.filter < 64 ? "LP" : "HP");
            int_text((int32_t)(hz >= 1000u ? hz / 1000u : hz), t + 2);
            if (hz >= 1000u)
                text_cat(t, "K");
        }
        value(180, 46, "FILTER", t, 200u);
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
    static int last_step = -2, last_roll = -2, last_playing = -1;
    static uint32_t last_ble, last_xruns, last_cpu;
    uint32_t now = plat_ms(), xruns = plat_xruns();
    int step = jungle_current_step(), roll = jungle_roll_active(), playing = jungle_playing();
    input();
    if (xruns != last_xruns) {               /* the render ran late: say so, as v1 did */
        if (last_xruns)
            app_say("AUDIO", "OVERLOAD");
        last_xruns = xruns;
    }
    if ((ui.btn >> B_SEL & 1u) && now - last_cpu >= 250u) {
        last_cpu = now;
        ui.redraw = 1;
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
