#ifndef TRAASH_SERVER_H
#define TRAASH_SERVER_H

#include "mux/session.h"

#include <stddef.h>

typedef struct TraashMuxServer {
  TraashSession *sessions;
  TraashSession *attached;
  int next_session_id;
  int listen_fd;
  char socket_path[512];
  int cols;
  int rows;
} TraashMuxServer;

int traash_mux_init(TraashMuxServer *srv, int cols, int rows);
void traash_mux_shutdown(TraashMuxServer *srv);
TraashSession *traash_mux_create_session(TraashMuxServer *srv, const char *name);
TraashSession *traash_mux_find_session(TraashMuxServer *srv, const char *name);
int traash_mux_attach(TraashMuxServer *srv, const char *name);
void traash_mux_detach(TraashMuxServer *srv);
void traash_mux_poll(TraashMuxServer *srv);
/* interactive_resize: keep the UI responsive while the window is being dragged. */
void traash_mux_poll_ex(TraashMuxServer *srv, int interactive_resize);
int traash_mux_list(TraashMuxServer *srv, char *buf, size_t n);
int traash_mux_start_listener(TraashMuxServer *srv);
void traash_mux_accept_once(TraashMuxServer *srv);

#endif
