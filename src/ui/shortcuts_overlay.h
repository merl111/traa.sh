#ifndef TRAASH_SHORTCUTS_OVERLAY_H
#define TRAASH_SHORTCUTS_OVERLAY_H

typedef struct {
  int open;
} TraashShortcutsOverlay;

typedef struct {
  float x, y, w, h; /* panel in framebuffer pixels */
  int columns;      /* 1 or 2 */
  float pad;
  float title_h;
  float subtitle_h;
  float row_h;
  float section_gap;
  float col_gap;
  float footer_h;
  float content_top; /* y where group rows start */
  float rad;
} TraashShortcutsLayout;

void traash_shortcuts_overlay_init(TraashShortcutsOverlay *o);
void traash_shortcuts_overlay_toggle(TraashShortcutsOverlay *o);
void traash_shortcuts_overlay_close(TraashShortcutsOverlay *o);

/* Compute centered panel geometry for the given framebuffer size. */
void traash_shortcuts_overlay_layout(int fb_w, int fb_h, float scale, int cell_h,
                                     TraashShortcutsLayout *out);

/* 1 if (mx,my) is outside the panel (backdrop). Requires open overlay. */
int traash_shortcuts_overlay_hit_backdrop(const TraashShortcutsOverlay *o, float mx, float my,
                                          int fb_w, int fb_h, float scale, int cell_h);

#endif
