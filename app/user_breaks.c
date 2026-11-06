/* SPDX-License-Identifier: GPL-3.0-only */
/* Your own breaks: four slots in fm1-base's data area, written from the breaks page over USB (app_usb_cmd), played
 * after the built-in ones (PRESET). Included by app.c (fm1-base's unity build).
 *
 * A slot is 17 sectors (68 KiB) from USER_BASE: a 64-byte head, then from byte 256 the break as SEA5 (the format
 * of the built-in ones, at the rate the page chose: 32 kHz, lower for a break over 3 s so that it fits). The
 * update loader never writes the data area, so the slots outlast updates; another firmware may not.
 *
 * Commands, in the update's framing (cmd, body), each answered with a frame of the same cmd:
 *   0x60 LIST                          -> version, slots, room (u32 bytes a slot), busy, then a slot each:
 *                                         used, name[12], frames (u32), rate (u32, Hz)
 *   0x61 WRITE slot, bytes (u32), crc32 (u32), frames (u32), name[12], shots[8], rate (u32)
 *                                      -> the FM-1 reads the bytes from the host (plat_host_read, 512 at a time),
 *                                         then answers status
 *   0x62 DELETE slot                   -> status
 *   0x63 ORDER o0 o1 o2 o3             -> status: the slots in the order PRESET plays them (a permutation of 0..3),
 *                                         kept in the store (OBJ_SETTINGS); LIST ends with the order
 * status: 0 done, 1 bad request, 2 busy (playing or following a clock: stop first), 3 the transfer, 4 the flash,
 * 5 the check (the CRC of what was written). Writing erases the slot first and writes its head last, so a slot is
 * whole or empty. */

enum { USER_SLOTS = 4, USER_BASE = 0x97000, USER_SLOT = 17 * 4096, USER_DATA = 256,
       USER_ROOM = USER_SLOT - USER_DATA, USER_MAGIC = 0x31424D41 /* "AMB1" */, USER_VERSION = 1 };
enum { CMD_LIST = 0x60, CMD_WRITE = 0x61, CMD_DELETE = 0x62, CMD_ORDER = 0x63 };
enum { ST_DONE, ST_BAD, ST_BUSY, ST_TRANSFER, ST_FLASH, ST_CHECK };

typedef struct {
    uint32_t magic, version, frames, bytes, crc;
    char name[12];
    uint8_t shots[8];
    uint32_t rate;                           /* Hz, as the page encoded it: for its tempo (480 s / duration) */
    uint8_t rsv[16];
} user_head_t;                               /* 64 bytes */

static struct jungle_break user[USER_SLOTS];   /* .data 0: an empty slot */
/* the order PRESET plays the slots in, kept in the store; its own magic, so a store without it reads as none */
#define ORDER_MAGIC 0x314F4D41u                /* "AMO1" */
static struct { uint32_t magic; uint8_t order[USER_SLOTS]; } user_order;

static int order_valid(const uint8_t *o)
{
    uint32_t seen = 0, i;
    for (i = 0; i < USER_SLOTS; i++) {
        if (o[i] >= USER_SLOTS || (seen >> o[i] & 1u))
            return 0;
        seen |= 1u << o[i];
    }
    return 1;
}
static void order_load(void)
{
    uint32_t i;
    if (plat_store_load(OBJ_SETTINGS, &user_order, sizeof user_order) == (int)sizeof user_order &&
        user_order.magic == ORDER_MAGIC && order_valid(user_order.order))
        return;
    user_order.magic = ORDER_MAGIC;
    for (i = 0; i < USER_SLOTS; i++)
        user_order.order[i] = (uint8_t)i;
}
static char user_name[USER_SLOTS][12];
static uint32_t user_rate[USER_SLOTS];

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static int user_valid(const user_head_t *h, const uint8_t *data)
{
    uint32_t i;
    if (h->magic != USER_MAGIC || h->version != USER_VERSION || h->frames < JUNGLE_SLICES * 320u ||
        h->bytes != (h->frames + 319u) / 320u * 228u || h->bytes > USER_ROOM)
        return 0;
    for (i = 0; i < 8u; i++)
        if (h->shots[i] >= JUNGLE_SLICES)
            return 0;
    return crc32(data, h->bytes) == h->crc;
}

