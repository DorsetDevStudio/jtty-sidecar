/* jtty-sidecar - Windows shared memory + event plumbing for the server side.
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#include "shm_win.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static HANDLE make_event(const char *name, const char *suffix)
{
    char full[512];
    snprintf(full, sizeof full, "%s%s", name, suffix);
    HANDLE h = CreateEventA(NULL, FALSE, FALSE, full);
    if (h && GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(h); return NULL; }
    return h;
}

int jtty_shm_create(JttyShmServer *s, const char *name)
{
    memset(s, 0, sizeof *s);
    HANDLE m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                  0, (DWORD)sizeof(JttyShm), name);
    if (!m) { fprintf(stderr, "CreateFileMapping failed: %lu\n", GetLastError()); return -1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "a server named %s already exists\n", name);
        CloseHandle(m);
        return -1;
    }
    JttyShm *p = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(JttyShm));
    if (!p) { fprintf(stderr, "MapViewOfFile failed: %lu\n", GetLastError()); CloseHandle(m); return -1; }
    memset(p, 0, sizeof *p);
    p->magic = JTTY_SHM_MAGIC;
    p->version = JTTY_SHM_VERSION;
    p->struct_size = (uint32_t)sizeof(JttyShm);
    p->header_size = JTTY_SHM_HEADER_SIZE;
    p->server_state = JTTY_STATE_STARTING;
    p->server_pid = (int32_t)GetCurrentProcessId();
    p->nfa = 200;
    p->nfb = 3000;
    p->f0 = 1500.0f;
    p->ftol = 500.0f;

    s->mapping = m;
    s->shm = p;
    s->rx_event = make_event(name, ".rx");
    s->res_event = make_event(name, ".res");
    s->tx_event = make_event(name, ".tx");
    if (!s->rx_event || !s->res_event || !s->tx_event) {
        fprintf(stderr, "could not create the %s.rx/.res/.tx events (another server?)\n", name);
        jtty_shm_close(s);
        return -1;
    }
    return 0;
}

void jtty_shm_close(JttyShmServer *s)
{
    if (s->shm) { s->shm->server_state = JTTY_STATE_QUITTING; UnmapViewOfFile(s->shm); }
    if (s->mapping) CloseHandle(s->mapping);
    if (s->rx_event) CloseHandle(s->rx_event);
    if (s->res_event) CloseHandle(s->res_event);
    if (s->tx_event) CloseHandle(s->tx_event);
    memset(s, 0, sizeof *s);
}

int jtty_shm_wait_rx(JttyShmServer *s, int ms)
{
    return WaitForSingleObject(s->rx_event, (DWORD)ms) == WAIT_OBJECT_0;
}

void jtty_shm_signal_results(JttyShmServer *s) { SetEvent(s->res_event); }
void jtty_shm_signal_tx(JttyShmServer *s) { SetEvent(s->tx_event); }

int jtty_process_gone(int pid)
{
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return 1;
    int gone = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
    CloseHandle(h);
    return gone;
}
