#include "term/screen_export.h"

#include "util/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const TraashCell *scrollback_cell(const TraashScreen *s, int logical_row, int col) {
  if (logical_row < 0 || logical_row >= s->scrollback_len || col < 0) {
    return NULL;
  }
  int stride = s->scrollback_cols > 0 ? s->scrollback_cols : s->cols;
  int phys = s->scrollback_start + logical_row;
  if (s->scrollback_cap > 0) {
    phys %= s->scrollback_cap;
  }
  return &s->scrollback[phys * stride + col];
}

static int append_cp(char *buf, size_t buf_len, size_t *o, uint32_t cp) {
  if (*o >= buf_len) {
    return -1;
  }
  if (cp == 0 || cp == ' ') {
    if (*o + 1 >= buf_len) {
      return -1;
    }
    buf[(*o)++] = ' ';
    return 0;
  }
  uint8_t enc[4];
  int n = traash_utf8_encode(cp, enc);
  if (n <= 0) {
    return 0;
  }
  if (*o + (size_t)n >= buf_len) {
    return -1;
  }
  memcpy(buf + *o, enc, (size_t)n);
  *o += (size_t)n;
  return 0;
}

static int export_row(const TraashCell *row, int cols, char *buf, size_t buf_len, size_t *o) {
  int end = cols - 1;
  while (end > 0 && (row[end].cp == 0 || row[end].cp == ' ')) {
    end--;
  }
  if (end == 0 && (row[0].cp == 0 || row[0].cp == ' ')) {
    if (*o + 1 >= buf_len) {
      return -1;
    }
    buf[(*o)++] = '\n';
    return 0;
  }
  for (int x = 0; x <= end; x++) {
    if (append_cp(buf, buf_len, o, row[x].cp) != 0) {
      return -1;
    }
  }
  if (*o + 1 >= buf_len) {
    return -1;
  }
  buf[(*o)++] = '\n';
  return 0;
}

int traash_screen_export_text(const TraashScreen *s, const TraashExportOpts *opts, char *buf,
                              size_t buf_len) {
  if (!s || !buf || buf_len < 2) {
    return -1;
  }
  TraashExportOpts def = {.scrollback_lines = 200, .max_bytes = 256 * 1024, .pane_id = 0};
  if (opts) {
    def = *opts;
  }
  if (def.scrollback_lines < 0) {
    def.scrollback_lines = 0;
  }
  if (def.max_bytes <= 0) {
    def.max_bytes = 256 * 1024;
  }
  size_t o = 0;
  buf[0] = 0;
  int sb_start = s->scrollback_len - def.scrollback_lines;
  if (sb_start < 0) {
    sb_start = 0;
  }
  for (int row = sb_start; row < s->scrollback_len; row++) {
    const TraashCell *line = scrollback_cell(s, row, 0);
    if (!line) {
      continue;
    }
    if (export_row(line, s->scrollback_cols > 0 ? s->scrollback_cols : s->cols, buf, buf_len,
                   &o) != 0) {
      break;
    }
    if (o >= (size_t)def.max_bytes) {
      break;
    }
  }
  for (int y = 0; y < s->rows; y++) {
    if (export_row(&s->cells[y * s->cols], s->cols, buf, buf_len, &o) != 0) {
      break;
    }
    if (o >= (size_t)def.max_bytes) {
      break;
    }
  }
  if (o < buf_len) {
    buf[o] = 0;
  } else if (buf_len) {
    buf[buf_len - 1] = 0;
  }
  return (int)o;
}

static int export_pane(const TraashPane *p, const TraashExportOpts *opts, TraashPaneExport *out) {
  memset(out, 0, sizeof(*out));
  out->pane_id = p->id;
  snprintf(out->title, sizeof(out->title), "%s", p->title[0] ? p->title : p->screen.title);
  snprintf(out->cwd, sizeof(out->cwd), "%s", p->screen.cwd);
  out->cols = p->screen.cols;
  out->rows = p->screen.rows;
  out->cursor_x = p->screen.cursor_x;
  out->cursor_y = p->screen.cursor_y;
  out->busy = p->agent_busy;
  size_t cap = 256 * 1024;
  out->text = malloc(cap);
  if (!out->text) {
    return -1;
  }
  int n = traash_screen_export_text(&p->screen, opts, out->text, cap);
  if (n < 0) {
    free(out->text);
    out->text = NULL;
    return -1;
  }
  out->text_len = (size_t)n;
  return 0;
}

int traash_session_export_window(const TraashSession *s, const TraashExportOpts *opts,
                                 TraashPaneExport **out, int *count) {
  if (!s || !out || !count) {
    return -1;
  }
  *out = NULL;
  *count = 0;
  TraashWindow *w = s->active ? s->active : s->windows;
  if (!w) {
    return 0;
  }
  int n = 0;
  for (TraashPane *p = w->panes; p; p = p->next) {
    if (opts && opts->pane_id > 0 && p->id != opts->pane_id) {
      continue;
    }
    n++;
  }
  if (n == 0) {
    return 0;
  }
  TraashPaneExport *arr = calloc((size_t)n, sizeof(*arr));
  if (!arr) {
    return -1;
  }
  int i = 0;
  for (TraashPane *p = w->panes; p; p = p->next) {
    if (opts && opts->pane_id > 0 && p->id != opts->pane_id) {
      continue;
    }
    if (export_pane(p, opts, &arr[i]) != 0) {
      traash_pane_export_free(arr, i);
      return -1;
    }
    i++;
  }
  *out = arr;
  *count = i;
  return 0;
}

void traash_pane_export_free(TraashPaneExport *panes, int count) {
  if (!panes) {
    return;
  }
  for (int i = 0; i < count; i++) {
    free(panes[i].text);
  }
  free(panes);
}
