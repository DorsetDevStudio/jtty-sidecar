/* jtty-sidecar - the shared-memory interface between the server and a client.
 *
 * This header IS the public contract: anyone may implement a client from it.
 * One shared-memory segment named <name> holds a single JttyShm. Three named
 * wake-up objects signal changes:
 *     <name>.rx    client -> server : new receive audio in rx_pcm
 *     <name>.res   server -> client : new entries in results[]
 *     <name>.tx    server -> client : tx_response updated
 * The server creates all four objects; the client opens them.
 *
 * On Windows the segment is a file mapping named <name> and the wake-ups are
 * auto-reset events named <name>.rx / <name>.res / <name>.tx.
 * On macOS and Linux the segment is a POSIX shared-memory object (shm_open)
 * named "/<name>" and the wake-ups are named semaphores "/<name>.rx",
 * "/<name>.res" and "/<name>.tx", each held at a count of at most one by the
 * signaller (drain, then post) so it behaves like an auto-reset event. macOS
 * limits these names to 31 characters including the slash, so <name> is at
 * most 25 characters, and macOS has no sem_timedwait: a client that waits
 * with a timeout there polls sem_trywait. A client may also ignore the
 * wake-ups entirely and poll the counters; the server polls at 100 ms anyway.
 *
 * All multi-byte fields are little-endian native (every platform built is
 * little-endian); int64 fields are 8-byte aligned; the layout is fixed by the
 * static asserts at the bottom and never changes within a protocol version.
 * A client checks magic, version and struct_size before touching anything
 * else. The server writes magic LAST, so a client that opens the segment
 * while it is still being set up sees magic 0: that means "not ready, try
 * again", not a protocol mismatch. On POSIX a client also checks the
 * object's size (fstat) before mapping it: the server creates the object and
 * then sizes it, and touching an unsized mapping is a SIGBUS. The same binary
 * layout is used on every platform and architecture.
 *
 * Receive path: the client writes 12 kHz mono int16 audio into the ring
 * rx_pcm[rx_written % JTTY_RX_RING_SAMPLES], advances rx_written (total
 * samples ever written) AFTER the samples are in place, and sets the .rx
 * event. The server consumes up to rx_written, runs the decoder on every
 * block exactly as the WSJT-X GUI does, and for every new or extended message
 * appends a JttyShmResult at results[results_written % JTTY_RESULT_RING] then
 * increments results_written and sets .res. A result's start_sample is the
 * absolute sample index in the client's own stream (its rx_written count),
 * so the client converts it to wall-clock time itself. message_id is stable
 * while a message grows; eom becomes 1 on the final update.
 *
 * Transmit path: the client fills tx_request (text, profile, sample_rate,
 * f0, chained), then increments tx_request.seq. The server renders the audio
 * into tx_pcm, fills tx_response (status, nsamples, nframes, canonical text),
 * sets tx_response.seq_done = seq and signals .tx. One request at a time.
 *
 * Control: the client writes a JTTY_CMD_* into command; the server performs
 * it and writes 0 back. nfa/nfb/f0/ftol may be changed at any time and are
 * read before every decoder call. heartbeat increments on every server loop
 * (~every 100 ms when idle), server_state tells the client where it stands.
 *
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE.
 */
#ifndef JTTY_PROTOCOL_H
#define JTTY_PROTOCOL_H

#include <stdint.h>

#define JTTY_SHM_MAGIC          0x31595454u          /* "TTY1" */
#define JTTY_SHM_VERSION        1u
#define JTTY_RX_RING_SAMPLES    (12000 * 60)         /* 60 s of receive audio */
#define JTTY_RESULT_RING        256
#define JTTY_TX_MAX_SAMPLES     (59 * 16 * 1536 + 1536)   /* 16 frames at 48 kHz, plus a symbol */
#define JTTY_SHM_TEXT           80

enum {
    JTTY_STATE_STARTING = 0,
    JTTY_STATE_RUNNING  = 1,
    JTTY_STATE_QUITTING = 2
};

