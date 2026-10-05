/* jtty-sidecar - shared-memory server loop.
 * Copyright (C) 2026 Station Master Group. GPLv3, see LICENSE. */
#ifndef JTTY_SERVER_H
#define JTTY_SERVER_H

/* Runs until JTTY_CMD_QUIT, or until parent_pid (if > 0) exits.
 * verbose prints every decode and request to stderr. Returns the exit code. */
int jtty_serve(const char *name, int parent_pid, int verbose);

#endif
