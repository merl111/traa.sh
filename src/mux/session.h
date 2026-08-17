#ifndef TRAASH_SESSION_H
#define TRAASH_SESSION_H

#include "mux/window.h"

typedef struct TraashSession {
  int id;
  char name[64];
  TraashWindow *windows;
  TraashWindow *active;
  int next_window_id;
  int next_pane_id;
  struct TraashSession *next;
} TraashSession;

TraashSession *traash_session_create(int id, const char *name, int cols, int rows);
void traash_session_destroy(TraashSession *s);
TraashWindow *traash_session_new_window(TraashSession *s, const char *name, int cols,
                                        int rows);
void traash_session_next_window(TraashSession *s);
void traash_session_prev_window(TraashSession *s);
/* Focus a window and clear its attention badge. */
void traash_session_select_window(TraashSession *s, TraashWindow *w);
/* Select window by number (id / decimal name). Returns 0 on success. */
int traash_session_goto_window(TraashSession *s, int number);
/* Returns 1 if the session has no windows left. */
int traash_session_close_window(TraashSession *s, TraashWindow *w);
/* Poll panes; closes empty windows. Returns 1 if session is empty.
 * interactive_resize: skip/minimize background tabs so window drags stay smooth. */
int traash_session_poll(TraashSession *s);
int traash_session_poll_ex(TraashSession *s, int interactive_resize);
TraashPane *traash_session_active_pane(TraashSession *s);

#endif
