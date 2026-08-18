#include "app/agent_cli.h"

#include "app/cli.h"
#include "mux/snapshot.h"
#include "input/actions.h"
#include "mux/client.h"
#include "mux/protocol.h"
#include "util/password.h"
#include "util/path.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
  (void)sig;
  g_stop = 1;
}

typedef struct {
  const char *host;
  int port;
  int password_fd;
  int read_only;
  char password[256];
} AgentOpts;

static int connect_mux(const char *session, AgentOpts *opt, TraashMuxConnection *conn,
                       int *role_out) {
  char runtime[400];
  char sock[512];
  if (opt->host) {
    if (traash_mux_connect_tcp(opt->host, opt->port, conn) != 0) {
      return -1;
    }
  } else {
    if (traash_runtime_dir(runtime, sizeof(runtime)) != 0) {
      return -1;
    }
    snprintf(sock, sizeof(sock), "%s/mux.sock", runtime);
    if (traash_mux_connect_unix(sock, conn) != 0) {
      return -1;
    }
  }
  if (traash_mux_client_auth(conn, session, opt->password, opt->read_only, role_out) != 0) {
    traash_mux_client_close(conn);
    return -1;
  }
  return 0;
}

static int parse_common(int argc, char **argv, int *ai, AgentOpts *opt, char **session_out) {
  memset(opt, 0, sizeof(*opt));
  opt->port = 9477;
  opt->password_fd = -1;
  *session_out = NULL;
  for (int i = *ai; i < argc; i++) {
    if (strcmp(argv[i], "--read-only") == 0) {
      opt->read_only = 1;
    } else if (strcmp(argv[i], "--password-fd") == 0 && i + 1 < argc) {
      opt->password_fd = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      opt->host = argv[++i];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      opt->port = atoi(argv[++i]);
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "traash agent: unknown option %s\n", argv[i]);
      return -1;
    } else {
      *session_out = argv[i];
      *ai = i + 1;
      return 0;
    }
  }
  return -1;
}

static int collect_password(AgentOpts *opt, const char *session) {
  if (opt->password[0]) {
    return 0;
  }
  if (opt->password_fd >= 0) {
    return traash_password_read_fd(opt->password_fd, opt->password, sizeof(opt->password));
  }
  if (traash_session_file_exists(session) || opt->host) {
    return traash_password_prompt("Password", opt->password, sizeof(opt->password));
  }
  return 0;
}

static int cmd_state(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: traash agent state SESSION [--read-only] [--host H] [--port P]\n");
    return 1;
  }
  AgentOpts opt;
  char *session = NULL;
  int ai = 3;
  if (parse_common(argc, argv, &ai, &opt, &session) != 0 || !session) {
    fprintf(stderr, "Usage: traash agent state SESSION\n");
    return 1;
  }
  if (collect_password(&opt, session) != 0) {
    return 1;
  }
  TraashMuxConnection conn;
  int role = 0;
  if (connect_mux(session, &opt, &conn, &role) != 0) {
    return 1;
  }
  char req[128] = "{\"v\":1,\"scrollback\":200,\"pane_id\":0}";
  char out[512 * 1024];
  int rt = traash_mux_client_request_json(&conn, TRAASH_PROTO_GET_STATE, req, TRAASH_PROTO_STATE, out,
                                          sizeof(out));
  if (rt != TRAASH_PROTO_STATE) {
    traash_mux_client_close(&conn);
    return rt == TRAASH_PROTO_ACTION_DENIED ? 3 : 1;
  }
  traash_mux_client_close(&conn);
  puts(out);
  return 0;
}

static int cmd_send(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: traash agent send SESSION --pane N --literal BYTES\n");
    return 1;
  }
  AgentOpts opt;
  char *session = NULL;
  int ai = 3;
  if (parse_common(argc, argv, &ai, &opt, &session) != 0 || !session) {
    return 1;
  }
  int pane_id = 1;
  const char *literal = NULL;
  for (int i = ai; i < argc; i++) {
    if (strcmp(argv[i], "--pane") == 0 && i + 1 < argc) {
      pane_id = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--literal") == 0 && i + 1 < argc) {
      literal = argv[++i];
    }
  }
  if (!literal) {
    fprintf(stderr, "traash agent send: --literal required\n");
    return 1;
  }
  if (collect_password(&opt, session) != 0) {
    return 1;
  }
  TraashMuxConnection conn;
  if (connect_mux(session, &opt, &conn, NULL) != 0) {
    return 1;
  }
  if (conn.role != TRAASH_ROLE_WRITE) {
    traash_mux_client_close(&conn);
    return 3;
  }
  if (traash_mux_client_send_input(&conn, pane_id, (const uint8_t *)literal, strlen(literal)) !=
      0) {
    traash_mux_client_close(&conn);
    return 1;
  }
  traash_mux_client_close(&conn);
  return 0;
}

