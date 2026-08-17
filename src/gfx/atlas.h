#ifndef TRAASH_ATLAS_H
#define TRAASH_ATLAS_H

#include "gfx/gl_loader.h"

#include <stdint.h>

typedef struct {
  float u0, v0, u1, v1;
  int w, h;
  int bearing_x, bearing_y;
  int advance;
  int used;
} TraashGlyph;

typedef struct {
  GLuint tex;
  int size;
  int pen_x, pen_y, row_h;
  TraashGlyph glyphs[2048];
  uint32_t keys[2048];
} TraashAtlas;

int traash_atlas_init(TraashAtlas *a, int size);
void traash_atlas_free(TraashAtlas *a);
TraashGlyph *traash_atlas_get(TraashAtlas *a, uint32_t key);
TraashGlyph *traash_atlas_insert(TraashAtlas *a, uint32_t key, const uint8_t *bitmap,
                                 int w, int h, int bearing_x, int bearing_y, int advance);

#endif
