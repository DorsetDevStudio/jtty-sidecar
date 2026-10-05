/* jtty-sidecar - text to JTTY audio.
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#ifndef JTTY_ENCODER_H
#define JTTY_ENCODER_H

#include <stdint.h>

enum {
    JTTY_ENC_OK = 0,
    JTTY_ENC_EMPTY = 1,       /* nothing left after normalisation */
    JTTY_ENC_FAILED = 2,      /* upstream packer refused the text */
    JTTY_ENC_BAD_PROFILE = 3,
    JTTY_ENC_BAD_RATE = 4,    /* sample_rate not a multiple of 31.25 baud */
    JTTY_ENC_TOO_LONG = 5     /* would exceed the output buffer */
};

/* Normalises text the way the upstream GUI does (upper case, unsupported
 * characters become '#', at most 80 characters; a leading space when chained
 * behind a message still being sent), packs it, and renders audio at
 * sample_rate (12000 or 48000) with the lowest tone at f0 Hz.
 * canonical (81 bytes) receives the text as actually transmitted.
 * Returns a JTTY_ENC_* code; *nsamples and *nframes are filled on success. */
int jtty_encode(const char *text, int profile, int chained, int sample_rate, float f0,
                int16_t *pcm, int pcm_capacity, int *nsamples, int *nframes, char *canonical);

#endif
