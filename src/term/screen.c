#include "term/screen.h"

#include "util/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TraashCell *cell_at(TraashScreen *s, int x, int y) {
  return &s->cells[y * s->cols + x];
}

/* Scrollback is a ring: logical line 0 is oldest. */
static int sb_phys(const TraashScreen *s, int logical) {
  if (s->scrollback_cap <= 0) {
    return 0;
  }
  int i = s->scrollback_start + logical;
  if (i >= s->scrollback_cap) {
    i -= s->scrollback_cap;
  }
  return i;
}

static const TraashCell *sb_row_c(const TraashScreen *s, int logical) {
  int stride = s->scrollback_cols > 0 ? s->scrollback_cols : s->cols;
  return &s->scrollback[sb_phys(s, logical) * stride];
}

int traash_screen_init(TraashScreen *s, int cols, int rows, int scrollback_rows) {
  memset(s, 0, sizeof(*s));
  s->cols = cols;
  s->rows = rows;
  s->scroll_top = 0;
  s->scroll_bottom = rows - 1;
  s->fg = 7;
  s->bg = 0;
  s->cursor_visible = 1;
  s->cursor_style = 0;
  s->auto_wrap = true;
  s->cells = calloc((size_t)cols * (size_t)rows, sizeof(TraashCell));
  s->cells_cap = cols * rows;
  s->alt_cap = 0;
  s->scrollback_cap = scrollback_rows;
  s->scrollback_cols = cols;
  s->scrollback = calloc((size_t)scrollback_rows * (size_t)cols, sizeof(TraashCell));
  s->row_wrap = calloc((size_t)rows, 1);
  s->row_wrap_cap = rows;
  s->sb_wrap = calloc((size_t)scrollback_rows, 1);
  if (!s->cells || !s->scrollback || !s->row_wrap || !s->sb_wrap) {
    traash_screen_free(s);
    return -1;
  }
  for (int i = 0; i < cols * rows; i++) {
    traash_cell_clear(&s->cells[i]);
  }
  s->dirty = true;
  return 0;
}

void traash_screen_free(TraashScreen *s) {
  free(s->cells);
  free(s->alt_cells);
  free(s->scrollback);
  free(s->row_wrap);
  free(s->sb_wrap);
  memset(s, 0, sizeof(*s));
}

static int row_last_occupied(const TraashCell *row, int cols) {
  int last = cols - 1;
  while (last > 0 && (row[last].cp == 0 || row[last].cp == ' ')) {
    last--;
  }
  if (last == 0 && (row[0].cp == 0 || row[0].cp == ' ')) {
    return -1;
  }
  return last;
}

static void clear_cells(TraashCell *cells, int n) {
  for (int i = 0; i < n; i++) {
    traash_cell_clear(&cells[i]);
  }
}

/* Resize only the live (and alt) grid — avoids scrollback reflow during drag.
 * Same-column grow/shrink reuses capacity; column changes use slack to cut churn. */
