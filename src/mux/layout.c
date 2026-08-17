#include "mux/layout.h"

void traash_layout_split_pair(TraashPane *a, TraashPane *b, int vertical) {
  if (vertical) {
    float half = a->w * 0.5f;
    b->x = a->x + half;
    b->y = a->y;
    b->w = a->w - half;
    b->h = a->h;
    a->w = half;
  } else {
    float half = a->h * 0.5f;
    b->x = a->x;
    b->y = a->y + half;
    b->w = a->w;
    b->h = a->h - half;
    a->h = half;
  }
}

void traash_layout_apply_pixel_sizes(TraashPane *panes, int win_cols, int win_rows,
                                     int *out_cols, int *out_rows) {
  int i = 0;
  for (TraashPane *p = panes; p; p = p->next, i++) {
    int cols = (int)(p->w * (float)win_cols);
    int rows = (int)(p->h * (float)win_rows);
    if (cols < 1) {
      cols = 1;
    }
    if (rows < 1) {
      rows = 1;
    }
    if (out_cols) {
      out_cols[i] = cols;
    }
    if (out_rows) {
      out_rows[i] = rows;
    }
    if (p->screen.cols != cols || p->screen.rows != rows) {
      traash_pane_resize_cells(p, cols, rows);
    }
  }
}

void traash_layout_reflow_equal(TraashPane *panes) {
  int n = 0;
  for (TraashPane *p = panes; p; p = p->next) {
    n++;
  }
  if (n < 1) {
    return;
  }
  float frac = 1.0f / (float)n;
  int i = 0;
  for (TraashPane *p = panes; p; p = p->next, i++) {
    p->x = frac * (float)i;
    p->y = 0.0f;
    p->w = frac;
    p->h = 1.0f;
  }
}
