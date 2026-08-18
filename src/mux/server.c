#include "mux/server.h"

#include "crypto/secret.h"
#include "input/actions.h"
#include "mux/agent_proto.h"
#include "mux/ipc.h"
#include "mux/protocol.h"
#include "mux/snapshot.h"
#include "term/screen_export.h"
#include "util/log.h"
#include "util/path.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  TraashMuxServer *srv;
  TraashSession *sess;
} PaneAgentCtx;

static int write_all(int fd, const uint8_t *buf, size_t n);
static int send_frame(int fd, uint32_t type, const uint8_t *payload, uint32_t len);

static int64_t mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_len) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < in_len; i += 3) {
    uint32_t v = in[i] << 16;
    if (i + 1 < in_len) {
      v |= in[i + 1] << 8;
    }
    if (i + 2 < in_len) {
      v |= in[i + 2];
    }
    if (o + 4 >= out_len) {
      return -1;
    }
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
    out[o++] = i + 1 < in_len ? tbl[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < in_len ? tbl[v & 63] : '=';
  }
  if (o >= out_len) {
    return -1;
  }
  out[o] = 0;
  return (int)o;
}

static int send_json_frame(int fd, uint32_t type, const char *json) {
  if (!json) {
    return send_frame(fd, type, NULL, 0);
  }
  return send_frame(fd, type, (const uint8_t *)json, (uint32_t)strlen(json));
}

void traash_mux_event_broadcast(TraashMuxServer *srv, TraashSession *sess, const char *json) {
  if (!srv || !sess || !json) {
    return;
  }
  for (TraashMuxClientConn *c = srv->clients; c; c = c->next) {
    if (c->session == sess && c->subscribed && c->fd >= 0) {
      (void)send_json_frame(c->fd, TRAASH_PROTO_EVENT, json);
    }
  }
}

static void pane_on_osc(TraashPane *p, int code, const char *data, void *ud) {
  PaneAgentCtx *ctx = ud;
  if (!ctx || !ctx->srv || !ctx->sess || !p) {
    return;
  }
  char ev[4096];
  if (code == 133 && data && data[0] == 'D') {
    p->agent_busy = 0;
    p->command_done_pending = 1;
    char extra[640];
    if (p->screen.cwd[0]) {
      char esc[512];
      traash_agent_json_escape(p->screen.cwd, esc, sizeof(esc));
      snprintf(extra, sizeof(extra), "\"cwd\":\"%s\"", esc);
    } else {
      extra[0] = 0;
    }
    if (traash_agent_build_event("command_finished", p->id, extra[0] ? extra : NULL, ev,
                                 sizeof(ev)) >= 0) {
      traash_mux_event_broadcast(ctx->srv, ctx->sess, ev);
    }
  } else if (code == 7 && data) {
    char esc[512];
    traash_agent_json_escape(p->screen.cwd, esc, sizeof(esc));
    char extra[640];
    snprintf(extra, sizeof(extra), "\"cwd\":\"%s\"", esc);
    if (traash_agent_build_event("cwd_changed", p->id, extra, ev, sizeof(ev)) >= 0) {
      traash_mux_event_broadcast(ctx->srv, ctx->sess, ev);
    }
  } else if ((code == 0 || code == 2) && data) {
    char esc[512];
    traash_agent_json_escape(p->screen.title, esc, sizeof(esc));
    char extra[640];
    snprintf(extra, sizeof(extra), "\"title\":\"%s\"", esc);
    if (traash_agent_build_event("title_changed", p->id, extra, ev, sizeof(ev)) >= 0) {
      traash_mux_event_broadcast(ctx->srv, ctx->sess, ev);
    }
  }
}

static void bind_pane_agent(TraashMuxServer *srv, TraashSession *sess, TraashPane *p) {
  static PaneAgentCtx ctxs[256];
  static int ctx_n;
  if (ctx_n >= (int)(sizeof(ctxs) / sizeof(ctxs[0]))) {
    return;
  }
  PaneAgentCtx *ctx = &ctxs[ctx_n++];
  ctx->srv = srv;
  ctx->sess = sess;
  traash_pane_set_agent_osc_hook(p, pane_on_osc, ctx);
}