int traash_screen_resize_live(TraashScreen *s, int cols, int rows) {
  if (cols < 1) {
    cols = 1;
  }
  if (rows < 1) {
    rows = 1;
  }
  if (cols == s->cols && rows == s->rows) {
    return 0;
  }

  int old_cols = s->cols;
  int old_rows = s->rows;

  /* Fast path: column count unchanged — only adjust visible rows. */
  if (cols == old_cols && s->cells && (size_t)cols * (size_t)rows <= (size_t)s->cells_cap &&
      rows <= s->row_wrap_cap) {
    if (rows > old_rows) {
      for (int y = old_rows; y < rows; y++) {
        clear_cells(&s->cells[y * cols], cols);
        s->row_wrap[y] = 0;
      }
      if (s->alt_cells && (size_t)cols * (size_t)rows <= (size_t)s->alt_cap) {
        for (int y = old_rows; y < rows; y++) {
          clear_cells(&s->alt_cells[y * cols], cols);
        }
      }
    }
    s->rows = rows;
    s->scroll_top = 0;
    s->scroll_bottom = rows - 1;
    if (s->cursor_y >= rows) {
      s->cursor_y = rows - 1;
    }
    s->dirty = true;
    return 0;
  }

  size_t need = (size_t)cols * (size_t)rows;
  /* Allocate with slack so consecutive drag sizes don't realloc every pixel. */
  size_t alloc = need + (need / 4) + (size_t)cols * 4;
  if (alloc < need) {
    alloc = need;
  }

  TraashCell *ncells = NULL;
  uint8_t *nrow_wrap = NULL;
  int reuse_cells = s->cells && need <= (size_t)s->cells_cap;
  int reuse_wrap = s->row_wrap && rows <= s->row_wrap_cap;

  if (reuse_cells && cols == old_cols) {
    ncells = s->cells;
    if (rows > old_rows) {
      for (int y = old_rows; y < rows; y++) {
        clear_cells(&ncells[y * cols], cols);
      }
    }
  } else if (reuse_cells && cols != old_cols) {
    /* Repack into a temp, then into existing buffer if it fits. */
    TraashCell *tmp = malloc(need * sizeof(TraashCell));
    if (!tmp) {
      return -1;
    }
    clear_cells(tmp, (int)need);
    int copy_cols = cols < old_cols ? cols : old_cols;
    int copy_rows = rows < old_rows ? rows : old_rows;
    for (int y = 0; y < copy_rows; y++) {
      memcpy(&tmp[y * cols], &s->cells[y * old_cols],
             (size_t)copy_cols * sizeof(TraashCell));
    }
    memcpy(s->cells, tmp, need * sizeof(TraashCell));
    free(tmp);
    ncells = s->cells;
  } else {
    ncells = malloc(alloc * sizeof(TraashCell));
    if (!ncells) {
      return -1;
    }
    clear_cells(ncells, (int)need);
    int copy_cols = cols < old_cols ? cols : old_cols;
    int copy_rows = rows < old_rows ? rows : old_rows;
    if (s->cells) {
      for (int y = 0; y < copy_rows; y++) {
        memcpy(&ncells[y * cols], &s->cells[y * old_cols],
               (size_t)copy_cols * sizeof(TraashCell));
      }
    }
  }

  if (reuse_wrap) {
    nrow_wrap = s->row_wrap;
    int copy_rows = rows < old_rows ? rows : old_rows;
    if (rows > old_rows) {
      memset(nrow_wrap + old_rows, 0, (size_t)(rows - old_rows));
    }
    (void)copy_rows;
  } else {
    nrow_wrap = calloc((size_t)rows + 8, 1);
    if (!nrow_wrap) {
      if (ncells != s->cells) {
        free(ncells);
      }
      return -1;
    }
    if (s->row_wrap) {
      int copy_rows = rows < old_rows ? rows : old_rows;
      memcpy(nrow_wrap, s->row_wrap, (size_t)copy_rows);
    }
  }

  if (s->alt_cells) {
    size_t alt_need = need;
    if ((size_t)s->alt_cap >= alt_need && cols == old_cols) {
      if (rows > old_rows) {
        for (int y = old_rows; y < rows; y++) {
          clear_cells(&s->alt_cells[y * cols], cols);
        }
      }
    } else {
      TraashCell *nalt = malloc(alloc * sizeof(TraashCell));
      if (nalt) {
        clear_cells(nalt, (int)alt_need);
        int copy_cols = cols < old_cols ? cols : old_cols;
        int copy_rows = rows < old_rows ? rows : old_rows;
        for (int y = 0; y < copy_rows; y++) {
          memcpy(&nalt[y * cols], &s->alt_cells[y * old_cols],
                 (size_t)copy_cols * sizeof(TraashCell));
        }
        free(s->alt_cells);
        s->alt_cells = nalt;
        s->alt_cap = (int)alloc;
      }
    }
  }

  if (ncells != s->cells) {
    free(s->cells);
    s->cells = ncells;
    s->cells_cap = (int)alloc;
  }
  if (nrow_wrap != s->row_wrap) {
    free(s->row_wrap);
    s->row_wrap = nrow_wrap;
    s->row_wrap_cap = rows + 8;
  }
  s->cols = cols;
  s->rows = rows;
  s->scroll_top = 0;
  s->scroll_bottom = rows - 1;
  if (s->cursor_x >= cols) {
    s->cursor_x = cols - 1;
  }
  if (s->cursor_y >= rows) {
    s->cursor_y = rows - 1;
  }
  s->dirty = true;
  return 0;
}

