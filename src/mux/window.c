#include "mux/window.h"

#include "mux/layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TraashWindow *traash_window_create(int id, const char *name, int cols, int rows) {
  TraashWindow *w = calloc(1, sizeof(*w));
  if (!w) {
    return NULL;
  }
  w->id = id;
  snprintf(w->name, sizeof(w->name), "%s", name ? name : "window");
  w->panes = traash_pane_create(1, cols, rows);
  w->active = w->panes;
  w->zoomed_pane_id = -1;
  return w;
}

void traash_window_destroy(TraashWindow *w) {
  if (!w) {
    return;
  }
  TraashPane *p = w->panes;
  while (p) {
    TraashPane *n = p->next;
    traash_pane_destroy(p);
    p = n;
  }
  free(w);
}

TraashPane *traash_window_active_pane(TraashWindow *w) {
  return w ? w->active : NULL;
}

int traash_window_pane_count(const TraashWindow *w) {
  int n = 0;
  for (TraashPane *p = w->panes; p; p = p->next) {
    n++;
  }
  return n;
}

TraashPane *traash_window_split(TraashWindow *w, int vertical, int *id_gen, int cols,
                               int rows) {
  if (!w || !w->active) {
    return NULL;
  }
  TraashPane *a = w->active;
  TraashPane *b = traash_pane_create((*id_gen)++, cols, rows);
  if (!b) {
    return NULL;
  }
  if (traash_pane_spawn_shell(b) != 0) {
    traash_pane_destroy(b);
    return NULL;
  }
  /* Insert after active */
  b->next = a->next;
  a->next = b;
  traash_layout_split_pair(a, b, vertical);
  w->active = b;
  return b;
}

void traash_window_focus_next(TraashWindow *w) {
  if (!w || !w->active) {
    return;
  }
  w->active = w->active->next ? w->active->next : w->panes;
}

void traash_window_focus_pane(TraashWindow *w, TraashPane *p) {
  if (!w || !p) {
    return;
  }
  for (TraashPane *cur = w->panes; cur; cur = cur->next) {
    if (cur == p) {
      w->active = p;
      return;
    }
  }
}

void traash_window_focus_dir(TraashWindow *w, int dx, int dy) {
  if (!w || !w->active || (!dx && !dy)) {
    return;
  }
  TraashPane *cur = w->active;
  float cx = cur->x + cur->w * 0.5f;
  float cy = cur->y + cur->h * 0.5f;
  TraashPane *best = NULL;
  float best_score = 1e9f;

  for (TraashPane *p = w->panes; p; p = p->next) {
    if (p == cur) {
      continue;
    }
    float px = p->x + p->w * 0.5f;
    float py = p->y + p->h * 0.5f;
    float vx = px - cx;
    float vy = py - cy;

    /* Must lie in the requested half-plane */
    if (dx < 0 && vx >= -0.001f) {
      continue;
    }
    if (dx > 0 && vx <= 0.001f) {
      continue;
    }
    if (dy < 0 && vy >= -0.001f) {
      continue;
    }
    if (dy > 0 && vy <= 0.001f) {
      continue;
    }

    float primary = dx ? (vx < 0 ? -vx : vx) : (vy < 0 ? -vy : vy);
    float secondary = dx ? (vy < 0 ? -vy : vy) : (vx < 0 ? -vx : vx);
    /* Prefer panes most aligned with the move axis */
    float score = primary + secondary * 3.0f;
    if (score < best_score) {
      best_score = score;
      best = p;
    }
  }

  if (best) {
    w->active = best;
  }
}

void traash_window_zoom_toggle(TraashWindow *w) {
  if (!w || !w->active) {
    return;
  }
  if (w->zoomed_pane_id == w->active->id) {
    w->zoomed_pane_id = -1;
    for (TraashPane *p = w->panes; p; p = p->next) {
      p->zoomed = 0;
    }
  } else {
    w->zoomed_pane_id = w->active->id;
    for (TraashPane *p = w->panes; p; p = p->next) {
      p->zoomed = (p->id == w->active->id);
    }
  }
}

int traash_window_close_pane(TraashWindow *w, TraashPane *pane) {
  if (!w || !pane) {
    return 0;
  }
  TraashPane *prev = NULL;
  TraashPane *cur = w->panes;
  while (cur && cur != pane) {
    prev = cur;
    cur = cur->next;
  }
  if (!cur) {
    return 0;
  }
  if (prev) {
    prev->next = pane->next;
  } else {
    w->panes = pane->next;
  }
  if (w->active == pane) {
    w->active = pane->next ? pane->next : (prev ? prev : w->panes);
  }
  if (w->zoomed_pane_id == pane->id) {
    w->zoomed_pane_id = -1;
  }
  traash_pane_destroy(pane);
  if (!w->panes) {
    w->active = NULL;
    return 1;
  }
  traash_layout_reflow_equal(w->panes);
  return 0;
}

int traash_window_poll_n(TraashWindow *w, int max_chunks) {
  int closed = 0;
  TraashPane *p = w->panes;
  while (p) {
    TraashPane *next = p->next;
    if (traash_pane_poll_n(p, max_chunks)) {
      if (traash_window_close_pane(w, p)) {
        return closed + 1;
      }
      closed++;
    }
    p = next;
  }
  return closed;
}

int traash_window_poll(TraashWindow *w) {
  return traash_window_poll_n(w, 16);
}
