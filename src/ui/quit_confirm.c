#include "ui/quit_confirm.h"

#include "mux/server.h"
#include "mux/window.h"
#include "term/pty.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void traash_quit_confirm_init(TraashQuitConfirm *q) {
  memset(q, 0, sizeof(*q));
}

void traash_quit_confirm_close(TraashQuitConfirm *q) {
  if (!q) {
    return;
  }
  q->open = 0;
  q->focus = 0;
  q->count = 0;
  q->kind = TRAASH_QUIT_KIND_APP;
  q->target_window_id = -1;
  q->target_pane_id = -1;
}

static void add_item(TraashQuitConfirm *q, const char *tab, const char *title,
                     const char *process, int pid) {
  if (!q || q->count >= TRAASH_QUIT_CONFIRM_MAX) {
    return;
  }
  TraashQuitConfirmItem *it = &q->items[q->count++];
  snprintf(it->tab, sizeof(it->tab), "%s", tab ? tab : "?");
  snprintf(it->title, sizeof(it->title), "%s", title && title[0] ? title : "shell");
  traash_quit_confirm_format_process(process, it->process, sizeof(it->process));
  it->pid = pid;
}

void traash_quit_confirm_format_process(const char *raw, char *out, size_t n) {
  if (!out || n < 2) {
    return;
  }
  out[0] = 0;
  if (!raw || !raw[0]) {
    snprintf(out, n, "process");
    return;
  }
  size_t o = 0;
  const char *p = raw;
  int first = 1;
  while (*p && o + 1 < n) {
    while (*p == ' ' || *p == '\t') {
      p++;
    }
    if (!*p) {
      break;
    }
    const char *start = p;
    while (*p && *p != ' ' && *p != '\t') {
      p++;
    }
    const char *tok = start;
    for (const char *s = start; s < p; s++) {
      if (*s == '/') {
        tok = s + 1;
      }
    }
    size_t nlen = (size_t)(p - tok);
    if (nlen == 0) {
      tok = start;
      nlen = (size_t)(p - start);
    }
    if (!first) {
      out[o++] = ' ';
      if (o + 1 >= n) {
        break;
      }
    }
    first = 0;
    if (o + nlen >= n) {
      nlen = n - o - 1;
    }
    memcpy(out + o, tok, nlen);
    o += nlen;
    out[o] = 0;
  }
  if (!out[0]) {
    snprintf(out, n, "%s", raw);
  }
}

int traash_quit_confirm_scan(TraashQuitConfirm *q, const TraashMuxServer *mux) {
  if (!q) {
    return 0;
  }
  q->count = 0;
  if (!mux) {
    return 0;
  }
  for (const TraashSession *s = mux->sessions; s; s = s->next) {
    for (const TraashWindow *w = s->windows; w; w = w->next) {
      int pane_i = 0;
      for (const TraashPane *p = w->panes; p; p = p->next, pane_i++) {
        char cmd[160];
        int pid = traash_pty_foreground_pid(&p->pty);
        if (pid <= 0) {
          continue;
        }
        if (traash_pty_foreground_cmdline(&p->pty, cmd, sizeof(cmd)) != 0) {
          snprintf(cmd, sizeof(cmd), "pid %d", pid);
        }
        const char *title = p->title[0] ? p->title : p->screen.title;
        char tab[32];
        if (mux->sessions && mux->sessions->next) {
          snprintf(tab, sizeof(tab), "%s/%s", s->name, w->name);
        } else {
          snprintf(tab, sizeof(tab), "%s", w->name);
        }
        if (pane_i > 0) {
          char with_pane[32];
          snprintf(with_pane, sizeof(with_pane), "%s·%d", tab, pane_i + 1);
          add_item(q, with_pane, title, cmd, pid);
        } else {
          add_item(q, tab, title, cmd, pid);
        }
      }
    }
  }
  return q->count;
}

static int open_if_needed(TraashQuitConfirm *q, int n, TraashQuitKind kind, int win_id,
                          int pane_id) {
  if (n <= 0) {
    traash_quit_confirm_close(q);
    return 0;
  }
  q->open = 1;
  q->focus = 0; /* default Cancel — safer */
  q->kind = kind;
  q->target_window_id = win_id;
  q->target_pane_id = pane_id;
  return n;
}

static void scan_pane_into(TraashQuitConfirm *q, const TraashPane *p, const char *tab) {
  if (!q || !p) {
    return;
  }
  char cmd[160];
  int pid = traash_pty_foreground_pid(&p->pty);
  if (pid <= 0) {
    return;
  }
  if (traash_pty_foreground_cmdline(&p->pty, cmd, sizeof(cmd)) != 0) {
    snprintf(cmd, sizeof(cmd), "pid %d", pid);
  }
  const char *title = p->title[0] ? p->title : p->screen.title;
  add_item(q, tab ? tab : "?", title, cmd, pid);
}

