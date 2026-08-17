#include "ui/overview_overlay.h"

#include <math.h>
#include <string.h>

void traash_overview_overlay_init(TraashOverviewOverlay *o) {
  memset(o, 0, sizeof(*o));
  o->pane_tab_id = -1;
}

void traash_overview_overlay_close(TraashOverviewOverlay *o) {
  if (!o) {
    return;
  }
  o->open = 0;
  o->mode = TRAASH_OVERVIEW_TABS;
  o->selected = 0;
  o->scroll = 0;
  o->pane_tab_id = -1;
}

static int window_count(const TraashSession *s) {
  int n = 0;
  if (!s) {
    return 0;
  }
  for (const TraashWindow *w = s->windows; w; w = w->next) {
    n++;
  }
  return n;
}

static int window_index(const TraashSession *s, const TraashWindow *want) {
  int i = 0;
  if (!s || !want) {
    return 0;
  }
  for (const TraashWindow *w = s->windows; w; w = w->next, i++) {
    if (w == want) {
      return i;
    }
  }
  return 0;
}

static int pane_index(const TraashWindow *w, const TraashPane *want) {
  int i = 0;
  if (!w || !want) {
    return 0;
  }
  for (const TraashPane *p = w->panes; p; p = p->next, i++) {
    if (p == want) {
      return i;
    }
  }
  return 0;
}

TraashWindow *traash_overview_overlay_nth_window(TraashSession *s, int i) {
  if (!s || i < 0) {
    return NULL;
  }
  int n = 0;
  for (TraashWindow *w = s->windows; w; w = w->next, n++) {
    if (n == i) {
      return w;
    }
  }
  return NULL;
}

TraashPane *traash_overview_overlay_nth_pane(TraashWindow *w, int i) {
  if (!w || i < 0) {
    return NULL;
  }
  int n = 0;
  for (TraashPane *p = w->panes; p; p = p->next, n++) {
    if (n == i) {
      return p;
    }
  }
  return NULL;
}

TraashWindow *traash_overview_overlay_pane_tab(const TraashOverviewOverlay *o, TraashSession *s) {
  if (!o || !s || o->pane_tab_id < 0) {
    return NULL;
  }
  for (TraashWindow *w = s->windows; w; w = w->next) {
    if (w->id == o->pane_tab_id) {
      return w;
    }
  }
  return NULL;
}

int traash_overview_overlay_item_count(const TraashOverviewOverlay *o, TraashSession *s) {
  if (!o || !s) {
    return 0;
  }
  if (o->mode == TRAASH_OVERVIEW_PANES) {
    TraashWindow *w = traash_overview_overlay_pane_tab(o, s);
    return traash_window_pane_count(w);
  }
  return window_count(s);
}

TraashWindow *traash_overview_overlay_selected_window(const TraashOverviewOverlay *o,
                                                      TraashSession *s) {
  if (!o || !s) {
    return NULL;
  }
  if (o->mode == TRAASH_OVERVIEW_PANES) {
    return traash_overview_overlay_pane_tab(o, s);
  }
  return traash_overview_overlay_nth_window(s, o->selected);
}

TraashPane *traash_overview_overlay_selected_pane(const TraashOverviewOverlay *o,
                                                  TraashSession *s) {
  if (!o || o->mode != TRAASH_OVERVIEW_PANES) {
    return NULL;
  }
  return traash_overview_overlay_nth_pane(traash_overview_overlay_pane_tab(o, s), o->selected);
}

void traash_overview_overlay_open_tabs(TraashOverviewOverlay *o, TraashSession *s) {
  if (!o) {
    return;
  }
  o->open = 1;
  o->mode = TRAASH_OVERVIEW_TABS;
  o->pane_tab_id = -1;
  o->scroll = 0;
  o->selected = window_index(s, s ? s->active : NULL);
}

void traash_overview_overlay_toggle(TraashOverviewOverlay *o, TraashSession *s) {
  if (!o) {
    return;
  }
  if (o->open) {
    traash_overview_overlay_close(o);
    return;
  }
  traash_overview_overlay_open_tabs(o, s);
}

void traash_overview_overlay_drill_panes(TraashOverviewOverlay *o, TraashWindow *w) {
  if (!o || !w) {
    return;
  }
  o->open = 1;
  o->mode = TRAASH_OVERVIEW_PANES;
  o->pane_tab_id = w->id;
  o->scroll = 0;
  o->selected = pane_index(w, w->active);
}