/* the slots as the flash has them */
static void user_scan(void)
{
    uint32_t s, i;
    for (s = 0; s < USER_SLOTS; s++) {
        const uint8_t *at = plat_data(USER_BASE + s * USER_SLOT);
        const user_head_t *h = (const user_head_t *)at;
        user[s].data = 0;
        if (!at || !user_valid(h, at + USER_DATA))
            continue;
        for (i = 0; i < 11u; i++)
            user_name[s][i] = h->name[i] >= ' ' && h->name[i] <= '~' ? h->name[i] : 0;
        user_name[s][11] = 0;
        user[s].name = user_name[s][0] ? user_name[s] : "MY BREAK";
        user[s].bytes = h->bytes;
        user[s].frames = h->frames;
        for (i = 0; i < 8u; i++)
            user[s].shots[i] = h->shots[i];
        user_rate[s] = h->rate;
        user[s].data = at + USER_DATA;
    }
}

/* PRESET's list: the built-in breaks, then the slots that hold one, in your order */
static unsigned breaks_count(void)
{
    unsigned n = jungle_breaks(), s;
    for (s = 0; s < USER_SLOTS; s++)
        n += user[s].data != 0;
    return n;
}
static const struct jungle_break *break_at(unsigned i)
{
    unsigned k;
    if (i < jungle_breaks())
        return jungle_break_at(i);
    i -= jungle_breaks();
    for (k = 0; k < USER_SLOTS; k++)
        if (user[user_order.order[k]].data && !i--)
            return &user[user_order.order[k]];
    return 0;
}

