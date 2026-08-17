#ifndef TRAASH_QUIT_CONFIRM_H
#define TRAASH_QUIT_CONFIRM_H

#include "mux/server.h"

#include <stddef.h>

#define TRAASH_QUIT_CONFIRM_MAX 32

typedef enum {
  TRAASH_QUIT_KIND_APP = 0,
  TRAASH_QUIT_KIND_TAB,
  TRAASH_QUIT_KIND_PANE
} TraashQuitKind;

typedef struct {
  char tab[32];
  char title[64];
  char process[160];
  int pid;
} TraashQuitConfirmItem;

typedef struct {
  int open;
  int focus; /* 0 = Cancel, 1 = Close anyway */
  int count;
  TraashQuitKind kind;
  int target_window_id;
  int target_pane_id;
  TraashQuitConfirmItem items[TRAASH_QUIT_CONFIRM_MAX];
} TraashQuitConfirm;

typedef struct {
  float x, y, w, h;
  float pad;
  float title_h;
  float row_h;
  float footer_h;
  float content_top;
  float rad;
  int visible_rows;
  float btn_cancel_x, btn_ok_x, btn_y, btn_w, btn_h;
} TraashQuitConfirmGeom;

void traash_quit_confirm_init(TraashQuitConfirm *q);
void traash_quit_confirm_format_process(const char *raw, char *out, size_t n);
void traash_quit_confirm_close(TraashQuitConfirm *q);
/* Scan mux for panes with a foreground process; opens dialog if any. Returns count. */
int traash_quit_confirm_scan(TraashQuitConfirm *q, const TraashMuxServer *mux);
int traash_quit_confirm_scan_window(TraashQuitConfirm *q, const TraashWindow *w);
int traash_quit_confirm_scan_pane(TraashQuitConfirm *q, const TraashPane *p);
/* 1 if the app should ask before quitting (fills/opens dialog). */
int traash_quit_confirm_needed(TraashQuitConfirm *q, const TraashMuxServer *mux);
int traash_quit_confirm_needed_window(TraashQuitConfirm *q, const TraashWindow *w);
int traash_quit_confirm_needed_pane(TraashQuitConfirm *q, const TraashPane *p);

void traash_quit_confirm_geom(int fb_w, int fb_h, float scale, int cell_h, int row_count,
                              TraashQuitConfirmGeom *out);

int traash_quit_confirm_hit_backdrop(const TraashQuitConfirm *q, float mx, float my, int fb_w,
                                     int fb_h, float scale, int cell_h);
/* -1 none, 0 cancel, 1 confirm */
int traash_quit_confirm_hit_button(const TraashQuitConfirm *q, float mx, float my, int fb_w,
                                   int fb_h, float scale, int cell_h);

#endif