int traash_screen_sync_scrollback(TraashScreen *s) {
  if (!s || !s->scrollback) {
    return 0;
  }
  if (s->scrollback_cols == s->cols) {
    return 0;
  }
  /* Rebuild scrollback at live width. */
  int cols = s->cols;

  TraashCell *nsb =
      calloc((size_t)s->scrollback_cap * (size_t)cols, sizeof(TraashCell));
  uint8_t *nsb_wrap = calloc((size_t)s->scrollback_cap, 1);
  if (!nsb || !nsb_wrap) {
    free(nsb);
    free(nsb_wrap);
    return -1;
  }

  int new_sb_len = 0;
  if (!s->alt_screen && s->sb_wrap) {
    int max_out = s->scrollback_cap;
    TraashCell *out = calloc((size_t)max_out * (size_t)cols, sizeof(TraashCell));
    uint8_t *owrap = calloc((size_t)max_out, 1);
    int nlines = 0;
    int col = 0;
    if (out && owrap) {
      clear_cells(out, max_out * cols);
      int old_cols = s->scrollback_cols;
      for (int src = 0; src < s->scrollback_len && nlines < max_out; src++) {
        const TraashCell *row = sb_row_c(s, src);
        int wrapped = s->sb_wrap[sb_phys(s, src)];
        int last = row_last_occupied(row, old_cols);
        if (last < 0) {
          if (!wrapped) {
            if (col > 0) {
              owrap[nlines] = 0;
              nlines++;
              col = 0;
            }
            if (nlines < max_out) {
              owrap[nlines] = 0;
              nlines++;
            }
          }
          continue;
        }
        for (int x = 0; x <= last && nlines < max_out; x++) {
          out[nlines * cols + col] = row[x];
          col++;
          if (col >= cols) {
            owrap[nlines] = 1;
            nlines++;
            col = 0;
          }
        }
        if (!wrapped && col > 0 && nlines < max_out) {
          owrap[nlines] = 0;
          nlines++;
          col = 0;
        }
      }
      if (col > 0 && nlines < max_out) {
        owrap[nlines] = 0;
        nlines++;
      }
      int sb_drop = 0;
      int keep = nlines;
      if (keep > s->scrollback_cap) {
        sb_drop = keep - s->scrollback_cap;
        keep = s->scrollback_cap;
      }
      for (int y = 0; y < keep; y++) {
        memcpy(&nsb[y * cols], &out[(sb_drop + y) * cols],
               (size_t)cols * sizeof(TraashCell));
        nsb_wrap[y] = owrap[sb_drop + y];
      }
      new_sb_len = keep;
    } else {
      int copy = cols < s->scrollback_cols ? cols : s->scrollback_cols;
      for (int y = 0; y < s->scrollback_len && y < s->scrollback_cap; y++) {
        const TraashCell *src = sb_row_c(s, y);
        memcpy(&nsb[y * cols], src, (size_t)copy * sizeof(TraashCell));
        if (s->sb_wrap) {
          nsb_wrap[y] = s->sb_wrap[sb_phys(s, y)];
        }
        new_sb_len++;
      }
    }
    free(out);
    free(owrap);
  } else {
    int copy = cols < s->scrollback_cols ? cols : s->scrollback_cols;
    for (int y = 0; y < s->scrollback_len && y < s->scrollback_cap; y++) {
      const TraashCell *src = sb_row_c(s, y);
      memcpy(&nsb[y * cols], src, (size_t)copy * sizeof(TraashCell));
      if (s->sb_wrap) {
        nsb_wrap[y] = s->sb_wrap[sb_phys(s, y)];
      }
      new_sb_len++;
    }
  }

  free(s->scrollback);
  free(s->sb_wrap);
  s->scrollback = nsb;
  s->sb_wrap = nsb_wrap;
  s->scrollback_len = new_sb_len;
  s->scrollback_start = 0;
  s->scrollback_cols = cols;
  if (s->scroll_offset > s->scrollback_len) {
    s->scroll_offset = s->scrollback_len;
  }
  s->dirty = true;
  return 0;
}

