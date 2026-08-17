#include "test_harness.h"
#include "util/utf8.h"
#include "util/ring.h"
#include "term/screen.h"
#include "term/selection.h"
#include "term/vt.h"
#include "mux/ipc.h"
#include "mux/session.h"
#include "input/keymap.h"
#include "input/actions.h"
#include "ui/overview_overlay.h"

static void test_utf8(void) {
  uint32_t cp = 0;
  TRAASH_CHECK_EQ(traash_utf8_decode((const uint8_t *)"A", 1, &cp), 1);
  TRAASH_CHECK_EQ(cp, (uint32_t)'A');
  TRAASH_CHECK_EQ(traash_utf8_decode((const uint8_t *)"\xE2\x9C\x93", 3, &cp), 3);
  TRAASH_CHECK_EQ(traash_utf8_width(0x4E2D), 2);
  uint8_t buf[4];
  TRAASH_CHECK_EQ(traash_utf8_encode(0x20AC, buf), 3);
}

static void test_ring(void) {
  TraashRing r;
  TRAASH_CHECK_EQ(traash_ring_init(&r, 8), 0);
  const uint8_t in[] = {1, 2, 3, 4, 5};
  TRAASH_CHECK_EQ(traash_ring_write(&r, in, 5), 5);
  uint8_t out[8] = {0};
  TRAASH_CHECK_EQ(traash_ring_read(&r, out, 3), 3);
  TRAASH_CHECK_EQ(out[0], 1);
  TRAASH_CHECK_EQ(traash_ring_len(&r), 2);
  traash_ring_free(&r);
}

static void test_vt_sgr(void) {
  TraashScreen s;
  TraashVt vt;
  TRAASH_CHECK_EQ(traash_screen_init(&s, 20, 5, 100), 0);
  traash_vt_init(&vt, &s);
  const char *seq = "\x1b[31;1mHi\x1b[0m";
  traash_vt_feed(&vt, (const uint8_t *)seq, strlen(seq));
  TRAASH_CHECK_EQ(s.cells[0].cp, (uint32_t)'H');
  TRAASH_CHECK_EQ(s.cells[0].fg, 1u);
  TRAASH_CHECK(s.cells[0].attrs & TRAASH_ATTR_BOLD);
  traash_screen_free(&s);
}

static void test_vt_cup_clear(void) {
  TraashScreen s;
  TraashVt vt;
  traash_screen_init(&s, 10, 4, 50);
  traash_vt_init(&vt, &s);
  traash_vt_feed(&vt, (const uint8_t *)"ABCD", 4);
  traash_vt_feed(&vt, (const uint8_t *)"\x1b[1;1H\x1b[2J", 10);
  TRAASH_CHECK_EQ(s.cells[0].cp, (uint32_t)' ');
  TRAASH_CHECK_EQ(s.cursor_x, 0);
  traash_screen_free(&s);
}

static void test_truecolor(void) {
  TraashScreen s;
  TraashVt vt;
  traash_screen_init(&s, 10, 2, 10);
  traash_vt_init(&vt, &s);
  const char *seq = "\x1b[38;2;10;20;30mX";
  traash_vt_feed(&vt, (const uint8_t *)seq, strlen(seq));
  TRAASH_CHECK(s.cells[0].attrs & TRAASH_ATTR_TRUECOLOR_FG);
  TRAASH_CHECK_EQ(s.cells[0].fg, 0x0a141eu);
  traash_screen_free(&s);
}

static void test_ipc(void) {
  TraashIpcMsg msg = {.type = TRAASH_IPC_LIST, .len = 0};
  uint8_t buf[64];
  size_t n = 0;
  TRAASH_CHECK_EQ(traash_ipc_encode(&msg, buf, sizeof(buf), &n), 0);
  TraashIpcMsg out;
  TRAASH_CHECK_EQ(traash_ipc_decode(buf, n, &out), 0);
  TRAASH_CHECK_EQ(out.type, (uint32_t)TRAASH_IPC_LIST);
}

