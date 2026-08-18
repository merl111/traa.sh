#ifndef TRAASH_SNAPSHOT_H
#define TRAASH_SNAPSHOT_H

#include "mux/session.h"

#include <stddef.h>
#include <stdint.h>

/* Serialize session layout + screen state (no PTY fds). Returns malloc'd buffer. */
int traash_snapshot_encode(const TraashSession *s, uint8_t **out, size_t *out_len);

/* Build a new session from snapshot; spawns fresh shells on each pane. */
TraashSession *traash_snapshot_decode(const uint8_t *buf, size_t len, int cols, int rows);

/* Load encrypted .tsn from disk; decrypt with password. */
int traash_snapshot_load_encrypted(const char *name, const char *password,
                                   int force_read_only, TraashSession **out, int *role_out);

/* Save session encrypted to ~/.local/share/traash/sessions/<name>.tsn */
int traash_snapshot_save_encrypted(const char *name, const TraashSession *s,
                                   const char *write_pw, const char *read_pw);

/* Path to encrypted session file. Returns 0 on success. */
int traash_session_file_path(const char *name, char *buf, size_t n);

int traash_session_file_exists(const char *name);

#endif
