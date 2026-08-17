#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int popen_write(const char *cmd, const char *text) {
  if (!text) {
    return -1;
  }
  FILE *f = popen(cmd, "w");
  if (!f) {
    return -1;
  }
  fputs(text, f);
  return pclose(f) == 0 ? 0 : -1;
}

static int popen_read(const char *cmd, char *buf, size_t n) {
  if (!buf || n == 0) {
    return -1;
  }
  FILE *f = popen(cmd, "r");
  if (!f) {
    buf[0] = 0;
    return -1;
  }
  size_t got = fread(buf, 1, n - 1, f);
  buf[got] = 0;
  pclose(f);
  return (int)got;
}

void traash_clipboard_set(const char *text) {
  /* Best-effort via wl-copy/xclip/pbcopy */
  popen_write("wl-copy 2>/dev/null || xclip -selection clipboard 2>/dev/null || "
              "pbcopy 2>/dev/null",
              text);
}

int traash_clipboard_get(char *buf, size_t n) {
  return popen_read("wl-paste 2>/dev/null || xclip -selection clipboard -o 2>/dev/null || "
                    "pbpaste 2>/dev/null",
                    buf, n);
}

void traash_primary_set(const char *text) {
  /* PRIMARY: select-to-copy buffer used by middle-click paste on Linux. */
  popen_write("wl-copy --primary 2>/dev/null || xclip -selection primary 2>/dev/null || "
              "xsel --primary --input 2>/dev/null",
              text);
}

int traash_primary_get(char *buf, size_t n) {
  return popen_read("wl-paste --primary 2>/dev/null || xclip -selection primary -o "
                    "2>/dev/null || xsel --primary --output 2>/dev/null",
                    buf, n);
}

void traash_open_url(const char *url) {
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "xdg-open '%s' 2>/dev/null || open '%s' 2>/dev/null &", url, url);
  system(cmd);
}
