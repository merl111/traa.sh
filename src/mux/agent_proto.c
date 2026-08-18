#include "mux/agent_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int json_find_int(const char *json, const char *key, int def) {
  if (!json || !key) {
    return def;
  }
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return def;
  }
  p = strchr(p + strlen(pat), ':');
  if (!p) {
    return def;
  }
  p++;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (strncmp(p, "true", 4) == 0) {
    return 1;
  }
  if (strncmp(p, "false", 5) == 0) {
    return 0;
  }
  return (int)strtol(p, NULL, 10);
}

static int json_find_bool(const char *json, const char *key, int def) {
  return json_find_int(json, key, def);
}

int traash_agent_parse_get_state(const char *json, size_t len, TraashGetStateReq *out) {
  if (!out) {
    return -1;
  }
  out->v = 1;
  out->scrollback = 200;
  out->pane_id = 0;
  if (!json || len == 0) {
    return 0;
  }
  char tmp[512];
  size_t n = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
  memcpy(tmp, json, n);
  tmp[n] = 0;
  out->v = json_find_int(tmp, "v", 1);
  out->scrollback = json_find_int(tmp, "scrollback", 200);
  out->pane_id = json_find_int(tmp, "pane_id", 0);
  return 0;
}

int traash_agent_parse_wait_idle(const char *json, size_t len, TraashWaitIdleReq *out) {
  if (!out) {
    return -1;
  }
  out->pane_id = 0;
  out->timeout_ms = 30000;
  if (!json || len == 0) {
    return -1;
  }
  char tmp[512];
  size_t n = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
  memcpy(tmp, json, n);
  tmp[n] = 0;
  out->pane_id = json_find_int(tmp, "pane_id", 0);
  out->timeout_ms = json_find_int(tmp, "timeout_ms", 30000);
  if (out->pane_id <= 0) {
    return -1;
  }
  return 0;
}

int traash_agent_parse_subscribe(const char *json, size_t len, TraashSubscribeReq *out) {
  if (!out) {
    return -1;
  }
  out->emit_output = 0;
  if (!json || len == 0) {
    return 0;
  }
  char tmp[256];
  size_t n = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
  memcpy(tmp, json, n);
  tmp[n] = 0;
  out->emit_output = json_find_bool(tmp, "emit_output", 0);
  return 0;
}

int traash_agent_json_escape(const char *in, char *out, size_t out_len) {
  if (!out || out_len == 0) {
    return -1;
  }
  if (!in) {
    out[0] = 0;
    return 0;
  }
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
    const char *rep = NULL;
    char hex[7];
    if (*p == '"' || *p == '\\') {
      if (o + 2 >= out_len) {
        return -1;
      }
      out[o++] = '\\';
      out[o++] = (char)*p;
      continue;
    }
    if (*p == '\n') {
      rep = "\\n";
    } else if (*p == '\r') {
      rep = "\\r";
    } else if (*p == '\t') {
      rep = "\\t";
    } else if (*p < 0x20) {
      snprintf(hex, sizeof(hex), "\\u%04x", *p);
      rep = hex;
    }
    if (rep) {
      size_t rl = strlen(rep);
      if (o + rl >= out_len) {
        return -1;
      }
      memcpy(out + o, rep, rl);
      o += rl;
      continue;
    }
    if (o + 1 >= out_len) {
      return -1;
    }
    out[o++] = (char)*p;
  }
  if (o >= out_len) {
    return -1;
  }
  out[o] = 0;
  return (int)o;
}