int traash_screen_resize(TraashScreen *s, int cols, int rows) {
  if (cols < 1) {
    cols = 1;
  }
  if (rows < 1) {
    rows = 1;
  }
  if (cols == s->cols && rows == s->rows && s->scrollback_cols == cols) {
    return 0;
  }

  /* Live grid first (cheap), then sync scrollback if width changed. */
  if (traash_screen_resize_live(s, cols, rows) != 0) {
    return -1;
  }
  return traash_screen_sync_scrollback(s);
}

void traash_screen_set_alt(TraashScreen *s, int enable) {
  enable = enable ? 1 : 0;
  if (enable == (s->alt_screen ? 1 : 0)) {
    return;
  }
  if (enable) {
    if (!s->alt_cells) {
      size_t n = (size_t)s->cols * (size_t)s->rows;
      if (n < (size_t)s->cells_cap) {
        n = (size_t)s->cells_cap;
      }
      s->alt_cells = calloc(n, sizeof(TraashCell));
      if (!s->alt_cells) {
        return;
      }
      s->alt_cap = (int)n;
      for (int i = 0; i < s->cols * s->rows; i++) {
        traash_cell_clear(&s->alt_cells[i]);
      }
    }
    /* Swap main ↔ alt so cells always points at the active buffer. */
    TraashCell *tmp = s->cells;
    s->cells = s->alt_cells;
    s->alt_cells = tmp;
    int tmp_cap = s->cells_cap;
    s->cells_cap = s->alt_cap;
    s->alt_cap = tmp_cap;
    s->alt_screen = true;
    s->scroll_offset = 0;
    traash_screen_clear(s, 2);
    traash_screen_move_cursor(s, 0, 0);
  } else {
    TraashCell *tmp = s->cells;
    s->cells = s->alt_cells;
    s->alt_cells = tmp;
    int tmp_cap = s->cells_cap;
    s->cells_cap = s->alt_cap;
    s->alt_cap = tmp_cap;
    s->alt_screen = false;
    s->scroll_offset = 0;
  }
  s->scroll_top = 0;
  s->scroll_bottom = s->rows - 1;
  s->dirty = true;
}

TraashCell *traash_screen_cell(TraashScreen *s, int x, int y) {
  if (x < 0 || y < 0 || x >= s->cols || y >= s->rows) {
    return NULL;
  }
  return cell_at(s, x, y);
}

const TraashCell *traash_screen_view_cell(const TraashScreen *s, int x, int y) {
  static const TraashCell blank = {0};
  if (x < 0 || y < 0 || x >= s->cols || y >= s->rows) {
    return NULL;
  }
  /* Contiguous window over scrollback + live screen.
   * offset 0 → live screen; larger offset → older history at the top. */
  if (!s->alt_screen && s->scroll_offset > 0) {
    int abs_row = s->scrollback_len - s->scroll_offset + y;
    if (abs_row < 0) {
      return &blank;
    }
    if (abs_row < s->scrollback_len) {
      int sb_cols = s->scrollback_cols > 0 ? s->scrollback_cols : s->cols;
      if (x >= sb_cols) {
        return &blank;
      }
      return &sb_row_c(s, abs_row)[x];
    }
    int live_y = abs_row - s->scrollback_len;
    if (live_y >= 0 && live_y < s->rows) {
      return &s->cells[live_y * s->cols + x];
    }
    return &blank;
  }
  return &s->cells[y * s->cols + x];
}