static void poll_wait_idle(TraashMuxServer *srv) {
  int64_t now = mono_ms();
  for (TraashMuxClientConn *c = srv->clients; c; c = c->next) {
    if (c->wait_pane_id <= 0 || !c->session || c->fd < 0) {
      continue;
    }
    TraashPane *pane = traash_mux_find_pane(c->session, c->wait_pane_id);
    if (pane && pane->command_done_pending) {
      pane->command_done_pending = 0;
      pane->screen.command_done_pending = 0;
      char json[64];
      if (traash_agent_build_idle(c->wait_pane_id, json, sizeof(json)) >= 0) {
        send_json_frame(c->fd, TRAASH_PROTO_IDLE, json);
      }
      c->wait_pane_id = 0;
      c->wait_deadline_ms = 0;
      continue;
    }
    if (c->wait_deadline_ms > 0 && now >= c->wait_deadline_ms) {
      char json[64];
      if (traash_agent_build_wait_timeout(c->wait_pane_id, json, sizeof(json)) >= 0) {
        send_json_frame(c->fd, TRAASH_PROTO_WAIT_TIMEOUT, json);
      }
      c->wait_pane_id = 0;
      c->wait_deadline_ms = 0;
    }
  }
}

static int write_all(int fd, const uint8_t *buf, size_t n) {
  while (n) {
    ssize_t w = write(fd, buf, n);
    if (w <= 0) {
      return -1;
    }
    buf += w;
    n -= (size_t)w;
  }
  return 0;
}

static int send_frame(int fd, uint32_t type, const uint8_t *payload, uint32_t len) {
  uint8_t *frame = NULL;
  size_t frame_len = 0;
  if (traash_proto_encode(type, payload, len, &frame, &frame_len) != 0) {
    return -1;
  }
  int r = write_all(fd, frame, frame_len);
  free(frame);
  return r;
}

static void client_free(TraashMuxClientConn *c) {
  if (!c) {
    return;
  }
  if (c->fd >= 0) {
    close(c->fd);
  }
  free(c->read_buf);
  free(c);
}

static void client_remove(TraashMuxServer *srv, TraashMuxClientConn *target) {
  TraashMuxClientConn **pp = &srv->clients;
  while (*pp) {
    if (*pp == target) {
      *pp = target->next;
      client_free(target);
      return;
    }
    pp = &(*pp)->next;
  }
}

static int parse_auth(const uint8_t *payload, uint32_t len, char *session, size_t sn,
                      char *password, size_t pn) {
  if (len < 4) {
    return -1;
  }
  uint16_t sl = 0;
  uint16_t pl = 0;
  memcpy(&sl, payload, 2);
  if (len < 4u + sl) {
    return -1;
  }
  if (sl >= sn) {
    return -1;
  }
  memcpy(session, payload + 2, sl);
  session[sl] = 0;
  memcpy(&pl, payload + 2 + sl, 2);
  if (len < 4u + sl + pl || pl >= pn) {
    return -1;
  }
  memcpy(password, payload + 4 + sl, pl);
  password[pl] = 0;
  return 0;
}

TraashPane *traash_mux_find_pane(TraashSession *s, int pane_id) {
  if (!s) {
    return NULL;
  }
  for (TraashWindow *w = s->windows; w; w = w->next) {
    for (TraashPane *p = w->panes; p; p = p->next) {
      if (p->id == pane_id) {
        return p;
      }
    }
  }
  return NULL;
}

int traash_mux_action_mutating(uint32_t action) {
  switch ((TraashAction)action) {
  case TRAASH_ACTION_SPLIT_H:
  case TRAASH_ACTION_SPLIT_V:
  case TRAASH_ACTION_PANE_NEXT:
  case TRAASH_ACTION_PANE_LEFT:
  case TRAASH_ACTION_PANE_DOWN:
  case TRAASH_ACTION_PANE_UP:
  case TRAASH_ACTION_PANE_RIGHT:
  case TRAASH_ACTION_ZOOM:
  case TRAASH_ACTION_NEW_WINDOW:
  case TRAASH_ACTION_NEXT_WINDOW:
  case TRAASH_ACTION_PREV_WINDOW:
  case TRAASH_ACTION_GOTO_WINDOW:
  case TRAASH_ACTION_PASTE:
  case TRAASH_ACTION_LAYOUT_PICKER:
    return 1;
  default:
    return 0;
  }
}

