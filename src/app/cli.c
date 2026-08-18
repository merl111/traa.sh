#include "app/cli.h"

#include "config/config.h"
#include "config/theme.h"
#include "crypto/secret.h"
#include "demo/demo.h"
#include "mux/client.h"
#include "mux/ipc.h"
#include "mux/protocol.h"
#include "mux/server.h"
#include "mux/server_loop.h"
#include "mux/snapshot.h"
#include "util/log.h"
#include "util/password.h"
#include "util/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int parse_bind_port(const char *s, int *port_out) {
  if (!s || !s[0]) {
    return -1;
  }
  *port_out = atoi(s);
  return *port_out > 0 ? 0 : -1;
}

int traash_cli_parse(TraashCli *cli, int argc, char **argv) {
  memset(cli, 0, sizeof(*cli));
  cli->port = 9477;
  cli->password_fd = -1;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--server") == 0) {
      cli->server_only = true;
    } else if (strcmp(argv[i], "--list-sessions") == 0) {
      cli->list_sessions = true;
    } else if (strcmp(argv[i], "--demo") == 0) {
      cli->demo = true;
    } else if (strcmp(argv[i], "--auto") == 0) {
      cli->demo_auto = true;
    } else if (strcmp(argv[i], "--headless-test") == 0) {
      cli->headless_test = true;
    } else if (strcmp(argv[i], "--encrypt") == 0) {
      cli->encrypt = true;
    } else if (strcmp(argv[i], "--read-only") == 0) {
      cli->read_only = true;
    } else if (strcmp(argv[i], "--attach") == 0 && i + 1 < argc) {
      cli->attach = argv[++i];
    } else if (strcmp(argv[i], "--create") == 0 && i + 1 < argc) {
      cli->create = argv[++i];
    } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
      cli->bind = argv[++i];
    } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      cli->host = argv[++i];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      if (parse_bind_port(argv[++i], &cli->port) != 0) {
        fprintf(stderr, "traash: invalid --port\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--password-fd") == 0 && i + 1 < argc) {
      cli->password_fd = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--log-json") == 0) {
      cli->log_json = true;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("traa.sh — modern terminal emulator\n"
             "Usage: traash [options]\n"
             "  traash agent state|send|wait|subscribe|run-action ...  Agent API (headless)\n"
             "  traash shell-init bash|zsh|fish                       Shell OSC snippets\n"
             "  --demo [--auto]           Start capability demo\n"
             "  --server [--bind H:P]     Run mux server (optional TCP bind)\n"
             "  --list-sessions           List sessions via mux socket\n"
             "  --attach NAME             Attach session (client if server running)\n"
             "  --create NAME             Create session\n"
             "  --encrypt                 Encrypt session (with --create)\n"
             "  --read-only               Force read-only attach\n"
             "  --host HOST --port PORT   Attach over TCP\n"
             "  --password-fd FD          Read attach password from fd\n"
             "  --log-json                Structured JSON logs on stderr (--server)\n"
             "  --headless-test           Init core and exit (CI)\n");
      return 1;
    }
  }
  return 0;
}

int traash_cli_collect_passwords(TraashCli *cli) {
  if (cli->encrypt && cli->create) {
    if (cli->write_pw[0] && cli->read_pw[0]) {
      return 0;
    }
    if (traash_password_prompt_pair("Write password", "Confirm write password", cli->write_pw,
                                    sizeof(cli->write_pw)) != 0) {
      return -1;
    }
    if (traash_password_prompt_pair("Read-only password", "Confirm read-only password",
                                    cli->read_pw, sizeof(cli->read_pw)) != 0) {
      return -1;
    }
    return 0;
  }
  if (cli->attach && traash_session_file_exists(cli->attach)) {
    if (cli->password[0]) {
      return 0;
    }
    if (cli->password_fd >= 0) {
      return traash_password_read_fd(cli->password_fd, cli->password, sizeof(cli->password));
    }
    return traash_password_prompt("Password", cli->password, sizeof(cli->password));
  }
  if (cli->attach && (cli->host || traash_mux_server_running())) {
    if (cli->password[0]) {
      return 0;
    }
    if (cli->password_fd >= 0) {
      return traash_password_read_fd(cli->password_fd, cli->password, sizeof(cli->password));
    }
    return traash_password_prompt("Password", cli->password, sizeof(cli->password));
  }
  return 0;
}

