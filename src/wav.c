/* jtty-sidecar - minimal RIFF/WAVE read and write (PCM16).
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

long wav_read_12k(const char *path, int16_t **out, const char **err)
{
    *out = NULL;
    *err = "";
    FILE *f = fopen(path, "rb");
    if (!f) { *err = "cannot open file"; return -1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44) { fclose(f); *err = "not a WAV file"; return -1; }
    unsigned char *buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); *err = "read failed"; return -1; }
    fclose(f);
    if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) { free(buf); *err = "not a RIFF/WAVE file"; return -1; }

    int channels = 0, rate = 0, bits = 0, format = 0;
    const unsigned char *data = NULL;
    uint32_t datalen = 0;
    long pos = 12;
    while (pos + 8 <= size) {
        const unsigned char *ck = buf + pos;
        uint32_t len = rd32(ck + 4);
        if (!memcmp(ck, "fmt ", 4) && len >= 16) {
            format = rd16(ck + 8);
            channels = rd16(ck + 10);
            rate = (int)rd32(ck + 12);
            bits = rd16(ck + 22);
        } else if (!memcmp(ck, "data", 4)) {
            data = ck + 8;
            datalen = len;
            if (pos + 8 + (long)len > size) datalen = (uint32_t)(size - pos - 8);
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!data || channels < 1) { free(buf); *err = "no data or fmt chunk"; return -1; }
    if (format != 1 || bits != 16) { free(buf); *err = "only 16-bit PCM is supported"; return -1; }
    int decim;
    if (rate == 12000) decim = 1;
    else if (rate == 48000) decim = 4;
    else { free(buf); *err = "sample rate must be 12000 or 48000"; return -1; }

    long frames = (long)(datalen / (uint32_t)(2 * channels));
    long nout = frames / decim;
    int16_t *pcm = malloc((size_t)(nout > 0 ? nout : 1) * sizeof(int16_t));
    if (!pcm) { free(buf); *err = "out of memory"; return -1; }
    for (long i = 0; i < nout; ++i) {
        int acc = 0;
        for (int d = 0; d < decim; ++d) {
            const unsigned char *s = data + ((size_t)(i * decim + d) * channels) * 2;
            acc += (int16_t)rd16(s);
        }
        pcm[i] = (int16_t)(acc / decim);
    }
    free(buf);
    *out = pcm;
    return nout;
}

static void wr32(FILE *f, uint32_t v) { unsigned char b[4] = { v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255 }; fwrite(b, 1, 4, f); }
static void wr16(FILE *f, uint16_t v) { unsigned char b[2] = { v & 255, (v >> 8) & 255 }; fwrite(b, 1, 2, f); }

int wav_write_mono(const char *path, const int16_t *pcm, long n, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t datalen = (uint32_t)n * 2u;
    fwrite("RIFF", 1, 4, f); wr32(f, 36 + datalen); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); wr32(f, 16); wr16(f, 1); wr16(f, 1); wr32(f, (uint32_t)rate);
    wr32(f, (uint32_t)rate * 2u); wr16(f, 2); wr16(f, 16);
    fwrite("data", 1, 4, f); wr32(f, datalen);
    for (long i = 0; i < n; ++i) wr16(f, (uint16_t)pcm[i]);
    fclose(f);
    return 0;
}