static void broadcast_pty_out(TraashMuxServer *srv, TraashSession *sess, int pane_id,
                              const uint8_t *data, size_t n) {
  uint8_t *payload = malloc(4 + n);
  if (!payload) {
    return;
  }
  memcpy(payload, &pane_id, 4);
  memcpy(payload + 4, data, n);
  for (TraashMuxClientConn *c = srv->clients; c; c = c->next) {
    if (c->session == sess && c->fd >= 0) {
      (void)send_frame(c->fd, TRAASH_PROTO_PTY_OUT, payload, (uint32_t)(4 + n));
    }
  }
  free(payload);
}

static int session_spawn_shells(TraashMuxServer *srv, TraashSession *s) {
  for (TraashWindow *w = s->windows; w; w = w->next) {
    for (TraashPane *p = w->panes; p; p = p->next) {
      bind_pane_agent(srv, s, p);
      if (p->pty.master_fd < 0) {
        if (traash_pane_spawn_shell(p) != 0) {
          return -1;
        }
      }
    }
  }
  return 0;
}

static int password_role(TraashSession *s, const char *password, int *role_out) {
  if (!s->encrypted || !password[0]) {
    if (role_out) {
      *role_out = TRAASH_ROLE_WRITE;
    }
    return 0;
  }
  if (s->pw_cached) {
    if (strcmp(password, s->write_pw) == 0) {
      if (role_out) {
        *role_out = TRAASH_ROLE_WRITE;
      }
      s->dek_valid = 1;
      return 0;
    }
    if (strcmp(password, s->read_pw) == 0) {
      if (role_out) {
        *role_out = TRAASH_ROLE_READ;
      }
      return 0;
    }
    return -1;
  }
  TraashSession *tmp = NULL;
  int role = TRAASH_ROLE_NONE;
  if (traash_snapshot_load_encrypted(s->name, password, 0, &tmp, &role) != 0) {
    return -1;
  }
  traash_session_destroy(tmp);
  if (role_out) {
    *role_out = role;
  }
  if (role == TRAASH_ROLE_WRITE) {
    s->dek_valid = 1;
    s->pw_cached = 1;
    snprintf(s->write_pw, sizeof(s->write_pw), "%s", password);
  }
  return 0;
}

static TraashSession *load_or_find_session(TraashMuxServer *srv, const char *name,
                                           const char *password, int *role_out) {
  TraashSession *s = traash_mux_find_session(srv, name);
  if (s) {
    if (password_role(s, password, role_out) != 0) {
      return NULL;
    }
    return s;
  }
  if (traash_session_file_exists(name)) {
    TraashSession *loaded = NULL;
    int role = TRAASH_ROLE_NONE;
    if (traash_snapshot_load_encrypted(name, password, 0, &loaded, &role) != 0) {
      return NULL;
    }
    loaded->next = srv->sessions;
    srv->sessions = loaded;
    loaded->encrypted = 1;
    loaded->dek_valid = role == TRAASH_ROLE_WRITE;
    loaded->pw_cached = role == TRAASH_ROLE_WRITE;
    if (loaded->pw_cached) {
      snprintf(loaded->write_pw, sizeof(loaded->write_pw), "%s", password);
    }
    if (role_out) {
      *role_out = role;
    }
    session_spawn_shells(srv, loaded);
    return loaded;
  }
  if (traash_session_file_exists(name)) {
    return NULL;
  }
  s = traash_mux_create_session(srv, name);
  if (role_out) {
    *role_out = TRAASH_ROLE_WRITE;
  }
  return s;
}

