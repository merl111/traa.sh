#include "app/app.h"
#include "app/cli.h"
#include "util/log.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  TraashCli cli;
  int help = traash_cli_parse(&cli, argc, argv);
  if (help == 1) {
    return 0;
  }
  if (getenv("TRAASH_DEBUG")) {
    traash_log_set_level(TRAASH_LOG_DEBUG);
  }

  int hr = traash_cli_run_headless(&cli);
  if (hr >= 0) {
    return hr;
  }
  if (cli.server_only) {
    /* GUI path still hosts server; for pure server use headless loop later */
    cli.headless_test = true;
    return traash_cli_run_headless(&cli);
  }
  return traash_app_run(&cli);
}
