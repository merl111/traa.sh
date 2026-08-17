#include "util/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static int ensure_dir(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode) ? 0 : -1;
  }
  return mkdir(path, 0700);
}

static int lua_probe(const char *dir) {
  char probe[768];
  if (!dir || !dir[0]) {
    return 0;
  }
  snprintf(probe, sizeof(probe), "%s/defaults/config.lua", dir);
  return access(probe, R_OK) == 0;
}

static int dirname_copy(const char *path, char *buf, size_t n) {
  const char *slash = strrchr(path, '/');
  if (!slash) {
    snprintf(buf, n, ".");
    return 0;
  }
  if (slash == path) {
    snprintf(buf, n, "/");
    return 0;
  }
  size_t len = (size_t)(slash - path);
  if (len + 1 > n) {
    return -1;
  }
  memcpy(buf, path, len);
  buf[len] = 0;
  return 0;
}

static int exe_dir(char *buf, size_t n) {
  char path[PATH_MAX];
#ifdef __APPLE__
  uint32_t sz = sizeof(path);
  if (_NSGetExecutablePath(path, &sz) != 0) {
    return -1;
  }
  char resolved[PATH_MAX];
  if (realpath(path, resolved)) {
    snprintf(path, sizeof(path), "%s", resolved);
  }
#elif defined(__linux__)
  ssize_t r = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (r < 0) {
    return -1;
  }
  path[r] = 0;
#else
  (void)path;
  return -1;
#endif
  return dirname_copy(path, buf, n);
}

int traash_config_dir(char *buf, size_t n) {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) {
    snprintf(buf, n, "%s/traash", xdg);
  } else {
    const char *home = getenv("HOME");
    if (!home) {
      return -1;
    }
    snprintf(buf, n, "%s/.config/traash", home);
  }
  ensure_dir(buf);
  return 0;
}

int traash_runtime_dir(char *buf, size_t n) {
  const char *xdg = getenv("XDG_RUNTIME_DIR");
  if (xdg && xdg[0]) {
    snprintf(buf, n, "%s/traash", xdg);
  } else {
    const char *home = getenv("HOME");
    if (!home) {
      return -1;
    }
    snprintf(buf, n, "%s/.traash", home);
  }
  ensure_dir(buf);
  return 0;
}

int traash_lua_dir(char *buf, size_t n) {
  const char *env = getenv("TRAASH_LUA_PATH");
  if (env && env[0]) {
    snprintf(buf, n, "%s", env);
    return 0;
  }

  char base[PATH_MAX];
  if (exe_dir(base, sizeof(base)) == 0) {
    char cand[PATH_MAX + 32];
    snprintf(cand, sizeof(cand), "%s/../share/traash/lua", base);
    if (lua_probe(cand)) {
      snprintf(buf, n, "%s", cand);
      return 0;
    }
    snprintf(cand, sizeof(cand), "%s/lua", base);
    if (lua_probe(cand)) {
      snprintf(buf, n, "%s", cand);
      return 0;
    }
  }

  /* Prefer source tree during development */
  if (lua_probe(TRAASH_SOURCE_LUA_DIR)) {
    snprintf(buf, n, "%s", TRAASH_SOURCE_LUA_DIR);
    return 0;
  }
  if (lua_probe("./lua")) {
    snprintf(buf, n, "./lua");
    return 0;
  }
  snprintf(buf, n, "%s", TRAASH_LUA_DIR);
  return 0;
}
