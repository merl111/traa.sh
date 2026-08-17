#include "mux/server.h"

#include "mux/ipc.h"
#include "util/log.h"
#include "util/path.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int traash_mux_init(TraashMuxServer *srv, int cols, int rows) {
  memset(srv, 0, sizeof(*srv));
  srv->cols = cols;
  srv->rows = rows;
  srv->next_session_id = 1;
  srv->listen_fd = -1;
  char runtime[400];
  if (traash_runtime_dir(runtime, sizeof(runtime)) != 0) {
    return -1;
  }
  snprintf(srv->socket_path, sizeof(srv->socket_path), "%s/mux.sock", runtime);
  srv->attached = traash_mux_create_session(srv, "default");
  return srv->attached ? 0 : -1;
}

void traash_mux_shutdown(TraashMuxServer *srv) {
  TraashSession *s = srv->sessions;
  while (s) {
    TraashSession *n = s->next;
    traash_session_destroy(s);
    s = n;
  }
  if (srv->listen_fd >= 0) {
    close(srv->listen_fd);
  }
  unlink(srv->socket_path);
  memset(srv, 0, sizeof(*srv));
  srv->listen_fd = -1;
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

void traash_mux_poll_ex(TraashMuxServer *srv, int interactive_resize) {
  TraashSession *s = srv->sessions;
  while (s) {
    TraashSession *next = s->next;
    if (traash_session_poll_ex(s, interactive_resize)) {
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
  traash_mux_accept_once(srv);
}

void traash_mux_poll(TraashMuxServer *srv) {
  traash_mux_poll_ex(srv, 0);
}

int traash_mux_list(TraashMuxServer *srv, char *buf, size_t n) {
  size_t o = 0;
  buf[0] = 0;
  for (TraashSession *s = srv->sessions; s; s = s->next) {
    int m = snprintf(buf + o, n > o ? n - o : 0, "%s\n", s->name);
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
  if (listen(fd, 8) < 0) {
    close(fd);
    return -1;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  srv->listen_fd = fd;
  TRAASH_LOGI("mux listening on %s", srv->socket_path);
  return 0;
}

void traash_mux_accept_once(TraashMuxServer *srv) {
  if (srv->listen_fd < 0) {
    return;
  }
  int cfd = accept(srv->listen_fd, NULL, NULL);
  if (cfd < 0) {
    return;
  }
  uint8_t buf[1024];
  ssize_t n = read(cfd, buf, sizeof(buf));
  TraashIpcMsg msg, reply;
  memset(&reply, 0, sizeof(reply));
  if (n > 0 && traash_ipc_decode(buf, (size_t)n, &msg) == 0) {
    if (msg.type == TRAASH_IPC_LIST) {
      reply.type = TRAASH_IPC_SESSION_LIST;
      traash_mux_list(srv, reply.payload, sizeof(reply.payload));
      reply.len = (uint32_t)strlen(reply.payload);
    } else if (msg.type == TRAASH_IPC_CREATE) {
      traash_mux_create_session(srv, msg.payload[0] ? msg.payload : "session");
      reply.type = TRAASH_IPC_OK;
    } else if (msg.type == TRAASH_IPC_ATTACH) {
      if (traash_mux_attach(srv, msg.payload) == 0) {
        reply.type = TRAASH_IPC_OK;
      } else {
        reply.type = TRAASH_IPC_ERR;
      }
    } else {
      reply.type = TRAASH_IPC_OK;
    }
  } else {
    reply.type = TRAASH_IPC_ERR;
  }
  size_t out = 0;
  uint8_t outb[1024];
  if (traash_ipc_encode(&reply, outb, sizeof(outb), &out) == 0) {
    (void)write(cfd, outb, out);
  }
  close(cfd);
}