static int ipc_request(uint32_t type, const uint8_t *payload, uint32_t plen, char *out,
                       size_t outn) {
  char runtime[400];
  char sock[512];
  if (traash_runtime_dir(runtime, sizeof(runtime)) != 0) {
    return -1;
  }
  snprintf(sock, sizeof(sock), "%s/mux.sock", runtime);
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  uint8_t *frame = NULL;
  size_t frame_len = 0;
  if (traash_proto_encode(type, payload, plen, &frame, &frame_len) != 0) {
    close(fd);
    return -1;
  }
  if (write(fd, frame, frame_len) != (ssize_t)frame_len) {
    free(frame);
    close(fd);
    return -1;
  }
  free(frame);
  uint8_t buf[4096];
  ssize_t r = read(fd, buf, sizeof(buf));
  close(fd);
  TraashProtoFrame reply;
  size_t consumed = 0;
  if (r > 0 && traash_proto_decode(buf, (size_t)r, &reply, &consumed) == 0) {
    int type = (int)reply.type;
    if (out && outn && reply.payload && reply.len) {
      size_t n = reply.len < outn - 1 ? reply.len : outn - 1;
      memcpy(out, reply.payload, n);
      out[n] = 0;
    }
    traash_proto_frame_free(&reply);
    return type;
  }
  return -1;
}

static int create_encrypted_offline(const TraashCli *cli) {
  TraashSession *s = traash_session_create(1, cli->create, 80, 24);
  if (!s) {
    return 1;
  }
  s->encrypted = 1;
  if (traash_snapshot_save_encrypted(cli->create, s, cli->write_pw, cli->read_pw) != 0) {
    traash_session_destroy(s);
    return 1;
  }
  traash_session_destroy(s);
  printf("Created encrypted session '%s'\n", cli->create);
  return 0;
}

int traash_cli_run_headless(const TraashCli *cli) {
  if (cli->server_only) {
    return traash_server_run(cli);
  }
  if (cli->list_sessions) {
    char out[512];
    if (ipc_request(TRAASH_PROTO_LIST, NULL, 0, out, sizeof(out)) < 0) {
      TraashMuxServer srv;
      if (traash_mux_init(&srv, 80, 24) != 0) {
        return 1;
      }
      traash_mux_list(&srv, out, sizeof(out));
      printf("%s", out);
      traash_mux_shutdown(&srv);
      return 0;
    }
    printf("%s", out);
    return 0;
  }
  if (cli->create) {
    if (cli->encrypt) {
      if (traash_mux_server_running()) {
        uint8_t payload[512];
        uint16_t n = (uint16_t)strlen(cli->create);
        memcpy(payload, &n, 2);
        memcpy(payload + 2, cli->create, n);
        ipc_request(TRAASH_PROTO_CREATE, payload, 2 + n, NULL, 0);
        return 0;
      }
      return create_encrypted_offline(cli);
    }
    if (ipc_request(TRAASH_PROTO_CREATE, (const uint8_t *)cli->create,
                    (uint32_t)strlen(cli->create), NULL, 0) < 0) {
      TraashMuxServer srv;
      if (traash_mux_init(&srv, 80, 24) != 0) {
        return 1;
      }
      traash_mux_create_session(&srv, cli->create);
      traash_mux_shutdown(&srv);
    }
    return 0;
  }
  if (cli->headless_test) {
    TraashConfig cfg;
    TraashMuxServer srv;
    TraashTheme theme;
    if (traash_config_init(&cfg) != 0) {
      return 1;
    }
    traash_config_load(&cfg);
    if (traash_mux_init(&srv, 80, 24) != 0) {
      return 1;
    }
    traash_theme_load(cfg.lua, cfg.theme, &theme);
    if (cli->demo) {
      TraashDemo demo;
      traash_demo_start(&demo, &srv, cfg.lua, true);
      for (int i = 0; i < 5; i++) {
        traash_demo_update(&demo, &srv, cfg.lua, (double)i * 1.5);
        traash_mux_poll(&srv);
      }
      traash_demo_stop(&demo, &srv);
    }
    traash_mux_shutdown(&srv);
    traash_config_shutdown(&cfg);
    TRAASH_LOGI("headless-test ok");
    return 0;
  }
  return -1;
}
