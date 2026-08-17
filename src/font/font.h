#ifndef TRAASH_FONT_H
#define TRAASH_FONT_H

#include "gfx/atlas.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#define TRAASH_FONT_MAX_FACES 8
#define TRAASH_FONT_MAX_FAMILIES 512

typedef struct {
  FT_Library lib;
  FT_Face faces[TRAASH_FONT_MAX_FACES];
  int face_count;
  int px_size;
  int cell_w;
  int cell_h;
  int ascender;
  TraashAtlas *atlas;
} TraashFont;

int traash_font_init(TraashFont *f, TraashAtlas *atlas, const char *family, int px);
void traash_font_free(TraashFont *f);
TraashGlyph *traash_font_glyph(TraashFont *f, uint32_t cp);
/* Fill out[] with unique monospace/dual-spaced family names (sorted). Returns count. */
int traash_font_list_monospace(char (*out)[128], int max_out);

#endif
