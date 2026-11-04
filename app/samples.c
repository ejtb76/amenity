/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "fm1_engine_control.h"
#include "fm1_device.h"
#include "instrument.h"
#include "sampledrum.h"
#include "fm_patch.h"
#include "jungle/jungle.h"
#include <string.h>

/* Single bank, no hidden second copy. CRC and a last-written commit word make
 * an interrupted import visibly incomplete. Projects occupy separate sectors.
 * The browser retains/downloads a complete bank before replacing one slot. */
struct sample_entry {
    uint32_t offset, frames, bytes;
    char name[16];
};
static struct sample_header {
    uint32_t magic, version, bytes, crc, commit, rate, reserved[2];
    struct sample_entry entry[16];
    uint8_t padding[32];
} header;
_Static_assert(sizeof header == 512, "sample bank header");
static unsigned state, total, position;
static uint32_t checksum, changed_at;

/* Breaks the GLO menu offers: the bank's loops in slot order, then the
 * firmware's built-in one. A slot is a break when it is named AMEN... or runs
 * at least 1.5 s (a two-bar loop up to 320 BPM); shorter slots are one-shots. */
enum { BREAK_MAX = SAMPLE_DRUM_PADS + 1, BREAK_MIN_FRAMES = 48000,
       BREAK_BIND_FRAMES = JUNGLE_SLICES * SAMPLE_DRUM_CHUNK_FRAMES };