static void push_scrollback_line(TraashScreen *s, const TraashCell *line, int wrapped) {
  int sb_cols = s->scrollback_cols > 0 ? s->scrollback_cols : s->cols;
  int copy = s->cols < sb_cols ? s->cols : sb_cols;
  if (s->scrollback_len < s->scrollback_cap) {
    int phys = sb_phys(s, s->scrollback_len);
    TraashCell *dst = &s->scrollback[phys * sb_cols];
    memcpy(dst, line, (size_t)copy * sizeof(TraashCell));
    for (int x = copy; x < sb_cols; x++) {
      traash_cell_clear(&dst[x]);
    }
    if (s->sb_wrap) {
      s->sb_wrap[phys] = wrapped ? 1 : 0;
    }
    s->scrollback_len++;
  } else {
    int phys = s->scrollback_start;
    TraashCell *dst = &s->scrollback[phys * sb_cols];
    memcpy(dst, line, (size_t)copy * sizeof(TraashCell));
    for (int x = copy; x < sb_cols; x++) {
      traash_cell_clear(&dst[x]);
    }
    if (s->sb_wrap) {
      s->sb_wrap[phys] = wrapped ? 1 : 0;
    }
    s->scrollback_start = s->scrollback_start + 1;
    if (s->scrollback_start >= s->scrollback_cap) {
      s->scrollback_start = 0;
    }
  }
}

void traash_screen_scroll_up(TraashScreen *s, int n) {
  for (int i = 0; i < n; i++) {
    if (s->scroll_top == 0 && !s->alt_screen) {
      int wrapped = s->row_wrap ? s->row_wrap[0] : 0;
      push_scrollback_line(s, &s->cells[0], wrapped);
    }
    for (int y = s->scroll_top; y < s->scroll_bottom; y++) {
      memcpy(cell_at(s, 0, y), cell_at(s, 0, y + 1),
             (size_t)s->cols * sizeof(TraashCell));
      if (s->row_wrap) {
        s->row_wrap[y] = s->row_wrap[y + 1];
      }
    }
    for (int x = 0; x < s->cols; x++) {
      traash_cell_clear(cell_at(s, x, s->scroll_bottom));
      cell_at(s, x, s->scroll_bottom)->fg = s->fg;
      cell_at(s, x, s->scroll_bottom)->bg = s->bg;
    }
    if (s->row_wrap) {
      s->row_wrap[s->scroll_bottom] = 0;
    }
  }
  s->dirty = true;
}

void traash_screen_scroll_down(TraashScreen *s, int n) {
  for (int i = 0; i < n; i++) {
    for (int y = s->scroll_bottom; y > s->scroll_top; y--) {
      memcpy(cell_at(s, 0, y), cell_at(s, 0, y - 1),
             (size_t)s->cols * sizeof(TraashCell));
      if (s->row_wrap) {
        s->row_wrap[y] = s->row_wrap[y - 1];
      }
    }
    for (int x = 0; x < s->cols; x++) {
      traash_cell_clear(cell_at(s, x, s->scroll_top));
    }
    if (s->row_wrap) {
      s->row_wrap[s->scroll_top] = 0;
    }
  }
  s->dirty = true;
}

void traash_screen_newline(TraashScreen *s) {
  if (s->cursor_y == s->scroll_bottom) {
    traash_screen_scroll_up(s, 1);
  } else if (s->cursor_y < s->rows - 1) {
    s->cursor_y++;
  }
  s->wrap_pending = false;
  s->dirty = true;
}

void traash_screen_cr(TraashScreen *s) {
  s->cursor_x = 0;
  s->wrap_pending = false;
  s->dirty = true;
}

void traash_screen_backspace(TraashScreen *s) {
  if (s->cursor_x > 0) {
    s->cursor_x--;
  }
  s->dirty = true;
}

