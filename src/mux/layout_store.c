#include "mux/layout_store.h"

#include "mux/pane.h"
#include "mux/window.h"
#include "util/log.h"
#include "util/path.h"

#include <dirent.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if LUA_VERSION_NUM >= 502
#define TRAASH_RAWLEN(L, i) lua_rawlen(L, i)
#else
#define TRAASH_RAWLEN(L, i) lua_objlen(L, i)
#endif

int traash_layout_name_valid(const char *name) {
  if (!name || !name[0] || strlen(name) >= TRAASH_LAYOUT_NAME_LEN) {
    return 0;
  }
  for (const char *p = name; *p; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
        *p == '-' || *p == '_') {
      continue;
    }
    return 0;
  }
  return 1;
}

static int ensure_user_layouts_dir(char *out, size_t n) {
  char conf[512];
  if (traash_config_dir(conf, sizeof(conf)) != 0) {
    return -1;
  }
  snprintf(out, n, "%s/layouts", conf);
  struct stat st;
  if (stat(out, &st) == 0) {
    return S_ISDIR(st.st_mode) ? 0 : -1;
  }
  return mkdir(out, 0700);
}

static void strip_lua_ext(char *name) {
  size_t n = strlen(name);
  if (n > 4 && strcmp(name + n - 4, ".lua") == 0) {
    name[n - 4] = 0;
  }
}

