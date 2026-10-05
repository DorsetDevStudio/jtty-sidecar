/* jtty-sidecar - C prototypes for the upstream Fortran entry points.
 *
 * These are the same five routines WSJT-X's GUI calls (widgets/mainwindow_jtty.cpp
 * in the upstream tree). gfortran name mangling: lower case + one trailing
 * underscore; every CHARACTER dummy adds a hidden size_t length at the end.
 *
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE.
 */
#ifndef JTTY_API_H
#define JTTY_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Receive sample rate and symbol geometry (lib/jtty/jtty_design.md). */
#define JTTY_RX_RATE        12000
#define JTTY_NSPS           384            /* samples per symbol at 12 kHz = 31.25 baud */
#define JTTY_FRAME_SYMBOLS  59             /* 13 sync + 46 coded */
#define JTTY_FRAME_SAMPLES  (JTTY_FRAME_SYMBOLS * JTTY_NSPS)   /* 22656 = 1.888 s */
#define JTTY_MAX_FRAMES     16             /* MAX_FRAMES in jtty_mod.f90 */
#define JTTY_MAX_TONES      (JTTY_FRAME_SYMBOLS * JTTY_MAX_FRAMES)   /* 944 */
#define JTTY_MSG_LEN        80             /* every message buffer is CHARACTER*80 */
#define JTTY_UPDATE_BATCH   30             /* BATCH_SIZE in jtty_get_updates */
#define JTTY_GFSK_BT        2.0f           /* bt used by the upstream GUI for TX */

/* Exchange profiles (jtty_mod.f90). */
#define JTTY_EXCHANGE_UNKNOWN    0
#define JTTY_EXCHANGE_FIELD_DAY  1
#define JTTY_EXCHANGE_RTTY       2

/* Streaming decoder. d2 is the whole capture so far (12 kHz int16), k the
 * number of valid samples. Internal state persists between calls; a call with
 * k <= the previous k restarts it. nfa/nfb bound the search in Hz, f0/ftol
 * (Hz) are the preferred channel and its tolerance. */
void rjtty_sub_(const int16_t *d2, const int *k, const int *nsps, const int *nfa,
                const int *nfb, const float *f0, const float *ftol);

/* Drain pending message updates. text_blocks is BATCH*80 chars, blank padded.
 * eom is LOGICAL*1 (one byte each). Call until count < JTTY_UPDATE_BATCH. */
void jtty_get_updates_(char *text_blocks, int64_t *message_ids, float *frequencies,
                       float *start_tsync, int8_t *eom, int *count, size_t text_blocks_len);

/* Encode a user message (CHARACTER*80, blank padded, modified in place to the
 * canonical text) into channel symbols. nsym <= 0 means it could not be encoded. */
void genjtty_profile_(char *msg80, const int *exchange_profile, int *itone, int *nsym,
                      size_t msg_len);

/* Render channel symbols to audio. nsps = fsample / 31.25. For a real output
 * pass icmplx = 0 and the same buffer for cwave and wave; nwave = nsps * nsym. */
void gen_jttywave_(const int *itone, const int *nsym, const int *nsps, const float *bt,
                   const float *fsample, const float *f0, float *cwave, float *wave,
                   const int *icmplx, const int *nwave);

/* Free the decoder's cached FFT plans (bind(C) in jtty_mdecode.f90). */
void jtty_release_fft_resources(void);

#ifdef __cplusplus
}
#endif
#endif