void traash_overview_overlay_back(TraashOverviewOverlay *o, TraashSession *s) {
  if (!o) {
    return;
  }
  if (o->mode != TRAASH_OVERVIEW_PANES) {
    traash_overview_overlay_close(o);
    return;
  }
  TraashWindow *w = traash_overview_overlay_pane_tab(o, s);
  o->mode = TRAASH_OVERVIEW_TABS;
  o->pane_tab_id = -1;
  o->scroll = 0;
  o->selected = window_index(s, w);
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

void traash_overview_overlay_layout(int fb_w, int fb_h, float scale, int cell_h, int item_count,
                                    TraashOverviewLayout *out) {
  memset(out, 0, sizeof(*out));
  if (scale < 0.1f) {
    scale = 1.0f;
  }
  if (cell_h < 8) {
    cell_h = 14;
  }
  if (item_count < 0) {
    item_count = 0;
  }

  float margin_x = fmaxf(20.0f * scale, (float)fb_w * 0.05f);
  float margin_y = fmaxf(20.0f * scale, (float)fb_h * 0.06f);
  float avail_w = fmaxf(280.0f * scale, (float)fb_w - margin_x * 2.0f);
  float avail_h = fmaxf(220.0f * scale, (float)fb_h - margin_y * 2.0f);
  float pad = clampf(avail_w * 0.03f, 20.0f * scale, 48.0f * scale);
  float title_h = (float)cell_h * 1.85f + 10.0f * scale;
  float subtitle_h = (float)cell_h + 12.0f * scale;
  float footer_h = (float)cell_h + 22.0f * scale;
  float chrome = pad + title_h + subtitle_h + 10.0f * scale + footer_h + pad;
  float body_w = avail_w - pad * 2.0f;
  float body_h = avail_h - chrome;
  if (body_h < 80.0f * scale) {
    body_h = 80.0f * scale;
  }

  int n = item_count > 0 ? item_count : 1;
  float aspect = body_w / fmaxf(body_h, 1.0f);
  int cols = (int)(sqrtf((float)n * aspect) + 0.5f);
  if (avail_w < 480.0f * scale) {
    cols = 1;
  }
  if (cols < 1) {
    cols = 1;
  }
  if (cols > 4) {
    cols = 4;
  }
  if (cols > n) {
    cols = n;
  }

  float min_card_w = 180.0f * scale;
  float min_card_h = 120.0f * scale;
  float gap_x = clampf(12.0f * scale, 8.0f * scale, 18.0f * scale);
  float gap_y = gap_x;
  int rows_fit = (int)((body_h + gap_y) / (min_card_h + gap_y));
  if (rows_fit < 1) {
    rows_fit = 1;
  }
  int total_rows = (n + cols - 1) / cols;
  int rows = total_rows;
  if (rows > rows_fit) {
    rows = rows_fit;
  }
  if (rows < 1) {
    rows = 1;
  }

  float card_w = (body_w - gap_x * (float)(cols - 1)) / (float)cols;
  float card_h = (body_h - gap_y * (float)(rows - 1)) / (float)rows;
  if (card_w < min_card_w && cols > 1) {
    cols = (int)((body_w + gap_x) / (min_card_w + gap_x));
    if (cols < 1) {
      cols = 1;
    }
    total_rows = (n + cols - 1) / cols;
    rows = total_rows > rows_fit ? rows_fit : total_rows;
    if (rows < 1) {
      rows = 1;
    }
    card_w = (body_w - gap_x * (float)(cols - 1)) / (float)cols;
    card_h = (body_h - gap_y * (float)(rows - 1)) / (float)rows;
  }
  if (card_h < 72.0f * scale) {
    card_h = 72.0f * scale;
  }

  float meta_h = (float)cell_h * 2.15f + 10.0f * scale;
  if (meta_h > card_h * 0.42f) {
    meta_h = card_h * 0.42f;
  }
  float preview_h = card_h - meta_h;
  if (preview_h < 28.0f * scale) {
    preview_h = 28.0f * scale;
    meta_h = card_h - preview_h;
  }

  out->w = avail_w;
  out->h = avail_h;
  out->x = ((float)fb_w - avail_w) * 0.5f;
  out->y = ((float)fb_h - avail_h) * 0.5f;
  if (out->x < margin_x * 0.5f) {
    out->x = margin_x * 0.5f;
  }
  if (out->y < margin_y * 0.5f) {
    out->y = margin_y * 0.5f;
  }
  out->pad = pad;
  out->title_h = title_h;
  out->subtitle_h = subtitle_h;
  out->footer_h = footer_h;
  out->content_top = out->y + pad + title_h + subtitle_h + 10.0f * scale;
  out->rad = clampf(16.0f * scale, 12.0f * scale, 24.0f * scale);
  out->cols = cols;
  out->rows = rows;
  out->visible = cols * rows;
  out->card_w = card_w;
  out->card_h = card_h;
  out->gap_x = gap_x;
  out->gap_y = gap_y;
  out->preview_h = preview_h;
  out->meta_h = meta_h;
  out->close_sz = clampf(18.0f * scale, 16.0f * scale, 24.0f * scale);
}

void traash_overview_overlay_card_rect(const TraashOverviewLayout *lay, int visible_index,
                                       float *x, float *y, float *w, float *h) {
  if (!lay || visible_index < 0 || lay->cols < 1) {
    if (x) {
      *x = 0;
    }
    if (y) {
      *y = 0;
    }
    if (w) {
      *w = 0;
    }
    if (h) {
      *h = 0;
    }
    return;
  }
  int col = visible_index % lay->cols;
  int row = visible_index / lay->cols;
  if (x) {
    *x = lay->x + lay->pad + (float)col * (lay->card_w + lay->gap_x);
  }
  if (y) {
    *y = lay->content_top + (float)row * (lay->card_h + lay->gap_y);
  }
  if (w) {
    *w = lay->card_w;
  }
  if (h) {
    *h = lay->card_h;
  }
}

void traash_overview_overlay_close_rect(const TraashOverviewLayout *lay, int visible_index,
                                        float *x, float *y, float *w, float *h) {
  float cx, cy, cw, ch;
  traash_overview_overlay_card_rect(lay, visible_index, &cx, &cy, &cw, &ch);
  float sz = lay ? lay->close_sz : 18.0f;
  if (x) {
    *x = cx + cw - sz - 6.0f;
  }
  if (y) {
    *y = cy + 6.0f;
  }
  if (w) {
    *w = sz;
  }
  if (h) {
    *h = sz;
  }
}

void traash_overview_overlay_clamp(TraashOverviewOverlay *o, TraashSession *s,
                                   const TraashOverviewLayout *lay) {
  if (!o) {
    return;
  }
  int count = traash_overview_overlay_item_count(o, s);
  if (count <= 0) {
    o->selected = 0;
    o->scroll = 0;
    return;
  }
  if (o->selected >= count) {
    o->selected = count - 1;
  }
  if (o->selected < 0) {
    o->selected = 0;
  }
  int vis = lay && lay->visible > 0 ? lay->visible : count;
  int cols = lay && lay->cols > 0 ? lay->cols : 1;
  if (o->scroll < 0) {
    o->scroll = 0;
  }
  if (o->scroll % cols) {
    o->scroll -= o->scroll % cols;
  }
  if (o->selected < o->scroll) {
    o->scroll = (o->selected / cols) * cols;
  }
  while (vis > 0 && o->selected >= o->scroll + vis) {
    o->scroll += cols;
  }
  int max_scroll = count - vis;
  if (max_scroll < 0) {
    max_scroll = 0;
  }
  if (o->scroll > max_scroll) {
    o->scroll = (max_scroll / cols) * cols;
  }
  if (o->scroll < 0) {
    o->scroll = 0;
  }
}

void traash_overview_overlay_move(TraashOverviewOverlay *o, TraashSession *s, int dx, int dy,
                                  const TraashOverviewLayout *lay) {
  if (!o || !lay || lay->cols < 1) {
    return;
  }
  int count = traash_overview_overlay_item_count(o, s);
  if (count <= 0) {
    return;
  }
  int cols = lay->cols;
  int idx = o->selected + dx + dy * cols;
  if (idx < 0) {
    idx = 0;
  }
  if (idx >= count) {
    idx = count - 1;
  }
  o->selected = idx;
  traash_overview_overlay_clamp(o, s, lay);
}

TraashOverviewHit traash_overview_overlay_hit(const TraashOverviewOverlay *o, TraashSession *s,
                                              float mx, float my, int fb_w, int fb_h, float scale,
                                              int cell_h) {
  TraashOverviewHit hit;
  memset(&hit, 0, sizeof(hit));
  if (!o || !o->open) {
    return hit;
  }
  int count = traash_overview_overlay_item_count(o, s);
  TraashOverviewLayout lay;
  traash_overview_overlay_layout(fb_w, fb_h, scale, cell_h, count, &lay);
  if (mx < lay.x || my < lay.y || mx >= lay.x + lay.w || my >= lay.y + lay.h) {
    hit.kind = TRAASH_OVERVIEW_HIT_BACKDROP;
    return hit;
  }
  int vis = lay.visible;
  if (vis > count - o->scroll) {
    vis = count - o->scroll;
  }
  for (int i = 0; i < vis; i++) {
    float cx, cy, cw, ch, zx, zy, zw, zh;
    traash_overview_overlay_card_rect(&lay, i, &cx, &cy, &cw, &ch);
    traash_overview_overlay_close_rect(&lay, i, &zx, &zy, &zw, &zh);
    int idx = o->scroll + i;
    TraashWindow *w = NULL;
    TraashPane *p = NULL;
    if (o->mode == TRAASH_OVERVIEW_PANES) {
      w = traash_overview_overlay_pane_tab(o, s);
      p = traash_overview_overlay_nth_pane(w, idx);
    } else {
      w = traash_overview_overlay_nth_window(s, idx);
    }
    if (mx >= zx && mx < zx + zw && my >= zy && my < zy + zh) {
      hit.kind = TRAASH_OVERVIEW_HIT_CLOSE;
      hit.index = idx;
      hit.window = w;
      hit.pane = p;
      return hit;
    }
    if (mx >= cx && mx < cx + cw && my >= cy && my < cy + ch) {
      hit.kind = TRAASH_OVERVIEW_HIT_CARD;
      hit.index = idx;
      hit.window = w;
      hit.pane = p;
      return hit;
    }
  }
  return hit;
}
