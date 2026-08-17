#ifndef TRAASH_PALETTE_H
#define TRAASH_PALETTE_H

#include "input/actions.h"

#include <stdbool.h>

typedef struct {
  bool open;
  char query[128];
  int selected;
  TraashAction actions[32];
  int count;
} TraashPalette;

void traash_palette_init(TraashPalette *p);
void traash_palette_toggle(TraashPalette *p);
void traash_palette_filter(TraashPalette *p, const char *query);
TraashAction traash_palette_activate(TraashPalette *p);

#endif