static void handle_auth(TraashMuxServer *srv, TraashMuxClientConn *c, const uint8_t *payload,
                        uint32_t len) {
  char session[64];
  char password[256];
  if (parse_auth(payload, len, session, sizeof(session), password, sizeof(password)) != 0) {
    send_frame(c->fd, TRAASH_PROTO_AUTH_ERR, NULL, 0);
    client_remove(srv, c);
    return;
  }
  int role = TRAASH_ROLE_WRITE;
  TraashSession *s = load_or_find_session(srv, session, password, &role);
  if (!s) {
    send_frame(c->fd, TRAASH_PROTO_AUTH_ERR, NULL, 0);
    client_remove(srv, c);
    return;
  }
  if (traash_session_file_exists(session) && !password[0]) {
    send_frame(c->fd, TRAASH_PROTO_AUTH_ERR, NULL, 0);
    client_remove(srv, c);
    return;
  }
  c->session = s;
  c->role = (uint8_t)role;
  snprintf(c->session_name, sizeof(c->session_name), "%s", session);
  uint8_t ok[1] = {(uint8_t)role};
  send_frame(c->fd, TRAASH_PROTO_AUTH_OK, ok, 1);
  uint8_t *snap = NULL;
  size_t snap_len = 0;
  if (traash_snapshot_encode(s, &snap, &snap_len) == 0) {
    send_frame(c->fd, TRAASH_PROTO_SNAPSHOT, snap, (uint32_t)snap_len);
    free(snap);
  }
  session_spawn_shells(srv, s);
}

static void handle_get_state(TraashMuxServer *srv, TraashMuxClientConn *c, const uint8_t *payload,
                              uint32_t len) {
  (void)srv;
  if (!c->session) {
    return;
  }
  TraashGetStateReq req;
  traash_agent_parse_get_state((const char *)payload, len, &req);
  TraashExportOpts opts = {.scrollback_lines = req.scrollback,
                           .max_bytes = 256 * 1024,
                           .pane_id = req.pane_id};
  TraashPaneExport *panes = NULL;
  int count = 0;
  if (traash_session_export_window(c->session, &opts, &panes, &count) != 0) {
    return;
  }
  char *json = malloc(512 * 1024);
  if (!json) {
    traash_pane_export_free(panes, count);
    return;
  }
  if (traash_agent_build_state(c->session, panes, count, json, 512 * 1024) >= 0) {
    send_json_frame(c->fd, TRAASH_PROTO_STATE, json);
  }
  free(json);
  traash_pane_export_free(panes, count);
}

static void handle_wait_idle(TraashMuxServer *srv, TraashMuxClientConn *c,
                             const uint8_t *payload, uint32_t len) {
  (void)srv;
  if (c->role != TRAASH_ROLE_WRITE || !c->session) {
    send_frame(c->fd, TRAASH_PROTO_ACTION_DENIED, NULL, 0);
    return;
  }
  TraashWaitIdleReq req;
  if (traash_agent_parse_wait_idle((const char *)payload, len, &req) != 0) {
    return;
  }
  TraashPane *pane = traash_mux_find_pane(c->session, req.pane_id);
  if (!pane) {
    return;
  }
  if (!pane->agent_busy && !pane->command_done_pending) {
    char json[64];
    if (traash_agent_build_idle(req.pane_id, json, sizeof(json)) >= 0) {
      send_json_frame(c->fd, TRAASH_PROTO_IDLE, json);
    }
    return;
  }
  c->wait_pane_id = req.pane_id;
  c->wait_deadline_ms = mono_ms() + (req.timeout_ms > 0 ? req.timeout_ms : 30000);
}

static void handle_subscribe(TraashMuxClientConn *c, const uint8_t *payload, uint32_t len) {
  TraashSubscribeReq req;
  traash_agent_parse_subscribe((const char *)payload, len, &req);
  c->subscribed = 1;
  c->emit_output = req.emit_output;
}

static void handle_unsubscribe(TraashMuxClientConn *c) {
  c->subscribed = 0;
  c->emit_output = 0;
}

