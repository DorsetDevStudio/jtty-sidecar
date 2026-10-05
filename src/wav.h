/* jtty-sidecar - minimal RIFF/WAVE read and write (PCM16).
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#ifndef JTTY_WAV_H
#define JTTY_WAV_H

#include <stdint.h>

/* Reads a PCM16 WAV, keeps channel 0, and returns 12 kHz mono samples
 * (malloc'd, caller frees). A 48 kHz file is decimated by 4 with a box
 * filter, good enough for test material; other rates are rejected.
 * Returns the sample count, or -1 with *err set to a static message. */
long wav_read_12k(const char *path, int16_t **out, const char **err);

/* Writes PCM16 mono. Returns 0 on success. */
int wav_write_mono(const char *path, const int16_t *pcm, long n, int rate);

#endif
