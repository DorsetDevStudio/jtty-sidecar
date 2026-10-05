/* jtty-sidecar - POSIX shared memory + named semaphores (macOS, Linux).
 *
 * The segment is a shm_open() object named "/<name>"; the three wake-ups are
 * named semaphores "/<name>.rx", "/<name>.res" and "/<name>.tx". A semaphore
 * stands in for a Windows auto-reset event: the signaller first drains it
 * (sem_trywait) and then posts once, so the count never exceeds one and a
 * client that is not waiting cannot make it grow without bound.
 *
 * macOS limits these names to 31 characters including the leading slash and
 * has no sem_timedwait, so a timed wait there is a trywait/sleep loop at
 * 5 ms. Both facts are noted in protocol.h for client authors.
 *
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#include "shm.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void object_name(char *out, size_t cap, const char *name, const char *suffix)
{
    snprintf(out, cap, "/%s%s", name, suffix);
}

static sem_t *make_sem(const char *name, const char *suffix)
{
    char full[256];
    object_name(full, sizeof full, name, suffix);
    sem_unlink(full);                 /* a stale one from a dead server, if any */
    sem_t *s = sem_open(full, O_CREAT | O_EXCL, 0600, 0);
    if (s == SEM_FAILED) { fprintf(stderr, "sem_open(%s) failed: %s\n", full, strerror(errno)); return NULL; }
    return s;
}

/* Is there a LIVE server behind an existing segment of this name? */
static int live_server_exists(const char *full)
{
    int fd = shm_open(full, O_RDONLY, 0);
    if (fd < 0) return 0;
    struct stat st;
    int live = 0;
    if (fstat(fd, &st) == 0 && (size_t)st.st_size >= JTTY_SHM_HEADER_SIZE) {
        const JttyShm *p = mmap(NULL, JTTY_SHM_HEADER_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED) {
            if (p->magic == JTTY_SHM_MAGIC && p->server_state != JTTY_STATE_QUITTING
                && p->server_pid > 0 && !jtty_process_gone(p->server_pid))
                live = 1;
            munmap((void *)p, JTTY_SHM_HEADER_SIZE);
        }
    }
    close(fd);
    return live;
}

int jtty_shm_create(JttyShmServer *s, const char *name)
{
    memset(s, 0, sizeof *s);
    s->fd = -1;
    if (strlen(name) + 5 > 30) {      /* "/" + name + ".res" must fit macOS's 31 */
        fprintf(stderr, "segment name \"%s\" is too long (at most 25 characters)\n", name);
        return -1;
    }
    char full[256];
    object_name(full, sizeof full, name, "");
    if (live_server_exists(full)) {
        fprintf(stderr, "a server named %s already exists\n", name);
        return -1;
    }
    shm_unlink(full);                 /* whatever a dead server left behind */
    int fd = shm_open(full, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { fprintf(stderr, "shm_open(%s) failed: %s\n", full, strerror(errno)); return -1; }
    if (ftruncate(fd, (off_t)sizeof(JttyShm)) != 0) {
        fprintf(stderr, "ftruncate failed: %s\n", strerror(errno));
        close(fd); shm_unlink(full);
        return -1;
    }
    JttyShm *p = mmap(NULL, sizeof(JttyShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        close(fd); shm_unlink(full);
        return -1;
    }
    memset(p, 0, sizeof *p);
    p->magic = JTTY_SHM_MAGIC;
    p->version = JTTY_SHM_VERSION;
    p->struct_size = (uint32_t)sizeof(JttyShm);
    p->header_size = JTTY_SHM_HEADER_SIZE;
    p->server_state = JTTY_STATE_STARTING;
    p->server_pid = (int32_t)getpid();
    p->nfa = 200;
    p->nfb = 3000;
    p->f0 = 1500.0f;
    p->ftol = 500.0f;

    s->fd = fd;
    s->shm = p;
    snprintf(s->name, sizeof s->name, "%s", name);
    s->rx_event = make_sem(name, ".rx");
    s->res_event = make_sem(name, ".res");
    s->tx_event = make_sem(name, ".tx");
    if (!s->rx_event || !s->res_event || !s->tx_event) {
        fprintf(stderr, "could not create the %s.rx/.res/.tx semaphores\n", name);
        jtty_shm_close(s);
        return -1;
    }
    return 0;
}

static void close_sem(const char *name, const char *suffix, void *sem)
{
    if (sem) sem_close((sem_t *)sem);
    char full[256];
    object_name(full, sizeof full, name, suffix);
    sem_unlink(full);
}

void jtty_shm_close(JttyShmServer *s)
{
    if (s->shm) { s->shm->server_state = JTTY_STATE_QUITTING; munmap(s->shm, sizeof(JttyShm)); }
    if (s->fd >= 0) close(s->fd);
    if (s->name[0]) {
        char full[256];
        object_name(full, sizeof full, s->name, "");
        shm_unlink(full);
        close_sem(s->name, ".rx", s->rx_event);
        close_sem(s->name, ".res", s->res_event);
        close_sem(s->name, ".tx", s->tx_event);
    }
    memset(s, 0, sizeof *s);
    s->fd = -1;
}

int jtty_shm_wait_rx(JttyShmServer *s, int ms)
{
    sem_t *sem = (sem_t *)s->rx_event;
#if defined(__APPLE__)
    /* no sem_timedwait on macOS: poll at 5 ms */
    for (int waited = 0;; waited += 5) {
        if (sem_trywait(sem) == 0) return 1;
        if (waited >= ms) return 0;
        struct timespec ts = { 0, 5 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
    for (;;) {
        if (sem_timedwait(sem, &ts) == 0) return 1;
        if (errno == EINTR) continue;
        return 0;
    }
#endif
}

/* Auto-reset semantics: at most one pending wake-up. */
static void signal_once(void *sem)
{
    sem_t *s = (sem_t *)sem;
    while (sem_trywait(s) == 0) { }
    sem_post(s);
}

void jtty_shm_signal_results(JttyShmServer *s) { signal_once(s->res_event); }
void jtty_shm_signal_tx(JttyShmServer *s) { signal_once(s->tx_event); }

void jtty_shm_barrier(void) { __sync_synchronize(); }

int jtty_process_gone(int pid)
{
    if (pid <= 0) return 1;
    if (kill((pid_t)pid, 0) == 0) return 0;
    return errno == ESRCH;            /* EPERM = exists but not ours: still alive */
}
