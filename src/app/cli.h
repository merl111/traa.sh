#ifndef TRAASH_CLI_H
#define TRAASH_CLI_H

#include <stdbool.h>

typedef struct {
  bool server_only;
  bool list_sessions;
  bool demo;
  bool demo_auto;
  bool headless_test;
  bool encrypt;
  bool read_only;
  bool log_json;
  const char *attach;
  const char *create;
  const char *bind;
  const char *host;
  int port;
  int password_fd;
  char write_pw[256];
  char read_pw[256];
  char password[256];
} TraashCli;

int traash_cli_parse(TraashCli *cli, int argc, char **argv);
int traash_cli_run_headless(const TraashCli *cli);
int traash_cli_collect_passwords(TraashCli *cli);

#endif
