#include "term/pty.h"

#include "util/log.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#include <libproc.h>
#else
#include <pty.h>
#endif

int traash_pty_open(TraashPty *pty, int cols, int rows, const char *shell,
                    char *const argv[]) {
  memset(pty, 0, sizeof(*pty));
  pty->cols = cols;
  pty->rows = rows;
  struct winsize ws = {.ws_col = (unsigned short)cols,
                       .ws_row = (unsigned short)rows};
  pid_t pid = forkpty(&pty->master_fd, NULL, NULL, &ws);
  if (pid < 0) {
    TRAASH_LOGE("forkpty failed: %s", strerror(errno));
    return -1;
  }
  if (pid == 0) {
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    const char *sh = shell ? shell : getenv("SHELL");
    if (!sh || !sh[0]) {
      sh = "/bin/sh";
    }
    if (argv) {
      execvp(argv[0], argv);
    } else {
      execl(sh, sh, (char *)NULL);
    }
    _exit(127);
  }
  pty->child_pid = (int)pid;
  int flags = fcntl(pty->master_fd, F_GETFL, 0);
  fcntl(pty->master_fd, F_SETFL, flags | O_NONBLOCK);
  return 0;
}

void traash_pty_close(TraashPty *pty) {
  if (pty->master_fd >= 0) {
    close(pty->master_fd);
    pty->master_fd = -1;
  }
  if (pty->child_pid > 0) {
    kill(pty->child_pid, SIGHUP);
    waitpid(pty->child_pid, NULL, WNOHANG);
    pty->child_pid = 0;
  }
}

int traash_pty_resize(TraashPty *pty, int cols, int rows) {
  pty->cols = cols;
  pty->rows = rows;
  struct winsize ws = {.ws_col = (unsigned short)cols,
                       .ws_row = (unsigned short)rows};
  return ioctl(pty->master_fd, TIOCSWINSZ, &ws);
}

ssize_t traash_pty_read(TraashPty *pty, uint8_t *buf, size_t n) {
  return read(pty->master_fd, buf, n);
}

ssize_t traash_pty_write(TraashPty *pty, const uint8_t *buf, size_t n) {
  if (!pty || pty->master_fd < 0 || !buf || n == 0) {
    return -1;
  }
  size_t off = 0;
  while (off < n) {
    ssize_t w = write(pty->master_fd, buf + off, n - off);
    if (w > 0) {
      off += (size_t)w;
      continue;
    }
    if (w < 0 && errno == EINTR) {
      continue;
    }
    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd pfd = {.fd = pty->master_fd, .events = POLLOUT, .revents = 0};
      if (poll(&pfd, 1, 25) <= 0) {
        return off > 0 ? (ssize_t)off : -1;
      }
      continue;
    }
    return off > 0 ? (ssize_t)off : -1;
  }
  return (ssize_t)off;
}

int traash_pty_child_exited(TraashPty *pty) {
  if (!pty || pty->child_pid <= 0) {
    return pty && pty->master_fd < 0;
  }
  int status = 0;
  pid_t r = waitpid((pid_t)pty->child_pid, &status, WNOHANG);
  if (r == (pid_t)pty->child_pid) {
    pty->child_pid = 0;
    return 1;
  }
  return 0;
}

int traash_pty_foreground_pid(const TraashPty *pty) {
  if (!pty || pty->master_fd < 0 || pty->child_pid <= 0) {
    return -1;
  }
  pid_t fg = 0;
  if (ioctl(pty->master_fd, TIOCGPGRP, &fg) != 0 || fg <= 1) {
    return -1;
  }
  /* Idle shell: foreground process group is the shell itself. */
  if (fg == (pid_t)pty->child_pid) {
    return -1;
  }
  /* Ensure something in that group is still alive. */
  if (kill(-fg, 0) != 0 && kill(fg, 0) != 0) {
    return -1;
  }
  return (int)fg;
}

int traash_pty_has_foreground_process(const TraashPty *pty) {
  return traash_pty_foreground_pid(pty) > 0;
}

static int read_proc_cmdline(pid_t pid, char *out, size_t n) {
  if (!out || n < 2 || pid <= 0) {
    return -1;
  }
#if defined(__APPLE__)
  char path[PROC_PIDPATHINFO_MAXSIZE];
  if (proc_pidpath(pid, path, sizeof(path)) > 0) {
    snprintf(out, n, "%s", path);
    return 0;
  }
  if (proc_name((int)pid, out, (uint32_t)n) > 0 && out[0]) {
    return 0;
  }
  return -1;
#else
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);
  FILE *f = fopen(path, "rb");
  if (!f) {
    snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
    f = fopen(path, "r");
    if (!f) {
      return -1;
    }
    if (!fgets(out, (int)n, f)) {
      fclose(f);
      return -1;
    }
    fclose(f);
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
      out[--len] = 0;
    }
    return len > 0 ? 0 : -1;
  }
  size_t got = fread(out, 1, n - 1, f);
  fclose(f);
  if (got == 0) {
    out[0] = 0;
    return -1;
  }
  out[got] = 0;
  for (size_t i = 0; i < got; i++) {
    if (out[i] == '\0') {
      out[i] = ' ';
    }
  }
  while (got > 0 && out[got - 1] == ' ') {
    out[--got] = 0;
  }
  return got > 0 ? 0 : -1;
#endif
}

int traash_pty_foreground_cmdline(const TraashPty *pty, char *out, size_t n) {
  int pid = traash_pty_foreground_pid(pty);
  if (pid <= 0) {
    return -1;
  }
  if (read_proc_cmdline((pid_t)pid, out, n) == 0) {
    return 0;
  }
  /* Process group id may not have its own /proc entry — try group leader via
   * scanning is overkill; fall back to a generic label. */
  snprintf(out, n, "pid %d", pid);
  return 0;
}
