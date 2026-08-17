#include "term/selection.h"

#include "util/utf8.h"

#include <ctype.h>
#include <string.h>

void traash_selection_clear(TraashSelection *sel) {
  memset(sel, 0, sizeof(*sel));
}

void traash_selection_begin(TraashSelection *sel, int x, int y) {
  sel->ax = sel->bx = x;
  sel->ay = sel->by = y;
  sel->active = 1;
  sel->selecting = 1;
}

void traash_selection_update(TraashSelection *sel, int x, int y) {
  if (!sel->active) {
    return;
  }
  sel->bx = x;
  sel->by = y;
}

void traash_selection_end(TraashSelection *sel) {
  sel->selecting = 0;
  if (sel->ax == sel->bx && sel->ay == sel->by) {
    sel->active = 0;
  }
}

/* 0 blank, 1 word (alnum/_), 2 other (path separators, punctuation, …) */
static int char_class(uint32_t cp) {
  if (cp == 0 || cp == ' ' || cp == '\t') {
    return 0;
  }
  if (cp < 0x80) {
    unsigned char c = (unsigned char)cp;
    if (isalnum(c) || c == '_') {
      return 1;
    }
    return 2;
  }
  /* Treat non-ASCII graphic as word so emoji/paths segments stay selectable */
  if (cp == 0xa0 /* nbsp */) {
    return 0;
  }
  return 1;
}

static uint32_t cell_cp(const TraashScreen *s, int x, int y) {
  const TraashCell *c = traash_screen_view_cell(s, x, y);
  if (!c || !c->cp) {
    return 0;
  }
  return c->cp;
}

void traash_selection_select_word(TraashSelection *sel, const TraashScreen *s, int x,
                                  int y) {
  if (!sel || !s || x < 0 || y < 0 || x >= s->cols || y >= s->rows) {
    traash_selection_clear(sel);
    return;
  }
  int cls = char_class(cell_cp(s, x, y));
  int x0 = x;
  int x1 = x;
  while (x0 > 0 && char_class(cell_cp(s, x0 - 1, y)) == cls) {
    x0--;
  }
  while (x1 + 1 < s->cols && char_class(cell_cp(s, x1 + 1, y)) == cls) {
    x1++;
  }
  sel->ax = x0;
  sel->ay = y;
  sel->bx = x1;
  sel->by = y;
  sel->active = 1;
  sel->selecting = 0;
}

static void norm(const TraashSelection *sel, int *x0, int *y0, int *x1, int *y1) {
  *x0 = sel->ax;
  *y0 = sel->ay;
  *x1 = sel->bx;
  *y1 = sel->by;
  if (*y1 < *y0 || (*y1 == *y0 && *x1 < *x0)) {
    int tx = *x0, ty = *y0;
    *x0 = *x1;
    *y0 = *y1;
    *x1 = tx;
    *y1 = ty;
  }
}

int traash_selection_contains(const TraashSelection *sel, int x, int y) {
  if (!sel || !sel->active) {
    return 0;
  }
  int x0, y0, x1, y1;
  norm(sel, &x0, &y0, &x1, &y1);
  if (y < y0 || y > y1) {
    return 0;
  }
  if (y0 == y1) {
    return x >= x0 && x <= x1;
  }
  if (y == y0) {
    return x >= x0;
  }
  if (y == y1) {
    return x <= x1;
  }
  return 1;
}

size_t traash_selection_text(const TraashSelection *sel, const TraashScreen *s,
                             char *buf, size_t n) {
  if (!sel->active || n == 0) {
    if (n) {
      buf[0] = 0;
    }
    return 0;
  }
  int x0, y0, x1, y1;
  norm(sel, &x0, &y0, &x1, &y1);
  size_t o = 0;
  for (int y = y0; y <= y1; y++) {
    int xs = (y == y0) ? x0 : 0;
    int xe = (y == y1) ? x1 : s->cols - 1;
    if (xs < 0) {
      xs = 0;
    }
    if (xe >= s->cols) {
      xe = s->cols - 1;
    }
    if (y < 0 || y >= s->rows) {
      continue;
    }
    for (int x = xs; x <= xe && o + 5 < n; x++) {
      const TraashCell *c = traash_screen_view_cell(s, x, y);
      if (!c) {
        continue;
      }
      uint8_t tmp[4];
      int k = traash_utf8_encode(c->cp ? c->cp : ' ', tmp);
      for (int i = 0; i < k && o + 1 < n; i++) {
        buf[o++] = (char)tmp[i];
      }
    }
    if (y != y1 && o + 1 < n) {
      buf[o++] = '\n';
    }
  }
  if (o < n) {
    buf[o] = 0;
  } else {
    buf[n - 1] = 0;
  }
  return o;
}
