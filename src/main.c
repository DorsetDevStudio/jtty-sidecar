/* jtty-sidecar - command line front end.
 *
 *   jtty-sidecar decode <file.wav> [--f0 Hz] [--ftol Hz] [--nfa Hz] [--nfb Hz] [--json]
 *   jtty-sidecar encode "<text>" --out <file.wav> [--rate 12000|48000] [--f0 Hz]
 *                       [--profile unknown|field-day|rtty-roundup] [--chained]
 *   jtty-sidecar loopback "<text>" [--snr dB]        encode then decode in memory
 *   jtty-sidecar serve <shm-name> [--parent <pid>] [--verbose]
 *   jtty-sidecar version
 *
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE.
 */
#include "decoder.h"
#include "encoder.h"
#include "jtty_api.h"
#include "protocol.h"
#include "server.h"
#include "wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *argval(int argc, char **argv, const char *name, const char *def)
{
    for (int i = 2; i + 1 < argc; ++i)
        if (!strcmp(argv[i], name)) return argv[i + 1];
    return def;
}

static int argflag(int argc, char **argv, const char *name)
{
    for (int i = 2; i < argc; ++i)
        if (!strcmp(argv[i], name)) return 1;
    return 0;
}

static int profile_from(const char *s)
{
    if (!strcmp(s, "unknown")) return JTTY_EXCHANGE_UNKNOWN;
    if (!strcmp(s, "field-day")) return JTTY_EXCHANGE_FIELD_DAY;
    if (!strcmp(s, "rtty-roundup")) return JTTY_EXCHANGE_RTTY;
    return -1;
}

static void usage(void)
{
    fputs("jtty-sidecar " JTTY_SIDECAR_VERSION " - JTTY modem as a separate process\n"
          "  decode <file.wav> [--f0 Hz] [--ftol Hz] [--nfa Hz] [--nfb Hz] [--json]\n"
          "  encode \"<text>\" --out <file.wav> [--rate 12000|48000] [--f0 Hz]\n"
          "         [--profile unknown|field-day|rtty-roundup] [--chained]\n"
          "  loopback \"<text>\" [--snr dB] [--profile ...]\n"
          "  serve <shm-name> [--parent <pid>] [--verbose]\n"
          "  version\n", stderr);
}

typedef struct { int json; int count; } PrintCtx;

static void print_decode(const JttyDecode *d, void *user)
{
    PrintCtx *p = user;
    p->count++;
    double t = (double)d->start_sample / JTTY_RX_RATE;
    if (p->json)
        printf("{\"id\":%lld,\"t\":%.3f,\"freq\":%.1f,\"eom\":%s,\"text\":\"%s\"}\n",
               (long long)d->message_id, t, d->frequency, d->eom ? "true" : "false", d->text);
    else
        printf("%8.3f  %7.1f  %4lld %s %s\n", t, d->frequency, (long long)d->message_id,
               d->eom ? "*" : " ", d->text);
    fflush(stdout);
}

/* Feed a buffer in 100 ms blocks, the cadence a live client uses, then a
 * stretch of silence so the last frame is scanned to the end. */
static int run_decoder(JttyDecoder *dec, const int16_t *pcm, long n, int json)
{
    PrintCtx pc = { json, 0 };
    const int block = JTTY_RX_RATE / 10;
    for (long i = 0; i < n; i += block) {
        int take = (int)((n - i) < block ? (n - i) : block);
        jtty_decoder_push(dec, pcm + i, take, print_decode, &pc);
    }
    int16_t zeros[JTTY_RX_RATE / 10];
    memset(zeros, 0, sizeof zeros);
    for (int i = 0; i < 30; ++i) jtty_decoder_push(dec, zeros, block, print_decode, &pc);
    return pc.count;
}

static int cmd_decode(int argc, char **argv)
{
    if (argc < 3) { usage(); return 2; }
    int16_t *pcm;
    const char *err;
    long n = wav_read_12k(argv[2], &pcm, &err);
    if (n < 0) { fprintf(stderr, "%s: %s\n", argv[2], err); return 1; }
    JttyDecoder dec;
    if (jtty_decoder_init(&dec, (int)(n / JTTY_RX_RATE) + 40) != 0) return 1;
    dec.f0 = (float)atof(argval(argc, argv, "--f0", "1500"));
    dec.ftol = (float)atof(argval(argc, argv, "--ftol", "500"));
    dec.nfa = atoi(argval(argc, argv, "--nfa", "200"));
    dec.nfb = atoi(argval(argc, argv, "--nfb", "3000"));
    int count = run_decoder(&dec, pcm, n, argflag(argc, argv, "--json"));
    jtty_decoder_free(&dec);
    free(pcm);
    fprintf(stderr, "%ld samples (%.1f s), %d updates\n", n, (double)n / JTTY_RX_RATE, count);
    return 0;
}

