#include "ui/layout_overlay.h"

#include <math.h>
#include <string.h>

void traash_layout_overlay_init(TraashLayoutOverlay *o) {
  memset(o, 0, sizeof(*o));
}

void traash_layout_overlay_close(TraashLayoutOverlay *o) {
  if (!o) {
    return;
  }
  o->open = 0;
  o->save_mode = 0;
  o->save_name[0] = 0;
}

void traash_layout_overlay_refresh(TraashLayoutOverlay *o) {
  if (!o) {
    return;
  }
  o->count = traash_layout_store_scan(o->names, TRAASH_LAYOUT_MAX_NAMES);
  if (o->selected >= o->count) {
    o->selected = o->count > 0 ? o->count - 1 : 0;
  }
  if (o->selected < 0) {
    o->selected = 0;
  }
}

void traash_layout_overlay_toggle(TraashLayoutOverlay *o) {
  if (!o) {
    return;
  }
  if (o->open) {
    traash_layout_overlay_close(o);
    return;
  }
  o->open = 1;
  o->save_mode = 0;
  o->save_name[0] = 0;
  o->selected = 0;
  traash_layout_overlay_refresh(o);
}

void traash_layout_overlay_geom(int fb_w, int fb_h, float scale, int cell_h, int row_count,
                                TraashLayoutOverlayGeom *out) {
  if (!out) {
    return;
  }
  if (scale < 0.1f) {
    scale = 1.0f;
  }
  if (cell_h < 8) {
    cell_h = 14;
  }
  float pad = 22.0f * scale;
  float title_h = (float)cell_h * 1.7f + 10.0f * scale;
  float row_h = (float)cell_h + 10.0f * scale;
  float footer_h = (float)cell_h + 16.0f * scale;
  int visible = row_count;
  if (visible < 4) {
    visible = 4;
  }
  if (visible > 12) {
    visible = 12;
  }
  float panel_h = pad + title_h + 8.0f * scale + row_h * (float)visible + footer_h + pad;
  float panel_w = fminf((float)fb_w * 0.55f, 520.0f * scale);
  if (panel_w < 320.0f * scale) {
    panel_w = 320.0f * scale;
  }
  if (panel_h > (float)fb_h * 0.85f) {
    panel_h = (float)fb_h * 0.85f;
    visible = (int)((panel_h - pad - title_h - footer_h - pad) / row_h);
    if (visible < 3) {
      visible = 3;
    }
  }
  out->w = panel_w;
  out->h = panel_h;
  out->x = ((float)fb_w - panel_w) * 0.5f;
  out->y = ((float)fb_h - panel_h) * 0.5f;
  out->pad = pad;
  out->title_h = title_h;
  out->row_h = row_h;
  out->footer_h = footer_h;
  out->content_top = out->y + pad + title_h + 8.0f * scale;
  out->rad = 14.0f * scale;
  out->visible_rows = visible;
}

int traash_layout_overlay_hit_backdrop(const TraashLayoutOverlay *o, float mx, float my,
                                       int fb_w, int fb_h, float scale, int cell_h) {
  if (!o || !o->open) {
    return 0;
  }
  TraashLayoutOverlayGeom g;
  traash_layout_overlay_geom(fb_w, fb_h, scale, cell_h, o->count, &g);
  if (mx < g.x || my < g.y || mx >= g.x + g.w || my >= g.y + g.h) {
    return 1;
  }
  return 0;
}

int traash_layout_overlay_hit_row(const TraashLayoutOverlay *o, float mx, float my, int fb_w,
                                  int fb_h, float scale, int cell_h) {
  if (!o || !o->open || o->save_mode) {
    return -1;
  }
  TraashLayoutOverlayGeom g;
  traash_layout_overlay_geom(fb_w, fb_h, scale, cell_h, o->count, &g);
  if (mx < g.x + g.pad || mx >= g.x + g.w - g.pad) {
    return -1;
  }
  float y0 = g.content_top;
  for (int i = 0; i < o->count && i < g.visible_rows; i++) {
    float y = y0 + (float)i * g.row_h;
    if (my >= y && my < y + g.row_h) {
      return i;
    }
  }
  return -1;
}
