/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Encode 32 kHz mono int16 raw PCM into SEA5 and pack it as slot 1 of an
 * FM-1 sample bank (the workbench backup format, see app/samples.c).
 * Prints the codec's signal-to-noise ratio against the input.
 *
 * Usage: make_bank in.raw out.bin [slot name] */
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sea_encode.h"
#define FM1_AUDIO_DATA
#include "../../app/jungle/jungle_sea_dqt.inc" /* the table the firmware decodes with */

enum { HEADER = 512, CHUNK = 228 };

static uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: make_bank in.raw out.bin [name]\n"); return 2; }
    const char *name = argc > 3 ? argv[3] : "AMEN 160";
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    unsigned frames = (unsigned)(ftell(f) / 2);
    fseek(f, 0, SEEK_SET);
    int16_t *pcm = malloc(frames * 2U);
    if (fread(pcm, 2, frames, f) != frames) return 1;
    fclose(f);

    uint8_t *sea = fm1_sea_encode(pcm, frames, jungle_dqt);
    if (!sea) return 1;
    unsigned chunks = (frames + 319U) / 320U, bytes = chunks * CHUNK, size = HEADER + bytes;

    /* Decode back for the error measurement. */
    double signal = 0, noise = 0;
    for (unsigned c = 0; c < chunks; ++c) {
        int16_t out[FM1_SEA_FRAMES];
        if (fm1_sea_decode(sea + c * CHUNK, CHUNK, 5, jungle_dqt, out)) return 1;
        for (unsigned i = 0; i < FM1_SEA_FRAMES && c * 320U + i < frames; ++i) {
            double a = pcm[c * 320U + i], b = out[i];
            signal += a * a;
            noise += (a - b) * (a - b);
        }
    }

    uint8_t *bank = calloc(size, 1);
    put32(bank + 0, 0x53314d46U); /* "FM1S" */
    put32(bank + 4, 1);
    put32(bank + 8, size);
    put32(bank + 20, 32000);
    unsigned offset = HEADER;
    for (unsigned i = 0; i < 16; ++i) {
        uint8_t *entry = bank + 32 + i * 28;
        put32(entry, offset);
        if (i == 0) {
            put32(entry + 4, frames);
            put32(entry + 8, bytes);
            strncpy((char *)entry + 12, name, 15);
            memcpy(bank + offset, sea, bytes);
            offset += bytes;
        }
    }
    /* CRC with the CRC and commit words counted as zero. */
    static const uint8_t zero[8];
    uint32_t crc = crc32(0, bank, 12);
    crc = crc32(crc, zero, 8);
    crc = crc32(crc, bank + 20, size - 20);
    put32(bank + 12, crc);

    f = fopen(argv[2], "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    fwrite(bank, 1, size, f);
    fclose(f);
    printf("%s: %u frames (%.3f s), SEA5 %u bytes, bank %u bytes, codec SNR %.1f dB\n",
           argv[2], frames, frames / 32000.0, bytes, size, 10 * log10(signal / noise));
    return 0;
}
