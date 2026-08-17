#ifndef TRAASH_WINDOW_H
#define TRAASH_WINDOW_H

#include "mux/pane.h"

typedef struct TraashWindow {
  int id;
  char name[64];
  TraashPane *panes;
  TraashPane *active;
  int zoomed_pane_id;
  /* Set when a background tab rings BEL / reports command finished; cleared on focus. */
  int attention;
  struct TraashWindow *next;
} TraashWindow;

TraashWindow *traash_window_create(int id, const char *name, int cols, int rows);
void traash_window_destroy(TraashWindow *w);
TraashPane *traash_window_active_pane(TraashWindow *w);
TraashPane *traash_window_split(TraashWindow *w, int vertical, int *id_gen, int cols,
                               int rows);
void traash_window_focus_next(TraashWindow *w);
void traash_window_focus_pane(TraashWindow *w, TraashPane *p);
/* Focus nearest pane in direction (dx,dy) where each is -1, 0, or 1. */
void traash_window_focus_dir(TraashWindow *w, int dx, int dy);
void traash_window_zoom_toggle(TraashWindow *w);
/* Returns number of panes closed this poll (shell exited). */
int traash_window_poll(TraashWindow *w);
int traash_window_poll_n(TraashWindow *w, int max_chunks);
int traash_window_pane_count(const TraashWindow *w);
/* Close a pane; returns 1 if the window is now empty (caller should destroy it). */
int traash_window_close_pane(TraashWindow *w, TraashPane *pane);

#endif