void traash_screen_put_codepoint(TraashScreen *s, uint32_t cp) {
  int w = traash_utf8_width(cp);
  if (w <= 0) {
    return;
  }
  if (s->wrap_pending) {
    if (s->row_wrap) {
      s->row_wrap[s->cursor_y] = 1;
    }
    traash_screen_cr(s);
    traash_screen_newline(s);
  }
  if (s->auto_wrap && s->cursor_x + w > s->cols) {
    if (s->row_wrap) {
      s->row_wrap[s->cursor_y] = 1;
    }
    traash_screen_cr(s);
    traash_screen_newline(s);
  }
  TraashCell *c = cell_at(s, s->cursor_x, s->cursor_y);
  traash_cell_set(c, cp, s->fg, s->bg, s->attrs);
  c->width = (uint8_t)w;
  s->cursor_x += w;
  if (s->cursor_x >= s->cols) {
    s->cursor_x = s->cols - 1;
    s->wrap_pending = s->auto_wrap;
  }
  s->dirty = true;
}

void traash_screen_clear(TraashScreen *s, int mode) {
  int start = 0, end = s->rows * s->cols;
  if (mode == 0) {
    start = s->cursor_y * s->cols + s->cursor_x;
  } else if (mode == 1) {
    end = s->cursor_y * s->cols + s->cursor_x + 1;
  }
  for (int i = start; i < end; i++) {
    traash_cell_clear(&s->cells[i]);
    s->cells[i].fg = s->fg;
    s->cells[i].bg = s->bg;
  }
  s->dirty = true;
}

void traash_screen_clear_line(TraashScreen *s, int mode) {
  int start = 0, end = s->cols;
  if (mode == 0) {
    start = s->cursor_x;
  } else if (mode == 1) {
    end = s->cursor_x + 1;
  }
  for (int x = start; x < end; x++) {
    traash_cell_clear(cell_at(s, x, s->cursor_y));
  }
  s->dirty = true;
}

void traash_screen_move_cursor(TraashScreen *s, int x, int y) {
  if (x < 0) {
    x = 0;
  }
  if (y < 0) {
    y = 0;
  }
  if (x >= s->cols) {
    x = s->cols - 1;
  }
  if (y >= s->rows) {
    y = s->rows - 1;
  }
  s->cursor_x = x;
  s->cursor_y = y;
  s->wrap_pending = false;
  s->dirty = true;
}

void traash_screen_set_scroll_region(TraashScreen *s, int top, int bottom) {
  if (top < 0) {
    top = 0;
  }
  if (bottom >= s->rows) {
    bottom = s->rows - 1;
  }
  if (top >= bottom) {
    return;
  }
  s->scroll_top = top;
  s->scroll_bottom = bottom;
  traash_screen_move_cursor(s, 0, top);
}

void traash_screen_save_cursor(TraashScreen *s) {
  s->saved_x = s->cursor_x;
  s->saved_y = s->cursor_y;
}

void traash_screen_restore_cursor(TraashScreen *s) {
  traash_screen_move_cursor(s, s->saved_x, s->saved_y);
}

void traash_screen_insert_cells(TraashScreen *s, int n) {
  if (n < 1) {
    return;
  }
  if (n > s->cols - s->cursor_x) {
    n = s->cols - s->cursor_x;
  }
  TraashCell *row = cell_at(s, 0, s->cursor_y);
  memmove(row + s->cursor_x + n, row + s->cursor_x,
          (size_t)(s->cols - s->cursor_x - n) * sizeof(TraashCell));
  for (int i = 0; i < n; i++) {
    traash_cell_clear(&row[s->cursor_x + i]);
    row[s->cursor_x + i].fg = s->fg;
    row[s->cursor_x + i].bg = s->bg;
  }
  s->dirty = true;
}

