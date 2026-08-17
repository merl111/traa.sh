#include "gfx/atlas.h"

#include <stdlib.h>
#include <string.h>

int traash_atlas_init(TraashAtlas *a, int size) {
  memset(a, 0, sizeof(*a));
  a->size = size;
  a->pen_x = 1;
  a->pen_y = 1;
  a->row_h = 0;
  traash_glGenTextures(1, &a->tex);
  traash_glBindTexture(GL_TEXTURE_2D, a->tex);
  traash_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  traash_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
  /* Glyph bitmaps are rasterized at their exact display size and drawn on
   * integer pixel boundaries. Linear filtering blurs that coverage a second
   * time; nearest preserves FreeType's own grayscale antialiasing. */
  traash_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  traash_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  traash_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  traash_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return 0;
}

void traash_atlas_free(TraashAtlas *a) {
  if (a->tex) {
    traash_glDeleteTextures(1, &a->tex);
  }
  memset(a, 0, sizeof(*a));
}

TraashGlyph *traash_atlas_get(TraashAtlas *a, uint32_t key) {
  for (int i = 0; i < 2048; i++) {
    if (a->glyphs[i].used && a->keys[i] == key) {
      return &a->glyphs[i];
    }
  }
  return NULL;
}

TraashGlyph *traash_atlas_insert(TraashAtlas *a, uint32_t key, const uint8_t *bitmap,
                                 int w, int h, int bearing_x, int bearing_y, int advance) {
  TraashGlyph *existing = traash_atlas_get(a, key);
  if (existing) {
    return existing;
  }
  /* 2px gap so GL_LINEAR does not bleed into neighboring glyphs */
  const int pad = 2;
  if (a->pen_x + w + pad >= a->size) {
    a->pen_x = 1;
    a->pen_y += a->row_h + pad;
    a->row_h = 0;
  }
  if (a->pen_y + h + pad >= a->size) {
    /* atlas full — reset (LRU-ish) */
    a->pen_x = 1;
    a->pen_y = 1;
    a->row_h = 0;
    memset(a->glyphs, 0, sizeof(a->glyphs));
    memset(a->keys, 0, sizeof(a->keys));
  }
  int slot = -1;
  for (int i = 0; i < 2048; i++) {
    if (!a->glyphs[i].used) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    slot = 0;
  }
  traash_glBindTexture(GL_TEXTURE_2D, a->tex);
  traash_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (bitmap && w > 0 && h > 0) {
    traash_glTexSubImage2D(GL_TEXTURE_2D, 0, a->pen_x, a->pen_y, w, h, GL_RED, GL_UNSIGNED_BYTE,
                    bitmap);
  }
  TraashGlyph *g = &a->glyphs[slot];
  g->used = 1;
  g->w = w;
  g->h = h;
  g->bearing_x = bearing_x;
  g->bearing_y = bearing_y;
  g->advance = advance;
  g->u0 = (float)a->pen_x / (float)a->size;
  g->v0 = (float)a->pen_y / (float)a->size;
  g->u1 = (float)(a->pen_x + w) / (float)a->size;
  g->v1 = (float)(a->pen_y + h) / (float)a->size;
  a->keys[slot] = key;
  a->pen_x += w + pad;
  if (h > a->row_h) {
    a->row_h = h;
  }
  return g;
}
