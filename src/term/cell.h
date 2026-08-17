#ifndef TRAASH_CELL_H
#define TRAASH_CELL_H

#include <stdint.h>

enum {
  TRAASH_ATTR_BOLD = 1 << 0,
  TRAASH_ATTR_ITALIC = 1 << 1,
  TRAASH_ATTR_UNDERLINE = 1 << 2,
  TRAASH_ATTR_INVERSE = 1 << 3,
  TRAASH_ATTR_FAINT = 1 << 4,
  TRAASH_ATTR_STRIKE = 1 << 5,
  TRAASH_ATTR_TRUECOLOR_FG = 1 << 6,
  TRAASH_ATTR_TRUECOLOR_BG = 1 << 7,
  TRAASH_ATTR_UNDERCURL = 1 << 8,
  TRAASH_ATTR_UNDERDOUBLE = 1 << 9
};

typedef struct {
  uint32_t cp;
  uint32_t fg; /* ANSI index or 0xRRGGBB if TRUECOLOR_FG */
  uint32_t bg;
  uint16_t attrs;
  uint8_t width;
} TraashCell;

void traash_cell_clear(TraashCell *c);
void traash_cell_set(TraashCell *c, uint32_t cp, uint32_t fg, uint32_t bg,
                     uint16_t attrs);

#endif
