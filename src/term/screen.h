#ifndef TRAASH_SCREEN_H
#define TRAASH_SCREEN_H

#include "term/cell.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  int cols;
  int rows;
  int cursor_x;
  int cursor_y;
  int scroll_top;
  int scroll_bottom;
  TraashCell *cells; /* rows * cols — active buffer */
  TraashCell *alt_cells; /* alternate screen (NULL until first use) */
  int cells_cap; /* allocated cells for live grid (>= cols*rows) */
  int alt_cap;
  int row_wrap_cap;
  TraashCell *scrollback;
  int scrollback_cap; /* rows */
  int scrollback_len;
  int scrollback_start; /* ring: physical index of oldest logical line */
  int scrollback_cols; /* column stride of scrollback (may lag live cols while resizing) */
  int scroll_offset; /* view offset into scrollback */
  uint32_t fg;
  uint32_t bg;
  uint16_t attrs;
  char title[256];
  char cwd[512];
  int bell_pending;
  /* OSC 133;D — shell integration reported a finished command. */
  int command_done_pending;
  /* Set when the PTY produced output (used for inactive-tab attention). */
  int activity_pending;
  int saved_x, saved_y;
  int cursor_visible; /* 1 show, 0 hide (DEC mode 25) */
  int cursor_style;   /* 0 block, 1 beam, 2 underline — DECSCUSR */
  bool alt_screen;
  bool dirty;
  bool bracketed_paste;
  bool origin_mode;
  bool wrap_pending;
  bool auto_wrap; /* DECAWM, default on */
  bool focus_report; /* DECSET 1004 */
  bool sync_output;  /* DECSET 2026 — hold presents while set */
  uint8_t *row_wrap; /* rows: soft-wrap continues onto next line */
  uint8_t *sb_wrap;  /* scrollback_cap: soft-wrap flags */
} TraashScreen;

int traash_screen_init(TraashScreen *s, int cols, int rows, int scrollback_rows);
void traash_screen_free(TraashScreen *s);
int traash_screen_resize(TraashScreen *s, int cols, int rows);
/* Fast live-grid resize for interactive window drags (scrollback synced later). */
int traash_screen_resize_live(TraashScreen *s, int cols, int rows);
/* Bring scrollback column stride in sync with live cols (reflow/clip). */
int traash_screen_sync_scrollback(TraashScreen *s);
void traash_screen_set_alt(TraashScreen *s, int enable);
TraashCell *traash_screen_cell(TraashScreen *s, int x, int y);
const TraashCell *traash_screen_view_cell(const TraashScreen *s, int x, int y);
void traash_screen_put_codepoint(TraashScreen *s, uint32_t cp);
void traash_screen_newline(TraashScreen *s);
void traash_screen_cr(TraashScreen *s);
void traash_screen_backspace(TraashScreen *s);
void traash_screen_clear(TraashScreen *s, int mode); /* 0 below, 1 above, 2 all */
void traash_screen_clear_line(TraashScreen *s, int mode);
void traash_screen_scroll_up(TraashScreen *s, int n);
void traash_screen_scroll_down(TraashScreen *s, int n);
void traash_screen_move_cursor(TraashScreen *s, int x, int y);
void traash_screen_set_scroll_region(TraashScreen *s, int top, int bottom);
void traash_screen_save_cursor(TraashScreen *s);
void traash_screen_restore_cursor(TraashScreen *s);
void traash_screen_insert_cells(TraashScreen *s, int n);
void traash_screen_delete_cells(TraashScreen *s, int n);
void traash_screen_erase_chars(TraashScreen *s, int n);
void traash_screen_insert_lines(TraashScreen *s, int n);
void traash_screen_delete_lines(TraashScreen *s, int n);
/* Absolute row: 0 = oldest scrollback, scrollback_len + rows - 1 = last live row. */
typedef struct {
  int row;
  int col;
} TraashSearchMatch;

/* Case-insensitive ASCII match over scrollback + live screen. Returns count written. */
int traash_screen_find_all(const TraashScreen *s, const char *query, TraashSearchMatch *out,
                           int max_out);
/* Codepoint length of query (0 if empty/invalid). */
int traash_screen_query_len(const char *query);
int traash_screen_view_abs_row(const TraashScreen *s, int view_y);
int traash_screen_abs_to_view_y(const TraashScreen *s, int abs_row);
void traash_screen_reveal_row(TraashScreen *s, int abs_row);
/* Legacy: first match on the live screen only. */
int traash_screen_search(const TraashScreen *s, const char *query, int *out_x, int *out_y);
void traash_screen_dump(const TraashScreen *s, char *buf, size_t n);

#endif
