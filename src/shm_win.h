/* jtty-sidecar - Windows shared memory + event plumbing for the server side.
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#ifndef JTTY_SHM_WIN_H
#define JTTY_SHM_WIN_H

#include "protocol.h"

typedef struct {
    void    *mapping;      /* HANDLE */
    void    *rx_event;     /* HANDLE: client -> server */
    void    *res_event;    /* HANDLE: server -> client */
    void    *tx_event;     /* HANDLE: server -> client */
    JttyShm *shm;
} JttyShmServer;

/* Creates the mapping and events under <name>; fails if a server with that
 * name already exists. Zero-fills and writes the header. */
int  jtty_shm_create(JttyShmServer *s, const char *name);
void jtty_shm_close(JttyShmServer *s);
/* Waits up to ms for the client's rx event. Returns 1 if signalled. */
int  jtty_shm_wait_rx(JttyShmServer *s, int ms);
void jtty_shm_signal_results(JttyShmServer *s);
void jtty_shm_signal_tx(JttyShmServer *s);

/* 1 if the process with that id has exited (or cannot be opened). */
int  jtty_process_gone(int pid);

#endif