static void handle_input(TraashMuxServer *srv, TraashMuxClientConn *c, const uint8_t *payload,
                         uint32_t len) {
  (void)srv;
  if (c->role != TRAASH_ROLE_WRITE || len < 4 || !c->session) {
    send_frame(c->fd, TRAASH_PROTO_ACTION_DENIED, NULL, 0);
    return;
  }
  int pane_id = 0;
  memcpy(&pane_id, payload, 4);
  TraashPane *pane = traash_mux_find_pane(c->session, pane_id);
  if (!pane || pane->pty.master_fd < 0) {
    return;
  }
  traash_pty_write(&pane->pty, payload + 4, len - 4);
  pane->agent_busy = 1;
  pane->command_done_pending = 0;
}

static void handle_action(TraashMuxServer *srv, TraashMuxClientConn *c, const uint8_t *payload,
                          uint32_t len) {
  if (len < 4 || !c->session) {
    return;
  }
  uint32_t action = 0;
  memcpy(&action, payload, 4);
  if (c->role != TRAASH_ROLE_WRITE && traash_mux_action_mutating(action)) {
    send_frame(c->fd, TRAASH_PROTO_ACTION_DENIED, NULL, 0);
    return;
  }
  TraashSession *sess = c->session;
  TraashWindow *win = sess->active;
  switch ((TraashAction)action) {
  case TRAASH_ACTION_SPLIT_H:
    if (win) {
      traash_window_split(win, 0, &sess->next_pane_id, srv->cols, srv->rows);
    }
    break;
  case TRAASH_ACTION_SPLIT_V:
    if (win) {
      traash_window_split(win, 1, &sess->next_pane_id, srv->cols, srv->rows);
    }
    break;
  case TRAASH_ACTION_PANE_NEXT:
    if (win) {
      traash_window_focus_next(win);
    }
    break;
  case TRAASH_ACTION_PANE_LEFT:
    if (win) {
      traash_window_focus_dir(win, -1, 0);
    }
    break;
  case TRAASH_ACTION_PANE_DOWN:
    if (win) {
      traash_window_focus_dir(win, 0, 1);
    }
    break;
  case TRAASH_ACTION_PANE_UP:
    if (win) {
      traash_window_focus_dir(win, 0, -1);
    }
    break;
  case TRAASH_ACTION_PANE_RIGHT:
    if (win) {
      traash_window_focus_dir(win, 1, 0);
    }
    break;
  case TRAASH_ACTION_ZOOM:
    if (win) {
      traash_window_zoom_toggle(win);
    }
    break;
  case TRAASH_ACTION_NEW_WINDOW:
    traash_session_new_window(sess, NULL, srv->cols, srv->rows);
    break;
  case TRAASH_ACTION_NEXT_WINDOW:
    traash_session_next_window(sess);
    break;
  case TRAASH_ACTION_PREV_WINDOW:
    traash_session_prev_window(sess);
    break;
  case TRAASH_ACTION_GOTO_WINDOW:
    if (len >= 8) {
      uint32_t num = 0;
      memcpy(&num, payload + 4, 4);
      traash_session_goto_window(sess, (int)num);
    }
    break;
  default:
    break;
  }
  uint8_t *snap = NULL;
  size_t snap_len = 0;
  if (traash_snapshot_encode(sess, &snap, &snap_len) == 0) {
    for (TraashMuxClientConn *cl = srv->clients; cl; cl = cl->next) {
      if (cl->session == sess && cl->fd >= 0) {
        send_frame(cl->fd, TRAASH_PROTO_SNAPSHOT, snap, (uint32_t)snap_len);
      }
    }
    free(snap);
  }
  if (traash_mux_action_mutating(action)) {
    char ev[512];
    char extra[128];
    snprintf(extra, sizeof(extra), "\"session\":\"%s\"", sess->name);
    if (traash_agent_build_event("layout_changed", 0, extra, ev, sizeof(ev)) >= 0) {
      traash_mux_event_broadcast(srv, sess, ev);
    }
    for (TraashWindow *w = sess->windows; w; w = w->next) {
      for (TraashPane *np = w->panes; np; np = np->next) {
        if (!np->agent_osc_hook) {
          bind_pane_agent(srv, sess, np);
        }
        if (np->pty.master_fd < 0) {
          traash_pane_spawn_shell(np);
        }
      }
    }
  }
}

