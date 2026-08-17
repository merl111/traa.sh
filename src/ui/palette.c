#include "ui/palette.h"

#include <stdio.h>
#include <string.h>

void traash_palette_init(TraashPalette *p) {
  memset(p, 0, sizeof(*p));
  static const TraashAction defaults[] = {
      TRAASH_ACTION_SPLIT_H,         TRAASH_ACTION_SPLIT_V,      TRAASH_ACTION_PANE_NEXT,
      TRAASH_ACTION_PANE_LEFT,       TRAASH_ACTION_PANE_DOWN,    TRAASH_ACTION_PANE_UP,
      TRAASH_ACTION_PANE_RIGHT,      TRAASH_ACTION_ZOOM,         TRAASH_ACTION_NEW_WINDOW,
      TRAASH_ACTION_NEXT_WINDOW,     TRAASH_ACTION_PREV_WINDOW,  TRAASH_ACTION_THEME_CYCLE,
      TRAASH_ACTION_STATUS_CYCLE,    TRAASH_ACTION_FONT_INCREASE, TRAASH_ACTION_FONT_DECREASE,
      TRAASH_ACTION_DEMO,            TRAASH_ACTION_RELOAD_CONFIG,
      TRAASH_ACTION_SETTINGS,        TRAASH_ACTION_SHORTCUTS,    TRAASH_ACTION_LAYOUT_PICKER,
      TRAASH_ACTION_OVERVIEW,        TRAASH_ACTION_SEARCH,       TRAASH_ACTION_QUIT};
  p->count = (int)(sizeof(defaults) / sizeof(defaults[0]));
  memcpy(p->actions, defaults, sizeof(defaults));
}

void traash_palette_toggle(TraashPalette *p) {
  p->open = !p->open;
  p->query[0] = 0;
  p->selected = 0;
}

void traash_palette_filter(TraashPalette *p, const char *query) {
  snprintf(p->query, sizeof(p->query), "%s", query ? query : "");
  p->selected = 0;
}

TraashAction traash_palette_activate(TraashPalette *p) {
  if (!p->open || p->count == 0) {
    return TRAASH_ACTION_NONE;
  }
  if (p->selected < 0 || p->selected >= p->count) {
    return TRAASH_ACTION_NONE;
  }
  TraashAction a = p->actions[p->selected];
  p->open = false;
  return a;
}