static void test_mux_tree(void) {
  TraashSession *s = traash_session_create(1, "test", 40, 12);
  TRAASH_CHECK(s != NULL);
  TRAASH_CHECK(s->active != NULL);
  TraashPane *p = traash_window_split(s->active, 1, &s->next_pane_id, 40, 12);
  TRAASH_CHECK(p != NULL);
  TRAASH_CHECK_EQ(traash_window_pane_count(s->active), 2);
  TraashPane *first = s->active->panes;
  traash_window_focus_pane(s->active, first);
  TRAASH_CHECK(s->active->active == first);
  traash_window_focus_next(s->active);
  traash_window_zoom_toggle(s->active);
  TRAASH_CHECK(s->active->zoomed_pane_id >= 0);
  TraashPane *second = first->next;
  TRAASH_CHECK(second != NULL);
  TRAASH_CHECK_EQ(traash_window_close_pane(s->active, second), 0);
  TRAASH_CHECK_EQ(traash_window_pane_count(s->active), 1);
  TRAASH_CHECK_EQ(traash_window_close_pane(s->active, s->active->panes), 1);
  traash_session_destroy(s);
}

static void test_overview_overlay(void) {
  TraashOverviewLayout lay;
  traash_overview_overlay_layout(400, 500, 1.0f, 14, 3, &lay);
  TRAASH_CHECK_EQ(lay.cols, 1);
  TRAASH_CHECK(lay.visible >= 1);

  traash_overview_overlay_layout(1400, 900, 1.0f, 14, 6, &lay);
  TRAASH_CHECK(lay.cols >= 2);
  TRAASH_CHECK(lay.visible >= 2);

  traash_overview_overlay_layout(900, 600, 1.0f, 14, 20, &lay);
  TRAASH_CHECK(lay.visible < 20);
  TRAASH_CHECK(lay.cols >= 1);

  TraashSession *s = traash_session_create(1, "ov", 40, 12);
  TRAASH_CHECK(s != NULL);
  traash_session_new_window(s, NULL, 40, 12);
  traash_session_new_window(s, NULL, 40, 12);
  TraashOverviewOverlay o;
  traash_overview_overlay_init(&o);
  traash_overview_overlay_open_tabs(&o, s);
  TRAASH_CHECK(o.open);
  TRAASH_CHECK_EQ(o.mode, TRAASH_OVERVIEW_TABS);
  TRAASH_CHECK_EQ(traash_overview_overlay_item_count(&o, s), 3);

  traash_overview_overlay_layout(1400, 900, 1.0f, 14, 3, &lay);
  o.selected = 0;
  traash_overview_overlay_move(&o, s, 1, 0, &lay);
  TRAASH_CHECK_EQ(o.selected, 1);
  traash_overview_overlay_move(&o, s, 0, 1, &lay);
  TRAASH_CHECK(o.selected >= 1);

  TraashWindow *w0 = traash_overview_overlay_nth_window(s, 0);
  TRAASH_CHECK(w0 != NULL);
  traash_window_split(w0, 1, &s->next_pane_id, 40, 12);
  traash_overview_overlay_drill_panes(&o, w0);
  TRAASH_CHECK_EQ(o.mode, TRAASH_OVERVIEW_PANES);
  TRAASH_CHECK_EQ(traash_overview_overlay_item_count(&o, s), 2);
  traash_overview_overlay_back(&o, s);
  TRAASH_CHECK_EQ(o.mode, TRAASH_OVERVIEW_TABS);

  o.scroll = 0;
  o.selected = 0;
  float cx, cy, cw, ch, zx, zy, zw, zh;
  traash_overview_overlay_layout(1400, 900, 1.0f, 14, 3, &lay);
  traash_overview_overlay_card_rect(&lay, 0, &cx, &cy, &cw, &ch);
  traash_overview_overlay_close_rect(&lay, 0, &zx, &zy, &zw, &zh);
  TraashOverviewHit hit = traash_overview_overlay_hit(&o, s, cx + cw * 0.5f, cy + ch * 0.5f,
                                                      1400, 900, 1.0f, 14);
  TRAASH_CHECK_EQ(hit.kind, TRAASH_OVERVIEW_HIT_CARD);
  hit = traash_overview_overlay_hit(&o, s, zx + zw * 0.5f, zy + zh * 0.5f, 1400, 900, 1.0f, 14);
  TRAASH_CHECK_EQ(hit.kind, TRAASH_OVERVIEW_HIT_CLOSE);
  hit = traash_overview_overlay_hit(&o, s, 1.0f, 1.0f, 1400, 900, 1.0f, 14);
  TRAASH_CHECK_EQ(hit.kind, TRAASH_OVERVIEW_HIT_BACKDROP);

  traash_overview_overlay_layout(900, 500, 1.0f, 14, 20, &lay);
  o.selected = 18;
  traash_overview_overlay_clamp(&o, s, &lay);
  TRAASH_CHECK_EQ(o.selected, 2);
  traash_session_destroy(s);
}

