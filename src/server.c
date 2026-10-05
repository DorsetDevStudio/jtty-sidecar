/* jtty-sidecar - shared-memory server loop. See protocol.h for the contract.
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#include "server.h"
#include "decoder.h"
#include "encoder.h"
#include "shm_win.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    JttyShmServer *s;
    int verbose;
} Ctx;

static void publish(const JttyDecode *d, void *user)
{
    Ctx *c = user;
    JttyShm *shm = c->s->shm;
    JttyShmResult *r = &shm->results[shm->results_written % JTTY_RESULT_RING];
    r->message_id = d->message_id;
    r->start_sample = d->start_sample;
    r->frequency = d->frequency;
    r->eom = d->eom;
    memset(r->text, ' ', sizeof r->text);
    memcpy(r->text, d->text, strlen(d->text));
    memset(r->reserved, 0, sizeof r->reserved);
    MemoryBarrier();
    shm->results_written++;
    if (c->verbose)
        fprintf(stderr, "decode id=%lld at %lld %.1f Hz%s: %s\n", (long long)d->message_id,
                (long long)d->start_sample, d->frequency, d->eom ? " [EOM]" : "", d->text);
}

static void handle_tx(Ctx *c)
{
    JttyShm *shm = c->s->shm;
    int32_t seq = shm->tx_request.seq;
    if (seq == shm->tx_response.seq_done) return;
    char text[JTTY_SHM_TEXT + 1];
    memcpy(text, shm->tx_request.text, JTTY_SHM_TEXT);
    text[JTTY_SHM_TEXT] = 0;
    int ns = 0, nf = 0;
    char canonical[81];
    int st = jtty_encode(text, shm->tx_request.profile, shm->tx_request.chained,
                         shm->tx_request.sample_rate, shm->tx_request.f0,
                         shm->tx_pcm, JTTY_TX_MAX_SAMPLES, &ns, &nf, canonical);
    shm->tx_response.status = st;
    shm->tx_response.nsamples = ns;
    shm->tx_response.nframes = nf;
    memset(shm->tx_response.text, 0, sizeof shm->tx_response.text);
    memcpy(shm->tx_response.text, canonical, strlen(canonical));
    MemoryBarrier();
    shm->tx_response.seq_done = seq;
    jtty_shm_signal_tx(c->s);
    if (c->verbose)
        fprintf(stderr, "tx seq=%d status=%d frames=%d samples=%d \"%s\"\n", seq, st, nf, ns, canonical);
}

int jtty_serve(const char *name, int parent_pid, int verbose)
{
    JttyShmServer s;
    if (jtty_shm_create(&s, name) != 0) return 2;
    JttyDecoder dec;
    if (jtty_decoder_init(&dec, 600) != 0) { jtty_shm_close(&s); return 2; }
    Ctx ctx = { &s, verbose };
    JttyShm *shm = s.shm;
    static int16_t block[JTTY_RX_RING_SAMPLES];

    shm->server_state = JTTY_STATE_RUNNING;
    fprintf(stderr, "jtty-sidecar serving \"%s\" (%u bytes shared)\n", name, shm->struct_size);
    int parent_checks = 0;

    for (;;) {
        jtty_shm_wait_rx(&s, 100);
        shm->heartbeat++;

        int cmd = shm->command;
        if (cmd == JTTY_CMD_QUIT) { shm->command = 0; break; }
        if (cmd == JTTY_CMD_RESET) {
            jtty_decoder_reset(&dec);
            shm->rx_consumed = shm->rx_written;
            shm->decoder_restarts = dec.restarts;
            shm->command = 0;
        }
        if (parent_pid > 0 && ++parent_checks >= 10) {
            parent_checks = 0;
            if (jtty_process_gone(parent_pid)) { fprintf(stderr, "parent process gone, exiting\n"); break; }
        }

        dec.nfa = shm->nfa;
        dec.nfb = shm->nfb;
        dec.f0 = shm->f0;
        dec.ftol = shm->ftol;

        /* Pull everything the client has written since we last looked. */
        int64_t written = shm->rx_written;
        int64_t consumed = shm->rx_consumed;
        if (written > consumed) {
            int64_t avail = written - consumed;
            if (avail > JTTY_RX_RING_SAMPLES) {
                /* the client ran more than a full ring ahead; the oldest is gone */
                shm->rx_dropped += avail - JTTY_RX_RING_SAMPLES;
                consumed = written - JTTY_RX_RING_SAMPLES;
                avail = JTTY_RX_RING_SAMPLES;
                jtty_decoder_reset(&dec);
            }
            int n = (int)avail;
            int start = (int)(consumed % JTTY_RX_RING_SAMPLES);
            int first = JTTY_RX_RING_SAMPLES - start;
            if (first > n) first = n;
            memcpy(block, shm->rx_pcm + start, (size_t)first * sizeof(int16_t));
            if (n > first) memcpy(block + first, shm->rx_pcm, (size_t)(n - first) * sizeof(int16_t));
            shm->rx_consumed = written;
            int64_t before = shm->results_written;
            /* keep decoder.total in step with the client's sample count so
             * start_sample is in the client's own units */
            if (dec.total != consumed) { dec.total = consumed; dec.base = consumed - dec.kz; }
            jtty_decoder_push(&dec, block, n, publish, &ctx);
            shm->decoder_restarts = dec.restarts;
            if (shm->results_written != before) jtty_shm_signal_results(&s);
        }

        handle_tx(&ctx);
    }

    shm->server_state = JTTY_STATE_QUITTING;
    jtty_decoder_free(&dec);
    jtty_shm_close(&s);
    return 0;
}