static int cmd_encode(int argc, char **argv)
{
    if (argc < 3) { usage(); return 2; }
    const char *out = argval(argc, argv, "--out", NULL);
    if (!out) { fprintf(stderr, "encode needs --out <file.wav>\n"); return 2; }
    int rate = atoi(argval(argc, argv, "--rate", "48000"));
    float f0 = (float)atof(argval(argc, argv, "--f0", "1500"));
    int profile = profile_from(argval(argc, argv, "--profile", "unknown"));
    if (profile < 0) { fprintf(stderr, "unknown profile\n"); return 2; }
    int cap = 59 * JTTY_MAX_FRAMES * (rate / 125 * 4) + 4096;
    int16_t *pcm = malloc((size_t)cap * sizeof(int16_t));
    int ns, nf;
    char canonical[81];
    int st = jtty_encode(argv[2], profile, argflag(argc, argv, "--chained"), rate, f0, pcm, cap, &ns, &nf, canonical);
    if (st != JTTY_ENC_OK) { fprintf(stderr, "encode failed (status %d)\n", st); free(pcm); return 1; }
    if (wav_write_mono(out, pcm, ns, rate) != 0) { fprintf(stderr, "cannot write %s\n", out); free(pcm); return 1; }
    printf("\"%s\"  %d frame%s  %.3f s  -> %s\n", canonical, nf, nf == 1 ? "" : "s", (double)ns / rate, out);
    free(pcm);
    return 0;
}

/* Gaussian noise, Box-Muller, deterministic seed so a test is repeatable. */
static float gauss(void)
{
    static unsigned s = 12345u;
    s = s * 1664525u + 1013904223u; float u1 = ((s >> 8) + 1.0f) / 16777217.0f;
    s = s * 1664525u + 1013904223u; float u2 = (s >> 8) / 16777216.0f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
}

static int cmd_loopback(int argc, char **argv)
{
    if (argc < 3) { usage(); return 2; }
    int profile = profile_from(argval(argc, argv, "--profile", "unknown"));
    if (profile < 0) { fprintf(stderr, "unknown profile\n"); return 2; }
    const char *snr_s = argval(argc, argv, "--snr", NULL);
    int cap = 59 * JTTY_MAX_FRAMES * JTTY_NSPS + 4096;
    int16_t *tone = malloc((size_t)cap * sizeof(int16_t));
    int ns, nf;
    char canonical[81];
    int st = jtty_encode(argv[2], profile, 0, JTTY_RX_RATE, 1500.0f, tone, cap, &ns, &nf, canonical);
    if (st != JTTY_ENC_OK) { fprintf(stderr, "encode failed (status %d)\n", st); return 1; }
    printf("sent: \"%s\" (%d frame%s)\n", canonical, nf, nf == 1 ? "" : "s");

    /* 2 s lead-in, the signal, 2 s tail; optional noise at the requested SNR
     * in the 2500 Hz reference bandwidth WSJT-X uses for its SNR scale. */
    long lead = 2 * JTTY_RX_RATE, n = lead + ns + lead;
    int16_t *pcm = calloc((size_t)n, sizeof(int16_t));
    float scale = 0.25f, nrms = 0.0f;
    if (snr_s) {
        float snr = (float)atof(snr_s);
        /* signal power of a unit sine is 0.5; noise in 6000 Hz Nyquist band
         * scaled so that the 2500 Hz portion gives the requested SNR */
        float sig_pwr = 0.5f * scale * scale;
        float noise_pwr_2500 = sig_pwr / powf(10.0f, snr / 10.0f);
        nrms = sqrtf(noise_pwr_2500 * 6000.0f / 2500.0f);
    }
    for (long i = 0; i < n; ++i) {
        float v = (snr_s ? nrms * gauss() : 0.0f);
        if (i >= lead && i < lead + ns) v += scale * tone[i - lead] / 32767.0f;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[i] = (int16_t)lrintf(v * 32767.0f);
    }
    JttyDecoder dec;
    if (jtty_decoder_init(&dec, 60) != 0) return 1;
    int count = run_decoder(&dec, pcm, n, argflag(argc, argv, "--json"));
    jtty_decoder_free(&dec);
    free(pcm); free(tone);
    if (count == 0) { fprintf(stderr, "FAIL: nothing decoded\n"); return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 2; }
    if (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")) {
        printf("jtty-sidecar %s\nupstream: %s\nshared memory protocol v%u, %u bytes\n",
               JTTY_SIDECAR_VERSION, JTTY_UPSTREAM_LABEL, JTTY_SHM_VERSION, (unsigned)sizeof(JttyShm));
        return 0;
    }
    if (!strcmp(argv[1], "decode")) return cmd_decode(argc, argv);
    if (!strcmp(argv[1], "encode")) return cmd_encode(argc, argv);
    if (!strcmp(argv[1], "loopback")) return cmd_loopback(argc, argv);
    if (!strcmp(argv[1], "serve")) {
        if (argc < 3) { usage(); return 2; }
        return jtty_serve(argv[2], atoi(argval(argc, argv, "--parent", "0")), argflag(argc, argv, "--verbose"));
    }
    usage();
    return 2;
}
