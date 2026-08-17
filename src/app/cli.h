#ifndef TRAASH_CLI_H
#define TRAASH_CLI_H

#include <stdbool.h>

typedef struct {
  bool server_only;
  bool list_sessions;
  bool demo;
  bool demo_auto;
  bool headless_test;
  const char *attach;
  const char *create;
} TraashCli;

int traash_cli_parse(TraashCli *cli, int argc, char **argv);
int traash_cli_run_headless(const TraashCli *cli);

#endif
