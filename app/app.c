/* SPDX-License-Identifier: GPL-3.0-only AND MPL-2.0
 * Copyright (C) 2026 Presethead. text_cat, int_text and app_say are from fm1-base's template (template/app.c),
 * Copyright (C) 2026 Charles Vestal, MPL-2.0 (base/LICENSE); the rest is GPL-3.0-only (LICENSE.md). */
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
#include "jungle_clock.h"                  /* (user_breaks.c: the clock's logs for CMD_CLOCK) */
#include "btdev.h"                        /* Bluetooth MIDI devices: the connections page */

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
enum { CLK_Q = 32 };
static struct { uint32_t at; uint8_t status; } clk_q[CLK_Q];
static volatile uint32_t clk_w, clk_r;
void jungle_midi_out(unsigned status, uint32_t at)
{
    uint32_t w = clk_w;
    if (w - clk_r >= CLK_Q)
        return;
    clk_q[w % CLK_Q].at = at;
    clk_q[w % CLK_Q].status = (uint8_t)status;
    __asm__ volatile("" ::: "memory");
    clk_w = w + 1u;
}

/* Clock in: every realtime message, stamped as the tick reads it (the tick runs right after the USB poll).
 * MIDI THRU, as a daisy chain: everything that comes in (notes, CC, clock, Start / Stop; not SysEx, the updater's
 * and the breaks page's) goes on at once to the other outs, never back where it came from: USB to Bluetooth, the TRS
 * jack to USB and Bluetooth, Bluetooth to USB (and from a device's link to the computer's, and back). Incoming clock
 * pulses are held back only while Amenity plays on its own clock (its own go out then: no double clock). */
static volatile uint8_t clock_ext;
static volatile uint16_t clock_bpm;
void app_tick(void)
{
    uint32_t pkt, now = now_ticks();
    unsigned bpm;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, cin = pkt & 0xFu, from = plat_midi_in_from();
        (void)port;
        if (cin >= 2u && (cin < 4u || cin > 7u) && !(st == 0xF8u && !clock_ext && jungle_playing()))
            plat_midi_send(pkt & ~0xF0u, (TO_USB | TO_BLE) & ~from);   /* (cable 0: the jack's come in as 1) */
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)   /* from which link (the computer's and a
                                                                         * device's are both Bluetooth), when it came */
#ifdef PLAT_HOST
            jungle_midi_realtime(st, plat_midi_in_from(), now);
#else
            jungle_midi_realtime(st, plat_midi_in_from(), plat_midi_in_t24());
#endif
    }
    now = now_ticks();                       /* (after the reads: their stamps are later than the tick's start) */
    clock_ext = (uint8_t)jungle_clock(&bpm, now);
    clock_bpm = (uint16_t)bpm;
    while (clk_r != clk_w) {
        uint32_t r = clk_r;
        if ((int32_t)(now - clk_q[r % CLK_Q].at) < 0)
            break;
        plat_midi_send(0x0Fu | (uint32_t)clk_q[r % CLK_Q].status << 8, TO_USB | TO_BLE);   /* CIN F: one byte */
        clk_r = r + 1u;
    }
}

