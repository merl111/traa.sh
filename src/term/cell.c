#include "term/cell.h"

void traash_cell_clear(TraashCell *c) {
  c->cp = ' ';
  c->fg = 7;
  c->bg = 0;
  c->attrs = 0;
  c->width = 1;
}

void traash_cell_set(TraashCell *c, uint32_t cp, uint32_t fg, uint32_t bg,
                     uint16_t attrs) {
  c->cp = cp ? cp : ' ';
  c->fg = fg;
  c->bg = bg;
  c->attrs = attrs;
  c->width = 1;
}