/* answers: a frame in the update's framing, cmd and body */
static void reply(uint32_t cmd, const uint8_t *body, uint32_t n)
{
    uint8_t raw[112], out[2 + 132];
    uint32_t i, k = 0, acc = 0, bits = 0, sum = 0;
    raw[0] = 0x00;
    raw[1] = 0x59;
    raw[2] = (uint8_t)cmd;
    raw[3] = (uint8_t)n;
    raw[4] = (uint8_t)(n >> 8);
    raw[5] = 0;
    for (i = 0; i < n; i++) {
        raw[6 + i] = body[i];
        sum += body[i];
    }
    raw[6 + n] = (uint8_t)~sum;
    out[k++] = 0xF0;
    for (i = 0; i < 7u + n; i++) {           /* 8 bits a byte into 7 a MIDI byte, low bits first */
        acc |= (uint32_t)raw[i] << bits;
        bits += 8;
        while (bits >= 7) {
            out[k++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            bits -= 7;
        }
    }
    if (bits)
        out[k++] = (uint8_t)(acc & 0x7Fu);
    out[k++] = 0xF7;
    plat_sysex_send(out, k);
}
static void reply_status(uint32_t cmd, uint32_t status)
{
    uint8_t b = (uint8_t)status;
    reply(cmd, &b, 1);
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int user_busy(void) { return jungle_playing() || st.external; }

/* a slot about to change: if it is playing (bound), the first built-in break instead */
static void user_unbind(unsigned slot)
{
    if (user[slot].data && break_at(st.brk) == &user[slot])
        select_break(0);
    user[slot].data = 0;
}

static uint32_t user_write(const uint8_t *b, uint32_t n)
{
    static uint8_t chunk[512];
    user_head_t h = {0};
    uint32_t slot, off, at, i;
    if (n < 37u || (slot = b[0]) >= USER_SLOTS)
        return ST_BAD;
    h.magic = USER_MAGIC;
    h.version = USER_VERSION;
    h.bytes = rd32(b + 1);
    h.crc = rd32(b + 5);
    h.frames = rd32(b + 9);
    for (i = 0; i < 12u; i++)
        h.name[i] = (char)b[13 + i];
    for (i = 0; i < 8u; i++)
        h.shots[i] = b[25 + i];
    h.rate = rd32(b + 33);
    if (h.frames < JUNGLE_SLICES * 320u || h.bytes != (h.frames + 319u) / 320u * 228u || h.bytes > USER_ROOM)
        return ST_BAD;
    for (i = 0; i < 8u; i++)
        if (h.shots[i] >= JUNGLE_SLICES)
            return ST_BAD;
    if (user_busy())
        return ST_BUSY;
    app_say("RECEIVING", "BREAK");
    user_unbind(slot);
    off = USER_BASE + slot * USER_SLOT;
    for (at = 0; at < USER_SLOT; at += 4096u)   /* the whole slot: its head first, so it reads empty from now */
        if (plat_data_erase(off + at))
            return ST_FLASH;
    for (at = 0; at < h.bytes; at += sizeof chunk) {
        uint32_t len = h.bytes - at < sizeof chunk ? h.bytes - at : sizeof chunk;
        if (plat_host_read(at, chunk, len))
            return ST_TRANSFER;
        if (plat_data_write(off + USER_DATA + at, chunk, len))
            return ST_FLASH;
    }
    if (crc32(plat_data(off + USER_DATA), h.bytes) != h.crc)
        return ST_CHECK;
    if (plat_data_write(off, &h, sizeof h))   /* the head last: now the slot holds the break */
        return ST_FLASH;
    user_scan();
    return ST_DONE;
}

/* the list changed: the break playing keeps playing, at its new place in it */
static void user_reindex(const struct jungle_break *playing)
{
    unsigned i, n = breaks_count();
    for (i = 0; i < n; i++)
        if (break_at(i) == playing) {
            st.brk = i;
            ui.redraw = 1;
            return;
        }
}

void app_usb_cmd(const uint8_t *d, uint32_t n)
{
    const uint8_t *b = d + 6;
    uint32_t len = n > 7u ? n - 7u : 0u, s, rc;
    if (d[2] == CMD_LIST) {
        uint8_t out[8 + USER_SLOTS * 22], *p = out;
        *p++ = USER_VERSION;
        *p++ = USER_SLOTS;
        *p++ = (uint8_t)USER_ROOM;
        *p++ = (uint8_t)(USER_ROOM >> 8);
        *p++ = (uint8_t)(USER_ROOM >> 16);
        *p++ = 0;
        *p++ = (uint8_t)user_busy();
        for (s = 0; s < USER_SLOTS; s++) {
            uint32_t i, f = user[s].data ? user[s].frames : 0u, r = user[s].data ? user_rate[s] : 0u;
            *p++ = user[s].data != 0;
            for (i = 0; i < 12u; i++)
                *p++ = user[s].data && i < 11u ? (uint8_t)user_name[s][i] : 0;
            *p++ = (uint8_t)f;
            *p++ = (uint8_t)(f >> 8);
            *p++ = (uint8_t)(f >> 16);
            *p++ = (uint8_t)(f >> 24);
            *p++ = (uint8_t)r;
            *p++ = (uint8_t)(r >> 8);
            *p++ = (uint8_t)(r >> 16);
            *p++ = (uint8_t)(r >> 24);
        }
        for (s = 0; s < USER_SLOTS; s++)
            *p++ = user_order.order[s];
        reply(CMD_LIST, out, (uint32_t)(p - out));
    } else if (d[2] == CMD_WRITE) {
        rc = user_write(b, len);
        if (rc == ST_DONE)
            app_say("BREAK SAVED", user_name[b[0]]);
        else if (rc == ST_BUSY)
            app_say("STOP FIRST", "THEN SEND");
        else if (rc != ST_BAD)
            app_say("BREAK NOT", "SAVED");
        reply_status(CMD_WRITE, rc);
    } else if (d[2] == CMD_DELETE) {
        if (len < 1u || b[0] >= USER_SLOTS)
            rc = ST_BAD;
        else if (user_busy())
            rc = ST_BUSY;
        else {
            user_unbind(b[0]);
            rc = plat_data_erase(USER_BASE + b[0] * USER_SLOT) ? ST_FLASH : ST_DONE;
            user_scan();
            if (!rc)
                app_say("BREAK", "DELETED");
        }
        reply_status(CMD_DELETE, rc);
    } else if (d[2] == CMD_ORDER) {
        const struct jungle_break *playing = break_at(st.brk);
        if (len < USER_SLOTS || !order_valid(b))
            rc = ST_BAD;
        else if (user_busy())
            rc = ST_BUSY;
        else {
            for (s = 0; s < USER_SLOTS; s++)
                user_order.order[s] = b[s];
            rc = plat_store_save(OBJ_SETTINGS, &user_order, sizeof user_order) ? ST_FLASH : ST_DONE;
            user_reindex(playing);
        }
        reply_status(CMD_ORDER, rc);
    }
}
