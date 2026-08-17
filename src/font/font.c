#include "font/font.h"

#include "util/log.h"

#include <fontconfig/fontconfig.h>
#include <hb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static const char *HARDCODED_FALLBACKS[] = {
    "Hack Nerd Font Mono",
    "Hack Nerd Font",
    "MesloLGS NF",
    "Symbols Nerd Font Mono",
    "Symbols Nerd Font",
    "Menlo",
    "SF Mono",
    "Monaco",
    "Noto Sans Symbols2",
    "Noto Sans Symbols",
    "Noto Color Emoji",
    "DejaVu Sans Mono",
    NULL};

static const char *PATH_FALLBACKS[] = {
    "/usr/share/fonts/TTF/HackNerdFontMono-Regular.ttf",
    "/usr/share/fonts/TTF/HackNerdFont-Regular.ttf",
    "/usr/share/fonts/TTF/MesloLGS-NF-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Monaco.ttf",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/Library/Fonts/SF-Mono-Regular.otf",
    NULL};

static int add_face_path(TraashFont *f, const char *path) {
  if (!path || !path[0] || access(path, R_OK) != 0 ||
      f->face_count >= TRAASH_FONT_MAX_FACES) {
    return -1;
  }
  for (int i = 0; i < f->face_count; i++) {
    if (f->faces[i] && f->faces[i]->family_name) {
      /* allow same family once */
    }
  }
  FT_Face face = NULL;
  if (FT_New_Face(f->lib, path, 0, &face) != 0) {
    return -1;
  }
  /* Deduplicate by identical path via face index / family+style */
  for (int i = 0; i < f->face_count; i++) {
    if (f->faces[i]->family_name && face->family_name &&
        strcmp(f->faces[i]->family_name, face->family_name) == 0 &&
        ((f->faces[i]->style_name && face->style_name &&
          strcmp(f->faces[i]->style_name, face->style_name) == 0) ||
         (!f->faces[i]->style_name && !face->style_name))) {
      FT_Done_Face(face);
      return -1;
    }
  }
  FT_Set_Pixel_Sizes(face, 0, (FT_UInt)f->px_size);
  f->faces[f->face_count++] = face;
  TRAASH_LOGI("font face[%d]: %s (%s)", f->face_count - 1,
              face->family_name ? face->family_name : "?", path);
  return 0;
}

static char *fc_resolve(const char *pattern) {
  if (!pattern || !pattern[0]) {
    return NULL;
  }
  FcPattern *pat = FcNameParse((const FcChar8 *)pattern);
  if (!pat) {
    return NULL;
  }
  FcConfigSubstitute(NULL, pat, FcMatchPattern);
  FcDefaultSubstitute(pat);
  FcResult result;
  FcPattern *match = FcFontMatch(NULL, pat, &result);
  FcPatternDestroy(pat);
  if (!match) {
    return NULL;
  }
  FcChar8 *file = NULL;
  if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch || !file) {
    FcPatternDestroy(match);
    return NULL;
  }
  char *out = strdup((const char *)file);
  FcPatternDestroy(match);
  return out;
}

static void add_family(TraashFont *f, const char *family) {
  char *path = fc_resolve(family);
  if (path) {
    add_face_path(f, path);
    free(path);
  }
}

int traash_font_init(TraashFont *f, TraashAtlas *atlas, const char *family, int px) {
  memset(f, 0, sizeof(*f));
  f->atlas = atlas;
  f->px_size = px > 0 ? px : 14;
  if (FT_Init_FreeType(&f->lib) != 0) {
    return -1;
  }
  FcInit();

  if (family && family[0] == '/') {
    add_face_path(f, family);
  } else if (family && family[0]) {
    add_family(f, family);
  }

  if (f->face_count == 0) {
    add_family(f, "Hack Nerd Font Mono");
  }
  if (f->face_count == 0) {
    add_family(f, "monospace");
  }

  for (int i = 0; HARDCODED_FALLBACKS[i]; i++) {
    add_family(f, HARDCODED_FALLBACKS[i]);
  }
  for (int i = 0; PATH_FALLBACKS[i]; i++) {
    add_face_path(f, PATH_FALLBACKS[i]);
  }

  if (f->face_count == 0) {
    TRAASH_LOGE("no monospace font found");
    FT_Done_FreeType(f->lib);
    return -1;
  }

  FT_Face primary = f->faces[0];
  f->ascender = (int)(primary->size->metrics.ascender >> 6);
  f->cell_h = (int)(primary->size->metrics.height >> 6);
  if (f->cell_h < f->px_size) {
    f->cell_h = f->px_size + 2;
  }
  const int load_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP;
  if (FT_Load_Char(primary, 'M', load_flags) == 0) {
    f->cell_w = (int)(primary->glyph->advance.x >> 6);
  }
  if (f->cell_w <= 0) {
    f->cell_w = f->px_size * 3 / 5;
  }
  TRAASH_LOGI("font cell %dx%d (%d faces)", f->cell_w, f->cell_h, f->face_count);

  /* Pre-rasterize printable ASCII so first keystrokes don't hitch */
  for (uint32_t cp = 32; cp < 127; cp++) {
    (void)traash_font_glyph(f, cp);
  }

  hb_buffer_t *buf = hb_buffer_create();
  hb_buffer_destroy(buf);
  return 0;
}

void traash_font_free(TraashFont *f) {
  for (int i = 0; i < f->face_count; i++) {
    if (f->faces[i]) {
      FT_Done_Face(f->faces[i]);
    }
  }
  if (f->lib) {
    FT_Done_FreeType(f->lib);
  }
  memset(f, 0, sizeof(*f));
}

