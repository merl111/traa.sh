#include "plugin/host.h"

#include "mux/server.h"
#include "util/log.h"
#include "util/path.h"

#include <lauxlib.h>
#include <lua.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if LUA_VERSION_NUM >= 502
#define TRAASH_RAWLEN(L, i) lua_rawlen(L, i)
#else
#define TRAASH_RAWLEN(L, i) lua_objlen(L, i)
#endif

static TraashPluginHost *g_host;
static unsigned long g_notification_id;

static int api_log(lua_State *L) {
  const char *msg = luaL_checkstring(L, 1);
  TRAASH_LOGI("[plugin] %s", msg);
  return 0;
}

static int api_notify(lua_State *L) {
  const char *msg = luaL_checkstring(L, 1);
  TRAASH_LOGI("[notify] %s", msg);
#if defined(__APPLE__)
  char esc[512];
  size_t o = 0;
  for (const char *p = msg; *p && o + 2 < sizeof(esc); p++) {
    if (*p == '\n' || *p == '\r') {
      esc[o++] = ' ';
      continue;
    }
    if (*p == '\\' || *p == '"') {
      esc[o++] = '\\';
    }
    esc[o++] = (char)*p;
  }
  esc[o] = 0;
  char full[768];
  snprintf(full, sizeof(full),
           "osascript -e 'display notification \"%s\" with title \"traa.sh\"' "
           ">/dev/null 2>&1",
           esc);
  (void)system(full);
  return 0;
#else
  if (access("/usr/bin/notify-send", X_OK) != 0 && access("/bin/notify-send", X_OK) != 0) {
    return 0;
  }
  char esc[512];
  size_t o = 0;
  esc[o++] = '\'';
  for (const char *p = msg; *p && o + 4 < sizeof(esc); p++) {
    if (*p == '\'') {
      memcpy(esc + o, "'\\''", 4);
      o += 4;
    } else {
      esc[o++] = *p;
    }
  }
  esc[o++] = '\'';
  esc[o] = 0;
  char replace[64] = "";
  if (g_notification_id > 0) {
    snprintf(replace, sizeof(replace), "-r %lu", g_notification_id);
  }
  char full[768];
  snprintf(full, sizeof(full),
           "notify-send -p %s -a traa.sh -t 3500 -h boolean:transient:true "
           "-h string:x-canonical-private-synchronous:traash "
           "-h string:x-dunst-stack-tag:traash traa.sh %s 2>/dev/null",
           replace, esc);
  FILE *pipe = popen(full, "r");
  if (pipe) {
    char id[64];
    if (fgets(id, sizeof(id), pipe)) {
      unsigned long parsed = strtoul(id, NULL, 10);
      if (parsed > 0) {
        g_notification_id = parsed;
      }
    }
    (void)pclose(pipe);
  }
  return 0;
#endif
}

static int api_on(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_getglobal(L, "traash");
  lua_getfield(L, -1, "hooks");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return luaL_error(L, "traash.hooks missing");
  }
  lua_getfield(L, -1, name);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, 1);
    lua_setfield(L, -2, name);
  } else if (lua_isfunction(L, -1)) {
    lua_newtable(L);
    lua_pushvalue(L, -2); /* old fn */
    lua_rawseti(L, -2, 1);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, 2);
    lua_setfield(L, -3, name);
    lua_pop(L, 1); /* old fn */
  } else if (lua_istable(L, -1)) {
    int n = (int)TRAASH_RAWLEN(L, -1);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, n + 1);
    lua_pop(L, 1);
  } else {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, 1);
    lua_setfield(L, -2, name);
  }
  lua_pop(L, 2); /* hooks, traash */
  return 0;
}

static int api_set_clipboard(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  if (!g_host) {
    return 0;
  }
  snprintf(g_host->clipboard_override, sizeof(g_host->clipboard_override), "%s", s);
  g_host->has_clipboard_override = 1;
  return 0;
}

