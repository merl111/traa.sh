#ifndef TRAASH_PANE_H
#define TRAASH_PANE_H

#include "term/pty.h"
#include "term/screen.h"
#include "term/selection.h"
#include "term/vt.h"

typedef struct TraashPane {
  int id;
  float x, y, w, h; /* normalized 0..1 within window */
  TraashScreen screen;
  TraashVt vt;
  TraashPty pty;
  TraashSelection selection;
  char title[256];
  int zoomed;
  int pty_pending_cols;
  int pty_pending_rows;
  double pty_resize_at; /* glfw/gettime seconds; 0 = none pending */
  int agent_busy;
  int command_done_pending;
  void (*agent_osc_hook)(struct TraashPane *p, int code, const char *data, void *ud);
  void *agent_osc_ud;
  struct TraashPane *next;
} TraashPane;

TraashPane *traash_pane_create(int id, int cols, int rows);
void traash_pane_destroy(TraashPane *p);
int traash_pane_spawn_shell(TraashPane *p);
/* Returns 1 if the child shell has exited (caller should close the pane). */
int traash_pane_poll(TraashPane *p);
/* Like poll, but stop after max_chunks PTY reads (keeps UI responsive). */
int traash_pane_poll_n(TraashPane *p, int max_chunks);
void traash_pane_resize_cells(TraashPane *p, int cols, int rows);
/* Flush deferred TIOCSWINSZ after resize settles (call each frame). */
void traash_pane_flush_pty_resize(TraashPane *p, double now);

void traash_pane_set_agent_osc_hook(TraashPane *p,
                                    void (*fn)(struct TraashPane *, int, const char *, void *),
                                    void *ud);

#endif
