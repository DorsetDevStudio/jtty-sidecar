/* jtty-sidecar - reference shared-memory client and end-to-end test.
 *
 *   jtty-shm-client <shm-name> "<text>" [--realtime]
 *
 * Opens the segment a running `jtty-sidecar serve <shm-name>` created, asks
 * it to encode <text> at 12 kHz, streams that audio back into the receive ring
 * in 100 ms blocks (paced to wall-clock with --realtime, else as fast as the
 * server takes it), prints every result the server publishes, and finally
 * tells the server to quit. Exit code 0 when the text came back with EOM.
 *
 * This file is also the worked example of the client side of the protocol in
 * src/protocol.h: everything a client needs is here, in about 150 lines.
 *
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE.
 */
#include "../src/protocol.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE open_event(const char *name, const char *suffix)
{
    char full[512];
    snprintf(full, sizeof full, "%s%s", name, suffix);
    return OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, full);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: jtty-shm-client <shm-name> \"<text>\" [--realtime]\n"); return 2; }
    const char *name = argv[1], *text = argv[2];
    int realtime = argc > 3 && !strcmp(argv[3], "--realtime");

    /* --- open: wait up to 5 s for the server to appear --- */
    HANDLE map = NULL;
    for (int i = 0; i < 50 && !map; ++i) {
        map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (!map) Sleep(100);
    }
    if (!map) { fprintf(stderr, "no server named %s\n", name); return 1; }
    JttyShm *shm = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!shm) { fprintf(stderr, "MapViewOfFile failed\n"); return 1; }
    if (shm->magic != JTTY_SHM_MAGIC || shm->version != JTTY_SHM_VERSION || shm->struct_size != sizeof(JttyShm)) {
        fprintf(stderr, "protocol mismatch: magic %08x version %u size %u (want %08x %u %u)\n",
                shm->magic, shm->version, shm->struct_size, JTTY_SHM_MAGIC, JTTY_SHM_VERSION, (unsigned)sizeof(JttyShm));
        return 1;
    }
    HANDLE rx = open_event(name, ".rx"), res = open_event(name, ".res"), tx = open_event(name, ".tx");
    if (!rx || !res || !tx) { fprintf(stderr, "cannot open the server's events\n"); return 1; }
    while (shm->server_state != JTTY_STATE_RUNNING) Sleep(10);
    printf("server pid %d, heartbeat %u, segment %u bytes\n", shm->server_pid, shm->heartbeat, shm->struct_size);

    /* --- transmit request --- */
    memset(shm->tx_request.text, 0, sizeof shm->tx_request.text);
    {
        size_t len = strlen(text);
        memcpy(shm->tx_request.text, text, len < JTTY_SHM_TEXT ? len : JTTY_SHM_TEXT);
    }
    shm->tx_request.profile = JTTY_PROFILE_UNKNOWN;
    shm->tx_request.sample_rate = 12000;
    shm->tx_request.f0 = 1200.0f;
    shm->tx_request.chained = 0;
    MemoryBarrier();
    int32_t seq = ++shm->tx_request.seq;
    for (int i = 0; i < 100 && shm->tx_response.seq_done != seq; ++i) WaitForSingleObject(tx, 100);
    if (shm->tx_response.seq_done != seq) { fprintf(stderr, "server did not answer the tx request\n"); return 1; }
    if (shm->tx_response.status != JTTY_TX_OK) { fprintf(stderr, "tx rejected, status %d\n", shm->tx_response.status); return 1; }
    int ns = shm->tx_response.nsamples;
    char canonical[JTTY_SHM_TEXT + 1];
    memcpy(canonical, shm->tx_response.text, JTTY_SHM_TEXT);
    canonical[JTTY_SHM_TEXT] = 0;
    printf("tx: \"%s\" -> %d frames, %d samples at 12 kHz\n", canonical, shm->tx_response.nframes, ns);
    int16_t *audio = malloc((size_t)ns * sizeof(int16_t));
    memcpy(audio, shm->tx_pcm, (size_t)ns * sizeof(int16_t));

    /* --- stream it back as receive audio: 1 s silence, audio, 3 s silence --- */
    const int block = 1200;
    int total = 12000 + ns + 3 * 12000;
    int16_t *stream = calloc((size_t)total, sizeof(int16_t));
    for (int i = 0; i < ns; ++i) stream[12000 + i] = (int16_t)(audio[i] / 4);
    int64_t read_results = shm->results_written;
    int got_eom = 0;
    char last[JTTY_SHM_TEXT + 1] = "";
    DWORD t0 = GetTickCount();
    for (int pos = 0; pos < total; pos += block) {
        int n = total - pos < block ? total - pos : block;
        int64_t w = shm->rx_written;
        for (int i = 0; i < n; ++i) shm->rx_pcm[(w + i) % JTTY_RX_RING_SAMPLES] = stream[pos + i];
        MemoryBarrier();
        shm->rx_written = w + n;
        SetEvent(rx);
        if (realtime) {
            DWORD due = t0 + (DWORD)((pos + n) / 12);
            DWORD now = GetTickCount();
            if (due > now) Sleep(due - now);
        }
        /* drain results published so far */
        WaitForSingleObject(res, realtime ? 0 : 20);
        while (read_results < shm->results_written) {
            const JttyShmResult *r = &shm->results[read_results % JTTY_RESULT_RING];
            memcpy(last, r->text, JTTY_SHM_TEXT);
            last[JTTY_SHM_TEXT] = 0;
            for (int k = JTTY_SHM_TEXT; k > 0 && last[k - 1] == ' '; --k) last[k - 1] = 0;
            printf("rx: id %lld at %.3f s %.1f Hz%s \"%s\"\n", (long long)r->message_id,
                   (double)r->start_sample / 12000.0, r->frequency, r->eom ? " [EOM]" : "", last);
            if (r->eom) got_eom = 1;
            read_results++;
        }
    }
    printf("heartbeat %u, consumed %lld of %lld, restarts %d, dropped %lld\n", shm->heartbeat,
           (long long)shm->rx_consumed, (long long)shm->rx_written, shm->decoder_restarts, (long long)shm->rx_dropped);

    int ok = got_eom && !strcmp(last, canonical);
    printf(ok ? "PASS\n" : "FAIL: expected \"%s\", last \"%s\"%s\n", canonical, last, got_eom ? "" : " (no EOM)");

    shm->command = JTTY_CMD_QUIT;
    SetEvent(rx);
    for (int i = 0; i < 20 && shm->server_state != JTTY_STATE_QUITTING; ++i) Sleep(50);
    UnmapViewOfFile(shm);
    CloseHandle(map); CloseHandle(rx); CloseHandle(res); CloseHandle(tx);
    free(audio); free(stream);
    return ok ? 0 : 1;
}
