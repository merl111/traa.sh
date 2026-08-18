#include "mux/client.h"

#include "mux/protocol.h"
#include "mux/snapshot.h"
#include "term/vt.h"
#include "util/path.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static TraashPane *find_pane(TraashSession *s, int pane_id) {
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

static int conn_write_all(int fd, const uint8_t *buf, size_t n) {
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

static int conn_send_frame(TraashMuxConnection *conn, uint32_t type, const uint8_t *payload,
                           uint32_t len) {
  uint8_t *frame = NULL;
  size_t frame_len = 0;
  if (traash_proto_encode(type, payload, len, &frame, &frame_len) != 0) {
    return -1;
  }
  int r = conn_write_all(conn->fd, frame, frame_len);
  free(frame);
  return r;
}

static int conn_read_more(TraashMuxConnection *conn) {
  if (conn->read_len + 4096 > conn->read_cap) {
    size_t nc = conn->read_cap ? conn->read_cap * 2 : 8192;
    uint8_t *nb = realloc(conn->read_buf, nc);
    if (!nb) {
      return -1;
    }
    conn->read_buf = nb;
    conn->read_cap = nc;
  }
  ssize_t r = read(conn->fd, conn->read_buf + conn->read_len, conn->read_cap - conn->read_len);
  if (r <= 0) {
    return -1;
  }
  conn->read_len += (size_t)r;
  return 0;
}

static void handle_pty_out(TraashMuxConnection *conn, const uint8_t *payload, uint32_t len) {
  if (len < 4 || !conn->session) {
    return;
  }
  int pane_id = 0;
  memcpy(&pane_id, payload, 4);
  TraashPane *pane = find_pane(conn->session, pane_id);
  if (!pane) {
    return;
  }
  traash_vt_feed(&pane->vt, payload + 4, len - 4);
  pane->screen.activity_pending = 1;
  if (pane->screen.title[0]) {
    snprintf(pane->title, sizeof(pane->title), "%s", pane->screen.title);
  }
}

static int process_frames(TraashMuxConnection *conn) {
  for (;;) {
    if (conn->read_len < 8) {
      return 0;
    }
    TraashProtoFrame frame;
    size_t consumed = 0;
    if (traash_proto_decode(conn->read_buf, conn->read_len, &frame, &consumed) != 0) {
      return 0;
    }
    switch (frame.type) {
    case TRAASH_PROTO_SNAPSHOT:
      if (conn->session) {
        traash_session_destroy(conn->session);
        conn->session = NULL;
      }
      conn->session = traash_snapshot_decode(frame.payload, frame.len, 120, 40);
      if (conn->session) {
        for (TraashWindow *w = conn->session->windows; w; w = w->next) {
          for (TraashPane *p = w->panes; p; p = p->next) {
            p->pty.master_fd = -1;
            traash_vt_init(&p->vt, &p->screen);
          }
        }
      }
      break;
    case TRAASH_PROTO_PTY_OUT:
      handle_pty_out(conn, frame.payload, frame.len);
      break;
    case TRAASH_PROTO_ACTION_DENIED:
      break;
    default:
      break;
    }
    traash_proto_frame_free(&frame);
    memmove(conn->read_buf, conn->read_buf + consumed, conn->read_len - consumed);
    conn->read_len -= consumed;
  }
}

int traash_mux_server_running(void) {
  char runtime[400];
  char sock[512];
  if (traash_runtime_dir(runtime, sizeof(runtime)) != 0) {
    return 0;
  }
  snprintf(sock, sizeof(sock), "%s/mux.sock", runtime);
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return 0;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock);
  int r = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  close(fd);
  return r == 0;
}

