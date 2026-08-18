#include "mux/server_loop.h"

#include "config/config.h"
#include "mux/server.h"
#include "util/log.h"
#include "util/password.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static int parse_bind(const char *bind, char *host, size_t hn, int *port) {
  if (!bind || !bind[0]) {
    return -1;
  }
  const char *colon = strrchr(bind, ':');
  if (!colon || colon == bind) {
    return -1;
  }
  size_t hlen = (size_t)(colon - bind);
  if (hlen >= hn) {
    return -1;
  }
  memcpy(host, bind, hlen);
  host[hlen] = 0;
  *port = atoi(colon + 1);
  return *port > 0 ? 0 : -1;
}

int traash_server_run(const TraashCli *cli) {
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  TraashMuxServer srv;
  if (traash_mux_init(&srv, 120, 40) != 0) {
    TRAASH_LOGE("server: mux init failed");
    return 1;
  }
  if (traash_mux_start_listener(&srv) != 0) {
    TRAASH_LOGE("server: unix listen failed");
    traash_mux_shutdown(&srv);
    return 1;
  }
  if (cli->bind && cli->bind[0]) {
    char host[128];
    int port = 0;
    if (parse_bind(cli->bind, host, sizeof(host), &port) != 0) {
      TRAASH_LOGE("server: invalid --bind (use ADDR:PORT)");
      traash_mux_shutdown(&srv);
      return 1;
    }
    if (traash_mux_start_tcp_listener(&srv, host, port) != 0) {
      TRAASH_LOGE("server: tcp listen failed on %s:%d", host, port);
      traash_mux_shutdown(&srv);
      return 1;
    }
  }
  if (cli->create && cli->create[0]) {
    if (cli->encrypt) {
      if (!cli->write_pw[0] || !cli->read_pw[0]) {
        TRAASH_LOGE("server: --encrypt requires passwords");
        traash_mux_shutdown(&srv);
        return 1;
      }
      traash_mux_create_encrypted_session(&srv, cli->create, cli->write_pw, cli->read_pw);
    } else {
      traash_mux_create_session(&srv, cli->create);
    }
  }

  TRAASH_LOGI("traash mux server running (Ctrl-C to stop)");
  double t = 0;
  while (!g_stop) {
    traash_mux_poll(&srv);
    traash_mux_flush_encrypted(&srv, t);
    t += 0.05;
    usleep(50000);
  }
  traash_mux_shutdown(&srv);
  TRAASH_LOGI("server stopped");
  return 0;
}
