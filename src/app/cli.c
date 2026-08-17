#include "app/cli.h"

#include "config/config.h"
#include "config/theme.h"
#include "demo/demo.h"
#include "mux/ipc.h"
#include "mux/server.h"
#include "util/log.h"
#include "util/path.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int traash_cli_parse(TraashCli *cli, int argc, char **argv) {
  memset(cli, 0, sizeof(*cli));
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
    } else if (strcmp(argv[i], "--attach") == 0 && i + 1 < argc) {
      cli->attach = argv[++i];
    } else if (strcmp(argv[i], "--create") == 0 && i + 1 < argc) {
      cli->create = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("traa.sh — modern terminal emulator\n"
             "Usage: traash [options]\n"
             "  --demo [--auto]     Start capability demo\n"
             "  --server            Run mux server only\n"
             "  --list-sessions     List sessions via mux socket\n"
             "  --attach NAME       Attach session name\n"
             "  --create NAME       Create session\n"
             "  --headless-test     Init core and exit (CI)\n");
      return 1;
    }
  }
  return 0;
}

static int ipc_request(uint32_t type, const char *payload, char *out, size_t outn) {
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
  TraashIpcMsg msg = {.type = type};
  if (payload) {
    snprintf(msg.payload, sizeof(msg.payload), "%s", payload);
    msg.len = (uint32_t)strlen(msg.payload);
  }
  uint8_t buf[1024];
  size_t n = 0;
  traash_ipc_encode(&msg, buf, sizeof(buf), &n);
  write(fd, buf, n);
  ssize_t r = read(fd, buf, sizeof(buf));
  close(fd);
  TraashIpcMsg reply;
  if (r > 0 && traash_ipc_decode(buf, (size_t)r, &reply) == 0) {
    if (out && outn) {
      snprintf(out, outn, "%s", reply.payload);
    }
    return (int)reply.type;
  }
  return -1;
}

int traash_cli_run_headless(const TraashCli *cli) {
  if (cli->list_sessions) {
    char out[512];
    if (ipc_request(TRAASH_IPC_LIST, NULL, out, sizeof(out)) < 0) {
      /* local fallback */
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
    ipc_request(TRAASH_IPC_CREATE, cli->create, NULL, 0);
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
  return -1; /* continue to GUI */
}
