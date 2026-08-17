#include "ui/context_menu.h"

#include <string.h>

void traash_context_menu_open(TraashContextMenu *m, float x, float y) {
  m->open = 1;
  m->x = x;
  m->y = y;
  m->hover = -1;
}

void traash_context_menu_close(TraashContextMenu *m) {
  memset(m, 0, sizeof(*m));
  m->hover = -1;
}

const char *traash_context_menu_label(int index) {
  static const char *labels[] = {"Copy", "Paste", "Split Right", "Split Down",
                                 "Preferences..."};
  if (index < 0 || index >= TRAASH_MENU_COUNT) {
    return "";
  }
  return labels[index];
}

int traash_context_menu_sep_after(int index) {
  return index == TRAASH_MENU_PASTE || index == TRAASH_MENU_SPLIT_V;
}

float traash_context_menu_width(float scale) {
  return 240.0f * scale;
}

float traash_context_menu_row_height(float scale) {
  return 36.0f * scale;
}

float traash_context_menu_height(float scale) {
  float row = traash_context_menu_row_height(scale);
  float sep = 9.0f * scale;
  float pad = 8.0f * scale;
  float h = pad * 2.0f + row * (float)TRAASH_MENU_COUNT;
  for (int i = 0; i < TRAASH_MENU_COUNT; i++) {
    if (traash_context_menu_sep_after(i)) {
      h += sep;
    }
  }
  return h;
}

int traash_context_menu_hit(const TraashContextMenu *m, float x, float y, float scale) {
  if (!m->open) {
    return -1;
  }
  float w = traash_context_menu_width(scale);
  float row = traash_context_menu_row_height(scale);
  float sep = 9.0f * scale;
  float pad = 8.0f * scale;
  float h = traash_context_menu_height(scale);
  if (x < m->x || y < m->y || x > m->x + w || y > m->y + h) {
    return -1;
  }
  float cy = m->y + pad;
  for (int i = 0; i < TRAASH_MENU_COUNT; i++) {
    if (y >= cy && y < cy + row) {
      return i;
    }
    cy += row;
    if (traash_context_menu_sep_after(i)) {
      cy += sep;
    }
  }
  return -1;
}
