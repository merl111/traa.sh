#include "mux/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Smallest positive tab number not used by an existing window name/id. */
static int next_free_window_number(const TraashSession *s) {
  for (int n = 1; n < 10000; n++) {
    char label[16];
    snprintf(label, sizeof(label), "%d", n);
    int used = 0;
    for (const TraashWindow *w = s->windows; w; w = w->next) {
      if (w->id == n || strcmp(w->name, label) == 0) {
        used = 1;
        break;
      }
    }
    if (!used) {
      return n;
    }
  }
  return 1;
}

TraashSession *traash_session_create(int id, const char *name, int cols, int rows) {
  TraashSession *s = calloc(1, sizeof(*s));
  if (!s) {
    return NULL;
  }
  s->id = id;
  snprintf(s->name, sizeof(s->name), "%s", name ? name : "default");
  s->next_window_id = 2;
  s->next_pane_id = 2;
  s->windows = traash_window_create(1, "1", cols, rows);
  s->active = s->windows;
  if (s->windows && s->windows->panes) {
    traash_pane_spawn_shell(s->windows->panes);
  }
  return s;
}

void traash_session_destroy(TraashSession *s) {
  if (!s) {
    return;
  }
  TraashWindow *w = s->windows;
  while (w) {
    TraashWindow *n = w->next;
    traash_window_destroy(w);
    w = n;
  }
  free(s);
}

TraashWindow *traash_session_new_window(TraashSession *s, const char *name, int cols,
                                        int rows) {
  char auto_name[32];
  int num = next_free_window_number(s);
  if (!name || !name[0]) {
    snprintf(auto_name, sizeof(auto_name), "%d", num);
    name = auto_name;
  }
  TraashWindow *w = traash_window_create(num, name, cols, rows);
  if (!w) {
    return NULL;
  }
  if (num >= s->next_window_id) {
    s->next_window_id = num + 1;
  }
  if (w->panes) {
    w->panes->id = s->next_pane_id++;
    traash_pane_spawn_shell(w->panes);
  }
  /* Keep tab bar order by window number (reuse fills gaps in place). */
  w->next = NULL;
  if (!s->windows || w->id < s->windows->id) {
    w->next = s->windows;
    s->windows = w;
  } else {
    TraashWindow *prev = s->windows;
    while (prev->next && prev->next->id < w->id) {
      prev = prev->next;
    }
    w->next = prev->next;
    prev->next = w;
  }
  s->active = w;
  w->attention = 0;
  return w;
}

void traash_session_select_window(TraashSession *s, TraashWindow *w) {
  if (!s || !w) {
    return;
  }
  s->active = w;
  w->attention = 0;
}

int traash_session_goto_window(TraashSession *s, int number) {
  if (!s || number < 0) {
    return -1;
  }
  char label[16];
  snprintf(label, sizeof(label), "%d", number);
  for (TraashWindow *w = s->windows; w; w = w->next) {
    if (w->id == number || strcmp(w->name, label) == 0) {
      traash_session_select_window(s, w);
      return 0;
    }
  }
  return -1;
}

void traash_session_next_window(TraashSession *s) {
  if (!s || !s->active) {
    return;
  }
  TraashWindow *next = s->active->next ? s->active->next : s->windows;
  traash_session_select_window(s, next);
}

void traash_session_prev_window(TraashSession *s) {
  if (!s || !s->active || !s->windows) {
    return;
  }
  if (s->active == s->windows) {
    TraashWindow *tail = s->windows;
    while (tail->next) {
      tail = tail->next;
    }
    traash_session_select_window(s, tail);
    return;
  }
  TraashWindow *prev = s->windows;
  while (prev->next && prev->next != s->active) {
    prev = prev->next;
  }
  traash_session_select_window(s, prev);
}

int traash_session_close_window(TraashSession *s, TraashWindow *w) {
  if (!s || !w) {
    return 0;
  }
  TraashWindow *prev = NULL;
  TraashWindow *cur = s->windows;
  while (cur && cur != w) {
    prev = cur;
    cur = cur->next;
  }
  if (!cur) {
    return 0;
  }
  if (prev) {
    prev->next = w->next;
  } else {
    s->windows = w->next;
  }
  if (s->active == w) {
    s->active = w->next ? w->next : (prev ? prev : s->windows);
  }
  traash_window_destroy(w);
  return s->windows ? 0 : 1;
}

int traash_session_poll_ex(TraashSession *s, int interactive_resize) {
  TraashWindow *w = s->windows;
  while (w) {
    TraashWindow *next = w->next;
    int chunks;
    if (interactive_resize) {
      /* During a live window drag, ignore background tabs entirely. */
      if (w != s->active) {
        w = next;
        continue;
      }
      chunks = 2;
    } else if (w == s->active) {
      chunks = 12;
    } else {
      chunks = 2;
    }
    traash_window_poll_n(w, chunks);
    if (!w->panes) {
      if (traash_session_close_window(s, w)) {
        return 1;
      }
    }
    w = next;
  }
  return s->windows ? 0 : 1;
}

int traash_session_poll(TraashSession *s) {
  return traash_session_poll_ex(s, 0);
}

TraashPane *traash_session_active_pane(TraashSession *s) {
  if (!s || !s->active) {
    return NULL;
  }
  return traash_window_active_pane(s->active);
}
