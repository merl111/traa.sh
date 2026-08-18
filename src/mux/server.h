#ifndef TRAASH_SERVER_H
#define TRAASH_SERVER_H

#include "mux/session.h"

#include <stddef.h>

#define TRAASH_MUX_MAX_CLIENTS 32

typedef struct TraashMuxClientConn {
  int fd;
  int role;
  char session_name[64];
  TraashSession *session;
  uint8_t *read_buf;
  size_t read_len;
  size_t read_cap;
  int subscribed;
  int emit_output;
  int wait_pane_id;
  int64_t wait_deadline_ms;
  struct TraashMuxClientConn *next;
} TraashMuxClientConn;

typedef struct TraashMuxServer {
  TraashSession *sessions;
  TraashSession *attached;
  TraashMuxClientConn *clients;
  int next_session_id;
  int listen_fd;
  int tcp_listen_fd;
  char socket_path[512];
  char tcp_bind[128];
  int tcp_port;
  int cols;
  int rows;
  double last_save_at;
} TraashMuxServer;

int traash_mux_init(TraashMuxServer *srv, int cols, int rows);
void traash_mux_shutdown(TraashMuxServer *srv);
TraashSession *traash_mux_create_session(TraashMuxServer *srv, const char *name);
TraashSession *traash_mux_create_encrypted_session(TraashMuxServer *srv, const char *name,
                                                   const char *write_pw, const char *read_pw);
TraashSession *traash_mux_find_session(TraashMuxServer *srv, const char *name);
int traash_mux_attach(TraashMuxServer *srv, const char *name);
void traash_mux_detach(TraashMuxServer *srv);
void traash_mux_poll(TraashMuxServer *srv);
void traash_mux_poll_ex(TraashMuxServer *srv, int interactive_resize);
int traash_mux_list(TraashMuxServer *srv, char *buf, size_t n);
int traash_mux_start_listener(TraashMuxServer *srv);
int traash_mux_start_tcp_listener(TraashMuxServer *srv, const char *bind_addr, int port);
void traash_mux_accept_clients(TraashMuxServer *srv);
void traash_mux_poll_clients(TraashMuxServer *srv);
void traash_mux_flush_encrypted(TraashMuxServer *srv, double now);
int traash_mux_action_mutating(uint32_t action);

TraashPane *traash_mux_find_pane(TraashSession *s, int pane_id);

void traash_mux_event_broadcast(TraashMuxServer *srv, TraashSession *sess, const char *json);

#endif
