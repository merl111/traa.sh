#include "app/shell_init.h"

#include <stdio.h>
#include <string.h>

static const char bash_snippet[] =
    "# traa.sh shell integration (OSC 133 + OSC 7)\n"
    "if [[ -n \"${TRAASH_SHELL_INTEGRATION:-}\" ]]; then\n"
    "  __traash_osc() { printf \"\\033]%s\\007\" \"$*\"; }\n"
    "  __traash_prompt() {\n"
    "    __traash_osc \"7;file://${HOSTNAME}${PWD}\"\n"
    "    __traash_osc \"133;A\"\n"
    "  }\n"
    "  __traash_preexec() { __traash_osc \"133;C\"; }\n"
    "  __traash_precmd() { __traash_osc \"133;D\"; __traash_prompt; }\n"
    "  if [[ -n \"${PROMPT_COMMAND:-}\" ]]; then\n"
    "    PROMPT_COMMAND=\"__traash_precmd;${PROMPT_COMMAND}\"\n"
    "  else\n"
    "    PROMPT_COMMAND=__traash_precmd\n"
    "  fi\n"
    "  trap '__traash_preexec' DEBUG\n"
    "fi\n";

static const char zsh_snippet[] =
    "# traa.sh shell integration (OSC 133 + OSC 7)\n"
    "if [[ -n \"${TRAASH_SHELL_INTEGRATION:-}\" ]]; then\n"
    "  __traash_osc() { print -Pn \"\\e]${1}\\a\"; }\n"
    "  __traash_precmd() {\n"
    "    __traash_osc \"133;D\"\n"
    "    __traash_osc \"7;file://${HOST}${PWD}\"\n"
    "    __traash_osc \"133;A\"\n"
    "  }\n"
    "  __traash_preexec() { __traash_osc \"133;C\"; }\n"
    "  autoload -Uz add-zsh-hook\n"
    "  add-zsh-hook precmd __traash_precmd\n"
    "  add-zsh-hook preexec __traash_preexec\n"
    "fi\n";

static const char fish_snippet[] =
    "# traa.sh shell integration (OSC 133 + OSC 7)\n"
    "if set -q TRAASH_SHELL_INTEGRATION\n"
    "  function __traash_osc -a code\n"
    "    printf \"\\033]%s\\007\" $code\n"
    "  end\n"
    "  function __traash_prompt --on-event fish_prompt\n"
    "    __traash_osc \"7;file://$hostname$PWD\"\n"
    "    __traash_osc \"133;A\"\n"
    "  end\n"
    "  function __traash_preexec --on-event fish_preexec\n"
    "    __traash_osc \"133;C\"\n"
    "  end\n"
    "  function __traash_postexec --on-event fish_postexec\n"
    "    __traash_osc \"133;D\"\n"
    "  end\n"
    "end\n";

int traash_shell_init_run(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: traash shell-init bash|zsh|fish\n");
    return 1;
  }
  const char *shell = argv[2];
  if (strcmp(shell, "bash") == 0) {
    fputs(bash_snippet, stdout);
    return 0;
  }
  if (strcmp(shell, "zsh") == 0) {
    fputs(zsh_snippet, stdout);
    return 0;
  }
  if (strcmp(shell, "fish") == 0) {
    fputs(fish_snippet, stdout);
    return 0;
  }
  fprintf(stderr, "traash shell-init: unknown shell '%s'\n", shell);
  return 1;
}
