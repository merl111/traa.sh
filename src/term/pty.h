#ifndef TRAASH_PTY_H
#define TRAASH_PTY_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
  int master_fd;
  int child_pid;
  int cols;
  int rows;
} TraashPty;

int traash_pty_open(TraashPty *pty, int cols, int rows, const char *shell,
                    char *const argv[]);
void traash_pty_close(TraashPty *pty);
int traash_pty_resize(TraashPty *pty, int cols, int rows);
ssize_t traash_pty_read(TraashPty *pty, uint8_t *buf, size_t n);
ssize_t traash_pty_write(TraashPty *pty, const uint8_t *buf, size_t n);
/* Non-blocking waitpid; returns 1 if the child has exited. */
int traash_pty_child_exited(TraashPty *pty);
/* 1 if a foreground process other than the shell is running on this PTY. */
int traash_pty_has_foreground_process(const TraashPty *pty);
/* Write a short cmdline for the foreground process (not the shell). Returns 0 on success. */
int traash_pty_foreground_cmdline(const TraashPty *pty, char *out, size_t n);
/* Foreground process id, or -1 if idle/unavailable. */
int traash_pty_foreground_pid(const TraashPty *pty);

#endif