static int cmd_wait(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: traash agent wait SESSION --pane N [--timeout MS]\n");
    return 1;
  }
  AgentOpts opt;
  char *session = NULL;
  int ai = 3;
  if (parse_common(argc, argv, &ai, &opt, &session) != 0 || !session) {
    return 1;
  }
  int pane_id = 1;
  int timeout_ms = 30000;
  for (int i = ai; i < argc; i++) {
    if (strcmp(argv[i], "--pane") == 0 && i + 1 < argc) {
      pane_id = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
      timeout_ms = atoi(argv[++i]);
    }
  }
  if (collect_password(&opt, session) != 0) {
    return 1;
  }
  TraashMuxConnection conn;
  if (connect_mux(session, &opt, &conn, NULL) != 0) {
    return 1;
  }
  if (conn.role != TRAASH_ROLE_WRITE) {
    traash_mux_client_close(&conn);
    return 3;
  }
  char req[128];
  snprintf(req, sizeof(req), "{\"pane_id\":%d,\"timeout_ms\":%d}", pane_id, timeout_ms);
  int r = traash_mux_client_request_json(&conn, TRAASH_PROTO_WAIT_IDLE, req, TRAASH_PROTO_IDLE, NULL,
                                         0);
  traash_mux_client_close(&conn);
  if (r == TRAASH_PROTO_WAIT_TIMEOUT) {
    return 2;
  }
  if (r == TRAASH_PROTO_ACTION_DENIED) {
    return 3;
  }
  if (r != TRAASH_PROTO_IDLE) {
    return 1;
  }
  return 0;
}

static int cmd_subscribe(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: traash agent subscribe SESSION [--emit-output]\n");
    return 1;
  }
  AgentOpts opt;
  char *session = NULL;
  int ai = 3;
  if (parse_common(argc, argv, &ai, &opt, &session) != 0 || !session) {
    return 1;
  }
  int emit_output = 0;
  for (int i = ai; i < argc; i++) {
    if (strcmp(argv[i], "--emit-output") == 0) {
      emit_output = 1;
    }
  }
  if (collect_password(&opt, session) != 0) {
    return 1;
  }
  signal(SIGINT, on_sigint);
  TraashMuxConnection conn;
  if (connect_mux(session, &opt, &conn, NULL) != 0) {
    return 1;
  }
  char req[64];
  snprintf(req, sizeof(req), "{\"emit_output\":%s}", emit_output ? "true" : "false");
  if (traash_mux_client_request_json(&conn, TRAASH_PROTO_SUBSCRIBE, req, 0, NULL, 0) != 0) {
    traash_mux_client_close(&conn);
    return 1;
  }
  char line[65536];
  while (!g_stop) {
    int t = traash_mux_client_read_event(&conn, line, sizeof(line));
    if (t == TRAASH_PROTO_EVENT) {
      puts(line);
      fflush(stdout);
    } else if (t < 0) {
      break;
    }
  }
  traash_mux_client_close(&conn);
  return 0;
}

static int cmd_run_action(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr, "Usage: traash agent run-action SESSION -- ACTION [...]\n");
    return 1;
  }
  AgentOpts opt;
  char *session = NULL;
  int ai = 3;
  if (parse_common(argc, argv, &ai, &opt, &session) != 0 || !session) {
    return 1;
  }
  int dash = -1;
  for (int i = ai; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      dash = i + 1;
      break;
    }
  }
  if (dash < 0 || dash >= argc) {
    fprintf(stderr, "traash agent run-action: missing -- ACTION\n");
    return 1;
  }
  const char *action_name = argv[dash];
  uint32_t action = 0;
  if (strcmp(action_name, "split-h") == 0) {
    action = TRAASH_ACTION_SPLIT_H;
  } else if (strcmp(action_name, "split-v") == 0) {
    action = TRAASH_ACTION_SPLIT_V;
  } else if (strcmp(action_name, "new-window") == 0) {
    action = TRAASH_ACTION_NEW_WINDOW;
  } else if (strcmp(action_name, "next-window") == 0) {
    action = TRAASH_ACTION_NEXT_WINDOW;
  } else if (strcmp(action_name, "prev-window") == 0) {
    action = TRAASH_ACTION_PREV_WINDOW;
  } else {
    fprintf(stderr, "traash agent run-action: unknown action '%s'\n", action_name);
    return 1;
  }
  if (collect_password(&opt, session) != 0) {
    return 1;
  }
  TraashMuxConnection conn;
  if (connect_mux(session, &opt, &conn, NULL) != 0) {
    return 1;
  }
  if (traash_mux_client_send_action(&conn, action, NULL, 0) != 0) {
    traash_mux_client_close(&conn);
    return 1;
  }
  traash_mux_client_close(&conn);
  return 0;
}

int traash_agent_cli_run(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr,
            "Usage: traash agent {state|send|wait|subscribe|run-action} SESSION [options]\n");
    return 1;
  }
  const char *sub = argv[2];
  if (strcmp(sub, "state") == 0) {
    return cmd_state(argc, argv);
  }
  if (strcmp(sub, "send") == 0) {
    return cmd_send(argc, argv);
  }
  if (strcmp(sub, "wait") == 0) {
    return cmd_wait(argc, argv);
  }
  if (strcmp(sub, "subscribe") == 0) {
    return cmd_subscribe(argc, argv);
  }
  if (strcmp(sub, "run-action") == 0) {
    return cmd_run_action(argc, argv);
  }
  fprintf(stderr, "traash agent: unknown subcommand '%s'\n", sub);
  return 1;
}
