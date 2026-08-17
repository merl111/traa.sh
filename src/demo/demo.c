#include "demo/demo.h"

#include "term/pty.h"
#include "util/log.h"
#include "util/path.h"

#include <lauxlib.h>
#include <lua.h>
#include <stdio.h>
#include <string.h>

int traash_demo_start(TraashDemo *demo, TraashMuxServer *srv, void *lua, bool auto_mode) {
  memset(demo, 0, sizeof(*demo));
  demo->active = true;
  demo->auto_mode = auto_mode;
  demo->step = 0;
  demo->next_tick = 0;
  demo->prev = srv->attached;

  TraashSession *existing = traash_mux_find_session(srv, "demo");
  if (existing) {
    /* recreate clean */
    /* leave existing; attach */
  } else {
    traash_mux_create_session(srv, "demo");
  }
  traash_mux_attach(srv, "demo");

  lua_State *L = (lua_State *)lua;
  char lua_dir[512];
  char path[640];
  traash_lua_dir(lua_dir, sizeof(lua_dir));
  snprintf(path, sizeof(path), "%s/demo/run.lua", lua_dir);
  if (luaL_dofile(L, path) != LUA_OK) {
    TRAASH_LOGW("demo script: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return -1;
  }
  if (lua_istable(L, -1)) {
    lua_setglobal(L, "demo_module");
  } else {
    lua_pop(L, 1);
  }
  TRAASH_LOGI("demo mode started");
  return traash_demo_update(demo, srv, lua, 0);
}

void traash_demo_stop(TraashDemo *demo, TraashMuxServer *srv) {
  if (!demo->active) {
    return;
  }
  demo->active = false;
  if (demo->prev) {
    srv->attached = demo->prev;
  } else {
    traash_mux_attach(srv, "default");
  }
  TRAASH_LOGI("demo mode stopped");
}

static int run_step(TraashDemo *demo, TraashMuxServer *srv, void *lua) {
  lua_State *L = (lua_State *)lua;
  lua_getglobal(L, "demo_module");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "steps");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }
  lua_len(L, -1);
  int nsteps = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (demo->step >= nsteps) {
    lua_pop(L, 2);
    return 0;
  }
  lua_rawgeti(L, -1, demo->step + 1);
  TraashPane *pane = traash_session_active_pane(srv->attached);
  if (lua_isfunction(L, -1) && pane && pane->pty.master_fd >= 0) {
    lua_pushinteger(L, demo->step + 1);
    if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
      if (lua_isstring(L, -1)) {
        const char *out = lua_tostring(L, -1);
        traash_pty_write(&pane->pty, (const uint8_t *)out, strlen(out));
      }
      lua_pop(L, 1);
    } else {
      TRAASH_LOGW("demo step: %s", lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  } else if (lua_isstring(L, -1) && pane) {
    const char *out = lua_tostring(L, -1);
    traash_pty_write(&pane->pty, (const uint8_t *)out, strlen(out));
    lua_pop(L, 1);
  } else {
    lua_pop(L, 1);
  }
  lua_pop(L, 2);
  return 1;
}

int traash_demo_update(TraashDemo *demo, TraashMuxServer *srv, void *lua, double now) {
  if (!demo->active) {
    return 0;
  }
  if (demo->auto_mode) {
    if (now >= demo->next_tick) {
      if (!run_step(demo, srv, lua)) {
        traash_demo_stop(demo, srv);
        return 0;
      }
      demo->step++;
      demo->next_tick = now + 1.2;
    }
  }
  return 1;
}

int traash_demo_key(TraashDemo *demo, TraashMuxServer *srv, void *lua, int key) {
  if (!demo->active) {
    return 0;
  }
  if (key == 'q' || key == 'Q') {
    traash_demo_stop(demo, srv);
    return 1;
  }
  if (key == 'n' || key == 'N' || key == ' ') {
    if (!run_step(demo, srv, lua)) {
      traash_demo_stop(demo, srv);
    } else {
      demo->step++;
    }
    return 1;
  }
  if (key == 'p' || key == 'P') {
    if (demo->step > 0) {
      demo->step--;
    }
    return 1;
  }
  return 0;
}
