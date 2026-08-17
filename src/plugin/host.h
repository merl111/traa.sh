#ifndef TRAASH_PLUGIN_HOST_H
#define TRAASH_PLUGIN_HOST_H

#include "config/config.h"

struct TraashMuxServer;

typedef struct {
  char name[64];
  int enabled;
  int failed;
} TraashPluginInfo;

typedef struct {
  TraashPluginInfo plugins[32];
  int count;
  void *lua;
  struct TraashMuxServer *mux;
  char theme_name[64];
  int reload_theme;
  char clipboard_override[65536];
  int has_clipboard_override;
  int last_focus_pane_id;
  char pending_action[64];
} TraashPluginHost;

int traash_plugins_init(TraashPluginHost *host, TraashConfig *cfg);
void traash_plugins_shutdown(TraashPluginHost *host);
void traash_plugins_set_mux(TraashPluginHost *host, struct TraashMuxServer *mux);
void traash_plugins_set_theme(TraashPluginHost *host, const char *theme);
void traash_plugins_set_ctx(TraashPluginHost *host, const char *title, const char *cwd);
void traash_plugins_emit(TraashPluginHost *host, const char *event);
void traash_plugins_emit_str(TraashPluginHost *host, const char *event, const char *arg);
/* Fill ctx.seg from traash.segments; expects ctx table at Lua stack top. */
void traash_plugins_fill_segments(void *lua_state);

#endif