static void test_keymap(void) {
  TraashKeymap km;
  traash_keymap_init_defaults(&km);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 66, 1), TRAASH_ACTION_NONE); /* ctrl-b prefix */
  TRAASH_CHECK(km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 79, 0), TRAASH_ACTION_PANE_NEXT); /* GLFW_KEY_O */
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 66, 1), TRAASH_ACTION_NONE);
  TRAASH_CHECK(km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 53, 2), TRAASH_ACTION_SPLIT_V); /* Shift-5 = % */
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 66, 1), TRAASH_ACTION_NONE);
  TRAASH_CHECK(km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 39, 2), TRAASH_ACTION_SPLIT_H); /* Shift-' = " */

  /* Custom leader */
  traash_keymap_set_leader(&km, 65, 1); /* Ctrl-A */
  TRAASH_CHECK_EQ(km.prefix_key, 65);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 66, 1), TRAASH_ACTION_NONE); /* old leader ignored */
  TRAASH_CHECK(!km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 65, 1), TRAASH_ACTION_NONE);
  TRAASH_CHECK(km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 79, 0), TRAASH_ACTION_PANE_NEXT);

  /* Unmatched follow-up cancels leader mode */
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 65, 1), TRAASH_ACTION_NONE);
  TRAASH_CHECK(km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 88, 0), TRAASH_ACTION_NONE); /* X */
  TRAASH_CHECK(!km.prefix_active);

  /* Shift alone must not cancel a pending leader (needed for % / ") */
  traash_keymap_set_leader(&km, 66, 1);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 66, 1), TRAASH_ACTION_NONE);
  TRAASH_CHECK(km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 340, 2), TRAASH_ACTION_NONE); /* LeftShift */
  TRAASH_CHECK(km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 53, 3), TRAASH_ACTION_SPLIT_V); /* Ctrl+Shift-5 sticky */
  TRAASH_CHECK(!km.prefix_active);

  /* Sticky Ctrl after leader: Ctrl-b then Ctrl-o still next-pane */
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 66, 1), TRAASH_ACTION_NONE);
  TRAASH_CHECK(km.prefix_active);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 79, 1), TRAASH_ACTION_PANE_NEXT);

  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 61, 1 | 2), TRAASH_ACTION_FONT_INCREASE);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 45, 1), TRAASH_ACTION_FONT_DECREASE);
  TRAASH_CHECK_EQ(traash_keymap_lookup(&km, 79, 1 | 2), TRAASH_ACTION_OVERVIEW);

  /* Legacy ASCII keys normalize to GLFW physical keys */
  int k = 'o', m = 0;
  TRAASH_CHECK(traash_keymap_normalize_key(&k, &m));
  TRAASH_CHECK_EQ(k, 79);
  k = '+';
  m = 1;
  TRAASH_CHECK(traash_keymap_normalize_key(&k, &m));
  TRAASH_CHECK_EQ(k, 61);
  TRAASH_CHECK_EQ(m, 1 | 2);

  TRAASH_CHECK_STREQ(traash_action_name(TRAASH_ACTION_DEMO), "demo");
  TRAASH_CHECK_STREQ(traash_action_name(TRAASH_ACTION_OVERVIEW), "overview");
}

static void test_selection_word(void) {
  TraashScreen s;
  TraashSelection sel;
  TRAASH_CHECK_EQ(traash_screen_init(&s, 40, 3, 10), 0);
  const char *line = "/home/mathias/themes.lua: error";
  for (int i = 0; line[i]; i++) {
    traash_cell_set(&s.cells[i], (uint32_t)(unsigned char)line[i], 7, 0, 0);
  }
  /* Click on "mathias" */
  traash_selection_select_word(&sel, &s, 6, 0);
  TRAASH_CHECK(sel.active);
  TRAASH_CHECK_EQ(sel.ax, 6);
  TRAASH_CHECK_EQ(sel.bx, 12);
  char buf[64];
  traash_selection_text(&sel, &s, buf, sizeof(buf));
  TRAASH_CHECK_STREQ(buf, "mathias");
  /* Click on extension after '.' */
  traash_selection_select_word(&sel, &s, 22, 0);
  traash_selection_text(&sel, &s, buf, sizeof(buf));
  TRAASH_CHECK_STREQ(buf, "lua");
  /* Click on '/' separator — single slash */
  traash_selection_select_word(&sel, &s, 5, 0);
  TRAASH_CHECK_EQ(sel.ax, 5);
  TRAASH_CHECK_EQ(sel.bx, 5);
  traash_screen_free(&s);
}