static void process_client(TraashMuxServer *srv, TraashMuxClientConn *c) {
  for (;;) {
    if (c->read_len < 8) {
      return;
    }
    TraashProtoFrame frame;
    size_t consumed = 0;
    if (traash_proto_decode(c->read_buf, c->read_len, &frame, &consumed) != 0) {
      return;
    }
    switch (frame.type) {
    case TRAASH_PROTO_AUTH:
      handle_auth(srv, c, frame.payload, frame.len);
      break;
    case TRAASH_PROTO_INPUT:
      handle_input(srv, c, frame.payload, frame.len);
      break;
    case TRAASH_PROTO_ACTION:
      handle_action(srv, c, frame.payload, frame.len);
      break;
    case TRAASH_PROTO_DETACH:
      client_remove(srv, c);
      traash_proto_frame_free(&frame);
      return;
    case TRAASH_PROTO_LIST: {
      char out[512];
      traash_mux_list(srv, out, sizeof(out));
      send_frame(c->fd, TRAASH_PROTO_SESSION_LIST, (const uint8_t *)out,
                 (uint32_t)strlen(out));
      break;
    }
    case TRAASH_PROTO_CREATE:
      if (frame.len && frame.payload) {
        char name[64];
        snprintf(name, sizeof(name), "%.*s", (int)frame.len, (const char *)frame.payload);
        TraashSession *ns = traash_mux_create_session(srv, name);
        if (ns) {
          session_spawn_shells(srv, ns);
        }
      }
      send_frame(c->fd, TRAASH_PROTO_OK, NULL, 0);
      break;
    case TRAASH_PROTO_GET_STATE:
      handle_get_state(srv, c, frame.payload, frame.len);
      break;
    case TRAASH_PROTO_WAIT_IDLE:
      handle_wait_idle(srv, c, frame.payload, frame.len);
      break;
    case TRAASH_PROTO_SUBSCRIBE:
      handle_subscribe(c, frame.payload, frame.len);
      break;
    case TRAASH_PROTO_UNSUBSCRIBE:
      handle_unsubscribe(c);
      break;
    default:
      break;
    }
    traash_proto_frame_free(&frame);
    memmove(c->read_buf, c->read_buf + consumed, c->read_len - consumed);
    c->read_len -= consumed;
  }
}

int traash_mux_init(TraashMuxServer *srv, int cols, int rows) {
  memset(srv, 0, sizeof(*srv));
  srv->cols = cols;
  srv->rows = rows;
  srv->next_session_id = 1;
  srv->listen_fd = -1;
  srv->tcp_listen_fd = -1;
  char runtime[400];
  if (traash_runtime_dir(runtime, sizeof(runtime)) != 0) {
    return -1;
  }
  snprintf(srv->socket_path, sizeof(srv->socket_path), "%s/mux.sock", runtime);
  srv->attached = traash_mux_create_session(srv, "default");
  if (srv->attached) {
    session_spawn_shells(srv, srv->attached);
  }
  return srv->attached ? 0 : -1;
}

void traash_mux_shutdown(TraashMuxServer *srv) {
  while (srv->clients) {
    client_remove(srv, srv->clients);
  }
  TraashSession *s = srv->sessions;
  while (s) {
    TraashSession *n = s->next;
    traash_session_destroy(s);
    s = n;
  }
  if (srv->listen_fd >= 0) {
    close(srv->listen_fd);
  }
  if (srv->tcp_listen_fd >= 0) {
    close(srv->tcp_listen_fd);
  }
  unlink(srv->socket_path);
  memset(srv, 0, sizeof(*srv));
  srv->listen_fd = -1;
  srv->tcp_listen_fd = -1;
}

TraashSession *traash_mux_create_session(TraashMuxServer *srv, const char *name) {
  TraashSession *s =
      traash_session_create(srv->next_session_id++, name, srv->cols, srv->rows);
  if (!s) {
    return NULL;
  }
  s->next = srv->sessions;
  srv->sessions = s;
  return s;
}