static struct break_entry {
    const uint8_t *data;
    uint32_t bytes, frames;
    uint16_t bpm_x10; /* the loop's own tempo */
    char name[16];
} breaks[BREAK_MAX];
static unsigned break_count, break_now;
static void add_break(const uint8_t *data, unsigned bytes, unsigned frames, unsigned bpm_x10,
                      const char *name)
{
    if (break_count >= BREAK_MAX)
        return;
    struct break_entry *b = &breaks[break_count++];
    b->data = data;
    b->bytes = bytes;
    b->frames = frames;
    b->bpm_x10 = (uint16_t)bpm_x10;
    memset(b->name, 0, sizeof b->name);
    for (unsigned i = 0; i < 15 && name[i]; ++i)
        b->name[i] = name[i];
}
/* Appends the built-in break; returns its index, or ~0U without one. */
static unsigned add_builtin(void)
{
    const uint8_t *data = NULL;
    unsigned bytes = 0, frames = 0, index = break_count;
    if (!jungle_fallback(&data, &bytes, &frames) || break_count >= BREAK_MAX)
        return ~0U;
    add_break(data, bytes, frames, JUNGLE_NATIVE_BPM_X10, "AMEN 20K");
    return index;
}
static uint32_t crc32(uint32_t crc, unsigned offset, const uint8_t *p, unsigned bytes)
{
    /* Treat the stored CRC and commit word as zero, including split writes. */
    static const uint8_t zero[8] = {0};
    if (offset < 12) {
        unsigned n = bytes < 12 - offset ? bytes : 12 - offset;
        crc = groove_crc32(crc, p, n);
        p += n; offset += n; bytes -= n;
    }
    if (offset < 20 && bytes) {
        unsigned n = bytes < 20 - offset ? bytes : 20 - offset;
        crc = groove_crc32(crc, zero, n);
        p += n; bytes -= n;
    }
    return groove_crc32(crc, p, bytes);
}
static void unbind(void)
{
    /* Before the bank can be rewritten: the built-in break, or silence. */
    const uint8_t *break_data = NULL;
    unsigned break_bytes = 0, break_frames = 0;
    (void)jungle_fallback(&break_data, &break_bytes, &break_frames);
    (void)jungle_bind(break_data, break_bytes, break_frames);
    fm1_engine_pause();
    groove_samples_stop();
    for (unsigned i = 0; i < SAMPLE_DRUM_PADS; ++i)
        (void)sampledrum_bind(i, NULL, 0, 0, NULL);
    fm1_engine_resume();
    /* Only the built-in break (if any) is on offer until the bank is bound. */
    break_count = break_now = 0;
    (void)add_builtin();
}
static int valid(void)
{
    if (header.magic != 0x53314d46U || header.version != 1 || header.commit ||
        header.rate != 32000 || header.bytes < sizeof header ||
        header.bytes > GROOVE_SAMPLE_BYTES || header.reserved[0] || header.reserved[1])
        return 0;
    for (unsigned i = 0; i < sizeof header.padding; ++i)
        if (header.padding[i])
            return 0;
    unsigned next = sizeof header;
    for (unsigned i = 0; i < 16; ++i) {
        const struct sample_entry *s = &header.entry[i];
        if (s->offset != next || s->frames > SAMPLE_DRUM_MAX_FRAMES ||
            (i >= SAMPLE_DRUM_PADS && (s->frames || s->name[0])) ||
            s->bytes != ((s->frames + 319U) / 320U) * 228U || s->bytes > header.bytes - next ||
            s->name[15])
            return 0;
        for (unsigned n = 0; n < 15; ++n)
            if ((uint8_t)s->name[n] && (s->name[n] < 32 || s->name[n] > 126))
                return 0;
        next += s->bytes;
    }
    return next == header.bytes;
}
static const uint8_t *mapped_data(void)
{
    const uint8_t *mapped = fm1_flash_map(GROOVE_SAMPLE_OFFSET, total);
    if (!mapped)
        return NULL;
    for (unsigned p = sizeof header; p < total; p += 228U)
        if (mapped[p] != 1 || mapped[p + 1] != 0x45 || mapped[p + 2] != 20 || mapped[p + 3] != 0x5a)
            return NULL;
    return mapped;
}
static int bind(void)
{
    const uint8_t *mapped = mapped_data();
    if (!mapped)
        return GROOVE_PROJECT_INVALID;
    fm1_engine_pause();
    for (unsigned i = 0; i < SAMPLE_DRUM_PADS; ++i) {
        const struct sample_entry *s = &header.entry[i];
        (void)sampledrum_bind(i, s->frames ? mapped + s->offset : NULL, s->bytes, s->frames,
                              s->name);
    }
    fm1_engine_resume();
    /* List the breaks for the GLO menu: every loop in the bank, then the
     * built-in one. AMENITY starts on the first slot whose name starts with
     * AMEN; without one the built-in break keeps playing; built without the
     * sample, the first loop in the bank, or the screen says what to do. */
    break_count = 0;
    unsigned pick = ~0U;
    for (unsigned i = 0; i < SAMPLE_DRUM_PADS; ++i) {
        const struct sample_entry *s = &header.entry[i];
        int amen = !memcmp(s->name, "AMEN", 4);
        if (!s->frames || s->frames < BREAK_BIND_FRAMES || (!amen && s->frames < BREAK_MIN_FRAMES))
            continue;
        if (amen && pick == ~0U)
            pick = break_count;
        add_break(mapped + s->offset, s->bytes, s->frames,
                  (153600000U + s->frames / 2U) / s->frames, s->name);
    }
    unsigned builtin = add_builtin();
    if (pick == ~0U)
        pick = builtin != ~0U ? builtin : 0;
    break_now = 0;
    if (break_count)
        (void)groove_break_select(pick);
    state = 2;
    return 0;
}
void groove_samples_init(void)
{
    unbind();
    state = total = position = 0;
    checksum = ~0U;
    state = 1;
}
void groove_samples_poll(void)
{
    if (state == 3 && fm1_millis() - changed_at > 10000U)
        groove_samples_cancel();
    if (state != 1)
        return;
    if (!total) {
        if (fm1_flash_size() != GROOVE_FLASH_BYTES) {
            state = 4;
            return;
        }
        int r = fm1_flash_read(GROOVE_SAMPLE_OFFSET, &header, sizeof header);
        if (r == FM1_FLASH_BUSY)
            return;
        if (r) {
            state = 4;
            return;
        }
        if (!valid()) {
            state = 0;
            return;
        }
        total = header.bytes;
    }
    uint8_t bytes[512];
    for (unsigned budget = 0; budget < 8 && position < total; ++budget) {
        unsigned n = total - position;
        if (n > sizeof bytes)
            n = sizeof bytes;
        int r = fm1_flash_read(GROOVE_SAMPLE_OFFSET + position, bytes, n);
        if (r == FM1_FLASH_BUSY) return;
        if (r) {
            state = 4;
            return;
        }
        checksum = crc32(checksum, position, bytes, n);
        position += n;
    }
    if (position == total && (~checksum != header.crc || bind()))
        state = 4;
}
unsigned groove_samples_status(void)
{
    return state;
}
unsigned groove_samples_size(void)
{
    return state == 2 ? total : 0;
}
int groove_samples_begin(unsigned bytes)
{
    if (bytes < sizeof header || bytes > GROOVE_SAMPLE_LIMIT ||
        fm1_flash_size() != GROOVE_FLASH_BYTES)
        return GROOVE_PROJECT_INVALID;
    unbind();
    /* Erase the commit first, before any old sample bytes can be overwritten. */
    groove_edit_record(0, 0, 0);
    fm1_engine_pause();
    fm1_engine_stop();
    fm1_engine_resume();
    int r = fm1_flash_erase(GROOVE_SAMPLE_OFFSET);
    if (r) {
        state = 4;
        return r;
    }
    state = 3;
    total = bytes;
    position = 0;
    checksum = ~0U;
    changed_at = fm1_millis();
    memset(&header, 0, sizeof header);
    return 0;
}
int groove_samples_write(unsigned offset, const uint8_t *data, unsigned bytes)
{
    if (state != 3 || !data || !bytes || bytes > 128 || offset != position ||
        bytes > total - position)
        return GROOVE_PROJECT_INVALID;
    uint8_t copy[128];
    memcpy(copy, data, bytes);
    if (offset < sizeof header) {
        unsigned n = sizeof header - offset;
        if (n > bytes)
            n = bytes;
        memcpy((uint8_t *)&header + offset, data, n);
        for (unsigned i = 0; i < bytes; ++i)
            if (offset + i >= 16 && offset + i < 20)
                copy[i] = 255;
    }
    /* Requests may straddle a sector. Erase only the as-yet-unwritten sector. */
    unsigned first = offset / 4096U, last = (offset + bytes - 1U) / 4096U;
    int r = 0;
    if (offset && !(offset % 4096U))
        r = fm1_flash_erase(GROOVE_SAMPLE_OFFSET + first * 4096U);
    if (!r && last != first)
        r = fm1_flash_erase(GROOVE_SAMPLE_OFFSET + last * 4096U);
    if (!r)
        r = fm1_flash_write(GROOVE_SAMPLE_OFFSET + offset, copy, bytes);
    if (r) {
        state = 4;
        return r;
    }
    checksum = crc32(checksum, offset, data, bytes);
    position += bytes;
    changed_at = fm1_millis();
    return 0;
}
int groove_samples_finish(void)
{
    if (state != 3 || position != total || header.bytes != total || !valid() ||
        ~checksum != header.crc)
        return GROOVE_PROJECT_INVALID;
    /* Validate codec headers before committing, including data just written
     * through the raw Flash API and not yet visible in the mapped cache. */
    int r = fm1_flash_invalidate(GROOVE_SAMPLE_OFFSET, total);
    if (!r && !mapped_data())
        r = GROOVE_PROJECT_INVALID;
    uint32_t commit = 0;
    if (!r)
        r = fm1_flash_write(GROOVE_SAMPLE_OFFSET + 16, &commit, sizeof commit);
    if (!r)
        r = bind();
    if (r)
        state = 4;
    else groove_fm_library_init();
    return r;
}
void groove_samples_cancel(void)
{
    if (state == 3)
        state = 4;
}
unsigned groove_breaks_count(void)
{
    return break_count;
}
unsigned groove_break_current(void)
{
    return break_now;
}
const char *groove_break_name(unsigned index)
{
    return index < break_count ? breaks[index].name : NULL;
}
unsigned groove_break_bpm_x10(unsigned index)
{
    return index < break_count ? breaks[index].bpm_x10 : 0;
}
int groove_break_select(unsigned index)
{
    /* Control task. Never while a bank is being written: its data is going away. */
    if (index >= break_count || state == 3)
        return -1;
    const struct break_entry *b = &breaks[index];
    if (jungle_bind(b->data, b->bytes, b->frames))
        return -1;
    break_now = index;
    return 0;
}
int groove_samples_read(unsigned offset, uint8_t *data, unsigned bytes)
{
    if (state != 2 || !data || !bytes || bytes > 64 || offset > total || bytes > total - offset)
        return GROOVE_PROJECT_INVALID;
    return fm1_flash_read(GROOVE_SAMPLE_OFFSET + offset, data, bytes);
}
