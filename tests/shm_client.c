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
 * src/protocol.h, on both platforms: the Windows file mapping + events, and
 * the POSIX shared-memory object + semaphores. Everything a client needs is
 * here, in about 200 lines; the platform differences are in the small block
 * of helpers at the top.
 *
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE.
 */
#include "../src/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- platform helpers ----------------------------------------------------- */
#if defined(_WIN32)
#  include <windows.h>
typedef HANDLE wake_t;
static JttyShm *open_segment(const char *name, void **handle)
{
    HANDLE map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!map) return NULL;
    JttyShm *shm = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!shm) { CloseHandle(map); return NULL; }
    *handle = map;
    return shm;
}
static void close_segment(JttyShm *shm, void *handle) { UnmapViewOfFile(shm); CloseHandle(handle); }
static wake_t open_wake(const char *name, const char *suffix)
{
    char full[512];
    snprintf(full, sizeof full, "%s%s", name, suffix);
    return OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, full);
}
static void close_wake(wake_t w) { CloseHandle(w); }
static void signal_wake(wake_t w) { SetEvent(w); }
static void wait_wake(wake_t w, int ms) { WaitForSingleObject(w, (DWORD)ms); }
static void barrier(void) { MemoryBarrier(); }
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
static unsigned long now_ms(void) { return GetTickCount(); }
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <semaphore.h>
#  include <sys/mman.h>
#  include <sys/time.h>
#  include <time.h>
#  include <unistd.h>
typedef sem_t *wake_t;
static JttyShm *open_segment(const char *name, void **handle)
{
    char full[256];
    snprintf(full, sizeof full, "/%s", name);
    int fd = shm_open(full, O_RDWR, 0);
    if (fd < 0) return NULL;
    JttyShm *shm = mmap(NULL, sizeof(JttyShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);                                   /* the mapping keeps the object alive */
    if (shm == MAP_FAILED) return NULL;
    *handle = NULL;
    return shm;
}
static void close_segment(JttyShm *shm, void *handle) { (void)handle; munmap(shm, sizeof(JttyShm)); }
static wake_t open_wake(const char *name, const char *suffix)
{
    char full[256];
    snprintf(full, sizeof full, "/%s%s", name, suffix);
    sem_t *s = sem_open(full, 0);
    return s == SEM_FAILED ? NULL : s;
}
static void close_wake(wake_t w) { sem_close(w); }
static void signal_wake(wake_t w) { while (sem_trywait(w) == 0) { } sem_post(w); }
static void sleep_ms(int ms) { struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
static void wait_wake(wake_t w, int ms)
{
    /* portable timed wait: macOS has no sem_timedwait */
    for (int waited = 0;; waited += 5) {
        if (sem_trywait(w) == 0) return;
        if (waited >= ms) return;
        sleep_ms(5);
    }
}
static void barrier(void) { __sync_synchronize(); }
static unsigned long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_sec * 1000UL + tv.tv_usec / 1000UL);
}
#endif

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: jtty-shm-client <shm-name> \"<text>\" [--realtime]\n"); return 2; }
    const char *name = argv[1], *text = argv[2];
    int realtime = argc > 3 && !strcmp(argv[3], "--realtime");

    /* --- open: wait up to 5 s for the server to appear --- */
    void *handle = NULL;
    JttyShm *shm = NULL;
    for (int i = 0; i < 50 && !shm; ++i) {
        shm = open_segment(name, &handle);
        if (!shm) sleep_ms(100);
    }
    if (!shm) { fprintf(stderr, "no server named %s\n", name); return 1; }
    if (shm->magic != JTTY_SHM_MAGIC || shm->version != JTTY_SHM_VERSION || shm->struct_size != sizeof(JttyShm)) {
        fprintf(stderr, "protocol mismatch: magic %08x version %u size %u (want %08x %u %u)\n",
                shm->magic, shm->version, shm->struct_size, JTTY_SHM_MAGIC, JTTY_SHM_VERSION, (unsigned)sizeof(JttyShm));
        return 1;
    }
    while (shm->server_state != JTTY_STATE_RUNNING) sleep_ms(10);
    wake_t rx = open_wake(name, ".rx"), res = open_wake(name, ".res"), tx = open_wake(name, ".tx");
    if (!rx || !res || !tx) { fprintf(stderr, "cannot open the server's wake-up objects\n"); return 1; }
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
    barrier();
    int32_t seq = ++shm->tx_request.seq;
    signal_wake(rx);                             /* wake the server's loop early */
    for (int i = 0; i < 100 && shm->tx_response.seq_done != seq; ++i) wait_wake(tx, 100);
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
    unsigned long t0 = now_ms();
    for (int pos = 0; pos < total; pos += block) {
        int n = total - pos < block ? total - pos : block;
        int64_t w = shm->rx_written;
        for (int i = 0; i < n; ++i) shm->rx_pcm[(w + i) % JTTY_RX_RING_SAMPLES] = stream[pos + i];
        barrier();
        shm->rx_written = w + n;
        signal_wake(rx);
        if (realtime) {
            unsigned long due = t0 + (unsigned long)((pos + n) / 12);
            unsigned long now = now_ms();
            if (due > now) sleep_ms((int)(due - now));
        }
        /* drain results published so far */
        wait_wake(res, realtime ? 0 : 20);
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
    signal_wake(rx);
    for (int i = 0; i < 20 && shm->server_state != JTTY_STATE_QUITTING; ++i) sleep_ms(50);
    close_segment(shm, handle);
    close_wake(rx); close_wake(res); close_wake(tx);
    free(audio); free(stream);
    return ok ? 0 : 1;
}