TraashSession *traash_mux_create_encrypted_session(TraashMuxServer *srv, const char *name,
                                                   const char *write_pw, const char *read_pw) {
  TraashSession *s = traash_mux_create_session(srv, name);
  if (!s) {
    return NULL;
  }
  s->encrypted = 1;
  s->dek_valid = 1;
  s->pw_cached = 1;
  snprintf(s->write_pw, sizeof(s->write_pw), "%s", write_pw);
  snprintf(s->read_pw, sizeof(s->read_pw), "%s", read_pw);
  traash_snapshot_save_encrypted(name, s, write_pw, read_pw);
  return s;
}

TraashSession *traash_mux_find_session(TraashMuxServer *srv, const char *name) {
  for (TraashSession *s = srv->sessions; s; s = s->next) {
    if (strcmp(s->name, name) == 0) {
      return s;
    }
  }
  return NULL;
}

int traash_mux_attach(TraashMuxServer *srv, const char *name) {
  TraashSession *s = traash_mux_find_session(srv, name);
  if (!s) {
    s = traash_mux_create_session(srv, name);
  }
  if (!s) {
    return -1;
  }
  srv->attached = s;
  return 0;
}

void traash_mux_detach(TraashMuxServer *srv) {
  srv->attached = NULL;
}

static void poll_session_pty(TraashMuxServer *srv, TraashSession *sess) {
  static int64_t output_window_ms;
  static int output_bytes[256];
  static int output_pane_id[256];
  static int output_n;
  int64_t now = mono_ms();
  if (output_window_ms == 0 || now - output_window_ms >= 1000) {
    output_window_ms = now;
    output_n = 0;
  }
  for (TraashWindow *w = sess->windows; w; w = w->next) {
    for (TraashPane *p = w->panes; p; p = p->next) {
      if (p->pty.master_fd < 0) {
        continue;
      }
      uint8_t buf[4096];
      for (int i = 0; i < 8; i++) {
        ssize_t n = traash_pty_read(&p->pty, buf, sizeof(buf));
        if (n > 0) {
          traash_vt_feed(&p->vt, buf, (size_t)n);
          p->screen.activity_pending = 1;
          broadcast_pty_out(srv, sess, p->id, buf, (size_t)n);
          if (p->screen.title[0]) {
            snprintf(p->title, sizeof(p->title), "%s", p->screen.title);
          }
          for (TraashMuxClientConn *cl = srv->clients; cl; cl = cl->next) {
            if (cl->session != sess || !cl->subscribed || !cl->emit_output) {
              continue;
            }
            int idx = -1;
            for (int j = 0; j < output_n; j++) {
              if (output_pane_id[j] == p->id) {
                idx = j;
                break;
              }
            }
            if (idx < 0 && output_n < (int)(sizeof(output_bytes) / sizeof(output_bytes[0]))) {
              idx = output_n++;
              output_pane_id[idx] = p->id;
              output_bytes[idx] = 0;
            }
            if (idx >= 0 && output_bytes[idx] + (int)n <= 51200) {
              char b64[8192];
              if (base64_encode(buf, (size_t)n, b64, sizeof(b64)) >= 0) {
                char extra[9000];
                snprintf(extra, sizeof(extra), "\"data_base64\":\"%s\"", b64);
                char ev[9216];
                if (traash_agent_build_event("pane_output", p->id, extra, ev, sizeof(ev)) >= 0) {
                  traash_mux_event_broadcast(srv, sess, ev);
                }
                output_bytes[idx] += (int)n;
              }
            }
          }
          continue;
        }
        break;
      }
    }
  }
}

void traash_mux_poll_ex(TraashMuxServer *srv, int interactive_resize) {
  (void)interactive_resize;
  TraashSession *s = srv->sessions;
  while (s) {
    TraashSession *next = s->next;
    poll_session_pty(srv, s);
    if (traash_session_poll_ex(s, 0)) {
      if (srv->attached == s) {
        srv->attached = NULL;
      }
      if (srv->sessions == s) {
        srv->sessions = s->next;
      } else {
        for (TraashSession *p = srv->sessions; p; p = p->next) {
          if (p->next == s) {
            p->next = s->next;
            break;
          }
        }
      }
      traash_session_destroy(s);
    }
    s = next;
  }
  traash_mux_accept_clients(srv);
  traash_mux_poll_clients(srv);
  poll_wait_idle(srv);
}