int traash_mux_connect_unix(const char *path, TraashMuxConnection *conn) {
  memset(conn, 0, sizeof(*conn));
  conn->fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (conn->fd < 0) {
    return -1;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
  if (connect(conn->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(conn->fd);
    conn->fd = -1;
    return -1;
  }
  return 0;
}

int traash_mux_connect_tcp(const char *host, int port, TraashMuxConnection *conn) {
  memset(conn, 0, sizeof(*conn));
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = NULL;
  if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
    return -1;
  }
  conn->fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (conn->fd < 0) {
    freeaddrinfo(res);
    return -1;
  }
  if (connect(conn->fd, res->ai_addr, res->ai_addrlen) < 0) {
    close(conn->fd);
    conn->fd = -1;
    freeaddrinfo(res);
    return -1;
  }
  freeaddrinfo(res);
  return 0;
}

int traash_mux_client_auth(TraashMuxConnection *conn, const char *session,
                           const char *password, int force_read_only, int *role_out) {
  uint8_t *auth = NULL;
  size_t auth_len = 0;
  if (traash_proto_build_auth(session, password, &auth, &auth_len) != 0) {
    return -1;
  }
  if (conn_send_frame(conn, TRAASH_PROTO_AUTH, auth, (uint32_t)auth_len) != 0) {
    free(auth);
    return -1;
  }
  free(auth);
  for (int tries = 0; tries < 64; tries++) {
    while (conn->read_len >= 8) {
      TraashProtoFrame frame;
      size_t consumed = 0;
      if (traash_proto_decode(conn->read_buf, conn->read_len, &frame, &consumed) != 0) {
        break;
      }
      memmove(conn->read_buf, conn->read_buf + consumed, conn->read_len - consumed);
      conn->read_len -= consumed;
      if (frame.type == TRAASH_PROTO_AUTH_OK && frame.len >= 1) {
        conn->role = frame.payload[0];
        conn->connected = 1;
      } else if (frame.type == TRAASH_PROTO_AUTH_ERR) {
        traash_proto_frame_free(&frame);
        return -1;
      } else if (frame.type == TRAASH_PROTO_SNAPSHOT) {
        if (conn->session) {
          traash_session_destroy(conn->session);
          conn->session = NULL;
        }
        conn->session = traash_snapshot_decode(frame.payload, frame.len, 120, 40);
        if (conn->session) {
          for (TraashWindow *w = conn->session->windows; w; w = w->next) {
            for (TraashPane *p = w->panes; p; p = p->next) {
              p->pty.master_fd = -1;
              traash_vt_init(&p->vt, &p->screen);
            }
          }
        }
        conn->connected = 1;
        traash_proto_frame_free(&frame);
        if (force_read_only) {
          conn->role = TRAASH_ROLE_READ;
        }
        if (role_out) {
          *role_out = conn->role ? conn->role : TRAASH_ROLE_READ;
        }
        return 0;
      }
      traash_proto_frame_free(&frame);
    }
    if (conn_read_more(conn) != 0) {
      return conn->connected && conn->session ? 0 : -1;
    }
  }
  return -1;
}

int traash_mux_client_send_input(TraashMuxConnection *conn, int pane_id, const uint8_t *data,
                                 size_t n) {
  if (!conn->connected || conn->role != TRAASH_ROLE_WRITE || conn->fd < 0) {
    return -1;
  }
  uint8_t *payload = malloc(4 + n);
  if (!payload) {
    return -1;
  }
  memcpy(payload, &pane_id, 4);
  memcpy(payload + 4, data, n);
  int r = conn_send_frame(conn, TRAASH_PROTO_INPUT, payload, (uint32_t)(4 + n));
  free(payload);
  return r;
}

int traash_mux_client_send_action(TraashMuxConnection *conn, uint32_t action,
                                  const uint8_t *extra, uint32_t extra_len) {
  if (!conn->connected || conn->fd < 0) {
    return -1;
  }
  uint32_t len = 4 + extra_len;
  uint8_t *payload = malloc(len);
  if (!payload) {
    return -1;
  }
  memcpy(payload, &action, 4);
  if (extra_len && extra) {
    memcpy(payload + 4, extra, extra_len);
  }
  int r = conn_send_frame(conn, TRAASH_PROTO_ACTION, payload, len);
  free(payload);
  return r;
}

int traash_mux_client_poll(TraashMuxConnection *conn) {
  if (conn->fd < 0) {
    return -1;
  }
  if (conn_read_more(conn) != 0) {
    return -1;
  }
  return process_frames(conn);
}

void traash_mux_client_close(TraashMuxConnection *conn) {
  if (!conn) {
    return;
  }
  if (conn->fd >= 0) {
    conn_send_frame(conn, TRAASH_PROTO_DETACH, NULL, 0);
    close(conn->fd);
    conn->fd = -1;
  }
  if (conn->session) {
    traash_session_destroy(conn->session);
    conn->session = NULL;
  }
  free(conn->read_buf);
  memset(conn, 0, sizeof(*conn));
  conn->fd = -1;
}

static int conn_drain_frame(TraashMuxConnection *conn, TraashProtoFrame *frame) {
  for (int tries = 0; tries < 256; tries++) {
    while (conn->read_len >= 8) {
      size_t consumed = 0;
      if (traash_proto_decode(conn->read_buf, conn->read_len, frame, &consumed) != 0) {
        return -1;
      }
      memmove(conn->read_buf, conn->read_buf + consumed, conn->read_len - consumed);
      conn->read_len -= consumed;
      return 0;
    }
    if (conn_read_more(conn) != 0) {
      return -1;
    }
  }
  return -1;
}

int traash_mux_client_request_json(TraashMuxConnection *conn, uint32_t req_type,
                                   const char *json_in, uint32_t expect_type, char *json_out,
                                   size_t json_out_len) {
  if (!conn || conn->fd < 0) {
    return -1;
  }
  const uint8_t *payload = NULL;
  uint32_t plen = 0;
  if (json_in) {
    payload = (const uint8_t *)json_in;
    plen = (uint32_t)strlen(json_in);
  }
  if (conn_send_frame(conn, req_type, payload, plen) != 0) {
    return -1;
  }
  if (expect_type == 0) {
    return 0;
  }
  for (int tries = 0; tries < 512; tries++) {
    TraashProtoFrame frame;
    if (conn_drain_frame(conn, &frame) != 0) {
      return -1;
    }
    if (frame.type == expect_type || frame.type == TRAASH_PROTO_ACTION_DENIED ||
        frame.type == TRAASH_PROTO_WAIT_TIMEOUT) {
      if (json_out && json_out_len && frame.payload && frame.len) {
        size_t n = frame.len < json_out_len - 1 ? frame.len : json_out_len - 1;
        memcpy(json_out, frame.payload, n);
        json_out[n] = 0;
      }
      int t = (int)frame.type;
      traash_proto_frame_free(&frame);
      return t;
    }
    if (frame.type == TRAASH_PROTO_SNAPSHOT) {
      if (conn->session) {
        traash_session_destroy(conn->session);
        conn->session = NULL;
      }
      conn->session = traash_snapshot_decode(frame.payload, frame.len, 120, 40);
    } else if (frame.type == TRAASH_PROTO_PTY_OUT) {
      handle_pty_out(conn, frame.payload, frame.len);
    }
    traash_proto_frame_free(&frame);
  }
  return -1;
}

int traash_mux_client_read_event(TraashMuxConnection *conn, char *json_out, size_t json_out_len) {
  if (!conn || conn->fd < 0) {
    return -1;
  }
  TraashProtoFrame frame;
  if (conn_drain_frame(conn, &frame) != 0) {
    return -1;
  }
  if (frame.type == TRAASH_PROTO_EVENT && json_out && json_out_len && frame.payload) {
    size_t n = frame.len < json_out_len - 1 ? frame.len : json_out_len - 1;
    memcpy(json_out, frame.payload, n);
    json_out[n] = 0;
  }
  int t = (int)frame.type;
  traash_proto_frame_free(&frame);
  return t;
}