int traash_quit_confirm_scan_window(TraashQuitConfirm *q, const TraashWindow *w) {
  if (!q) {
    return 0;
  }
  q->count = 0;
  if (!w) {
    return 0;
  }
  int pane_i = 0;
  for (const TraashPane *p = w->panes; p; p = p->next, pane_i++) {
    char tab[32];
    if (pane_i > 0) {
      snprintf(tab, sizeof(tab), "%s·%d", w->name, pane_i + 1);
    } else {
      snprintf(tab, sizeof(tab), "%s", w->name);
    }
    scan_pane_into(q, p, tab);
  }
  return q->count;
}

int traash_quit_confirm_scan_pane(TraashQuitConfirm *q, const TraashPane *p) {
  if (!q) {
    return 0;
  }
  q->count = 0;
  scan_pane_into(q, p, "pane");
  return q->count;
}

int traash_quit_confirm_needed(TraashQuitConfirm *q, const TraashMuxServer *mux) {
  if (!q) {
    return 0;
  }
  return open_if_needed(q, traash_quit_confirm_scan(q, mux), TRAASH_QUIT_KIND_APP, -1, -1);
}

int traash_quit_confirm_needed_window(TraashQuitConfirm *q, const TraashWindow *w) {
  if (!q) {
    return 0;
  }
  return open_if_needed(q, traash_quit_confirm_scan_window(q, w), TRAASH_QUIT_KIND_TAB,
                        w ? w->id : -1, -1);
}

int traash_quit_confirm_needed_pane(TraashQuitConfirm *q, const TraashPane *p) {
  if (!q) {
    return 0;
  }
  return open_if_needed(q, traash_quit_confirm_scan_pane(q, p), TRAASH_QUIT_KIND_PANE, -1,
                        p ? p->id : -1);
}

void traash_quit_confirm_geom(int fb_w, int fb_h, float scale, int cell_h, int row_count,
                              TraashQuitConfirmGeom *out) {
  if (!out) {
    return;
  }
  if (scale < 0.1f) {
    scale = 1.0f;
  }
  if (cell_h < 8) {
    cell_h = 14;
  }
  float pad = 22.0f * scale;
  float title_h = (float)cell_h * 2.2f + 12.0f * scale;
  float row_h = (float)cell_h * 2.0f + 10.0f * scale;
  float footer_h = 52.0f * scale;
  int visible = row_count;
  if (visible < 1) {
    visible = 1;
  }
  if (visible > 8) {
    visible = 8;
  }
  float panel_h = pad + title_h + 8.0f * scale + row_h * (float)visible + footer_h + pad;
  float panel_w = fminf((float)fb_w * 0.62f, 560.0f * scale);
  if (panel_w < 360.0f * scale) {
    panel_w = 360.0f * scale;
  }
  if (panel_h > (float)fb_h * 0.85f) {
    panel_h = (float)fb_h * 0.85f;
    visible = (int)((panel_h - pad - title_h - footer_h - pad) / row_h);
    if (visible < 1) {
      visible = 1;
    }
  }
  out->w = panel_w;
  out->h = panel_h;
  out->x = ((float)fb_w - panel_w) * 0.5f;
  out->y = ((float)fb_h - panel_h) * 0.5f;
  out->pad = pad;
  out->title_h = title_h;
  out->row_h = row_h;
  out->footer_h = footer_h;
  out->content_top = out->y + pad + title_h + 8.0f * scale;
  out->rad = 14.0f * scale;
  out->visible_rows = visible;
  out->btn_h = 34.0f * scale;
  out->btn_w = 130.0f * scale;
  out->btn_y = out->y + out->h - pad - out->btn_h;
  float gap = 12.0f * scale;
  float total = out->btn_w * 2.0f + gap;
  out->btn_cancel_x = out->x + (out->w - total) * 0.5f;
  out->btn_ok_x = out->btn_cancel_x + out->btn_w + gap;
}

int traash_quit_confirm_hit_backdrop(const TraashQuitConfirm *q, float mx, float my, int fb_w,
                                     int fb_h, float scale, int cell_h) {
  if (!q || !q->open) {
    return 0;
  }
  TraashQuitConfirmGeom g;
  traash_quit_confirm_geom(fb_w, fb_h, scale, cell_h, q->count, &g);
  if (mx < g.x || my < g.y || mx >= g.x + g.w || my >= g.y + g.h) {
    return 1;
  }
  return 0;
}

int traash_quit_confirm_hit_button(const TraashQuitConfirm *q, float mx, float my, int fb_w,
                                   int fb_h, float scale, int cell_h) {
  if (!q || !q->open) {
    return -1;
  }
  TraashQuitConfirmGeom g;
  traash_quit_confirm_geom(fb_w, fb_h, scale, cell_h, q->count, &g);
  if (my < g.btn_y || my >= g.btn_y + g.btn_h) {
    return -1;
  }
  if (mx >= g.btn_cancel_x && mx < g.btn_cancel_x + g.btn_w) {
    return 0;
  }
  if (mx >= g.btn_ok_x && mx < g.btn_ok_x + g.btn_w) {
    return 1;
  }
  return -1;
}