void traash_mux_poll(TraashMuxServer *srv) {
  traash_mux_poll_ex(srv, 0);
}

int traash_mux_list(TraashMuxServer *srv, char *buf, size_t n) {
  size_t o = 0;
  buf[0] = 0;
  for (TraashSession *s = srv->sessions; s; s = s->next) {
    int m = snprintf(buf + o, n > o ? n - o : 0, "%s%s\n", s->name, s->encrypted ? " *" : "");
    if (m < 0) {
      break;
    }
    o += (size_t)m;
  }
  return 0;
}

int traash_mux_start_listener(TraashMuxServer *srv) {
  unlink(srv->socket_path);
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", srv->socket_path);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  if (listen(fd, 16) < 0) {
    close(fd);
    return -1;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  srv->listen_fd = fd;
  TRAASH_LOGI("mux listening on %s", srv->socket_path);
  return 0;
}

int traash_mux_start_tcp_listener(TraashMuxServer *srv, const char *bind_addr, int port) {
  if (!bind_addr || port <= 0) {
    return -1;
  }
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = AI_PASSIVE;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = NULL;
  if (getaddrinfo(bind_addr, port_str, &hints, &res) != 0 || !res) {
    return -1;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(res);
    return -1;
  }
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
    close(fd);
    freeaddrinfo(res);
    return -1;
  }
  freeaddrinfo(res);
  if (listen(fd, 16) < 0) {
    close(fd);
    return -1;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  srv->tcp_listen_fd = fd;
  snprintf(srv->tcp_bind, sizeof(srv->tcp_bind), "%s", bind_addr);
  srv->tcp_port = port;
  TRAASH_LOGI("mux tcp listening on %s:%d", bind_addr, port);
  return 0;
}

static void accept_one(TraashMuxServer *srv, int listen_fd) {
  int cfd = accept(listen_fd, NULL, NULL);
  if (cfd < 0) {
    return;
  }
  int flags = fcntl(cfd, F_GETFL, 0);
  fcntl(cfd, F_SETFL, flags | O_NONBLOCK);
  TraashMuxClientConn *c = calloc(1, sizeof(*c));
  if (!c) {
    close(cfd);
    return;
  }
  c->fd = cfd;
  c->next = srv->clients;
  srv->clients = c;
}

void traash_mux_accept_clients(TraashMuxServer *srv) {
  if (srv->listen_fd >= 0) {
    accept_one(srv, srv->listen_fd);
  }
  if (srv->tcp_listen_fd >= 0) {
    accept_one(srv, srv->tcp_listen_fd);
  }
}

void traash_mux_poll_clients(TraashMuxServer *srv) {
  for (TraashMuxClientConn *c = srv->clients; c;) {
    TraashMuxClientConn *next = c->next;
    if (c->fd < 0) {
      client_remove(srv, c);
      c = next;
      continue;
    }
    uint8_t buf[4096];
    ssize_t r = read(c->fd, buf, sizeof(buf));
    if (r > 0) {
      if (c->read_len + (size_t)r > c->read_cap) {
        size_t nc = c->read_cap ? c->read_cap * 2 : 8192;
        uint8_t *nb = realloc(c->read_buf, nc);
        if (!nb) {
          client_remove(srv, c);
          c = next;
          continue;
        }
        c->read_buf = nb;
        c->read_cap = nc;
      }
      memcpy(c->read_buf + c->read_len, buf, (size_t)r);
      c->read_len += (size_t)r;
      process_client(srv, c);
    } else if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
      client_remove(srv, c);
    }
    c = next;
  }
}

void traash_mux_flush_encrypted(TraashMuxServer *srv, double now) {
  if (now - srv->last_save_at < 5.0) {
    return;
  }
  srv->last_save_at = now;
  for (TraashSession *s = srv->sessions; s; s = s->next) {
    if (!s->encrypted || !s->dek_valid || !s->pw_cached) {
      continue;
    }
    traash_snapshot_save_encrypted(s->name, s, s->write_pw, s->read_pw);
  }
}