static int api_theme_path(lua_State *L) {
  char lua_dir[512];
  char path[640];
  traash_lua_dir(lua_dir, sizeof(lua_dir));
  const char *theme = (g_host && g_host->theme_name[0]) ? g_host->theme_name : "tokyo-night";
  snprintf(path, sizeof(path), "%s/themes/%s.lua", lua_dir, theme);
  lua_pushstring(L, path);
  return 1;
}

static int api_reload_theme(lua_State *L) {
  (void)L;
  if (g_host) {
    g_host->reload_theme = 1;
  }
  return 0;
}

static int api_sessions(lua_State *L) {
  lua_newtable(L);
  if (!g_host || !g_host->mux) {
    return 1;
  }
  int i = 1;
  for (TraashSession *s = g_host->mux->sessions; s; s = s->next) {
    lua_pushstring(L, s->name);
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

static int api_run_action(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  if (g_host) {
    snprintf(g_host->pending_action, sizeof(g_host->pending_action), "%s", name);
  }
  return 0;
}

static void register_api(lua_State *L) {
  lua_newtable(L);
  lua_pushcfunction(L, api_log);
  lua_setfield(L, -2, "log");
  lua_pushcfunction(L, api_notify);
  lua_setfield(L, -2, "notify");
  lua_pushcfunction(L, api_on);
  lua_setfield(L, -2, "on");
  lua_pushcfunction(L, api_set_clipboard);
  lua_setfield(L, -2, "set_clipboard");
  lua_pushcfunction(L, api_theme_path);
  lua_setfield(L, -2, "theme_path");
  lua_pushcfunction(L, api_reload_theme);
  lua_setfield(L, -2, "reload_theme");
  lua_pushcfunction(L, api_sessions);
  lua_setfield(L, -2, "sessions");
  lua_pushcfunction(L, api_run_action);
  lua_setfield(L, -2, "run_action");
  lua_newtable(L);
  lua_setfield(L, -2, "segments");
  lua_newtable(L);
  lua_setfield(L, -2, "hooks");
  lua_newtable(L);
  lua_setfield(L, -2, "ctx");
  lua_setglobal(L, "traash");
}

static void emit_hook_value(lua_State *L, const char *event, int with_arg, const char *arg) {
  if (lua_isfunction(L, -1)) {
    int nargs = 0;
    if (with_arg) {
      lua_pushstring(L, arg ? arg : "");
      nargs = 1;
    }
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
      TRAASH_LOGW("hook %s: %s", event, lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  } else if (lua_istable(L, -1)) {
    int n = (int)TRAASH_RAWLEN(L, -1);
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(L, -1, i);
      if (lua_isfunction(L, -1)) {
        int nargs = 0;
        if (with_arg) {
          lua_pushstring(L, arg ? arg : "");
          nargs = 1;
        }
        if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
          TRAASH_LOGW("hook %s[%d]: %s", event, i, lua_tostring(L, -1));
          lua_pop(L, 1);
        }
      } else {
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
  } else {
    lua_pop(L, 1);
  }
}

static void emit_inner(TraashPluginHost *host, const char *event, int with_arg, const char *arg) {
  lua_State *L = (lua_State *)host->lua;
  if (!L) {
    return;
  }
  lua_getglobal(L, "traash");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_getfield(L, -1, "hooks");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return;
  }
  lua_getfield(L, -1, event);
  emit_hook_value(L, event, with_arg, arg);
  lua_pop(L, 2); /* hooks, traash */
}

int traash_plugins_init(TraashPluginHost *host, TraashConfig *cfg) {
  memset(host, 0, sizeof(*host));
  host->lua = cfg->lua;
  host->last_focus_pane_id = -1;
  snprintf(host->theme_name, sizeof(host->theme_name), "%s",
           cfg->theme[0] ? cfg->theme : "tokyo-night");
  g_host = host;
  lua_State *L = (lua_State *)host->lua;
  register_api(L);

  char lua_dir[512];
  traash_lua_dir(lua_dir, sizeof(lua_dir));

  for (int i = 0; i < cfg->plugin_count && host->count < 32; i++) {
    TraashPluginInfo *info = &host->plugins[host->count];
    snprintf(info->name, sizeof(info->name), "%s", cfg->plugins[i]);
    info->enabled = 1;
    char path[640];
    snprintf(path, sizeof(path), "%s/plugins/examples/%s/init.lua", lua_dir, info->name);
    if (luaL_dofile(L, path) != LUA_OK) {
      TRAASH_LOGW("plugin %s failed: %s", info->name, lua_tostring(L, -1));
      lua_pop(L, 1);
      info->failed = 1;
      info->enabled = 0;
    } else {
      TRAASH_LOGI("loaded plugin %s", info->name);
      if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
          TRAASH_LOGW("plugin setup %s: %s", info->name, lua_tostring(L, -1));
          lua_pop(L, 1);
          info->failed = 1;
        }
      } else if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "setup");
        if (lua_isfunction(L, -1)) {
          if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            TRAASH_LOGW("plugin setup %s: %s", info->name, lua_tostring(L, -1));
            lua_pop(L, 1);
            info->failed = 1;
          }
        } else {
          lua_pop(L, 1);
        }
        lua_pop(L, 1);
      } else {
        lua_pop(L, 1);
      }
    }
    host->count++;
  }
  return 0;
}

