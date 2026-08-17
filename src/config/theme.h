#ifndef TRAASH_THEME_H
#define TRAASH_THEME_H

#include <stdint.h>

typedef struct {
  uint32_t foreground;
  uint32_t background;
  uint32_t cursor;
  uint32_t cursor_text;
  uint32_t selection_fg;
  uint32_t selection_bg;
  uint32_t ansi[16];
  uint32_t tab_bar;
  uint32_t active_tab;
  uint32_t inactive_tab;
  uint32_t pane_border;
  uint32_t active_pane_border;
  uint32_t status_bar_fg;
  uint32_t status_bar_bg;
  char name[64];
} TraashTheme;

int traash_theme_load(void *lua_state, const char *name, TraashTheme *out);
uint32_t traash_theme_resolve_fg(const TraashTheme *t, uint32_t cell_fg, int truecolor);
uint32_t traash_theme_resolve_bg(const TraashTheme *t, uint32_t cell_bg, int truecolor);

#endif
