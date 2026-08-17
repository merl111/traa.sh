#include "config/status.h"

#include "plugin/host.h"
#include "util/log.h"
#include "util/path.h"

#include <lauxlib.h>
#include <lua.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint32_t parse_hex_color(const char *s) {
  unsigned int v = 0xffffff;
  if (s && s[0] == '#') {
    sscanf(s + 1, "%x", &v);
  }
  return (uint32_t)v;
}

static float field_number(lua_State *L, int idx, const char *key, float fallback) {
  lua_getfield(L, idx, key);
  float v = fallback;
  if (lua_isnumber(L, -1)) {
    v = (float)lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return v;
}

int traash_status_render(void *lua_state, const char *style, const TraashSession *session,
                         TraashStatusModel *out) {
  memset(out, 0, sizeof(*out));
  lua_State *L = (lua_State *)lua_state;
  char lua_dir[512];
  char path[640];
  traash_lua_dir(lua_dir, sizeof(lua_dir));
  snprintf(path, sizeof(path), "%s/status/%s.lua", lua_dir, style ? style : "minimal");

  if (luaL_dofile(L, path) != LUA_OK) {
    TRAASH_LOGW("status style: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    /* fallback */
    out->count = 1;
    snprintf(out->segs[0].text, sizeof(out->segs[0].text), "[%s]",
             session ? session->name : "traash");
    snprintf(out->segs[0].align, sizeof(out->segs[0].align), "left");
    return 0;
  }

  /* Module should return a table or a function */
  if (lua_isfunction(L, -1)) {
    lua_newtable(L);
    lua_pushstring(L, session ? session->name : "default");
    lua_setfield(L, -2, "session");
    const char *title = "";
    const char *cwd = "";
    if (session && session->active) {
      lua_pushinteger(L, session->active->id);
      lua_setfield(L, -2, "window");
      TraashPane *p = traash_session_active_pane((TraashSession *)session);
      title = (p && p->title[0]) ? p->title : "";
      cwd = (p && p->screen.cwd[0]) ? p->screen.cwd : "";
      lua_pushstring(L, title);
      lua_setfield(L, -2, "title");
      lua_pushstring(L, cwd);
      lua_setfield(L, -2, "cwd");
    } else {
      lua_pushstring(L, "");
      lua_setfield(L, -2, "title");
      lua_pushstring(L, "");
      lua_setfield(L, -2, "cwd");
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char timestr[32];
    strftime(timestr, sizeof(timestr), "%H:%M", &tm);
    lua_pushstring(L, timestr);
    lua_setfield(L, -2, "time");
    char host[64] = "localhost";
    gethostname(host, sizeof(host));
    lua_pushstring(L, host);
    lua_setfield(L, -2, "host");

    /* Keep traash.ctx in sync so segment helpers can read title/cwd */
    lua_getglobal(L, "traash");
    if (lua_istable(L, -1)) {
      lua_getfield(L, -1, "ctx");
      if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "ctx");
      }
      lua_pushstring(L, title);
      lua_setfield(L, -2, "title");
      lua_pushstring(L, cwd);
      lua_setfield(L, -2, "cwd");
      lua_pop(L, 2); /* ctx, traash */
    } else {
      lua_pop(L, 1);
    }

    traash_plugins_fill_segments(L);

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
      TRAASH_LOGW("status render: %s", lua_tostring(L, -1));
      lua_pop(L, 1);
      return -1;
    }
  }

  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return -1;
  }

  /* Optional pill layout knobs on the returned table */
  out->gap = field_number(L, -1, "gap", 0.0f);
  out->radius = field_number(L, -1, "radius", 0.0f);
  out->pad_x = field_number(L, -1, "pad_x", 0.0f);
  out->v_pad = field_number(L, -1, "v_pad", 0.0f);
  lua_getfield(L, -1, "style");
  if (lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "pills") == 0) {
    if (out->gap <= 0.0f) {
      out->gap = 8.0f;
    }
    if (out->radius <= 0.0f) {
      out->radius = 10.0f;
    }
    if (out->pad_x <= 0.0f) {
      out->pad_x = 12.0f;
    }
    if (out->v_pad <= 0.0f) {
      out->v_pad = 6.0f;
    }
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  while (lua_next(L, -2) != 0 && out->count < 32) {
    if (lua_istable(L, -1)) {
      TraashStatusSegment *seg = &out->segs[out->count];
      lua_getfield(L, -1, "text");
      snprintf(seg->text, sizeof(seg->text), "%s",
               lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
      lua_pop(L, 1);
      lua_getfield(L, -1, "align");
      snprintf(seg->align, sizeof(seg->align), "%s",
               lua_isstring(L, -1) ? lua_tostring(L, -1) : "left");
      lua_pop(L, 1);
      lua_getfield(L, -1, "fg");
      seg->fg = lua_isstring(L, -1) ? parse_hex_color(lua_tostring(L, -1)) : 0xffffff;
      lua_pop(L, 1);
      lua_getfield(L, -1, "bg");
      seg->bg = lua_isstring(L, -1) ? parse_hex_color(lua_tostring(L, -1)) : 0x000000;
      lua_pop(L, 1);
      out->count++;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return 0;
}
