#include "config/theme.h"

#include "util/log.h"
#include "util/path.h"

#include <lauxlib.h>
#include <lua.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_hex(const char *s, uint32_t *out) {
  if (!s || s[0] != '#') {
    return -1;
  }
  unsigned int v = 0;
  if (sscanf(s + 1, "%x", &v) != 1) {
    return -1;
  }
  *out = (uint32_t)v;
  return 0;
}

static uint32_t field_color(lua_State *L, const char *key, uint32_t defv) {
  lua_getfield(L, -1, key);
  uint32_t v = defv;
  if (lua_isstring(L, -1)) {
    parse_hex(lua_tostring(L, -1), &v);
  }
  lua_pop(L, 1);
  return v;
}

int traash_theme_load(void *lua_state, const char *name, TraashTheme *out) {
  lua_State *L = (lua_State *)lua_state;
  memset(out, 0, sizeof(*out));
  if (!name || !name[0]) {
    name = "tokyo-night";
  }
  snprintf(out->name, sizeof(out->name), "%s", name);

  char lua_dir[512];
  char path[640];
  traash_lua_dir(lua_dir, sizeof(lua_dir));

  /* user override */
  char conf[512];
  if (traash_config_dir(conf, sizeof(conf)) == 0) {
    snprintf(path, sizeof(path), "%s/themes/%s.lua", conf, name);
    if (luaL_dofile(L, path) == LUA_OK) {
      goto loaded;
    }
    lua_pop(L, 1);
  }

  snprintf(path, sizeof(path), "%s/themes/%s.lua", lua_dir, name);
  if (luaL_dofile(L, path) != LUA_OK) {
    TRAASH_LOGW("theme load failed: %s (%s)", name, lua_tostring(L, -1));
    lua_pop(L, 1);
    /* fallback traash-dark hardcode */
    out->foreground = 0xc0caf5;
    out->background = 0x1a1b26;
    out->cursor = 0x7dcfff;
    out->cursor_text = 0x1a1b26;
    out->selection_fg = 0xc0caf5;
    out->selection_bg = 0x33467c;
    out->ansi[0] = 0x15161e;
    out->ansi[1] = 0xf7768e;
    out->ansi[2] = 0x9ece6a;
    out->ansi[3] = 0xe0af68;
    out->ansi[4] = 0x7aa2f7;
    out->ansi[5] = 0xbb9af7;
    out->ansi[6] = 0x7dcfff;
    out->ansi[7] = 0xa9b1d6;
    out->ansi[8] = 0x414868;
    out->ansi[9] = 0xf7768e;
    out->ansi[10] = 0x9ece6a;
    out->ansi[11] = 0xe0af68;
    out->ansi[12] = 0x7aa2f7;
    out->ansi[13] = 0xbb9af7;
    out->ansi[14] = 0x7dcfff;
    out->ansi[15] = 0xc0caf5;
    out->tab_bar = 0x16161e;
    out->active_tab = 0x7aa2f7;
    out->inactive_tab = 0x414868;
    out->pane_border = 0x414868;
    out->active_pane_border = 0x7aa2f7;
    out->status_bar_fg = 0xa9b1d6;
    out->status_bar_bg = 0x16161e;
    return 0;
  }

loaded:
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return -1;
  }
  out->foreground = field_color(L, "foreground", 0xc0caf5);
  out->background = field_color(L, "background", 0x1a1b26);
  out->cursor = field_color(L, "cursor", 0x7dcfff);
  out->cursor_text = field_color(L, "cursor_text", 0x1a1b26);
  out->selection_fg = field_color(L, "selection_fg", out->foreground);
  out->selection_bg = field_color(L, "selection_bg", 0x33467c);
  lua_getfield(L, -1, "ansi");
  if (lua_istable(L, -1)) {
    for (int i = 0; i < 16; i++) {
      lua_rawgeti(L, -1, i);
      if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        lua_rawgeti(L, -1, i + 1);
      }
      if (lua_isstring(L, -1)) {
        parse_hex(lua_tostring(L, -1), &out->ansi[i]);
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  out->tab_bar = field_color(L, "tab_bar", out->background);
  out->active_tab = field_color(L, "active_tab", out->ansi[4]);
  out->inactive_tab = field_color(L, "inactive_tab", out->ansi[8]);
  out->pane_border = field_color(L, "pane_border", out->ansi[8]);
  out->active_pane_border = field_color(L, "active_pane_border", out->ansi[4]);
  out->status_bar_fg = field_color(L, "status_bar_fg", out->foreground);
  out->status_bar_bg = field_color(L, "status_bar_bg", out->tab_bar);
  lua_pop(L, 1);
  return 0;
}

uint32_t traash_theme_resolve_fg(const TraashTheme *t, uint32_t cell_fg, int truecolor) {
  if (truecolor) {
    return cell_fg;
  }
  if (cell_fg < 16) {
    return t->ansi[cell_fg];
  }
  return t->foreground;
}

uint32_t traash_theme_resolve_bg(const TraashTheme *t, uint32_t cell_bg, int truecolor) {
  if (truecolor) {
    return cell_bg;
  }
  if (cell_bg < 16) {
    return t->ansi[cell_bg];
  }
  return t->background;
}