void traash_screen_delete_cells(TraashScreen *s, int n) {
  if (n < 1) {
    return;
  }
  if (n > s->cols - s->cursor_x) {
    n = s->cols - s->cursor_x;
  }
  TraashCell *row = cell_at(s, 0, s->cursor_y);
  memmove(row + s->cursor_x, row + s->cursor_x + n,
          (size_t)(s->cols - s->cursor_x - n) * sizeof(TraashCell));
  for (int i = s->cols - n; i < s->cols; i++) {
    traash_cell_clear(&row[i]);
    row[i].fg = s->fg;
    row[i].bg = s->bg;
  }
  s->dirty = true;
}

void traash_screen_erase_chars(TraashScreen *s, int n) {
  if (n < 1) {
    return;
  }
  if (n > s->cols - s->cursor_x) {
    n = s->cols - s->cursor_x;
  }
  for (int i = 0; i < n; i++) {
    TraashCell *c = cell_at(s, s->cursor_x + i, s->cursor_y);
    traash_cell_clear(c);
    c->fg = s->fg;
    c->bg = s->bg;
  }
  s->dirty = true;
}

void traash_screen_insert_lines(TraashScreen *s, int n) {
  /* Insert blank lines at cursor within scroll region (like CSI L). */
  if (n < 1 || s->cursor_y < s->scroll_top || s->cursor_y > s->scroll_bottom) {
    return;
  }
  int bottom = s->scroll_bottom;
  int space = bottom - s->cursor_y + 1;
  if (n > space) {
    n = space;
  }
  for (int i = 0; i < n; i++) {
    for (int y = bottom; y > s->cursor_y; y--) {
      memcpy(cell_at(s, 0, y), cell_at(s, 0, y - 1), (size_t)s->cols * sizeof(TraashCell));
    }
    for (int x = 0; x < s->cols; x++) {
      traash_cell_clear(cell_at(s, x, s->cursor_y));
      cell_at(s, x, s->cursor_y)->fg = s->fg;
      cell_at(s, x, s->cursor_y)->bg = s->bg;
    }
  }
  s->dirty = true;
}

void traash_screen_delete_lines(TraashScreen *s, int n) {
  if (n < 1 || s->cursor_y < s->scroll_top || s->cursor_y > s->scroll_bottom) {
    return;
  }
  int bottom = s->scroll_bottom;
  int space = bottom - s->cursor_y + 1;
  if (n > space) {
    n = space;
  }
  for (int i = 0; i < n; i++) {
    for (int y = s->cursor_y; y < bottom; y++) {
      memcpy(cell_at(s, 0, y), cell_at(s, 0, y + 1), (size_t)s->cols * sizeof(TraashCell));
    }
    for (int x = 0; x < s->cols; x++) {
      traash_cell_clear(cell_at(s, x, bottom));
      cell_at(s, x, bottom)->fg = s->fg;
      cell_at(s, x, bottom)->bg = s->bg;
    }
  }
  s->dirty = true;
}

static uint32_t fold_cp(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') {
    return cp + 32u;
  }
  return cp;
}

static const TraashCell *abs_cell(const TraashScreen *s, int abs_row, int col) {
  if (!s || col < 0 || col >= s->cols || abs_row < 0) {
    return NULL;
  }
  if (abs_row < s->scrollback_len) {
    int sb_cols = s->scrollback_cols > 0 ? s->scrollback_cols : s->cols;
    if (col >= sb_cols) {
      return NULL;
    }
    return &sb_row_c(s, abs_row)[col];
  }
  int y = abs_row - s->scrollback_len;
  if (y < 0 || y >= s->rows) {
    return NULL;
  }
  return &s->cells[y * s->cols + col];
}

int traash_screen_query_len(const char *query) {
  if (!query || !query[0]) {
    return 0;
  }
  int n = 0;
  const uint8_t *p = (const uint8_t *)query;
  size_t left = strlen(query);
  while (left > 0) {
    uint32_t cp = 0;
    int used = traash_utf8_decode(p, left, &cp);
    if (used <= 0) {
      break;
    }
    n++;
    p += used;
    left -= (size_t)used;
  }
  return n;
}

