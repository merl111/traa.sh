#include "app/app.h"
#include "app/agent_cli.h"
#include "app/cli.h"
#include "app/shell_init.h"
#include "util/log.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc >= 2 && strcmp(argv[1], "agent") == 0) {
    return traash_agent_cli_run(argc, argv);
  }
  if (argc >= 2 && strcmp(argv[1], "shell-init") == 0) {
    return traash_shell_init_run(argc, argv);
  }
  TraashCli cli;
  int help = traash_cli_parse(&cli, argc, argv);
  if (help == 1) {
    return 0;
  }
  if (getenv("TRAASH_DEBUG")) {
    traash_log_set_level(TRAASH_LOG_DEBUG);
  }

  if (traash_cli_collect_passwords(&cli) != 0) {
    return 1;
  }

  int hr = traash_cli_run_headless(&cli);
  if (hr >= 0) {
    return hr;
  }
  return traash_app_run(&cli);
}