TraashGlyph *traash_font_glyph(TraashFont *f, uint32_t cp) {
  TraashGlyph *g = traash_atlas_get(f->atlas, cp);
  if (g) {
    return g;
  }

  /* Native grid fitting gives strong, pixel-aligned stems. Avoid embedded
   * bitmap strikes: they vary by font and become visibly uneven under scaling. */
  const int load_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP;
  int is_blank = (cp == ' ' || cp == '\t' || cp == 0xa0);
  FT_Face used = NULL;
  for (int i = 0; i < f->face_count; i++) {
    FT_Face face = f->faces[i];
    if (FT_Get_Char_Index(face, cp) == 0) {
      continue;
    }
    if (FT_Load_Char(face, cp, load_flags) != 0) {
      continue;
    }
    /* Spaces often have a valid advance but a null/empty bitmap — still usable. */
    used = face;
    if (face->glyph->bitmap.buffer || is_blank || face->glyph->bitmap.width == 0) {
      break;
    }
  }

  if (!used) {
    if (is_blank) {
      unsigned char empty = 0;
      return traash_atlas_insert(f->atlas, cp, &empty, 0, 0, 0, 0, f->cell_w);
    }
    /* Missing glyph: advance without drawing a tofu '?' (was polluting UI/status). */
    unsigned char empty = 0;
    return traash_atlas_insert(f->atlas, cp, &empty, 0, 0, 0, 0, f->cell_w);
  }

  FT_GlyphSlot slot = used->glyph;
  int advance = (int)(slot->advance.x >> 6);
  if (advance <= 0) {
    advance = f->cell_w;
  }
  FT_Bitmap *bm = &slot->bitmap;
  int w = (int)bm->width;
  int h = (int)bm->rows;
  int pitch = bm->pitch;
  const uint8_t *src = bm->buffer;
  uint8_t *tight = NULL;

  /* Unpack mono bitmaps into 8-bit coverage so atlas AA works. */
  if (src && w > 0 && h > 0 && bm->pixel_mode == FT_PIXEL_MODE_MONO) {
    tight = (uint8_t *)malloc((size_t)w * (size_t)h);
    if (tight) {
      int abs_pitch = pitch < 0 ? -pitch : pitch;
      for (int row = 0; row < h; row++) {
        const uint8_t *line =
            pitch < 0 ? src + (h - 1 - row) * abs_pitch : src + row * abs_pitch;
        for (int col = 0; col < w; col++) {
          int bit = (line[col >> 3] >> (7 - (col & 7))) & 1;
          tight[(size_t)row * (size_t)w + (size_t)col] = bit ? 255 : 0;
        }
      }
      src = tight;
    }
  } else if (src && w > 0 && h > 0 && pitch != w) {
    /* Atlas upload expects tightly packed rows (pitch == width). */
    tight = (uint8_t *)malloc((size_t)w * (size_t)h);
    if (tight) {
      int abs_pitch = pitch < 0 ? -pitch : pitch;
      for (int row = 0; row < h; row++) {
        const uint8_t *line =
            pitch < 0 ? src + (h - 1 - row) * abs_pitch : src + row * abs_pitch;
        memcpy(tight + (size_t)row * (size_t)w, line, (size_t)w);
      }
      src = tight;
    }
  } else if (pitch < 0 && src && h > 0) {
    tight = (uint8_t *)malloc((size_t)w * (size_t)h);
    if (tight) {
      int abs_pitch = -pitch;
      for (int row = 0; row < h; row++) {
        memcpy(tight + (size_t)row * (size_t)w, src + (h - 1 - row) * abs_pitch, (size_t)w);
      }
      src = tight;
    }
  }
  if (!src || w <= 0 || h <= 0) {
    unsigned char empty = 0;
    TraashGlyph *out = traash_atlas_insert(f->atlas, cp, &empty, 0, 0, 0, 0, advance);
    free(tight);
    return out;
  }
  TraashGlyph *out = traash_atlas_insert(f->atlas, cp, src, w, h, slot->bitmap_left,
                                         slot->bitmap_top, advance);
  free(tight);
  return out;
}

static int family_cmp(const void *a, const void *b) {
  return strcasecmp((const char *)a, (const char *)b);
}

static int family_already(char (*out)[128], int count, const char *name) {
  for (int i = 0; i < count; i++) {
    if (strcasecmp(out[i], name) == 0) {
      return 1;
    }
  }
  return 0;
}

int traash_font_list_monospace(char (*out)[128], int max_out) {
  if (!out || max_out < 1) {
    return 0;
  }
  int count = 0;
  snprintf(out[count++], 128, "monospace");

  FcPattern *pat = FcPatternCreate();
  if (!pat) {
    return count;
  }
  FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_SPACING, (char *)0);
  if (!os) {
    FcPatternDestroy(pat);
    return count;
  }
  FcFontSet *fs = FcFontList(NULL, pat, os);
  FcObjectSetDestroy(os);
  FcPatternDestroy(pat);
  if (!fs) {
    return count;
  }

  for (int i = 0; i < fs->nfont && count < max_out; i++) {
    int spacing = FC_PROPORTIONAL;
    if (FcPatternGetInteger(fs->fonts[i], FC_SPACING, 0, &spacing) != FcResultMatch) {
      continue;
    }
    if (spacing != FC_MONO && spacing != FC_DUAL) {
      continue;
    }
    FcChar8 *family = NULL;
    if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &family) != FcResultMatch || !family ||
        !family[0]) {
      continue;
    }
    const char *name = (const char *)family;
    if (family_already(out, count, name)) {
      continue;
    }
    snprintf(out[count++], 128, "%s", name);
  }
  FcFontSetDestroy(fs);

  if (count > 1) {
    /* Keep "monospace" first; sort the rest. */
    qsort(out + 1, (size_t)(count - 1), 128, family_cmp);
  }
  return count;
}