int traash_screen_view_abs_row(const TraashScreen *s, int view_y) {
  if (!s || view_y < 0 || view_y >= s->rows) {
    return -1;
  }
  if (!s->alt_screen && s->scroll_offset > 0) {
    return s->scrollback_len - s->scroll_offset + view_y;
  }
  return s->scrollback_len + view_y;
}

int traash_screen_abs_to_view_y(const TraashScreen *s, int abs_row) {
  if (!s || abs_row < 0) {
    return -1;
  }
  for (int y = 0; y < s->rows; y++) {
    if (traash_screen_view_abs_row(s, y) == abs_row) {
      return y;
    }
  }
  return -1;
}

void traash_screen_reveal_row(TraashScreen *s, int abs_row) {
  if (!s || abs_row < 0) {
    return;
  }
  int total = s->scrollback_len + s->rows;
  if (abs_row >= total) {
    return;
  }
  if (abs_row >= s->scrollback_len) {
    s->scroll_offset = 0;
    return;
  }
  /* Center the match: abs_row = scrollback_len - offset + rows/2 */
  int offset = s->scrollback_len - abs_row + s->rows / 2;
  if (offset < 0) {
    offset = 0;
  }
  if (offset > s->scrollback_len) {
    offset = s->scrollback_len;
  }
  s->scroll_offset = offset;
}

int traash_screen_find_all(const TraashScreen *s, const char *query, TraashSearchMatch *out,
                           int max_out) {
  if (!s || !query || !query[0] || !out || max_out <= 0) {
    return 0;
  }

  uint32_t qcp[64];
  int qlen = 0;
  const uint8_t *p = (const uint8_t *)query;
  size_t left = strlen(query);
  while (left > 0 && qlen < (int)(sizeof(qcp) / sizeof(qcp[0]))) {
    uint32_t cp = 0;
    int used = traash_utf8_decode(p, left, &cp);
    if (used <= 0) {
      break;
    }
    qcp[qlen++] = fold_cp(cp);
    p += used;
    left -= (size_t)used;
  }
  if (qlen <= 0) {
    return 0;
  }

  int total_rows = s->scrollback_len + s->rows;
  int count = 0;
  for (int row = 0; row < total_rows && count < max_out; row++) {
    for (int col = 0; col + qlen <= s->cols && count < max_out; col++) {
      int match = 1;
      for (int i = 0; i < qlen; i++) {
        const TraashCell *c = abs_cell(s, row, col + i);
        uint32_t cp = c ? c->cp : 0;
        if (cp == 0) {
          cp = ' ';
        }
        if (fold_cp(cp) != qcp[i]) {
          match = 0;
          break;
        }
      }
      if (match) {
        out[count].row = row;
        out[count].col = col;
        count++;
      }
    }
  }
  return count;
}

int traash_screen_search(const TraashScreen *s, const char *query, int *out_x,
                         int *out_y) {
  TraashSearchMatch all[64];
  int n = traash_screen_find_all(s, query, all, 64);
  if (n <= 0) {
    return 0;
  }
  int pick = 0;
  for (int i = 0; i < n; i++) {
    if (all[i].row >= s->scrollback_len) {
      pick = i;
      break;
    }
  }
  if (out_x) {
    *out_x = all[pick].col;
  }
  if (out_y) {
    *out_y = all[pick].row >= s->scrollback_len ? all[pick].row - s->scrollback_len : 0;
  }
  return 1;
}

void traash_screen_dump(const TraashScreen *s, char *buf, size_t n) {
  size_t o = 0;
  for (int y = 0; y < s->rows && o + 2 < n; y++) {
    for (int x = 0; x < s->cols && o + 2 < n; x++) {
      uint32_t cp = s->cells[y * s->cols + x].cp;
      if (cp < 128) {
        buf[o++] = (char)(cp ? cp : ' ');
      } else {
        buf[o++] = '?';
      }
    }
    buf[o++] = '\n';
  }
  if (o < n) {
    buf[o] = 0;
  } else if (n) {
    buf[n - 1] = 0;
  }
}
