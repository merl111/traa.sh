#ifndef TRAASH_LAYOUT_OVERLAY_H
#define TRAASH_LAYOUT_OVERLAY_H

#include "mux/layout_store.h"

typedef struct {
  int open;
  int selected;
  int count;
  TraashLayoutName names[TRAASH_LAYOUT_MAX_NAMES];
  /* 1 = typing a name to save the current session */
  int save_mode;
  char save_name[TRAASH_LAYOUT_NAME_LEN];
} TraashLayoutOverlay;

typedef struct {
  float x, y, w, h;
  float pad;
  float title_h;
  float row_h;
  float footer_h;
  float content_top;
  float rad;
  int visible_rows;
} TraashLayoutOverlayGeom;

void traash_layout_overlay_init(TraashLayoutOverlay *o);
void traash_layout_overlay_close(TraashLayoutOverlay *o);
void traash_layout_overlay_toggle(TraashLayoutOverlay *o);
void traash_layout_overlay_refresh(TraashLayoutOverlay *o);

void traash_layout_overlay_geom(int fb_w, int fb_h, float scale, int cell_h,
                                int row_count, TraashLayoutOverlayGeom *out);

/* 1 if click is on backdrop (outside panel). */
int traash_layout_overlay_hit_backdrop(const TraashLayoutOverlay *o, float mx, float my,
                                       int fb_w, int fb_h, float scale, int cell_h);

/* Returns row index under cursor, or -1. */
int traash_layout_overlay_hit_row(const TraashLayoutOverlay *o, float mx, float my, int fb_w,
                                  int fb_h, float scale, int cell_h);

#endif