static void test_screen_search(void) {
  TraashScreen s;
  TraashVt vt;
  TRAASH_CHECK_EQ(traash_screen_init(&s, 20, 4, 20), 0);
  traash_vt_init(&vt, &s);
  traash_vt_feed(&vt, (const uint8_t *)"hello HELLO world", 17);
  TraashSearchMatch hits[8];
  int n = traash_screen_find_all(&s, "hello", hits, 8);
  TRAASH_CHECK_EQ(n, 2);
  TRAASH_CHECK_EQ(hits[0].col, 0);
  TRAASH_CHECK_EQ(hits[1].col, 6);
  TRAASH_CHECK_EQ(traash_screen_query_len("hello"), 5);
  traash_screen_free(&s);
}

typedef struct {
  char buf[128];
  size_t len;
} ReplyCap;

static void test_reply_cap(void *ud, const char *data, size_t n) {
  ReplyCap *c = ud;
  if (c->len + n >= sizeof(c->buf)) {
    n = sizeof(c->buf) - c->len - 1;
  }
  memcpy(c->buf + c->len, data, n);
  c->len += n;
  c->buf[c->len] = 0;
}

static void test_vt_charset_and_dsr(void) {
  TraashScreen s;
  TraashVt vt;
  ReplyCap cap = {0};
  TRAASH_CHECK_EQ(traash_screen_init(&s, 40, 5, 10), 0);
  traash_vt_init(&vt, &s);
  vt.reply = test_reply_cap;
  vt.ud = &cap;

  /* ESC ( B must not print 'B' (G0 = USASCII). */
  traash_vt_feed(&vt, (const uint8_t *)"\033(BHi", 5);
  TRAASH_CHECK_EQ(s.cells[0].cp, (uint32_t)'H');
  TRAASH_CHECK_EQ(s.cells[1].cp, (uint32_t)'i');

  /* DSR status → CSI 0 n */
  cap.len = 0;
  cap.buf[0] = 0;
  traash_vt_feed(&vt, (const uint8_t *)"\033[5n", 4);
  TRAASH_CHECK_STREQ(cap.buf, "\033[0n");

  /* OSC 11 query → rgb reply */
  cap.len = 0;
  cap.buf[0] = 0;
  traash_vt_set_report_colors(&vt, 0xffffff, 0x112233, 0xffffff);
  traash_vt_feed(&vt, (const uint8_t *)"\033]11;?\007", 7);
  TRAASH_CHECK(strstr(cap.buf, "\033]11;rgb:1111/2222/3333") != NULL);

  /* Colon truecolor SGR */
  traash_vt_feed(&vt, (const uint8_t *)"\033[38:2::10:20:30mX", 18);
  TRAASH_CHECK(s.attrs & TRAASH_ATTR_TRUECOLOR_FG);
  TRAASH_CHECK_EQ(s.fg, 0x0a141eu);

  /* Alternate screen */
  traash_vt_feed(&vt, (const uint8_t *)"\033[?1049h", 8);
  TRAASH_CHECK(s.alt_screen);
  traash_vt_feed(&vt, (const uint8_t *)"\033[?1049l", 8);
  TRAASH_CHECK(!s.alt_screen);

  /* Undercurl via colon SGR */
  traash_vt_feed(&vt, (const uint8_t *)"\033[4:3mU", 8);
  TRAASH_CHECK(s.attrs & TRAASH_ATTR_UNDERCURL);
  TRAASH_CHECK(!(s.attrs & TRAASH_ATTR_ITALIC)); /* 4:3 must not also set italic */

  /* XTGETTCAP for Tc */
  cap.len = 0;
  cap.buf[0] = 0;
  traash_vt_feed(&vt, (const uint8_t *)"\033P+q5463\033\\", 10);
  TRAASH_CHECK(strstr(cap.buf, "5463=") != NULL);

  /* Synchronized output mode */
  traash_vt_feed(&vt, (const uint8_t *)"\033[?2026h", 8);
  TRAASH_CHECK(s.sync_output);
  traash_vt_feed(&vt, (const uint8_t *)"\033[?2026l", 8);
  TRAASH_CHECK(!s.sync_output);

  traash_screen_free(&s);
}

int main(void) {
  test_utf8();
  test_ring();
  test_vt_sgr();
  test_vt_cup_clear();
  test_truecolor();
  test_ipc();
  test_mux_tree();
  test_overview_overlay();
  test_keymap();
  test_selection_word();
  test_screen_search();
  test_vt_charset_and_dsr();
  return traash_test_report();
}