void traash_plugins_shutdown(TraashPluginHost *host) {
  if (g_host == host) {
    g_host = NULL;
  }
  memset(host, 0, sizeof(*host));
}

void traash_plugins_set_mux(TraashPluginHost *host, TraashMuxServer *mux) {
  if (host) {
    host->mux = mux;
  }
}

void traash_plugins_set_theme(TraashPluginHost *host, const char *theme) {
  if (!host) {
    return;
  }
  snprintf(host->theme_name, sizeof(host->theme_name), "%s", theme ? theme : "tokyo-night");
}

void traash_plugins_set_ctx(TraashPluginHost *host, const char *title, const char *cwd) {
  lua_State *L = host ? (lua_State *)host->lua : NULL;
  if (!L) {
    return;
  }
  lua_getglobal(L, "traash");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_getfield(L, -1, "ctx");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "ctx");
  }
  lua_pushstring(L, title ? title : "");
  lua_setfield(L, -2, "title");
  lua_pushstring(L, cwd ? cwd : "");
  lua_setfield(L, -2, "cwd");
  lua_pop(L, 2);
}

void traash_plugins_emit(TraashPluginHost *host, const char *event) {
  if (!host || !event) {
    return;
  }
  emit_inner(host, event, 0, NULL);
}

void traash_plugins_emit_str(TraashPluginHost *host, const char *event, const char *arg) {
  if (!host || !event) {
    return;
  }
  emit_inner(host, event, 1, arg);
}

void traash_plugins_fill_segments(void *lua_state) {
  lua_State *L = (lua_State *)lua_state;
  if (!L) {
    return;
  }
  /* Expect ctx table at stack top. Build ctx.seg from traash.segments. */
  lua_newtable(L);
  int seg_idx = lua_gettop(L);

  lua_getglobal(L, "traash");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushvalue(L, seg_idx);
    lua_setfield(L, seg_idx - 1, "seg");
    lua_remove(L, seg_idx);
    return;
  }
  lua_getfield(L, -1, "segments");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    lua_pushvalue(L, seg_idx);
    lua_setfield(L, seg_idx - 1, "seg");
    lua_remove(L, seg_idx);
    return;
  }

  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1)) {
      const char *name = lua_tostring(L, -2);
      lua_pushvalue(L, -1);
      if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
        if (lua_isstring(L, -1) && lua_tostring(L, -1)[0]) {
          lua_pushvalue(L, -1);
          lua_setfield(L, seg_idx, name);
        }
        lua_pop(L, 1);
      } else {
        TRAASH_LOGW("segment %s: %s", name, lua_tostring(L, -1));
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 2); /* segments, traash */

  lua_pushvalue(L, seg_idx);
  lua_setfield(L, seg_idx - 1, "seg");
  lua_remove(L, seg_idx);
}
