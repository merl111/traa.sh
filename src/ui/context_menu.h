#ifndef TRAASH_CONTEXT_MENU_H
#define TRAASH_CONTEXT_MENU_H

enum {
  TRAASH_MENU_COPY = 0,
  TRAASH_MENU_PASTE = 1,
  TRAASH_MENU_SPLIT_H = 2,
  TRAASH_MENU_SPLIT_V = 3,
  TRAASH_MENU_SETTINGS = 4,
  TRAASH_MENU_COUNT = 5
};

typedef struct {
  int open;
  float x, y; /* framebuffer pixels */
  int hover;  /* row index or -1 */
} TraashContextMenu;

void traash_context_menu_open(TraashContextMenu *m, float x, float y);
void traash_context_menu_close(TraashContextMenu *m);
int traash_context_menu_hit(const TraashContextMenu *m, float x, float y, float scale);
const char *traash_context_menu_label(int index);
/* 1 = separator row drawn after this item */
int traash_context_menu_sep_after(int index);
float traash_context_menu_width(float scale);
float traash_context_menu_row_height(float scale);
float traash_context_menu_height(float scale);

#endif