enum {
    JTTY_CMD_NONE  = 0,
    JTTY_CMD_QUIT  = 1,     /* server exits */
    JTTY_CMD_RESET = 2      /* forget all receive state; next audio starts a fresh capture */
};

enum {                          /* tx_request.profile: how ambiguous exchange text is read */
    JTTY_PROFILE_UNKNOWN   = 0,
    JTTY_PROFILE_FIELD_DAY = 1,
    JTTY_PROFILE_RTTY      = 2  /* RTTY Roundup: "599 05" is serial 005, "599 MA" a state */
};

enum {
    JTTY_TX_OK          = 0,
    JTTY_TX_EMPTY       = 1,
    JTTY_TX_FAILED      = 2,
    JTTY_TX_BAD_PROFILE = 3,
    JTTY_TX_BAD_RATE    = 4,
    JTTY_TX_TOO_LONG    = 5
};

typedef struct {
    int64_t message_id;
    int64_t start_sample;
    float   frequency;              /* audio Hz */
    int32_t eom;                    /* 0/1 */
    char    text[JTTY_SHM_TEXT];    /* space padded, no terminator guaranteed */
    int32_t reserved[4];
} JttyShmResult;                    /* 120 bytes */

typedef struct {
    volatile int32_t seq;           /* client increments to submit */
    int32_t profile;                /* 0 unknown, 1 Field Day, 2 RTTY Roundup */
    int32_t sample_rate;            /* 12000 or 48000 */
    int32_t chained;                /* 1 = queued behind a message still being sent */
    float   f0;                     /* lowest tone, Hz */
    int32_t reserved[3];
    char    text[JTTY_SHM_TEXT];    /* NUL terminated or 80 chars */
} JttyShmTxRequest;                 /* 112 bytes */

typedef struct {
    volatile int32_t seq_done;      /* mirrors the request seq when complete */
    int32_t status;                 /* JTTY_TX_* */
    int32_t nsamples;               /* valid samples in tx_pcm */
    int32_t nframes;                /* 1.888 s frames the message took */
    char    text[JTTY_SHM_TEXT];    /* canonical text as transmitted */
} JttyShmTxResponse;                /* 96 bytes */

typedef struct {
    /* --- header: 64 bytes --- */
    uint32_t magic;
    uint32_t version;
    uint32_t struct_size;
    uint32_t header_size;
    volatile int32_t  server_state;
    volatile int32_t  command;
    volatile uint32_t heartbeat;
    volatile int32_t  decoder_restarts;
    volatile int32_t  nfa;          /* search bounds, Hz (default 200..3000) */
    volatile int32_t  nfb;
    volatile float    f0;           /* preferred channel, Hz (default 1500) */
    volatile float    ftol;         /* its tolerance, Hz (default 500) */
    int32_t  server_pid;
    int32_t  reserved0[3];
    /* --- counters: 48 bytes --- */
    volatile int64_t rx_written;     /* client: total samples written */
    volatile int64_t rx_consumed;    /* server: total samples taken */
    volatile int64_t rx_dropped;     /* server: samples lost when the client got > ring ahead */
    volatile int64_t results_written;/* server: total results published */
    int64_t reserved1[2];
    /* --- transmit --- */
    JttyShmTxRequest  tx_request;
    JttyShmTxResponse tx_response;
    /* --- rings --- */
    JttyShmResult results[JTTY_RESULT_RING];
    int16_t tx_pcm[JTTY_TX_MAX_SAMPLES];
    int16_t rx_pcm[JTTY_RX_RING_SAMPLES];
} JttyShm;

#define JTTY_SHM_HEADER_SIZE 64

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(JttyShmResult) == 120, "result layout");
_Static_assert(sizeof(JttyShmTxRequest) == 112, "tx request layout");
_Static_assert(sizeof(JttyShmTxResponse) == 96, "tx response layout");
_Static_assert(sizeof(JttyShm) == 64 + 48 + 112 + 96 + 120 * JTTY_RESULT_RING
               + 2 * JTTY_TX_MAX_SAMPLES + 2 * JTTY_RX_RING_SAMPLES, "shm layout");
#endif

#endif
