#ifndef TRAASH_AGENT_PROTO_H
#define TRAASH_AGENT_PROTO_H

#include "mux/session.h"
#include "term/screen_export.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
  int v;
  int scrollback;
  int pane_id;
} TraashGetStateReq;

typedef struct {
  int pane_id;
  int timeout_ms;
} TraashWaitIdleReq;

typedef struct {
  int emit_output;
} TraashSubscribeReq;

int traash_agent_parse_get_state(const char *json, size_t len, TraashGetStateReq *out);
int traash_agent_parse_wait_idle(const char *json, size_t len, TraashWaitIdleReq *out);
int traash_agent_parse_subscribe(const char *json, size_t len, TraashSubscribeReq *out);

/* Build STATE JSON into buf. Returns length or -1. */
int traash_agent_build_state(const TraashSession *s, const TraashPaneExport *panes, int pane_count,
                             char *buf, size_t buf_len);

int traash_agent_build_idle(int pane_id, char *buf, size_t buf_len);
int traash_agent_build_wait_timeout(int pane_id, char *buf, size_t buf_len);

int traash_agent_build_event(const char *type, int pane_id, const char *extra_json, char *buf,
                             size_t buf_len);

int traash_agent_json_escape(const char *in, char *out, size_t out_len);

#endif
