#include "util/password.h"

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int read_tty_line(FILE *tty, char *out, size_t out_len) {
  if (!tty || !out || out_len < 2) {
    return -1;
  }
  struct termios oldt;
  struct termios newt;
  if (tcgetattr(fileno(tty), &oldt) != 0) {
    return -1;
  }
  newt = oldt;
  newt.c_lflag &= (tcflag_t)~(ECHO);
  if (tcsetattr(fileno(tty), TCSANOW, &newt) != 0) {
    return -1;
  }
  if (!fgets(out, (int)out_len, tty)) {
    tcsetattr(fileno(tty), TCSANOW, &oldt);
    return -1;
  }
  tcsetattr(fileno(tty), TCSANOW, &oldt);
  size_t n = strlen(out);
  while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
    out[--n] = 0;
  }
  fputc('\n', tty);
  return out[0] ? 0 : -1;
}

int traash_password_prompt(const char *label, char *out, size_t out_len) {
  FILE *tty = fopen("/dev/tty", "r+");
  if (!tty) {
    tty = stdin;
  }
  if (label && label[0]) {
    fprintf(tty, "%s: ", label);
    fflush(tty);
  }
  int r = read_tty_line(tty, out, out_len);
  if (tty != stdin) {
    fclose(tty);
  }
  return r;
}

int traash_password_read_fd(int fd, char *out, size_t out_len) {
  if (fd < 0 || !out || out_len < 2) {
    return -1;
  }
  size_t n = 0;
  while (n + 1 < out_len) {
    char c = 0;
    ssize_t r = read(fd, &c, 1);
    if (r <= 0) {
      return -1;
    }
    if (c == '\n' || c == '\r') {
      break;
    }
    out[n++] = c;
  }
  out[n] = 0;
  return n > 0 ? 0 : -1;
}

int traash_password_prompt_pair(const char *label1, const char *label2, char *out,
                                size_t out_len) {
  char confirm[256];
  if (traash_password_prompt(label1, out, out_len) != 0) {
    return -1;
  }
  if (traash_password_prompt(label2, confirm, sizeof(confirm)) != 0) {
    return -1;
  }
  if (strcmp(out, confirm) != 0) {
    fprintf(stderr, "traash: passwords do not match\n");
    return -1;
  }
  return 0;
}
