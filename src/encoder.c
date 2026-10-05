/* jtty-sidecar - text to JTTY audio.
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#include "encoder.h"
#include "jtty_api.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The JTTY source alphabet, ALPHABET in jtty_source_codec.f90:
 *   0-9 A-Z space + - . / ? ! " # $ % , & * ( ) _ ' = [ ] { } < > | : ;
 * (the GUI also admits a-z, which the packer folds to upper case). */
static int is_source_char(int c)
{
    if (c == 0) return 0;
    if (c == ' ' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) return 1;
    return strchr("+-./?!\"#$%,&*()_'=[]{}<>|:;", c) != NULL;
}

int jtty_encode(const char *text, int profile, int chained, int sample_rate, float f0,
                int16_t *pcm, int pcm_capacity, int *nsamples, int *nframes, char *canonical)
{
    *nsamples = 0;
    *nframes = 0;
    if (canonical) canonical[0] = 0;
    if (profile < JTTY_EXCHANGE_UNKNOWN || profile > JTTY_EXCHANGE_RTTY) return JTTY_ENC_BAD_PROFILE;
    if (sample_rate % 125 != 0 || sample_rate < 8000) return JTTY_ENC_BAD_RATE;   /* 31.25 baud = rate/320*... must divide */
    int nsps = sample_rate / 125 * 4;    /* rate / 31.25 */

    /* Normalise exactly as the GUI does: upper case, '~' and NUL -> space,
     * anything outside the alphabet -> '#', truncate to 80 (79 if chained). */
    char msg[JTTY_MSG_LEN + 1];
    int n = 0;
    if (chained) msg[n++] = ' ';
    for (const char *p = text; *p && n < JTTY_MSG_LEN; ++p) {
        int c = toupper((unsigned char)*p);
        if (c == '~') c = ' ';
        msg[n++] = (char)(is_source_char(c) ? c : '#');
    }
    /* collapse to blank-padded CHARACTER*80 */
    while (n < JTTY_MSG_LEN) msg[n++] = ' ';
    msg[JTTY_MSG_LEN] = 0;
    {
        int blank = 1;
        for (int i = 0; i < JTTY_MSG_LEN; ++i) if (msg[i] != ' ') { blank = 0; break; }
        if (blank) return JTTY_ENC_EMPTY;
    }

    int itone[JTTY_MAX_TONES];
    int nsym = 0;
    genjtty_profile_(msg, &profile, itone, &nsym, JTTY_MSG_LEN);
    if (nsym <= 0) return JTTY_ENC_FAILED;

    if (canonical) {
        memcpy(canonical, msg, JTTY_MSG_LEN);
        canonical[JTTY_MSG_LEN] = 0;
        int k = JTTY_MSG_LEN;
        while (k > 0 && canonical[k - 1] == ' ') canonical[--k] = 0;
    }

    int nwave = nsps * nsym;
    if (nwave > pcm_capacity) return JTTY_ENC_TOO_LONG;
    float *wave = malloc((size_t)nwave * sizeof(float));
    if (!wave) return JTTY_ENC_FAILED;
    float bt = JTTY_GFSK_BT, fs = (float)sample_rate;
    int icmplx = 0;
    gen_jttywave_(itone, &nsym, &nsps, &bt, &fs, &f0, wave, wave, &icmplx, &nwave);
    for (int i = 0; i < nwave; ++i) {
        float v = wave[i] * 32767.0f;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        pcm[i] = (int16_t)lrintf(v);
    }
    free(wave);
    *nsamples = nwave;
    *nframes = nsym / JTTY_FRAME_SYMBOLS;
    return JTTY_ENC_OK;
}