static int find_name(const TraashLayoutName *list, int count, const char *name) {
  for (int i = 0; i < count; i++) {
    if (strcmp(list[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

static int scan_dir(const char *dir, int user, TraashLayoutName *out, int max_out, int count) {
  DIR *d = opendir(dir);
  if (!d) {
    return count;
  }
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && count < max_out) {
    if (ent->d_name[0] == '.') {
      continue;
    }
    size_t n = strlen(ent->d_name);
    if (n < 5 || strcmp(ent->d_name + n - 4, ".lua") != 0) {
      continue;
    }
    char name[TRAASH_LAYOUT_NAME_LEN];
    snprintf(name, sizeof(name), "%s", ent->d_name);
    strip_lua_ext(name);
    if (!traash_layout_name_valid(name)) {
      continue;
    }
    int idx = find_name(out, count, name);
    if (idx >= 0) {
      if (user) {
        out[idx].user = 1;
      }
      continue;
    }
    snprintf(out[count].name, sizeof(out[count].name), "%s", name);
    out[count].user = user;
    count++;
  }
  closedir(d);
  return count;
}

int traash_layout_store_scan(TraashLayoutName *out, int max_out) {
  if (!out || max_out <= 0) {
    return 0;
  }
  int count = 0;
  char lua_dir[512];
  char path[640];
  if (traash_lua_dir(lua_dir, sizeof(lua_dir)) == 0) {
    snprintf(path, sizeof(path), "%s/layouts", lua_dir);
    count = scan_dir(path, 0, out, max_out, count);
  }
  if (ensure_user_layouts_dir(path, sizeof(path)) == 0) {
    count = scan_dir(path, 1, out, max_out, count);
  }
  /* Stable-ish alphabetical order */
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      if (strcmp(out[j].name, out[i].name) < 0) {
        TraashLayoutName tmp = out[i];
        out[i] = out[j];
        out[j] = tmp;
      }
    }
  }
  return count;
}

static int read_float_field(lua_State *L, const char *key, float *dst, float defv) {
  lua_getfield(L, -1, key);
  if (lua_isnumber(L, -1)) {
    *dst = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return 0;
  }
  *dst = defv;
  lua_pop(L, 1);
  return -1;
}

static int parse_layout_table(lua_State *L, TraashLayoutDesc *out) {
  memset(out, 0, sizeof(*out));
  lua_getfield(L, -1, "name");
  if (lua_isstring(L, -1)) {
    snprintf(out->name, sizeof(out->name), "%s", lua_tostring(L, -1));
  }
  lua_pop(L, 1);

  lua_getfield(L, -1, "windows");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return -1;
  }
  int nwin = (int)TRAASH_RAWLEN(L, -1);
  if (nwin > TRAASH_LAYOUT_MAX_WINDOWS) {
    nwin = TRAASH_LAYOUT_MAX_WINDOWS;
  }
  for (int wi = 1; wi <= nwin; wi++) {
    lua_rawgeti(L, -1, wi);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    TraashLayoutWindow *w = &out->windows[out->window_count];
    memset(w, 0, sizeof(*w));
    lua_getfield(L, -1, "name");
    if (lua_isstring(L, -1)) {
      snprintf(w->name, sizeof(w->name), "%s", lua_tostring(L, -1));
    } else {
      snprintf(w->name, sizeof(w->name), "%d", out->window_count + 1);
    }
    lua_pop(L, 1);
    lua_getfield(L, -1, "active");
    w->active = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : 1;
    lua_pop(L, 1);
    if (w->active < 1) {
      w->active = 1;
    }

    lua_getfield(L, -1, "panes");
    if (lua_istable(L, -1)) {
      int np = (int)TRAASH_RAWLEN(L, -1);
      if (np > TRAASH_LAYOUT_MAX_PANES) {
        np = TRAASH_LAYOUT_MAX_PANES;
      }
      for (int pi = 1; pi <= np; pi++) {
        lua_rawgeti(L, -1, pi);
        if (lua_istable(L, -1)) {
          TraashLayoutPane *p = &w->panes[w->pane_count];
          read_float_field(L, "x", &p->x, 0.0f);
          read_float_field(L, "y", &p->y, 0.0f);
          read_float_field(L, "w", &p->w, 1.0f);
          read_float_field(L, "h", &p->h, 1.0f);
          if (p->w < 0.05f) {
            p->w = 0.05f;
          }
          if (p->h < 0.05f) {
            p->h = 0.05f;
          }
          w->pane_count++;
        }
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1); /* panes */
    lua_pop(L, 1); /* window */
    if (w->pane_count < 1) {
      w->panes[0] = (TraashLayoutPane){0, 0, 1, 1};
      w->pane_count = 1;
      w->active = 1;
    }
    if (w->active > w->pane_count) {
      w->active = 1;
    }
    out->window_count++;
  }
  lua_pop(L, 1); /* windows */
  return out->window_count > 0 ? 0 : -1;
}

static int load_from_path(const char *path, const char *fallback_name, TraashLayoutDesc *out) {
  lua_State *L = luaL_newstate();
  if (!L) {
    return -1;
  }
  luaL_openlibs(L);
  int rc = -1;
  if (luaL_dofile(L, path) != LUA_OK) {
    TRAASH_LOGW("layout load %s: %s", path, lua_tostring(L, -1));
    lua_close(L);
    return -1;
  }
  /* Accept returned table or global `layout` */
  if (lua_istable(L, -1)) {
    rc = parse_layout_table(L, out);
  } else {
    lua_pop(L, 1);
    lua_getglobal(L, "layout");
    if (lua_istable(L, -1)) {
      rc = parse_layout_table(L, out);
    }
    lua_pop(L, 1);
  }
  lua_close(L);
  if (rc == 0 && !out->name[0]) {
    snprintf(out->name, sizeof(out->name), "%s", fallback_name);
  }
  return rc;
}

int traash_layout_store_load(const char *name, TraashLayoutDesc *out) {
  if (!name || !out || !traash_layout_name_valid(name)) {
    return -1;
  }
  char path[640];
  char base[512];
  if (ensure_user_layouts_dir(base, sizeof(base)) == 0) {
    snprintf(path, sizeof(path), "%s/%s.lua", base, name);
    if (access(path, R_OK) == 0) {
      return load_from_path(path, name, out);
    }
  }
  if (traash_lua_dir(base, sizeof(base)) == 0) {
    snprintf(path, sizeof(path), "%s/layouts/%s.lua", base, name);
    if (access(path, R_OK) == 0) {
      return load_from_path(path, name, out);
    }
  }
  return -1;
}

int traash_layout_store_save(const char *name, const TraashSession *session) {
  if (!name || !session || !traash_layout_name_valid(name)) {
    return -1;
  }
  char dir[512];
  char path[640];
  if (ensure_user_layouts_dir(dir, sizeof(dir)) != 0) {
    return -1;
  }
  snprintf(path, sizeof(path), "%s/%s.lua", dir, name);
  FILE *f = fopen(path, "w");
  if (!f) {
    return -1;
  }
  fprintf(f, "-- traa.sh layout: %s\n", name);
  fprintf(f, "layout = {\n");
  fprintf(f, "  name = \"%s\",\n", name);
  fprintf(f, "  windows = {\n");
  for (const TraashWindow *w = session->windows; w; w = w->next) {
    fprintf(f, "    {\n");
    fprintf(f, "      name = \"%s\",\n", w->name);
    int active = 1;
    int pi = 0;
    for (const TraashPane *p = w->panes; p; p = p->next) {
      pi++;
      if (p == w->active) {
        active = pi;
      }
    }
    fprintf(f, "      active = %d,\n", active);
    fprintf(f, "      panes = {\n");
    for (const TraashPane *p = w->panes; p; p = p->next) {
      fprintf(f, "        { x = %.6g, y = %.6g, w = %.6g, h = %.6g },\n", p->x, p->y, p->w,
              p->h);
    }
    fprintf(f, "      },\n");
    fprintf(f, "    },\n");
  }
  fprintf(f, "  },\n");
  fprintf(f, "}\n");
  fprintf(f, "return layout\n");
  fclose(f);
  TRAASH_LOGI("saved layout %s", path);
  return 0;
}

int traash_layout_store_delete(const char *name) {
  if (!name || !traash_layout_name_valid(name)) {
    return -1;
  }
  char dir[512];
  char path[640];
  if (ensure_user_layouts_dir(dir, sizeof(dir)) != 0) {
    return -1;
  }
  snprintf(path, sizeof(path), "%s/%s.lua", dir, name);
  if (unlink(path) != 0) {
    return -1;
  }
  return 0;
}

static void destroy_all_windows(TraashSession *s) {
  TraashWindow *w = s->windows;
  while (w) {
    TraashWindow *n = w->next;
    traash_window_destroy(w);
    w = n;
  }
  s->windows = NULL;
  s->active = NULL;
}

int traash_layout_apply(TraashSession *session, const TraashLayoutDesc *desc, int cols,
                        int rows) {
  if (!session || !desc || desc->window_count < 1) {
    return -1;
  }
  if (cols < 1) {
    cols = 80;
  }
  if (rows < 1) {
    rows = 24;
  }

  destroy_all_windows(session);
  session->next_window_id = 1;
  session->next_pane_id = 2;

  TraashWindow *tail = NULL;
  for (int wi = 0; wi < desc->window_count; wi++) {
    const TraashLayoutWindow *wd = &desc->windows[wi];
    int wid = session->next_window_id++;
    char wname[64];
    if (wd->name[0]) {
      snprintf(wname, sizeof(wname), "%s", wd->name);
    } else {
      snprintf(wname, sizeof(wname), "%d", wid);
    }

    TraashWindow *w = calloc(1, sizeof(*w));
    if (!w) {
      return -1;
    }
    w->id = wid;
    snprintf(w->name, sizeof(w->name), "%s", wname);
    w->zoomed_pane_id = -1;

    TraashPane *ptail = NULL;
    for (int pi = 0; pi < wd->pane_count; pi++) {
      const TraashLayoutPane *pd = &wd->panes[pi];
      int id = (pi == 0) ? 1 : session->next_pane_id++;
      TraashPane *p = traash_pane_create(id, cols, rows);
      if (!p) {
        traash_window_destroy(w);
        return -1;
      }
      p->x = pd->x;
      p->y = pd->y;
      p->w = pd->w;
      p->h = pd->h;
      if (traash_pane_spawn_shell(p) != 0) {
        traash_pane_destroy(p);
        traash_window_destroy(w);
        return -1;
      }
      if (!w->panes) {
        w->panes = p;
      } else {
        ptail->next = p;
      }
      ptail = p;
      if (pi + 1 == wd->active) {
        w->active = p;
      }
    }
    if (!w->active) {
      w->active = w->panes;
    }

    if (!session->windows) {
      session->windows = w;
    } else {
      tail->next = w;
    }
    tail = w;
  }
  session->active = session->windows;
  return 0;
}
