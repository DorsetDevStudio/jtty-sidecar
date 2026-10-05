/* jtty-sidecar - streaming decoder wrapper.
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#include "decoder.h"
#include "jtty_api.h"

#include <stdlib.h>
#include <string.h>

/* Audio carried across a buffer rollover so a message straddling it is still
 * caught on the new side. A frame wholly inside the carried stretch can be
 * reported a second time under a new id; clients de-duplicate on
 * (start_sample, frequency, text) if that matters to them. */
#define CARRY_SECONDS 3

int jtty_decoder_init(JttyDecoder *d, int seconds)
{
    memset(d, 0, sizeof *d);
    if (seconds < 30) seconds = 30;
    d->cap = seconds * JTTY_RX_RATE;
    d->buf = calloc((size_t)d->cap, sizeof(int16_t));
    if (!d->buf) return -1;
    d->nfa = 200;
    d->nfb = 3000;
    d->f0 = 1500.0f;
    d->ftol = 500.0f;
    return 0;
}

void jtty_decoder_free(JttyDecoder *d)
{
    free(d->buf);
    d->buf = NULL;
    jtty_release_fft_resources();
}

void jtty_decoder_reset(JttyDecoder *d)
{
    d->kz = 0;
    d->base = d->total;
    d->restarts++;
}

static void drain(JttyDecoder *d, JttyDecodeFn fn, void *user)
{
    char text[JTTY_UPDATE_BATCH * JTTY_MSG_LEN];
    int64_t ids[JTTY_UPDATE_BATCH];
    float freqs[JTTY_UPDATE_BATCH], tsync[JTTY_UPDATE_BATCH];
    int8_t eom[JTTY_UPDATE_BATCH];
    int count = 0;
    do {
        memset(text, ' ', sizeof text);
        jtty_get_updates_(text, ids, freqs, tsync, eom, &count, sizeof text);
        for (int i = 0; i < count; ++i) {
            if (ids[i] <= 0) continue;
            JttyDecode r;
            r.message_id = ids[i];
            r.frequency = freqs[i];
            r.eom = eom[i] ? 1 : 0;
            /* tsync is seconds from the start of the current capture buffer */
            r.start_sample = d->base + (int64_t)(tsync[i] * (float)JTTY_RX_RATE + 0.5f);
            memcpy(r.text, text + (size_t)i * JTTY_MSG_LEN, JTTY_MSG_LEN);
            r.text[JTTY_MSG_LEN] = 0;
            int n = JTTY_MSG_LEN;
            while (n > 0 && (r.text[n - 1] == ' ' || r.text[n - 1] == 0)) r.text[--n] = 0;
            if (fn) fn(&r, user);
        }
    } while (count == JTTY_UPDATE_BATCH);
}

void jtty_decoder_push(JttyDecoder *d, const int16_t *pcm, int n, JttyDecodeFn fn, void *user)
{
    while (n > 0) {
        if (d->kz >= d->cap) {
            /* Rollover: keep the tail, restart the upstream state (it restarts
             * itself when it sees a shorter buffer). */
            int carry = CARRY_SECONDS * JTTY_RX_RATE;
            memmove(d->buf, d->buf + (d->kz - carry), (size_t)carry * sizeof(int16_t));
            d->base += d->kz - carry;
            d->kz = carry;
            d->restarts++;
        }
        int room = d->cap - d->kz;
        int take = n < room ? n : room;
        memcpy(d->buf + d->kz, pcm, (size_t)take * sizeof(int16_t));
        d->kz += take;
        d->total += take;
        pcm += take;
        n -= take;

        int nsps = JTTY_NSPS;
        rjtty_sub_(d->buf, &d->kz, &nsps, &d->nfa, &d->nfb, &d->f0, &d->ftol);
        drain(d, fn, user);
    }
}
