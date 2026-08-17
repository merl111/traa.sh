#include "ui/shortcuts_overlay.h"

#include "input/shortcut_catalog.h"

#include <math.h>
#include <string.h>

void traash_shortcuts_overlay_init(TraashShortcutsOverlay *o) {
  memset(o, 0, sizeof(*o));
}

void traash_shortcuts_overlay_toggle(TraashShortcutsOverlay *o) {
  if (!o) {
    return;
  }
  o->open = !o->open;
}

void traash_shortcuts_overlay_close(TraashShortcutsOverlay *o) {
  if (o) {
    o->open = 0;
  }
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

void traash_shortcuts_overlay_layout(int fb_w, int fb_h, float scale, int cell_h,
                                     TraashShortcutsLayout *out) {
  memset(out, 0, sizeof(*out));
  if (scale < 0.1f) {
    scale = 1.0f;
  }
  if (cell_h < 8) {
    cell_h = 14;
  }

  int groups = traash_shortcut_group_count();
  int total_rows = 0;
  for (int i = 0; i < groups; i++) {
    total_rows += 1 + TRAASH_SHORTCUT_GROUPS[i].count;
  }

  /* Track the window: margins grow with size, panel fills most of the view. */
  float margin_x = fmaxf(20.0f * scale, (float)fb_w * 0.05f);
  float margin_y = fmaxf(20.0f * scale, (float)fb_h * 0.06f);
  float avail_w = fmaxf(280.0f * scale, (float)fb_w - margin_x * 2.0f);
  float avail_h = fmaxf(220.0f * scale, (float)fb_h - margin_y * 2.0f);

  int columns = (avail_w >= 640.0f * scale) ? 2 : 1;

  float pad = clampf(avail_w * 0.035f, 24.0f * scale, 56.0f * scale);
  float col_gap = clampf(avail_w * 0.04f, 28.0f * scale, 64.0f * scale);
  float section_gap = clampf((float)cell_h * 0.9f, 14.0f * scale, 28.0f * scale);
  float title_h = (float)cell_h * 1.85f + 12.0f * scale;
  float subtitle_h = (float)cell_h + 14.0f * scale;
  float footer_h = (float)cell_h + 24.0f * scale;
  float chrome = pad + title_h + subtitle_h + 12.0f * scale + footer_h + pad;

  int half = columns == 2 ? (groups + 1) / 2 : groups;
  int left_rows = 0, right_rows = 0;
  for (int i = 0; i < groups; i++) {
    int n = 1 + TRAASH_SHORTCUT_GROUPS[i].count;
    if (columns == 2 && i >= half) {
      right_rows += n;
    } else {
      left_rows += n;
    }
  }
  int col_rows = columns == 2 ? (left_rows > right_rows ? left_rows : right_rows) : total_rows;
  int col_sections = columns == 2
                         ? (half > (groups - half) ? half : (groups - half))
                         : groups;
  int section_gaps = col_sections > 0 ? col_sections - 1 : 0;

  /* Grow row spacing with available height so the sheet fills the window. */
  float body_budget = avail_h - chrome;
  float base_row = (float)cell_h + 12.0f * scale;
  float row_h = base_row;
  if (col_rows > 0 && body_budget > 0.0f) {
    float fitted =
        (body_budget - (float)section_gaps * section_gap) / (float)col_rows;
    row_h = clampf(fitted, base_row, (float)cell_h * 2.8f);
  }
  float body_h =
      (float)col_rows * row_h + (float)section_gaps * section_gap;

  float w = avail_w;
  float h = chrome + body_h;
  if (h > avail_h) {
    /* Shrink rows to fit if the window is short */
    float shrink_budget = avail_h - chrome;
    if (col_rows > 0 && shrink_budget > 0.0f) {
      row_h = clampf((shrink_budget - (float)section_gaps * section_gap) / (float)col_rows,
                     (float)cell_h + 4.0f * scale, row_h);
      body_h = (float)col_rows * row_h + (float)section_gaps * section_gap;
    }
    h = fminf(avail_h, chrome + body_h);
  }

  out->w = w;
  out->h = h;
  out->x = ((float)fb_w - w) * 0.5f;
  out->y = ((float)fb_h - h) * 0.5f;
  if (out->x < margin_x * 0.5f) {
    out->x = margin_x * 0.5f;
  }
  if (out->y < margin_y * 0.5f) {
    out->y = margin_y * 0.5f;
  }
  out->columns = columns;
  out->pad = pad;
  out->title_h = title_h;
  out->subtitle_h = subtitle_h;
  out->row_h = row_h;
  out->section_gap = section_gap;
  out->col_gap = col_gap;
  out->footer_h = footer_h;
  out->content_top = out->y + pad + title_h + subtitle_h + 12.0f * scale;
  out->rad = clampf(18.0f * scale + avail_w * 0.004f, 14.0f * scale, 28.0f * scale);
}

int traash_shortcuts_overlay_hit_backdrop(const TraashShortcutsOverlay *o, float mx, float my,
                                          int fb_w, int fb_h, float scale, int cell_h) {
  if (!o || !o->open) {
    return 0;
  }
  TraashShortcutsLayout lay;
  traash_shortcuts_overlay_layout(fb_w, fb_h, scale, cell_h, &lay);
  if (mx < lay.x || my < lay.y || mx >= lay.x + lay.w || my >= lay.y + lay.h) {
    return 1;
  }
  return 0;
}