/* ------------------------------------------------------------------ state --- */
enum { PAGE_MAIN, PAGE_EQ, PAGE_FX, PAGE_LINK };
enum { Q_SWEEP = 14, Q_FLAT = 8 };       /* the DJ filter rings only while K4 turns */
static struct {
    unsigned tempo, rhythm, filter, warp, feedback, root, lofi, saturation, compression, brk;
    int transpose, eq_db[4];
    uint8_t playing, frozen, auto_roll, page, fx_used, external, filter_q;
    uint8_t calm;                            /* the screen's flashes off (connections page: UI), kept */
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
static const char *upper(const char *s)        /* in capitals, as the screen writes (at most 31 characters) */
{
    static char u[32];
    unsigned i;
    for (i = 0; i < 31u && s[i]; i++)
        u[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    u[i] = 0;
    return u;
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
    uint32_t edit_t;                         /* when EDIT went down: a tap opens the connections page, held the CPU */
    uint8_t link_row;                        /* the connections page's cursor (SELECT moves it, SAVE enters) */
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

/* the settings kept over a power-off (in OBJ_SETTINGS with the breaks' order, user_breaks.c): the break, the tempo,
 * every knob, SEQ and the flashes on or off; not PLAY, REC, half / double time or the page. Saved when the sequencer stops, or 2 s after a
 * change while it stands still: a save silences the audio for tens of ms (the flash's erase), so never while playing */
struct prefs {
    uint16_t tempo;
    uint8_t brk, rhythm, filter, warp, feedback, root, lofi, saturation, compression, auto_roll;
    int8_t transpose, eq_db[4];
    uint8_t flags;                           /* bit 0: the flashes off (2.0.x before it: 0, on) */
};
_Static_assert(sizeof(struct prefs) == 18u, "struct prefs: no padding (compared byte by byte)");
static int settings_save(void);

#include "user_breaks.c"

static void prefs_take(struct prefs *p)
{
    unsigned k;
    p->tempo = (uint16_t)st.tempo;
    p->brk = (uint8_t)st.brk, p->rhythm = (uint8_t)st.rhythm, p->filter = (uint8_t)st.filter;
    p->warp = (uint8_t)st.warp, p->feedback = (uint8_t)st.feedback, p->root = (uint8_t)st.root;
    p->lofi = (uint8_t)st.lofi, p->saturation = (uint8_t)st.saturation, p->compression = (uint8_t)st.compression;
    p->auto_roll = st.auto_roll, p->transpose = (int8_t)st.transpose, p->flags = st.calm ? 1u : 0u;
    for (k = 0; k < 4u; k++)
        p->eq_db[k] = (int8_t)st.eq_db[k];
}
static void prefs_give(const struct prefs *p)   /* what the store held, each value kept inside its range */
{
    unsigned k;
    st.tempo = (unsigned)clamp(p->tempo, JUNGLE_BPM_MIN_X10, JUNGLE_BPM_MAX_X10);
    st.brk = p->brk;
    st.rhythm = (unsigned)clamp(p->rhythm, 0, 127), st.filter = (unsigned)clamp(p->filter, 0, 127);
    st.warp = (unsigned)clamp(p->warp, 0, 127), st.feedback = (unsigned)clamp(p->feedback, 0, 127);
    st.root = p->root % 12u, st.lofi = (unsigned)clamp(p->lofi, 0, 127);
    st.saturation = (unsigned)clamp(p->saturation, 0, 127), st.compression = (unsigned)clamp(p->compression, 0, 127);
    st.auto_roll = p->auto_roll ? 1u : 0u, st.transpose = clamp(p->transpose, -12, 0);
    st.calm = p->flags & 1u;
    for (k = 0; k < 4u; k++)
        st.eq_db[k] = clamp(p->eq_db[k], -12, 12);
}
static int prefs_same(const struct prefs *a, const struct prefs *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    unsigned i;
    for (i = 0; i < sizeof *a; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}
static int settings_save(void)                 /* 0 = saved */
{
    user_order.magic = SETTINGS_MAGIC;
    prefs_take(&user_order.prefs);
    return plat_store_save(OBJ_SETTINGS, &user_order, sizeof user_order);
}
/* every frame: saved when the sequencer stops, or 2 s after the last change while it stands still */
static void settings_keep(uint32_t now)
{
    static struct prefs last;
    static uint32_t changed_t;
    static int was_playing;
    struct prefs cur;
    int playing = jungle_playing(), stopped = was_playing && !playing;
    was_playing = playing;
    prefs_take(&cur);
    if (!prefs_same(&cur, &last)) {
        last = cur;
        changed_t = now;
    }
    if (playing || prefs_same(&cur, &user_order.prefs))
        return;
    if (stopped || now - changed_t >= 2000u) {
        if (settings_save()) {
            user_order.prefs.tempo = 0;          /* not saved: again in 2 s */
            changed_t = now;
        }
    }
}

void app_init(void)
{
    unsigned k;
    jungle_init();
    order_load();
    user_scan();
    if (prefs_loaded)
        prefs_give(&user_order.prefs);
    select_break(st.brk < breaks_count() ? st.brk : 0u);
    prefs_take(&user_order.prefs);          /* what is in the store now (a 2.0.0 store: saved at the first change) */
    for (k = 0; k < 4u; k++)
        jungle_eq(k, st.eq_db[k]);
    jungle_saturation(st.saturation);
    jungle_compression(st.compression);
    jungle_auto_roll(st.auto_roll);
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

/* half time / normal / double time: from the next bar (shown amber until then), the tempo and the clock as
 * they are, so it works following a MIDI clock too */
static void time_next(int t)
{
    static const char *const NAME[3] = {"HALF TIME", "NORMAL", "DOUBLE TIME"};
    if (t < -1 || t > 1) {
        app_say("TIME", t < 0 ? "HALF ALREADY" : "DOUBLE ALREADY");
        return;
    }
    jungle_time_next(t);
    app_say("TIME", NAME[t + 1]);
    ui.redraw = 1;
}

/* the connections page as a list: the Bluetooth devices found (or the one connected; "LOOKING" while none), then
 * the USB port. SELECT moves the cursor, SAVE does what the row's action says */
static int link_devrows(void) { return btdev_busy() || plat_ble_linked() ? 1 : btdev_count() ? btdev_count() : 1; }
static int link_usb_row(void) { return link_devrows(); }
static int link_ui_row(void) { return link_devrows() + 1; }   /* the last: the screen's flashes on / off */
static const char *link_action(void)
{
    if (ui.link_row >= link_ui_row())
        return st.calm ? "FLASHES ON" : "FLASHES OFF";
    if (ui.link_row == link_usb_row())
        return plat_usb_host_state() == USBH_OFF ? "HOST" : "DEVICE";
    return btdev_busy() ? "LET GO" : plat_ble_linked() ? "" : btdev_count() ? "CONNECT" : "";
}
static void link_enter(void)
{
    if (ui.link_row >= link_ui_row()) {
        st.calm = !st.calm;
        ui.spark_t = ui.pop_t = ui.flip_t = ui.glitch_t = ui.warp_t = 0;
        app_say("SCREEN", st.calm ? "FLASHES OFF" : "FLASHES ON");
        return;
    }
    if (ui.link_row == link_usb_row()) {
        plat_usb_host(plat_usb_host_state() == USBH_OFF);
        return;
    }
    if (btdev_busy() || (btdev_count() && !plat_ble_linked())) {
        int ev;
        if (!btdev_busy())
            btdev_choose(ui.link_row);
        ev = btdev_save();
        app_say(ev == BTDEV_CONNECTING ? "CONNECTING" : "BLUETOOTH", ev == BTDEV_CONNECTING ? upper(btdev_name()) : btdev_word(ev));
    }
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
    if (PRESSED(B_EDIT))
        ui.edit_t = now;
    if ((released >> B_EDIT & 1u) && now - ui.edit_t < 400u)   /* a tap: the connections page, or back */
        st.page = st.page == PAGE_LINK ? PAGE_MAIN : PAGE_LINK;
    if (PRESSED(B_SAVE) && st.page == PAGE_LINK)             /* enter: the row's action */
        link_enter();
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
    if (PRESSED(B_HOME)) {                   /* the plain break: no variation, warp or filter until K2-K4 turn again */
        st.rhythm = st.warp = 0;
        st.page = PAGE_MAIN;
        jungle_variation(0);
        jungle_warp(0);
        jungle_filter(st.filter = 64);
        jungle_filter_resonance(st.filter_q = Q_FLAT);
        jungle_straight();
        app_say("STRAIGHT", "BREAK");
    }
    /* half time / double time from the next bar, with or without a MIDI clock; with SEL held, 0.1 BPM down / up
     * (the internal clock only) */
    if (PRESSED(B_OCTDN)) {
        if (btn >> B_SEL & 1u) {
            if (!st.external)
                set_tempo((int)st.tempo - 1);
        } else
            time_next(jungle_time_target() - 1);
    }
    if (PRESSED(B_OCTUP)) {
        if (btn >> B_SEL & 1u) {
            if (!st.external)
                set_tempo((int)st.tempo + 1);
        } else
            time_next(jungle_time_target() + 1);
    }
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
    if (e[EN_SELECT] && st.page == PAGE_LINK) {   /* on the connections page: the cursor */
        int row = (int)ui.link_row + (e[EN_SELECT] > 0 ? 1 : -1), last = link_ui_row();
        ui.link_row = (uint8_t)(row < 0 ? 0 : row > last ? last : row);
    } else if (e[EN_SELECT] && !st.external)   /* SELECT: the tempo, 1 BPM a detent (2 turned fast), 0.1 with SEL */
        set_tempo((int)st.tempo + e[EN_SELECT] * ((btn >> B_SEL & 1u) ? 1 : e[EN_SELECT] > 2 || e[EN_SELECT] < -2 ? 20 : 10));
    if (e[EN_PRESET] && breaks_count()) {        /* PRESET: the next or the previous break, round */
        int n = (int)breaks_count(), next = ((int)st.brk + e[EN_PRESET]) % n;
        select_break((unsigned)(next < 0 ? next + n : next));
        app_say("BREAK", break_at(st.brk)->name);
    }
    if (st.page == PAGE_LINK) {
        /* (the page's knobs rest: SELECT and SAVE work it) */
    } else if (st.page == PAGE_EQ) {
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
    if (st.page == PAGE_LINK)
        btn |= 1u << B_EDIT;
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

/* the connections page (EDIT): Bluetooth MIDI devices (lib/btdev: the scan while it is up) and the USB port's role
 * (DEVICE, attached to a computer, or HOST to a MIDI device), as a list: SELECT moves the cursor, SAVE enters. The
 * MIDI clock goes to all of them; a Start from any of them is followed. */
static void link_row(int32_t y, int row, const char *label, const char *text, uint16_t c)
{
    int on = ui.link_row == row;
    if (label)
        cv_text(6, y + 3, &FONT_XS, label, C_GRAY);
    if (on)
        cv_text(34, y, &FONT_S, ">", C_CYAN);
    cv_text(44, y, &FONT_S, text, on ? C_CYAN : c);
}
static void link_arrow(int32_t y, int up)   /* a 9 x 5 triangle at the right edge */
{
    int32_t r;
    for (r = 0; r < 5; r++)
        cv_rect(222 + (up ? 4 - r : r), y + r, 1 + 2 * (up ? r : 4 - r), 1, C_GRAY);
}
static void draw_link(void)
{
    char t[40], name[24];
    int n = btdev_count(), u = plat_usb_host_state(), rows = link_devrows(), first, i, y = 40;
    first = ui.link_row < rows ? (int)ui.link_row - 1 : rows - 2;     /* two device rows at most, the cursor's in view */
    first = first > rows - 2 ? rows - 2 : first;
    first = first < 0 ? 0 : first;
    if (btdev_busy()) {
        t[0] = 0;
        text_cat(t, upper(btdev_name()));
        text_cat(t, btdev_ready() ? "  CONNECTED" : "  CONNECTING");
        link_row(y, 0, "BT", t, btdev_ready() ? C_GREEN : C_ORANGE);
        y += 16;
    } else if (plat_ble_linked()) {          /* the other side connected to us (its name is not known here) */
        link_row(y, 0, "BT", "CONNECTED", C_GREEN);
        y += 16;
    } else if (!n) {
        link_row(y, 0, "BT", "LOOKING...", C_GRAY);
        y += 16;
    } else {
        for (i = first; i < first + 2 && i < n; i++, y += 16)
            if (btdev_found(i, name, 0))
                link_row(y, i, i == first ? "BT" : 0, upper(name), C_WHITE);
        if (first > 0)                       /* more devices above / below: a small arrow at the right */
            link_arrow(40 + 4, 1);
        if (first + 2 < n)
            link_arrow(56 + 4, 0);
    }
    /* (USB and UI right under the last BT row) */
    if (u == USBH_OFF)
        link_row(y, rows, "USB", "DEVICE", C_WHITE);
    else if (u == USBH_CONNECTED) {
        const char *dev = upper(plat_usb_host_name());
        t[0] = 0;
        text_cat(t, "HOST TO ");
        text_cat(t, dev[0] ? dev : "A DEVICE");
        t[24] = 0;                                   /* (what fits) */
        link_row(y, rows, "USB", t, C_GREEN);
    } else
        link_row(y, rows, "USB", "HOST - LOOKING", C_ORANGE);
    link_row(y + 16, rows + 1, "UI", st.calm ? "FLASHES OFF" : "FLASHES ON", st.calm ? C_GRAY : C_WHITE);
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
            int tm = jungle_time_target();
            int32_t tx;
            tenths_text(st.tempo, t);
            tx = 234 - text_w(&FONT_L, t);
            cv_text(tx, 1, &FONT_L, t, C_WHITE);
            if (tm || jungle_time_pending()) {   /* half / double time beside the tempo: amber until it comes in */
                const char *lb = tm < 0 ? "1/2X" : tm > 0 ? "2X" : "1X";
                cv_text(tx - 4 - text_w(&FONT_XS, lb), 4, &FONT_XS, lb, jungle_time_pending() ? C_ORANGE : C_CYAN);
            }
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
        unsigned fx = st.calm ? 0u : jungle_instant_active();   /* (the flashes off: a still grid) */
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
    if (st.page == PAGE_LINK)
        draw_link();
    else if (st.page == PAGE_EQ) {
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
    {
        const char *hint = st.page == PAGE_EQ ? "EQ: ENV=BACK" : st.page == PAGE_FX ? "FX: FX=BACK" :
                           st.page == PAGE_LINK ? "SELECT=MOVE  EDIT=BACK" : "HOLD WHITE=ROLL";
        cv_text(234 - text_w(&FONT_XS, hint), 104, &FONT_XS, hint, C_GRAY);
        if (st.page == PAGE_LINK && link_action()[0]) {   /* what SAVE does on the row: "SAVE=CONNECT" */
            char a[24] = "SAVE=";
            text_cat(a, link_action());
            cv_text(6, 104, &FONT_XS, a, C_CYAN);
        }
    }
    cv_blit(0, 114);
}

#ifdef APP_HOST_REBOOT
void app_host_reboot(void)                  /* the host's "reboot": RAM forgotten as on a power-on (the store kept) */
{
    unsigned k;
    st.tempo = 1600, st.rhythm = st.warp = st.feedback = st.lofi = st.saturation = st.compression = st.brk = 0;
    st.filter = 64, st.root = 4, st.filter_q = Q_FLAT, st.transpose = 0;
    st.playing = st.frozen = st.auto_roll = st.page = st.external = st.calm = 0;
    for (k = 0; k < 4u; k++)
        st.eq_db[k] = 0;
    prefs_loaded = 0;
}
#endif
#ifdef APP_HOST_EXPECT
int app_expect(const char *what, int *got)
{
    static const struct { const char *n; const void *v; int sign; } T[] = {
        {"tempo", &st.tempo, 0}, {"brk", &st.brk, 0}, {"feedback", &st.feedback, 0}, {"filter", &st.filter, 0},
        {"lofi", &st.lofi, 0}, {"warp", &st.warp, 0}, {"calm", &st.calm, 2}, {"playing", &st.playing, 2}, {"eq0", &st.eq_db[0], 1}, {"transpose", &st.transpose, 1}};
    unsigned i;
    if (!strcmp(what, "engine_playing")) {
        *got = jungle_playing();
        return 1;
    }
    if (!strcmp(what, "seq")) {
        *got = st.auto_roll;
        return 1;
    }
    for (i = 0; i < sizeof T / sizeof T[0]; i++)
        if (!strcmp(what, T[i].n)) {
            *got = T[i].sign == 2 ? *(const uint8_t *)T[i].v : T[i].sign ? *(const int *)T[i].v : (int)*(const unsigned *)T[i].v;
            return 1;
        }
    return 0;
}
#endif

#ifdef PLAT_HOST
#include <string.h>
#endif
/* the splash (APP_SPLASH): the logo (assets/splash.png, coded by tools/jungle/make_splash.py), two bands of 120 rows
 * through the canvas, the version written under it */
#if __has_include("../build/jungle/splash.inc")
#include "../build/jungle/splash.inc"
void app_splash(void)
{
    uint32_t at = 0, left = 0, lit = 0, band, y, x;
    uint8_t ix = 0;
    for (band = 0; band < 2u; band++) {
        cv_begin(240, 120, C_BLACK);
        for (y = 0; y < 120u; y++)
            for (x = 0; x < 240u; x++) {
                if (!left) {                     /* the next run or the next literals */
                    uint8_t n = at < sizeof SPLASH_RLE ? SPLASH_RLE[at++] : 0u;
                    lit = n >= 128u;
                    left = lit ? n - 127u : n + 1u;
                    if (!lit)
                        ix = at < sizeof SPLASH_RLE ? SPLASH_RLE[at++] : 0u;
                }
                if (lit)
                    ix = at < sizeof SPLASH_RLE ? SPLASH_RLE[at++] : 0u;
                left--;
                cv_pset((int32_t)x, (int32_t)y, SPLASH_PAL[ix]);   /* (256 entries: any index) */
            }
        if (band == 1u) {
            char v[24] = "";
            if (APP_VERSION[0] >= '0' && APP_VERSION[0] <= '9')
                text_cat(v, "V");
            text_cat(v, APP_VERSION);
            cv_text((240 - text_w(&FONT_XS, v)) / 2, 84, &FONT_XS, v, C_GRAY);
        }
        cv_blit(0, band * 120u);
    }
}
#ifdef APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a, (void)b, (void)c, (void)d;
    if (strcmp(cmd, "splash"))
        return 0;
    app_splash();
    return 1;
}
#endif
#endif

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
    settings_keep(now);
    {   /* the connections: the scan while the page is up, what happened (said, and the page drawn again) */
        static int last_n = -1, last_u = -1, last_pick = -1, last_usb_row = -1, last_in = -1;
        int ev, n, u;
        btdev_page(st.page == PAGE_LINK);
        while ((ev = btdev_event()) != BTDEV_NONE) {
            app_say("BLUETOOTH", ev == BTDEV_COMPUTER ? "CONNECTED" : ev == BTDEV_COMPUTER_GONE ? "DISCONNECTED" :
                                 btdev_word(ev));   /* (the other side: a computer, or another FM-1) */
            ui.redraw = 1;
        }
        switch (plat_usb_host_msg()) {
        case USBH_MSG_ON: app_say("USB", "HOST"); break;
        case USBH_MSG_OFF: app_say("USB", "DEVICE"); break;
        case USBH_MSG_MIDI: app_say("USB HOST TO", plat_usb_host_name()[0] ? upper(plat_usb_host_name()) : "A DEVICE"); break;
        case USBH_MSG_NOMIDI: app_say("USB", "NOT A MIDI DEVICE"); break;
        case USBH_MSG_LOW: app_say("USB", "NEEDS POWER"); break;
        case USBH_MSG_LOST: app_say("USB", "DEVICE LOST"); break;
        default: break;
        }
        n = btdev_count(), u = plat_usb_host_state();
        if (n != last_n || u != last_u || btdev_pick() != last_pick || plat_ble_linked() != last_in) {
            last_n = n, last_u = u, last_pick = btdev_pick(), last_in = plat_ble_linked();
            ui.redraw = 1;
        }
        if (last_usb_row >= 0 && ui.link_row >= last_usb_row)   /* on USB or UI: it stays there as devices come */
            ui.link_row = (uint8_t)(link_usb_row() + (ui.link_row - last_usb_row > 0 ? 1 : 0));
        if (ui.link_row > link_ui_row())
            ui.link_row = (uint8_t)link_ui_row();
        last_usb_row = link_usb_row();
    }
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
        if (st.calm)                         /* the flashes off: none of them starts */
            ui.spark_t = ui.pop_t = ui.flip_t = ui.glitch_t = ui.warp_t = 0;
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
    {                                        /* half / double time came in: no longer amber */
        static unsigned last_pending;
        unsigned p = jungle_time_pending();
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