int traash_agent_build_state(const TraashSession *s, const TraashPaneExport *panes, int pane_count,
                             char *buf, size_t buf_len) {
  if (!s || !buf || buf_len < 64) {
    return -1;
  }
  TraashWindow *w = s->active ? s->active : s->windows;
  int wid = w ? w->id : 0;
  const char *wname = w && w->name[0] ? w->name : "1";
  size_t o = 0;
  int m = snprintf(buf, buf_len, "{\"v\":1,\"session\":\"");
  if (m < 0 || (size_t)m >= buf_len) {
    return -1;
  }
  o = (size_t)m;
  char esc[1024];
  traash_agent_json_escape(s->name, esc, sizeof(esc));
  m = snprintf(buf + o, buf_len - o, "%s\",\"window\":{\"id\":%d,\"name\":\"", esc, wid);
  if (m < 0 || o + (size_t)m >= buf_len) {
    return -1;
  }
  o += (size_t)m;
  traash_agent_json_escape(wname, esc, sizeof(esc));
  m = snprintf(buf + o, buf_len - o, "%s\"},\"panes\":[", esc);
  if (m < 0 || o + (size_t)m >= buf_len) {
    return -1;
  }
  o += (size_t)m;
  for (int i = 0; i < pane_count; i++) {
    const TraashPaneExport *p = &panes[i];
    char tesc[512];
    char cesc[1024];
    char txtesc[512 * 1024];
    traash_agent_json_escape(p->title, tesc, sizeof(tesc));
    traash_agent_json_escape(p->cwd, cesc, sizeof(cesc));
    if (p->text && p->text_len) {
      traash_agent_json_escape(p->text, txtesc, sizeof(txtesc));
    } else {
      txtesc[0] = 0;
    }
    m = snprintf(buf + o, buf_len - o,
                 "%s{\"id\":%d,\"title\":\"%s\",\"cwd\":\"%s\",\"cols\":%d,\"rows\":%d,"
                 "\"cursor\":{\"x\":%d,\"y\":%d},\"busy\":%s,\"text\":\"%s\"}",
                 i ? "," : "", p->pane_id, tesc, cesc, p->cols, p->rows, p->cursor_x, p->cursor_y,
                 p->busy ? "true" : "false", txtesc);
    if (m < 0 || o + (size_t)m >= buf_len) {
      return -1;
    }
    o += (size_t)m;
  }
  m = snprintf(buf + o, buf_len - o, "]}");
  if (m < 0 || o + (size_t)m >= buf_len) {
    return -1;
  }
  return (int)(o + (size_t)m);
}

int traash_agent_build_idle(int pane_id, char *buf, size_t buf_len) {
  return snprintf(buf, buf_len, "{\"pane_id\":%d}", pane_id) >= 0 ? (int)strlen(buf) : -1;
}

int traash_agent_build_wait_timeout(int pane_id, char *buf, size_t buf_len) {
  return snprintf(buf, buf_len, "{\"pane_id\":%d}", pane_id) >= 0 ? (int)strlen(buf) : -1;
}

int traash_agent_build_event(const char *type, int pane_id, const char *extra_json, char *buf,
                             size_t buf_len) {
  if (!type || !buf) {
    return -1;
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  int64_t ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  if (pane_id > 0) {
    if (extra_json && extra_json[0]) {
      return snprintf(buf, buf_len, "{\"type\":\"%s\",\"pane_id\":%d,\"ts\":%lld,%s}", type,
                      pane_id, (long long)ms, extra_json) >= 0
                 ? (int)strlen(buf)
                 : -1;
    }
    return snprintf(buf, buf_len, "{\"type\":\"%s\",\"pane_id\":%d,\"ts\":%lld}", type, pane_id,
                    (long long)ms) >= 0
               ? (int)strlen(buf)
               : -1;
  }
  if (extra_json && extra_json[0]) {
    return snprintf(buf, buf_len, "{\"type\":\"%s\",\"ts\":%lld,%s}", type, (long long)ms,
                    extra_json) >= 0
               ? (int)strlen(buf)
               : -1;
  }
  return snprintf(buf, buf_len, "{\"type\":\"%s\",\"ts\":%lld}", type, (long long)ms) >= 0
             ? (int)strlen(buf)
             : -1;
}
