#ifndef TRAASH_CONFIG_H
#define TRAASH_CONFIG_H

#include "input/keymap.h"

#include <stdbool.h>

typedef struct {
  char theme[64];
  char status_bar[64];
  char font_family[128];
  float font_size;
  float opacity;
  int cursor_style; /* 0 block, 1 beam, 2 underline */
  int scrollback_lines;
  char default_layout[64]; /* empty = single pane; else layout name */
  char plugins[16][64];
  int plugin_count;
  bool loaded;
  void *lua; /* lua_State* */
} TraashConfig;

int traash_config_init(TraashConfig *cfg);
void traash_config_shutdown(TraashConfig *cfg);
int traash_config_load(TraashConfig *cfg);
int traash_config_reload(TraashConfig *cfg);
int traash_config_save(const TraashConfig *cfg, const TraashKeymap *km);
/* Apply config.keys table from loaded Lua state into keymap (if present). */
int traash_config_apply_keys(TraashConfig *cfg, TraashKeymap *km);

#endif
