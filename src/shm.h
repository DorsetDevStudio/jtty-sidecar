/* jtty-sidecar - shared memory + wake-up plumbing for the server side.
 *
 * One interface, two implementations: shm_win.c (a Windows file mapping and
 * three named auto-reset events) and shm_posix.c (a POSIX shared-memory
 * object and three named semaphores, macOS and Linux). The segment layout is
 * identical everywhere - see protocol.h, which also says how the objects are
 * named on each platform.
 *
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#ifndef JTTY_SHM_H
#define JTTY_SHM_H

#include "protocol.h"

typedef struct {
    void    *mapping;      /* Windows: HANDLE of the file mapping. POSIX: unused */
    void    *rx_event;     /* client -> server  (Windows HANDLE / POSIX sem_t*) */
    void    *res_event;    /* server -> client */
    void    *tx_event;     /* server -> client */
    int      fd;           /* POSIX: the shm_open descriptor */
    char     name[64];     /* for unlinking on close (at most 25 used) */
    JttyShm *shm;
} JttyShmServer;

/* Creates the segment and wake-up objects under <name>. Fails if a LIVE server
 * with that name exists; objects left behind by a dead one are replaced.
 * Zero-fills the segment and writes the header. */
int  jtty_shm_create(JttyShmServer *s, const char *name);
void jtty_shm_close(JttyShmServer *s);
/* Waits up to ms for the client's rx wake-up. Returns 1 if signalled. */
int  jtty_shm_wait_rx(JttyShmServer *s, int ms);
void jtty_shm_signal_results(JttyShmServer *s);
void jtty_shm_signal_tx(JttyShmServer *s);

/* Full memory barrier: data written before it is visible before anything
 * written after it (the counters that publish ring contents rely on this). */
void jtty_shm_barrier(void);

/* 1 if the process with that id has exited (or cannot be seen). */
int  jtty_process_gone(int pid);

#endif
