#ifndef TRAASH_OVERVIEW_OVERLAY_H
#define TRAASH_OVERVIEW_OVERLAY_H

#include "mux/session.h"

typedef enum { TRAASH_OVERVIEW_TABS = 0, TRAASH_OVERVIEW_PANES } TraashOverviewMode;

typedef struct {
  int open;
  TraashOverviewMode mode;
  int selected;     /* index into the current level */
  int scroll;       /* first visible card index */
  int pane_tab_id;  /* window id while viewing panes */
} TraashOverviewOverlay;

typedef struct {
  float x, y, w, h;
  float pad;
  float title_h;
  float subtitle_h;
  float footer_h;
  float content_top;
  float rad;
  int cols;
  int rows;
  int visible;
  float card_w, card_h;
  float gap_x, gap_y;
  float preview_h;
  float meta_h;
  float close_sz;
} TraashOverviewLayout;

typedef enum {
  TRAASH_OVERVIEW_HIT_NONE = 0,
  TRAASH_OVERVIEW_HIT_CARD,
  TRAASH_OVERVIEW_HIT_CLOSE,
  TRAASH_OVERVIEW_HIT_BACKDROP
} TraashOverviewHitKind;

typedef struct {
  TraashOverviewHitKind kind;
  int index;
  TraashWindow *window;
  TraashPane *pane;
} TraashOverviewHit;

void traash_overview_overlay_init(TraashOverviewOverlay *o);
void traash_overview_overlay_close(TraashOverviewOverlay *o);
void traash_overview_overlay_toggle(TraashOverviewOverlay *o, TraashSession *s);
void traash_overview_overlay_open_tabs(TraashOverviewOverlay *o, TraashSession *s);
void traash_overview_overlay_drill_panes(TraashOverviewOverlay *o, TraashWindow *w);
void traash_overview_overlay_back(TraashOverviewOverlay *o, TraashSession *s);

int traash_overview_overlay_item_count(const TraashOverviewOverlay *o, TraashSession *s);
TraashWindow *traash_overview_overlay_nth_window(TraashSession *s, int i);
TraashPane *traash_overview_overlay_nth_pane(TraashWindow *w, int i);
TraashWindow *traash_overview_overlay_pane_tab(const TraashOverviewOverlay *o, TraashSession *s);
TraashWindow *traash_overview_overlay_selected_window(const TraashOverviewOverlay *o,
                                                      TraashSession *s);
TraashPane *traash_overview_overlay_selected_pane(const TraashOverviewOverlay *o,
                                                  TraashSession *s);

void traash_overview_overlay_layout(int fb_w, int fb_h, float scale, int cell_h, int item_count,
                                    TraashOverviewLayout *out);
void traash_overview_overlay_card_rect(const TraashOverviewLayout *lay, int visible_index,
                                       float *x, float *y, float *w, float *h);
void traash_overview_overlay_close_rect(const TraashOverviewLayout *lay, int visible_index,
                                        float *x, float *y, float *w, float *h);
void traash_overview_overlay_clamp(TraashOverviewOverlay *o, TraashSession *s,
                                   const TraashOverviewLayout *lay);
void traash_overview_overlay_move(TraashOverviewOverlay *o, TraashSession *s, int dx, int dy,
                                  const TraashOverviewLayout *lay);

TraashOverviewHit traash_overview_overlay_hit(const TraashOverviewOverlay *o, TraashSession *s,
                                              float mx, float my, int fb_w, int fb_h, float scale,
                                              int cell_h);

#endif
