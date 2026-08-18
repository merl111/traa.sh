#include "mux/agent_proto.h"
#include "mux/session.h"
#include "term/screen_export.h"
#include "term/vt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_utf8_export(void) {
  TraashScreen s;
  if (traash_screen_init(&s, 10, 3, 100) != 0) {
    return 1;
  }
  s.cells[0].cp = (uint32_t)'c';
  s.cells[1].cp = (uint32_t)'a';
  s.cells[2].cp = 0xe9;
  char buf[256];
  TraashExportOpts opts = {.scrollback_lines = 0, .max_bytes = 256, .pane_id = 0};
  int n = traash_screen_export_text(&s, &opts, buf, sizeof(buf));
  traash_screen_free(&s);
  if (n < 0) {
    return 1;
  }
  if (!strstr(buf, "ca") || !strstr(buf, "\xc3\xa9")) {
    fprintf(stderr, "export missing utf-8: %s\n", buf);
    return 1;
  }
  return 0;
}

static int test_json_roundtrip(void) {
  TraashGetStateReq req;
  const char *in = "{\"v\":1,\"scrollback\":50,\"pane_id\":2}";
  if (traash_agent_parse_get_state(in, strlen(in), &req) != 0) {
    return 1;
  }
  if (req.v != 1 || req.scrollback != 50 || req.pane_id != 2) {
    return 1;
  }
  TraashWaitIdleReq w;
  if (traash_agent_parse_wait_idle("{\"pane_id\":1,\"timeout_ms\":5000}", 32, &w) != 0) {
    return 1;
  }
  if (w.pane_id != 1 || w.timeout_ms != 5000) {
    return 1;
  }
  char out[512];
  char esc[64];
  traash_agent_json_escape("line \"a\"\n", esc, sizeof(esc));
  if (strcmp(esc, "line \\\"a\\\"\\n") != 0) {
    fprintf(stderr, "escape: %s\n", esc);
    return 1;
  }
  TraashPaneExport panes[1] = {0};
  panes[0].pane_id = 1;
  snprintf(panes[0].title, sizeof(panes[0].title), "bash");
  snprintf(panes[0].cwd, sizeof(panes[0].cwd), "/tmp");
  panes[0].cols = 80;
  panes[0].rows = 24;
  panes[0].text = strdup("hello");
  panes[0].text_len = 5;
  TraashSession *sess = traash_session_create(1, "dev", 80, 24);
  if (!sess) {
    free(panes[0].text);
    return 1;
  }
  if (traash_agent_build_state(sess, panes, 1, out, sizeof(out)) < 0) {
    traash_session_destroy(sess);
    free(panes[0].text);
    return 1;
  }
  if (!strstr(out, "\"session\":\"dev\"") || !strstr(out, "\"busy\":false")) {
    fprintf(stderr, "state json: %s\n", out);
    traash_session_destroy(sess);
    free(panes[0].text);
    return 1;
  }
  traash_session_destroy(sess);
  free(panes[0].text);
  return 0;
}

static int test_wait_idle_osc(void) {
  TraashScreen s;
  if (traash_screen_init(&s, 20, 5, 100) != 0) {
    return 1;
  }
  TraashPane p = {0};
  p.id = 1;
  p.screen = s;
  traash_vt_init(&p.vt, &p.screen);
  p.agent_busy = 1;
  traash_vt_feed(&p.vt, (const uint8_t *)"\033]133;D\007", 8);
  if (!p.screen.command_done_pending) {
    traash_screen_free(&s);
    return 1;
  }
  traash_screen_free(&s);
  return 0;
}

int main(void) {
  int fails = 0;
  if (test_utf8_export() != 0) {
    fprintf(stderr, "test_utf8_export failed\n");
    fails++;
  }
  if (test_json_roundtrip() != 0) {
    fprintf(stderr, "test_json_roundtrip failed\n");
    fails++;
  }
  if (test_wait_idle_osc() != 0) {
    fprintf(stderr, "test_wait_idle_osc failed\n");
    fails++;
  }
  return fails ? 1 : 0;
}
