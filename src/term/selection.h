#ifndef TRAASH_SELECTION_H
#define TRAASH_SELECTION_H

#include "term/screen.h"

#include <stddef.h>

typedef struct {
  int ax, ay, bx, by;
  int active;
  int selecting; /* mouse drag in progress */
} TraashSelection;

void traash_selection_clear(TraashSelection *sel);
void traash_selection_begin(TraashSelection *sel, int x, int y);
void traash_selection_update(TraashSelection *sel, int x, int y);
void traash_selection_end(TraashSelection *sel);
/* Double-click: select run of same character class around (x,y) on that line. */
void traash_selection_select_word(TraashSelection *sel, const TraashScreen *s, int x,
                                  int y);
int traash_selection_contains(const TraashSelection *sel, int x, int y);
/* Extract selected text into buf. Returns bytes written. */
size_t traash_selection_text(const TraashSelection *sel, const TraashScreen *s,
                             char *buf, size_t n);

#endif
