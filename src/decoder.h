/* jtty-sidecar - streaming decoder wrapper around rjtty_sub / jtty_get_updates.
 *
 * Owns the growing 12 kHz capture buffer the upstream decoder expects, calls it
 * after every block of new audio (as the WSJT-X GUI does), and converts its
 * buffer-relative times to absolute sample positions in the client's stream.
 *
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#ifndef JTTY_DECODER_H
#define JTTY_DECODER_H

#include <stdint.h>

typedef struct {
    int64_t message_id;     /* upstream id, stable while a message grows */
    int64_t start_sample;   /* absolute position (samples at 12 kHz) of the message start */
    float   frequency;      /* audio Hz */
    int     eom;            /* 1 once the sender's end-of-message frame arrived */
    char    text[81];       /* trimmed, NUL terminated */
} JttyDecode;

typedef void (*JttyDecodeFn)(const JttyDecode *d, void *user);

typedef struct {
    int16_t *buf;           /* capture buffer */
    int      cap;           /* capacity in samples */
    int      kz;            /* valid samples */
    int64_t  base;          /* absolute sample index of buf[0] */
    int64_t  total;         /* samples pushed in total */
    int      restarts;      /* buffer rollovers so far */
    int      nfa, nfb;      /* search bounds, Hz */
    float    f0, ftol;      /* preferred channel and tolerance, Hz */
} JttyDecoder;

/* seconds = capture buffer length before a rollover (600 is plenty; 14 MB). */
int  jtty_decoder_init(JttyDecoder *d, int seconds);
void jtty_decoder_free(JttyDecoder *d);
/* Forget everything; the next push starts a fresh capture. */
void jtty_decoder_reset(JttyDecoder *d);
/* Append audio and run the decoder; every update is reported through fn. */
void jtty_decoder_push(JttyDecoder *d, const int16_t *pcm, int n, JttyDecodeFn fn, void *user);

#endif
