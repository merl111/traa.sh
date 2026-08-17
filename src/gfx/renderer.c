#include "gfx/renderer.h"

#include "config/config.h"
#include "input/shortcut_catalog.h"
#include "term/cell.h"
#include "term/pty.h"
#include "term/selection.h"
#include "util/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *VERT =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_color;\n"
    "out vec2 v_uv; out vec4 v_color;\n"
    "uniform vec2 u_res;\n"
    "void main(){\n"
    "  vec2 p = vec2(a_pos.x / u_res.x * 2.0 - 1.0, 1.0 - a_pos.y / u_res.y * 2.0);\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "  v_uv=a_uv; v_color=a_color;\n"
    "}\n";

static const char *FRAG =
    "#version 330 core\n"
    "in vec2 v_uv; in vec4 v_color;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_mode;\n"
    "uniform float u_opacity;\n"
    "void main(){\n"
    "  float op = clamp(u_opacity, 0.0, 1.0);\n"
    "  if(u_mode==0){ frag = vec4(v_color.rgb, v_color.a * op); }\n"
    "  else {\n"
    "    float a = texture(u_tex, v_uv).r;\n"
    "    a = smoothstep(0.04, 0.96, clamp(a, 0.0, 1.0));\n"
    "    frag = vec4(v_color.rgb, v_color.a * a * op);\n"
    "  }\n"
    "}\n";

typedef struct {
  float x, y, u, v, r, g, b, a;
} Vtx;

/* Reused every frame — malloc/free of multi‑MB buffers caused mouse hitching */
static Vtx *g_vtx_solid;
static Vtx *g_vtx_text;
static Vtx *g_vtx_overlay;
static int g_vtx_cap;

static int ensure_vtx_bufs(int cap) {
  if (cap < 1) {
    cap = 1;
  }
  if (g_vtx_cap >= cap && g_vtx_solid && g_vtx_text && g_vtx_overlay) {
    return 0;
  }
  Vtx *s = (Vtx *)malloc((size_t)cap * sizeof(Vtx));
  Vtx *t = (Vtx *)malloc((size_t)cap * sizeof(Vtx));
  Vtx *o = (Vtx *)malloc((size_t)cap * sizeof(Vtx));
  if (!s || !t || !o) {
    free(s);
    free(t);
    free(o);
    return -1;
  }
  free(g_vtx_solid);
  free(g_vtx_text);
  free(g_vtx_overlay);
  g_vtx_solid = s;
  g_vtx_text = t;
  g_vtx_overlay = o;
  g_vtx_cap = cap;
  return 0;
}

static GLuint compile(GLenum type, const char *src) {
  GLuint s = traash_glCreateShader(type);
  traash_glShaderSource(s, 1, &src, NULL);
  traash_glCompileShader(s);
  GLint ok = 0;
  traash_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    traash_glGetShaderInfoLog(s, sizeof(log), NULL, log);
    TRAASH_LOGE("shader: %s", log);
  }
  return s;
}

static void rgba(uint32_t c, float *r, float *g, float *b) {
  *r = ((c >> 16) & 0xff) / 255.0f;
  *g = ((c >> 8) & 0xff) / 255.0f;
  *b = (c & 0xff) / 255.0f;
}

static void push_quad(Vtx *v, int *n, int cap, float x, float y, float w, float h, float u0,
                      float v0, float u1, float v1, uint32_t color, float a) {
  if (*n + 6 > cap) {
    return;
  }
  float r, g, b;
  rgba(color, &r, &g, &b);
  Vtx q[6] = {
      {x, y, u0, v0, r, g, b, a},         {x + w, y, u1, v0, r, g, b, a},
      {x + w, y + h, u1, v1, r, g, b, a}, {x, y, u0, v0, r, g, b, a},
      {x + w, y + h, u1, v1, r, g, b, a}, {x, y + h, u0, v1, r, g, b, a},
  };
  memcpy(v + *n, q, sizeof(q));
  *n += 6;
}

/* Soft rounded rect via 9-slice quads (good enough for chrome / menus). */
static void push_tri(Vtx *v, int *n, int cap, float x0, float y0, float x1, float y1, float x2,
                     float y2, uint32_t color, float a) {
  if (*n + 3 > cap) {
    return;
  }
  float r, g, b;
  rgba(color, &r, &g, &b);
  Vtx q[3] = {
      {x0, y0, 0, 0, r, g, b, a},
      {x1, y1, 0, 0, r, g, b, a},
      {x2, y2, 0, 0, r, g, b, a},
  };
  memcpy(v + *n, q, sizeof(q));
  *n += 3;
}

static void push_corner_fan(Vtx *v, int *n, int cap, float cx, float cy, float rad, float a0,
                            float a1, uint32_t color, float a) {
  const int segs = 16;
  for (int i = 0; i < segs; i++) {
    float t0 = a0 + (a1 - a0) * ((float)i / (float)segs);
    float t1 = a0 + (a1 - a0) * ((float)(i + 1) / (float)segs);
    push_tri(v, n, cap, cx, cy, cx + rad * cosf(t0), cy + rad * sinf(t0),
             cx + rad * cosf(t1), cy + rad * sinf(t1), color, a);
  }
}

static void push_round_rect(Vtx *v, int *n, int cap, float x, float y, float w, float h,
                            float rad, uint32_t color, float a) {
  if (rad < 1.0f || w < rad * 2.0f || h < rad * 2.0f) {
    push_quad(v, n, cap, x, y, w, h, 0, 0, 0, 0, color, a);
    return;
  }
  float r = rad;
  /* Center + sides */
  push_quad(v, n, cap, x + r, y, w - 2 * r, h, 0, 0, 0, 0, color, a);
  push_quad(v, n, cap, x, y + r, r, h - 2 * r, 0, 0, 0, 0, color, a);
  push_quad(v, n, cap, x + w - r, y + r, r, h - 2 * r, 0, 0, 0, 0, color, a);
  /* True quarter-circle corners (y grows downward) */
  push_corner_fan(v, n, cap, x + r, y + r, r, (float)M_PI, 1.5f * (float)M_PI, color, a);
  push_corner_fan(v, n, cap, x + w - r, y + r, r, 1.5f * (float)M_PI, 2.0f * (float)M_PI, color,
                  a);
  push_corner_fan(v, n, cap, x + w - r, y + h - r, r, 0.0f, 0.5f * (float)M_PI, color, a);
  push_corner_fan(v, n, cap, x + r, y + h - r, r, 0.5f * (float)M_PI, (float)M_PI, color, a);
}

static void push_menu_icon(Vtx *v, int *n, int cap, int index, float x, float y, float s,
                           uint32_t color) {
  /* x,y = top-left of icon box; s = box size */
  float p = s * 0.18f;
  float ix = x + p;
  float iy = y + p;
  float iw = s - p * 2.0f;
  float ih = s - p * 2.0f;
  switch (index) {
  case TRAASH_MENU_COPY: {
    /* Back page */
    push_round_rect(v, n, cap, ix + iw * 0.18f, iy, iw * 0.72f, ih * 0.78f, 2.0f, color, 0.55f);
    /* Front page */
    push_round_rect(v, n, cap, ix, iy + ih * 0.18f, iw * 0.72f, ih * 0.78f, 2.0f, color, 1.0f);
    break;
  }
  case TRAASH_MENU_PASTE: {
    /* Clipboard body */
    push_round_rect(v, n, cap, ix + iw * 0.12f, iy + ih * 0.18f, iw * 0.76f, ih * 0.72f, 2.0f,
                    color, 1.0f);
    /* Clip */
    push_quad(v, n, cap, ix + iw * 0.28f, iy, iw * 0.44f, ih * 0.28f, 0, 0, 0, 0, color, 1.0f);
    break;
  }
  case TRAASH_MENU_SPLIT_H: {
    /* Two panes side by side */
    push_round_rect(v, n, cap, ix, iy, iw * 0.42f, ih, 2.0f, color, 1.0f);
    push_round_rect(v, n, cap, ix + iw * 0.58f, iy, iw * 0.42f, ih, 2.0f, color, 1.0f);
    break;
  }
  case TRAASH_MENU_SPLIT_V: {
    /* Two panes stacked */
    push_round_rect(v, n, cap, ix, iy, iw, ih * 0.42f, 2.0f, color, 1.0f);
    push_round_rect(v, n, cap, ix, iy + ih * 0.58f, iw, ih * 0.42f, 2.0f, color, 1.0f);
    break;
  }
  case TRAASH_MENU_SETTINGS: {
    /* Preferences: three slider rows */
    for (int k = 0; k < 3; k++) {
      float ly = iy + ih * (0.12f + (float)k * 0.32f);
      push_quad(v, n, cap, ix, ly + ih * 0.06f, iw, s * 0.06f, 0, 0, 0, 0, color, 0.7f);
      float kx = ix + iw * (k == 0 ? 0.55f : k == 1 ? 0.3f : 0.7f);
      push_round_rect(v, n, cap, kx - s * 0.08f, ly, s * 0.16f, s * 0.18f, s * 0.08f, color,
                      1.0f);
    }
    break;
  }
  default:
    break;
  }
}

int traash_renderer_init(TraashRenderer *r, const char *font, int font_px) {
  memset(r, 0, sizeof(*r));
  r->content_scale = 1.0f;
  r->base_font_px = font_px > 0 ? (float)font_px : 14.0f;
  snprintf(r->font_family, sizeof(r->font_family), "%s", font ? font : "monospace");
  r->tab_h = 40;
  r->status_h = 36;
  r->opacity = 1.0f;
  r->fb_transparent = 0;
  GLuint vs = compile(GL_VERTEX_SHADER, VERT);
  GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG);
  r->prog = traash_glCreateProgram();
  traash_glAttachShader(r->prog, vs);
  traash_glAttachShader(r->prog, fs);
  traash_glLinkProgram(r->prog);
  traash_glDeleteShader(vs);
  traash_glDeleteShader(fs);
  traash_glGenVertexArrays(1, &r->vao);
  traash_glGenBuffers(1, &r->vbo);
  traash_glBindVertexArray(r->vao);
  traash_glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
  traash_glEnableVertexAttribArray(0);
  traash_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
  traash_glEnableVertexAttribArray(1);
  traash_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(2 * sizeof(float)));
  traash_glEnableVertexAttribArray(2);
  traash_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(4 * sizeof(float)));
  traash_atlas_init(&r->atlas, 2048);
  int px = (int)(r->base_font_px * r->content_scale + 0.5f);
  if (px < 8) {
    px = 8;
  }
  if (traash_font_init(&r->font, &r->atlas, r->font_family, px) != 0) {
    return -1;
  }
  traash_glEnable(GL_BLEND);
  traash_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  return 0;
}

void traash_renderer_free(TraashRenderer *r) {
  traash_font_free(&r->font);
  traash_atlas_free(&r->atlas);
  if (r->prog) {
    traash_glDeleteProgram(r->prog);
  }
  if (r->vbo) {
    traash_glDeleteBuffers(1, &r->vbo);
  }
  if (r->vao) {
    traash_glDeleteVertexArrays(1, &r->vao);
  }
  free(g_vtx_solid);
  free(g_vtx_text);
  free(g_vtx_overlay);
  g_vtx_solid = NULL;
  g_vtx_text = NULL;
  g_vtx_overlay = NULL;
  g_vtx_cap = 0;
}

void traash_renderer_resize(TraashRenderer *r, int fb_w, int fb_h) {
  r->fb_w = fb_w;
  r->fb_h = fb_h;
  traash_glViewport(0, 0, fb_w, fb_h);
}

int traash_renderer_set_content_scale(TraashRenderer *r, float scale) {
  if (scale < 0.5f) {
    scale = 0.5f;
  }
  if (scale > 4.0f) {
    scale = 4.0f;
  }
  if (fabsf(scale - r->content_scale) < 0.01f && r->font.face_count > 0) {
    return 0;
  }
  r->content_scale = scale;
  int px = (int)(r->base_font_px * scale + 0.5f);
  if (px < 8) {
    px = 8;
  }
  r->tab_h = (int)(40.0f * scale + 0.5f);
  r->status_h = (int)(36.0f * scale + 0.5f);
  if (r->tab_h < 28) {
    r->tab_h = 28;
  }
  if (r->status_h < 28) {
    r->status_h = 28;
  }

  traash_font_free(&r->font);
  traash_atlas_free(&r->atlas);
  traash_atlas_init(&r->atlas, scale >= 1.5f ? 2048 : 1024);
  if (traash_font_init(&r->font, &r->atlas, r->font_family, px) != 0) {
    TRAASH_LOGE("font rebuild failed at scale %.2f", scale);
    return -1;
  }
  TRAASH_LOGI("content scale %.2f → font %dpx cell %dx%d", scale, px, r->font.cell_w,
              r->font.cell_h);
  return 0;
}

void traash_renderer_show_toast(TraashRenderer *r, const char *text, double until) {
  if (!r) {
    return;
  }
  snprintf(r->toast, sizeof(r->toast), "%s", text ? text : "");
  r->toast_until = until;
}

static void draw_batch(TraashRenderer *r, Vtx *v, int n, int mode) {
  if (n <= 0) {
    return;
  }
  float op = r->fb_transparent ? r->opacity : 1.0f;
  if (op < 0.3f) {
    op = 0.3f;
  }
  if (op > 1.0f) {
    op = 1.0f;
  }
  traash_glUseProgram(r->prog);
  traash_glUniform2f(traash_glGetUniformLocation(r->prog, "u_res"), (float)r->fb_w,
                     (float)r->fb_h);
  traash_glUniform1i(traash_glGetUniformLocation(r->prog, "u_mode"), mode);
  traash_glUniform1f(traash_glGetUniformLocation(r->prog, "u_opacity"), op);
  traash_glActiveTexture(GL_TEXTURE0);
  traash_glBindTexture(GL_TEXTURE_2D, r->atlas.tex);
  traash_glUniform1i(traash_glGetUniformLocation(r->prog, "u_tex"), 0);
  traash_glBindVertexArray(r->vao);
  traash_glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
  traash_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vtx) * (size_t)n), v, GL_DYNAMIC_DRAW);
  traash_glDrawArrays(GL_TRIANGLES, 0, n);
}

static const char *utf8_next(const char *s, uint32_t *cp) {
  const unsigned char *p = (const unsigned char *)s;
  if (!*p) {
    *cp = 0;
    return s;
  }
  if (p[0] < 0x80) {
    *cp = p[0];
    return s + 1;
  }
  if ((p[0] & 0xe0) == 0xc0 && p[1]) {
    *cp = ((uint32_t)(p[0] & 0x1f) << 6) | (uint32_t)(p[1] & 0x3f);
    return s + 2;
  }
  if ((p[0] & 0xf0) == 0xe0 && p[1] && p[2]) {
    *cp = ((uint32_t)(p[0] & 0x0f) << 12) | ((uint32_t)(p[1] & 0x3f) << 6) |
          (uint32_t)(p[2] & 0x3f);
    return s + 3;
  }
  if ((p[0] & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) {
    *cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3f) << 12) |
          ((uint32_t)(p[2] & 0x3f) << 6) | (uint32_t)(p[3] & 0x3f);
    return s + 4;
  }
  *cp = '?';
  return s + 1;
}

static void draw_text(TraashRenderer *r, Vtx *text, int *nt, int capt, float x, float y,
                      const char *s, uint32_t color) {
  float cx = floorf(x + 0.5f);
  float base_y = floorf(y + 0.5f);
  if (!s) {
    return;
  }
  for (const char *p = s; *p;) {
    uint32_t cp = 0;
    p = utf8_next(p, &cp);
    if (!cp) {
      break;
    }
    TraashGlyph *g = traash_font_glyph(&r->font, cp);
    if (!g) {
      continue;
    }
    float gx = cx + (float)g->bearing_x;
    float gy = base_y - (float)g->bearing_y + (float)r->font.ascender;
    push_quad(text, nt, capt, gx, gy, (float)g->w, (float)g->h, g->u0, g->v0, g->u1, g->v1,
              color, 1.0f);
    cx += (float)(g->advance > 0 ? g->advance : r->font.cell_w);
  }
}

static float measure_text(TraashRenderer *r, const char *s) {
  float w = 0;
  if (!s) {
    return 0;
  }
  for (const char *p = s; *p;) {
    uint32_t cp = 0;
    p = utf8_next(p, &cp);
    if (!cp) {
      break;
    }
    TraashGlyph *g = traash_font_glyph(&r->font, cp);
    if (g && g->advance > 0) {
      w += (float)g->advance;
    } else {
      w += (float)r->font.cell_w;
    }
  }
  return w;
}

static void draw_screen_preview(TraashRenderer *r, const TraashScreen *screen,
                                const TraashTheme *theme, float dst_x, float dst_y, float dst_w,
                                float dst_h, Vtx *solid, int *ns, Vtx *text, int *nt, int cap) {
  push_quad(solid, ns, cap, dst_x, dst_y, dst_w, dst_h, 0, 0, 0, 0, theme->background, 1.0f);
  if (!screen || screen->cols < 1 || screen->rows < 1 || dst_w < 6.0f || dst_h < 6.0f) {
    return;
  }
  int max_c = dst_w >= 180.0f ? 40 : (dst_w >= 100.0f ? 28 : 16);
  int max_r = dst_h >= 90.0f ? 14 : (dst_h >= 50.0f ? 10 : 6);
  int prev_cols = (int)(dst_w / 3.5f);
  int prev_rows = (int)(dst_h / 5.0f);
  if (prev_cols > screen->cols) {
    prev_cols = screen->cols;
  }
  if (prev_rows > screen->rows) {
    prev_rows = screen->rows;
  }
  if (prev_cols > max_c) {
    prev_cols = max_c;
  }
  if (prev_rows > max_r) {
    prev_rows = max_r;
  }
  if (prev_cols < 1) {
    prev_cols = 1;
  }
  if (prev_rows < 1) {
    prev_rows = 1;
  }
  float cw = dst_w / (float)prev_cols;
  float ch = dst_h / (float)prev_rows;
  int draw_glyphs = (cw >= 4.5f && ch >= 7.0f);
  float gx_scale = r->font.cell_w > 0 ? cw / (float)r->font.cell_w : 1.0f;
  float gy_scale = r->font.cell_h > 0 ? ch / (float)r->font.cell_h : 1.0f;
  for (int y = 0; y < prev_rows; y++) {
    int sy = y * screen->rows / prev_rows;
    for (int x = 0; x < prev_cols; x++) {
      if (*ns + 6 >= cap || *nt + 6 >= cap) {
        return;
      }
      int sx = x * screen->cols / prev_cols;
      const TraashCell *c = traash_screen_view_cell(screen, sx, sy);
      if (!c) {
        continue;
      }
      uint32_t fg =
          traash_theme_resolve_fg(theme, c->fg, c->attrs & TRAASH_ATTR_TRUECOLOR_FG);
      uint32_t bgc =
          traash_theme_resolve_bg(theme, c->bg, c->attrs & TRAASH_ATTR_TRUECOLOR_BG);
      if (c->attrs & TRAASH_ATTR_INVERSE) {
        uint32_t tmp = fg;
        fg = bgc;
        bgc = tmp;
      }
      float gx = floorf(dst_x + (float)x * cw);
      float gy = floorf(dst_y + (float)y * ch);
      if (bgc != theme->background) {
        push_quad(solid, ns, cap, gx, gy, ceilf(cw), ceilf(ch), 0, 0, 0, 0, bgc, 1.0f);
      }
      if (draw_glyphs && c->cp && c->cp != ' ') {
        TraashGlyph *g = traash_font_glyph(&r->font, c->cp);
        if (g) {
          float qx = gx + (float)g->bearing_x * gx_scale;
          float qy = gy + ((float)r->font.ascender - (float)g->bearing_y) * gy_scale;
          push_quad(text, nt, cap, qx, qy, (float)g->w * gx_scale, (float)g->h * gy_scale,
                    g->u0, g->v0, g->u1, g->v1, fg, 1.0f);
        }
      }
    }
  }
}

static void draw_status_bar(TraashRenderer *r, Vtx *solid, int *ns, Vtx *text, int *nt, int cap,
                            const TraashTheme *theme, const TraashStatusModel *status) {
  float bar_y = (float)(r->fb_h - r->status_h);
  uint32_t bar_bg = theme->status_bar_bg ? theme->status_bar_bg : 0x1a1a1e;
  push_quad(solid, ns, cap, 0, bar_y, (float)r->fb_w, (float)r->status_h, 0, 0, 0, 0, bar_bg,
            1.0f);
  /* top hairline */
  push_quad(solid, ns, cap, 0, bar_y, (float)r->fb_w, 1, 0, 0, 0, 0, 0x3f3f46, 1.0f);
  if (!status || status->count <= 0) {
    return;
  }

  float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
  float edge = 8.0f * scale;
  float gap = status->gap * scale;
  float rad = status->radius * scale;
  float pad_x = status->pad_x * scale;
  float v_pad = status->v_pad * scale;
  int pills = (rad > 0.5f || gap > 0.5f || pad_x > 0.5f);

  if (v_pad < 0.0f) {
    v_pad = 0.0f;
  }
  if (v_pad * 2.0f >= (float)r->status_h - 4.0f) {
    v_pad = ((float)r->status_h - 4.0f) * 0.5f;
  }

  float pill_h = (float)r->status_h - v_pad * 2.0f;
  float pill_y = bar_y + v_pad;
  float text_y =
      floorf(pill_y + (pill_h - (float)r->font.cell_h) * 0.5f + 0.5f);
  if (text_y < bar_y + 2) {
    text_y = bar_y + 2;
  }

  float seg_w[32];
  float left_w = 0, center_w = 0, right_w = 0;
  int left_n = 0, center_n = 0, right_n = 0;
  for (int i = 0; i < status->count; i++) {
    float tw = measure_text(r, status->segs[i].text);
    if (pills) {
      tw += pad_x * 2.0f;
    }
    seg_w[i] = tw;
    if (strcmp(status->segs[i].align, "right") == 0) {
      if (right_n > 0) {
        right_w += gap;
      }
      right_w += tw;
      right_n++;
    } else if (strcmp(status->segs[i].align, "center") == 0) {
      if (center_n > 0) {
        center_w += gap;
      }
      center_w += tw;
      center_n++;
    } else {
      if (left_n > 0) {
        left_w += gap;
      }
      left_w += tw;
      left_n++;
    }
  }

  float lx = edge;
  float rx = (float)r->fb_w - edge - right_w;
  float cx = ((float)r->fb_w - center_w) * 0.5f;
  if (cx < lx + left_w + edge) {
    cx = lx + left_w + edge;
  }
  if (cx + center_w > rx - edge) {
    cx = rx - edge - center_w;
  }

  for (int i = 0; i < status->count; i++) {
    const TraashStatusSegment *seg = &status->segs[i];
    float tw = seg_w[i];
    float x;
    if (strcmp(seg->align, "right") == 0) {
      x = rx;
      rx += tw + gap;
    } else if (strcmp(seg->align, "center") == 0) {
      x = cx;
      cx += tw + gap;
    } else {
      x = lx;
      lx += tw + gap;
    }
    if (pills) {
      float rr = rad;
      if (rr > pill_h * 0.5f) {
        rr = pill_h * 0.5f;
      }
      push_round_rect(solid, ns, cap, x, pill_y, tw, pill_h, rr, seg->bg, 1.0f);
      draw_text(r, text, nt, cap, x + pad_x, text_y, seg->text, seg->fg);
    } else {
      push_quad(solid, ns, cap, x, bar_y, tw, (float)r->status_h, 0, 0, 0, 0, seg->bg, 1.0f);
      draw_text(r, text, nt, cap, x, text_y, seg->text, seg->fg);
    }
  }
}

static void pane_geom(const TraashRenderer *r, const TraashPane *pane, int zoom, int term_top,
                      int term_h, float *px, float *py, float *pw, float *ph) {
  *px = (float)pane->x * (float)r->fb_w;
  *py = (float)term_top + (float)pane->y * (float)term_h;
  *pw = zoom ? (float)r->fb_w : (float)pane->w * (float)r->fb_w;
  *ph = zoom ? (float)term_h : (float)pane->h * (float)term_h;
}

static void tab_bar_metrics(const TraashRenderer *r, float *pad, float *ty, float *th,
                            float *tab_w, float *gap, float *close_w, float *new_w) {
  float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
  *pad = 8.0f * scale;
  *ty = *pad;
  *th = (float)r->tab_h - *pad * 2.0f;
  *tab_w = 118.0f * scale;
  *gap = 6.0f * scale;
  *close_w = 22.0f * scale;
  *new_w = *th; /* square-ish + button */
}

int traash_renderer_tab_hit(const TraashRenderer *r, TraashSession *session, float mx,
                            float my, TraashTabHit *out) {
  memset(out, 0, sizeof(*out));
  if (!session || my < 0 || my >= (float)r->tab_h) {
    return 0;
  }
  float pad, ty, th, tab_w, gap, close_w, new_w;
  tab_bar_metrics(r, &pad, &ty, &th, &tab_w, &gap, &close_w, &new_w);
  if (my < ty || my >= ty + th) {
    return 0;
  }

  float new_x = (float)r->fb_w - pad - new_w;
  if (mx >= new_x && mx < new_x + new_w) {
    out->kind = TRAASH_TAB_HIT_NEW;
    return 1;
  }

  float tx = pad;
  float tabs_right = new_x - gap;
  for (TraashWindow *w = session->windows; w; w = w->next) {
    if (tx + tab_w > tabs_right) {
      break;
    }
    if (mx >= tx && mx < tx + tab_w) {
      float close_x = tx + tab_w - close_w - 4.0f * (r->content_scale > 0.1f ? r->content_scale : 1.0f);
      out->window = w;
      if (mx >= close_x && mx < tx + tab_w) {
        out->kind = TRAASH_TAB_HIT_CLOSE;
      } else {
        out->kind = TRAASH_TAB_HIT_TAB;
      }
      return 1;
    }
    tx += tab_w + gap;
  }
  return 0;
}

int traash_renderer_hit_test(const TraashRenderer *r, TraashSession *session, float mx,
                             float my, TraashHit *out) {
  memset(out, 0, sizeof(*out));
  if (!session || !session->active) {
    return 0;
  }
  int cw = r->font.cell_w;
  int ch = r->font.cell_h;
  if (cw < 1 || ch < 1) {
    return 0;
  }
  int term_top = r->tab_h;
  int term_h = r->fb_h - r->tab_h - r->status_h;
  if (term_h < ch) {
    term_h = ch;
  }
  TraashWindow *win = session->active;
  int zoom = win->zoomed_pane_id >= 0;
  for (TraashPane *pane = win->panes; pane; pane = pane->next) {
    if (zoom && pane->id != win->zoomed_pane_id) {
      continue;
    }
    float px, py, pw, ph;
    pane_geom(r, pane, zoom, term_top, term_h, &px, &py, &pw, &ph);
    if (mx < px || my < py || mx >= px + pw || my >= py + ph) {
      continue;
    }
    int cx = (int)((mx - px - 2.0f) / (float)cw);
    int cy = (int)((my - py - 2.0f) / (float)ch);
    if (cx < 0) {
      cx = 0;
    }
    if (cy < 0) {
      cy = 0;
    }
    if (cx >= pane->screen.cols) {
      cx = pane->screen.cols - 1;
    }
    if (cy >= pane->screen.rows) {
      cy = pane->screen.rows - 1;
    }
    out->pane = pane;
    out->cell_x = cx;
    out->cell_y = cy;
    out->pane_x = px;
    out->pane_y = py;
    out->pane_w = pw;
    out->pane_h = ph;
    return 1;
  }
  return 0;
}

void traash_renderer_draw(TraashRenderer *r, TraashSession *session, const TraashTheme *theme,
                          const TraashStatusModel *status, double now,
                          const TraashContextMenu *menu,
                          const TraashShortcutsOverlay *shortcuts,
                          const TraashLayoutOverlay *layouts, const TraashOverviewOverlay *overview,
                          const TraashQuitConfirm *quit_confirm, const TraashKeymap *keymap,
                          const TraashSearchDraw *search) {
  traash_renderer_draw_ex(r, session, theme, status, now, menu, shortcuts, layouts, overview,
                          quit_confirm, keymap, search, 0);
}

void traash_renderer_draw_ex(TraashRenderer *r, TraashSession *session, const TraashTheme *theme,
                             const TraashStatusModel *status, double now,
                             const TraashContextMenu *menu,
                             const TraashShortcutsOverlay *shortcuts,
                             const TraashLayoutOverlay *layouts,
                             const TraashOverviewOverlay *overview,
                             const TraashQuitConfirm *quit_confirm, const TraashKeymap *keymap,
                             const TraashSearchDraw *search, int fast_resize) {
  float br, bg, bb;
  rgba(theme->background, &br, &bg, &bb);
  if (r->fb_transparent) {
    traash_glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  } else {
    traash_glClearColor(br, bg, bb, 1.0f);
  }
  traash_glClear(GL_COLOR_BUFFER_BIT);

  const int cap = 400000; /* overlay holds UI label pixels too */
  if (ensure_vtx_bufs(cap) != 0) {
    return;
  }
  Vtx *solid = g_vtx_solid;
  Vtx *text = g_vtx_text;
  Vtx *overlay = g_vtx_overlay;
  int ns = 0, nt = 0, no = 0;

  /* Full-window backdrop so transparent FB composites with desktop */
  if (r->fb_transparent) {
    push_quad(solid, &ns, cap, 0, 0, (float)r->fb_w, (float)r->fb_h, 0, 0, 0, 0,
              theme->background, 1.0f);
  }

  int cw = r->font.cell_w;
  int ch = r->font.cell_h;
  int term_top = r->tab_h;
  int term_h = r->fb_h - r->tab_h - r->status_h;
  if (term_h < ch) {
    term_h = ch;
  }

  /* Adwaita-like tab bar */
  uint32_t chrome_bg = 0x1a1a1e;
  uint32_t tab_idle = 0x2a2a2e;
  uint32_t tab_active = 0x33d17a;
  push_quad(solid, &ns, cap, 0, 0, (float)r->fb_w, (float)r->tab_h, 0, 0, 0, 0, chrome_bg,
            1.0f);
  if (session) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    float pad, ty, th, tab_w, gap, close_w, new_w;
    tab_bar_metrics(r, &pad, &ty, &th, &tab_w, &gap, &close_w, &new_w);
    float new_x = (float)r->fb_w - pad - new_w;
    float tabs_right = new_x - gap;
    float tx = pad;
    float text_y = ty + (th - (float)r->font.cell_h) * 0.5f;

    for (TraashWindow *w = session->windows; w; w = w->next) {
      if (tx + tab_w > tabs_right) {
        break;
      }
      int active = (w == session->active);
      uint32_t col = active ? tab_active : tab_idle;
      uint32_t fg = active ? 0x14181a : 0xf5f5f5;
      uint32_t close_fg = active ? 0x2a2a2e : 0xa0a0a8;
      push_round_rect(solid, &ns, cap, tx, ty, tab_w, th, 8.0f * scale, col, 1.0f);
      /* Title — leave room for the close control */
      draw_text(r, text, &nt, cap, tx + 12.0f * scale, text_y, w->name, fg);
      /* Attention badge: command finished / bell while this tab was in the background */
      if (w->attention && !active) {
        float d = 7.0f * scale;
        float dx = tx + tab_w - close_w - d - 8.0f * scale;
        float dy = ty + (th - d) * 0.5f;
        float pulse = 0.7f + 0.3f * (0.5f + 0.5f * sinf((float)(now * 5.0)));
        push_round_rect(solid, &ns, cap, dx, dy, d, d, d * 0.5f, 0xffb454, pulse);
      }
      draw_text(r, text, &nt, cap, tx + tab_w - close_w - 2.0f * scale, text_y, "×", close_fg);
      tx += tab_w + gap;
    }

    /* New tab (+) — top right */
    push_round_rect(solid, &ns, cap, new_x, ty, new_w, th, 8.0f * scale, tab_idle, 1.0f);
    float plus = 5.0f * scale;
    float px = new_x + new_w * 0.5f;
    float py = ty + th * 0.5f;
    float thick = fmaxf(1.6f, 1.8f * scale);
    push_quad(solid, &ns, cap, px - plus, py - thick * 0.5f, plus * 2.0f, thick, 0, 0, 0, 0,
              0xf5f5f5, 1.0f);
    push_quad(solid, &ns, cap, px - thick * 0.5f, py - plus, thick, plus * 2.0f, 0, 0, 0, 0,
              0xf5f5f5, 1.0f);
  }

  int blink_on = ((int)(now * 2.0) % 2) == 0;
  float ui_scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;

  if (session && session->active) {
    TraashWindow *win = session->active;
    int zoom = win->zoomed_pane_id >= 0;
    for (TraashPane *pane = win->panes; pane; pane = pane->next) {
      if (zoom && pane->id != win->zoomed_pane_id) {
        continue;
      }
      float px, py, pw, ph;
      pane_geom(r, pane, zoom, term_top, term_h, &px, &py, &pw, &ph);
      /* Normalized split geometry often lands on half pixels. Snap both edges
       * so pane borders and every glyph in the pane share the pixel grid. */
      float x1 = floorf(px + pw + 0.5f);
      float y1 = floorf(py + ph + 0.5f);
      px = floorf(px + 0.5f);
      py = floorf(py + 0.5f);
      pw = fmaxf(1.0f, x1 - px);
      ph = fmaxf(1.0f, y1 - py);
      int active = (pane == win->active);

      int cols = (int)((pw - 4) / (float)cw);
      int rows = (int)((ph - 4) / (float)ch);
      if (cols < 1) {
        cols = 1;
      }
      if (rows < 1) {
        rows = 1;
      }

      /* Live window drag: fill pane background only — per-cell glyph builds are
       * what made resize feel like a time-lapse under Wayland/X11. Defer grid
       * realloc until the drag settles. */
      if (fast_resize) {
        if (pane->screen.cols != cols || pane->screen.rows != rows) {
          pane->pty_pending_cols = cols;
          pane->pty_pending_rows = rows;
          pane->pty_resize_at = -1.0;
        }
        push_quad(solid, &ns, cap, px, py, pw, ph, 0, 0, 0, 0, theme->background, 1.0f);
        uint32_t border = active ? theme->active_pane_border : theme->pane_border;
        float bw = fmaxf(1.0f, ui_scale);
        push_quad(solid, &ns, cap, px, py, pw, bw, 0, 0, 0, 0, border, 1.0f);
        push_quad(solid, &ns, cap, px, py + ph - bw, pw, bw, 0, 0, 0, 0, border, 1.0f);
        push_quad(solid, &ns, cap, px, py, bw, ph, 0, 0, 0, 0, border, 1.0f);
        push_quad(solid, &ns, cap, px + pw - bw, py, bw, ph, 0, 0, 0, 0, border, 1.0f);
        continue;
      }

      if (pane->screen.cols != cols || pane->screen.rows != rows) {
        traash_pane_resize_cells(pane, cols, rows);
      }

      /* Mark search hits on the visible viewport (1 = match, 2 = current). */
      unsigned char *search_mark = NULL;
      int mark_n = pane->screen.rows * pane->screen.cols;
      if (search && search->active && search->pane == pane && search->match_len > 0 &&
          search->match_count > 0 && mark_n > 0) {
        search_mark = calloc((size_t)mark_n, 1);
        if (search_mark) {
          for (int i = 0; i < search->match_count; i++) {
            const TraashSearchMatch *m = &search->matches[i];
            int vy = traash_screen_abs_to_view_y(&pane->screen, m->row);
            if (vy < 0) {
              continue;
            }
            unsigned char kind = (i == search->current) ? 2 : 1;
            for (int k = 0; k < search->match_len; k++) {
              int vx = m->col + k;
              if (vx < 0 || vx >= pane->screen.cols) {
                continue;
              }
              int idx = vy * pane->screen.cols + vx;
              if (kind > search_mark[idx]) {
                search_mark[idx] = kind;
              }
            }
          }
        }
      }

      for (int y = 0; y < pane->screen.rows; y++) {
        for (int x = 0; x < pane->screen.cols; x++) {
          const TraashCell *c = traash_screen_view_cell(&pane->screen, x, y);
          if (!c) {
            continue;
          }
          uint32_t fg =
              traash_theme_resolve_fg(theme, c->fg, c->attrs & TRAASH_ATTR_TRUECOLOR_FG);
          uint32_t bgc =
              traash_theme_resolve_bg(theme, c->bg, c->attrs & TRAASH_ATTR_TRUECOLOR_BG);
          if (c->attrs & TRAASH_ATTR_INVERSE) {
            uint32_t tmp = fg;
            fg = bgc;
            bgc = tmp;
          }
          int selected = traash_selection_contains(&pane->selection, x, y);
          int search_kind =
              search_mark ? (int)search_mark[y * pane->screen.cols + x] : 0;
          if (selected) {
            bgc = 0x1f6feb;
            fg = 0xffffff;
          } else if (search_kind == 2) {
            bgc = 0xe3b341; /* current match */
            fg = 0x0d1117;
          } else if (search_kind == 1) {
            bgc = 0x6e5120; /* other matches */
            fg = 0xf0f0f0;
          }
          float gx = floorf(px + 2.0f + (float)(x * cw) + 0.5f);
          float gy = floorf(py + 2.0f + (float)(y * ch) + 0.5f);
          if (bgc != theme->background || selected || search_kind) {
            push_quad(solid, &ns, cap, gx, gy, (float)cw, (float)ch, 0, 0, 0, 0, bgc, 1);
          }
          if (c->cp && c->cp != ' ') {
            TraashGlyph *g = traash_font_glyph(&r->font, c->cp);
            if (g) {
              float qx = gx + (float)g->bearing_x;
              float qy = gy + (float)r->font.ascender - (float)g->bearing_y;
              push_quad(text, &nt, cap, qx, qy, (float)g->w, (float)g->h, g->u0, g->v0, g->u1,
                        g->v1, fg, 1.0f);
            }
          }
          if (c->attrs & (TRAASH_ATTR_UNDERLINE | TRAASH_ATTR_UNDERDOUBLE |
                          TRAASH_ATTR_UNDERCURL | TRAASH_ATTR_STRIKE)) {
            float thick = fmaxf(1.0f, floorf((float)ch * 0.06f + 0.5f));
            if (c->attrs & TRAASH_ATTR_STRIKE) {
              float sy = gy + (float)ch * 0.5f;
              push_quad(solid, &ns, cap, gx, sy, (float)cw, thick, 0, 0, 0, 0, fg, 1.0f);
            }
            if (c->attrs & TRAASH_ATTR_UNDERDOUBLE) {
              float uy = gy + (float)ch - thick * 3.0f;
              push_quad(solid, &ns, cap, gx, uy, (float)cw, thick, 0, 0, 0, 0, fg, 1.0f);
              push_quad(solid, &ns, cap, gx, uy + thick * 1.8f, (float)cw, thick, 0, 0, 0, 0,
                        fg, 1.0f);
            } else if (c->attrs & TRAASH_ATTR_UNDERCURL) {
              /* Approximate curl with three short dashes along the baseline. */
              float uy = gy + (float)ch - thick * 2.0f;
              float seg = (float)cw / 3.0f;
              for (int s = 0; s < 3; s++) {
                float ox = gx + seg * (float)s;
                float oy = uy + ((s & 1) ? thick : -thick * 0.5f);
                push_quad(solid, &ns, cap, ox, oy, seg * 0.85f, thick, 0, 0, 0, 0, fg, 1.0f);
              }
            } else if (c->attrs & TRAASH_ATTR_UNDERLINE) {
              float uy = gy + (float)ch - thick * 1.5f;
              push_quad(solid, &ns, cap, gx, uy, (float)cw, thick, 0, 0, 0, 0, fg, 1.0f);
            }
          }
        }
      }
      free(search_mark);

      /* Cursor drawn after text so it stays visible */
      if (active && pane->screen.scroll_offset == 0 && pane->screen.cursor_visible &&
          blink_on) {
        int cx = pane->screen.cursor_x;
        int cy = pane->screen.cursor_y;
        if (cx >= 0 && cy >= 0 && cx < pane->screen.cols && cy < pane->screen.rows) {
          float gx = floorf(px + 2.0f + (float)(cx * cw) + 0.5f);
          float gy = floorf(py + 2.0f + (float)(cy * ch) + 0.5f);
          int style = pane->screen.cursor_style;
          if (style == 0) {
            style = r->cursor_style; /* fall back to config default for block */
          }
          if (style == 1) {
            float beam = fmaxf(2.0f, (float)cw * 0.15f);
            push_quad(overlay, &no, cap, gx, gy, beam, (float)ch, 0, 0, 0, 0, theme->cursor,
                      1.0f);
          } else if (style == 2) {
            float uh = fmaxf(2.0f, (float)ch * 0.15f);
            push_quad(overlay, &no, cap, gx, gy + (float)ch - uh, (float)cw, uh, 0, 0, 0, 0,
                      theme->cursor, 1.0f);
          } else {
            push_quad(overlay, &no, cap, gx, gy, (float)cw, (float)ch, 0, 0, 0, 0,
                      theme->cursor, 1.0f);
            const TraashCell *cc = traash_screen_cell(&pane->screen, cx, cy);
            if (cc && cc->cp && cc->cp != ' ') {
              TraashGlyph *g = traash_font_glyph(&r->font, cc->cp);
              if (g) {
                float qx = gx + (float)g->bearing_x;
                float qy = gy + (float)r->font.ascender - (float)g->bearing_y;
                push_quad(text, &nt, cap, qx, qy, (float)g->w, (float)g->h, g->u0, g->v0,
                          g->u1, g->v1, theme->cursor_text, 1.0f);
              }
            }
          }
        }
      }
    }
  }

  draw_status_bar(r, solid, &ns, text, &nt, cap, theme, status);

  if (menu && menu->open) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    float mw = traash_context_menu_width(scale);
    float row = traash_context_menu_row_height(scale);
    float sep = 9.0f * scale;
    float pad = 8.0f * scale;
    float mh = traash_context_menu_height(scale);
    float rad = 14.0f * scale;
    float icon = 20.0f * scale;
    /* Soft stacked shadow */
    push_round_rect(overlay, &no, cap, menu->x + 3 * scale, menu->y + 5 * scale, mw, mh, rad,
                    0x000000, 0.18f);
    push_round_rect(overlay, &no, cap, menu->x + 1 * scale, menu->y + 2 * scale, mw, mh, rad,
                    0x000000, 0.28f);
    /* Panel */
    push_round_rect(overlay, &no, cap, menu->x, menu->y, mw, mh, rad, 0x2a2a2e, 1.0f);
    /* Inner rim */
    push_round_rect(overlay, &no, cap, menu->x + 1.0f * scale, menu->y + 1.0f * scale,
                    mw - 2.0f * scale, mh - 2.0f * scale, rad - 1.0f * scale, 0x3a3a40, 0.35f);
    push_round_rect(overlay, &no, cap, menu->x + 2.0f * scale, menu->y + 2.0f * scale,
                    mw - 4.0f * scale, mh - 4.0f * scale, rad - 2.0f * scale, 0x2a2a2e, 1.0f);

    float cy = menu->y + pad;
    for (int i = 0; i < TRAASH_MENU_COUNT; i++) {
      uint32_t icon_col = 0xc8c8ce;
      if (menu->hover == i) {
        push_round_rect(overlay, &no, cap, menu->x + 6 * scale, cy, mw - 12 * scale, row,
                        10.0f * scale, 0x33d17a, 1.0f);
        icon_col = 0x14181a;
      }
      float ix = menu->x + 14.0f * scale;
      float iy = cy + (row - icon) * 0.5f;
      push_menu_icon(overlay, &no, cap, i, ix, iy, icon, icon_col);
      cy += row;
      if (traash_context_menu_sep_after(i)) {
        float sy = cy + sep * 0.45f;
        push_quad(overlay, &no, cap, menu->x + 14 * scale, sy, mw - 28 * scale, 1, 0, 0, 0, 0,
                  0x3f3f46, 1.0f);
        cy += sep;
      }
    }
  }

  /* Shortcuts cheatsheet — drawn above context menu */
  TraashShortcutsLayout sc_lay;
  int sc_open = shortcuts && shortcuts->open;
  if (sc_open) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    traash_shortcuts_overlay_layout(r->fb_w, r->fb_h, scale, r->font.cell_h, &sc_lay);
    /* Dim the terminal */
    push_quad(overlay, &no, cap, 0, 0, (float)r->fb_w, (float)r->fb_h, 0, 0, 0, 0, 0x000000,
              0.45f);
    float rad = sc_lay.rad;
    /* Soft shadow */
    push_round_rect(overlay, &no, cap, sc_lay.x + 4 * scale, sc_lay.y + 8 * scale, sc_lay.w,
                    sc_lay.h, rad, 0x000000, 0.22f);
    push_round_rect(overlay, &no, cap, sc_lay.x + 1 * scale, sc_lay.y + 3 * scale, sc_lay.w,
                    sc_lay.h, rad, 0x000000, 0.30f);
    /* Translucent panel */
    push_round_rect(overlay, &no, cap, sc_lay.x, sc_lay.y, sc_lay.w, sc_lay.h, rad, 0x1e1e24,
                    0.88f);
    /* Subtle rim */
    push_round_rect(overlay, &no, cap, sc_lay.x + 1.0f * scale, sc_lay.y + 1.0f * scale,
                    sc_lay.w - 2.0f * scale, sc_lay.h - 2.0f * scale, rad - 1.0f * scale,
                    0x3a3a44, 0.40f);
    push_round_rect(overlay, &no, cap, sc_lay.x + 2.0f * scale, sc_lay.y + 2.0f * scale,
                    sc_lay.w - 4.0f * scale, sc_lay.h - 4.0f * scale, rad - 2.0f * scale,
                    0x1e1e24, 0.88f);

    /* Accent underline under title area */
    float accent_y =
        sc_lay.y + sc_lay.pad + sc_lay.title_h + (float)r->font.cell_h + 6.0f * scale;
    push_quad(overlay, &no, cap, sc_lay.x + sc_lay.pad, accent_y, sc_lay.w - sc_lay.pad * 2.0f,
              2.0f * scale, 0, 0, 0, 0, 0x33d17a, 0.85f);
  }

  draw_batch(r, solid, ns, 0);
  /* Transparent FB: keep window alpha from the solid pass so UI overlays
   * composite over the terminal instead of punching through to the desktop. */
  if (r->fb_transparent && traash_glBlendFuncSeparate) {
    traash_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
  }
  draw_batch(r, text, nt, 1);
  draw_batch(r, overlay, no, 0);

  /* Menu labels above panel (atlas text, anti-aliased). */
  if (menu && menu->open) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    float row = traash_context_menu_row_height(scale);
    float sep = 9.0f * scale;
    float pad = 8.0f * scale;
    int nmt = 0;
    float cy = menu->y + pad;
    for (int i = 0; i < TRAASH_MENU_COUNT; i++) {
      uint32_t label_fg = (menu->hover == i) ? 0x14181a : 0xf5f5f5;
      float text_x = menu->x + 42.0f * scale;
      float text_y = cy + (row - (float)r->font.cell_h) * 0.5f;
      draw_text(r, text, &nmt, cap, text_x, text_y, traash_context_menu_label(i), label_fg);
      cy += row;
      if (traash_context_menu_sep_after(i)) {
        cy += sep;
      }
    }
    draw_batch(r, text, nmt, 1);
  }

  /* Shortcuts overlay labels + keycap pills */
  if (sc_open) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    float pad = sc_lay.pad;
    float row_h = sc_lay.row_h;
    float section_gap = sc_lay.section_gap;
    float col_gap = sc_lay.col_gap;
    float pill_pad_x = 10.0f * scale;
    float pill_pad_y = 4.0f * scale;
    float pill_rad = 7.0f * scale;

    int nst = 0;
    int nso = 0;
    /* Reuse overlay buffer for keycap pills */
    no = 0;

    float content_top = sc_lay.content_top;
    float content_w = sc_lay.w - pad * 2.0f;
    float col_w = sc_lay.columns == 2 ? (content_w - col_gap) * 0.5f : content_w;

    /* Title + subtitle */
    float title_y = sc_lay.y + pad;
    draw_text(r, text, &nst, cap, sc_lay.x + pad, title_y, "Keyboard shortcuts", 0xf5f5f5);
    char sub[96];
    TraashKeyBind tb;
    if (keymap && traash_keymap_find_action(keymap, TRAASH_ACTION_SHORTCUTS, &tb)) {
      char chord[64];
      traash_keymap_format(keymap, &tb, chord, sizeof(chord));
      snprintf(sub, sizeof(sub), "Toggle with %s", chord);
    } else {
      snprintf(sub, sizeof(sub), "Toggle with Ctrl-Shift-/");
    }
    float sub_y = sc_lay.y + pad + (float)r->font.cell_h + 6.0f * scale;
    draw_text(r, text, &nst, cap, sc_lay.x + pad, sub_y, sub, 0x9a9aa2);

    int groups = traash_shortcut_group_count();
    int half = sc_lay.columns == 2 ? (groups + 1) / 2 : groups;

    for (int col = 0; col < sc_lay.columns; col++) {
      int g0 = col == 0 ? 0 : half;
      int g1 = col == 0 ? half : groups;
      float cx = sc_lay.x + pad + (float)col * (col_w + col_gap);
      float cy = content_top;

      for (int gi = g0; gi < g1; gi++) {
        const TraashShortcutGroup *g = &TRAASH_SHORTCUT_GROUPS[gi];
        if (gi > g0) {
          cy += section_gap;
        }
        /* Section header */
        draw_text(r, text, &nst, cap, cx, cy, g->title, 0x33d17a);
        cy += row_h;

        for (int ai = 0; ai < g->count; ai++) {
          TraashAction a = g->actions[ai];
          const char *label = traash_action_label(a);
          char chord[64];
          TraashKeyBind b;
          if (keymap && traash_keymap_find_action(keymap, a, &b)) {
            traash_keymap_format(keymap, &b, chord, sizeof(chord));
          } else {
            snprintf(chord, sizeof(chord), "—");
          }

          float text_y = cy + (row_h - (float)r->font.cell_h) * 0.5f;
          draw_text(r, text, &nst, cap, cx, text_y, label, 0xe8e8ec);

          float cw = measure_text(r, chord);
          float pill_w = cw + pill_pad_x * 2.0f;
          float pill_h = (float)r->font.cell_h + pill_pad_y * 2.0f;
          float px = cx + col_w - pill_w;
          float py = cy + (row_h - pill_h) * 0.5f;
          if (px < cx + measure_text(r, label) + 12.0f * scale) {
            /* Overlap: keep pill at end, label may clip visually on narrow cols */
            px = cx + col_w - pill_w;
          }
          push_round_rect(overlay, &nso, cap, px, py, pill_w, pill_h, pill_rad, 0x2f2f38, 0.95f);
          draw_text(r, text, &nst, cap, px + pill_pad_x, text_y, chord, 0xd0d0d8);
          cy += row_h;
        }
      }
    }

    /* Footer */
    const char *footer = "Esc to close";
    float fw = measure_text(r, footer);
    draw_text(r, text, &nst, cap, sc_lay.x + (sc_lay.w - fw) * 0.5f,
              sc_lay.y + sc_lay.h - pad - (float)r->font.cell_h, footer, 0x7a7a82);

    draw_batch(r, overlay, nso, 0);
    draw_batch(r, text, nst, 1);
  }

  /* Layout picker overlay */
  int lo_open = layouts && layouts->open;
  if (lo_open) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    TraashLayoutOverlayGeom lg;
    traash_layout_overlay_geom(r->fb_w, r->fb_h, scale, r->font.cell_h, layouts->count, &lg);

    int nlo = 0;
    int nlt = 0;
    push_quad(overlay, &nlo, cap, 0, 0, (float)r->fb_w, (float)r->fb_h, 0, 0, 0, 0, 0x000000,
              0.45f);
    push_round_rect(overlay, &nlo, cap, lg.x + 4 * scale, lg.y + 8 * scale, lg.w, lg.h, lg.rad,
                    0x000000, 0.22f);
    push_round_rect(overlay, &nlo, cap, lg.x, lg.y, lg.w, lg.h, lg.rad, 0x1e1e24, 0.92f);
    push_quad(overlay, &nlo, cap, lg.x + lg.pad, lg.y + lg.pad + lg.title_h,
              lg.w - lg.pad * 2.0f, 2.0f * scale, 0, 0, 0, 0, 0x33d17a, 0.85f);

    float title_y = lg.y + lg.pad;
    draw_text(r, text, &nlt, cap, lg.x + lg.pad, title_y, "Layouts", 0xf5f5f5);
    const char *sub = layouts->save_mode ? "Save current session as…"
                                         : "Apply a saved tab + split layout";
    draw_text(r, text, &nlt, cap, lg.x + lg.pad,
              title_y + (float)r->font.cell_h + 4.0f * scale, sub, 0x9a9aa2);

    if (layouts->save_mode) {
      char prompt[96];
      snprintf(prompt, sizeof(prompt), "Name: %s_", layouts->save_name);
      float ty = lg.content_top + lg.row_h;
      push_round_rect(overlay, &nlo, cap, lg.x + lg.pad, ty, lg.w - lg.pad * 2.0f, lg.row_h,
                      6.0f * scale, 0x2a2a32, 1.0f);
      draw_text(r, text, &nlt, cap, lg.x + lg.pad + 10.0f * scale,
                ty + (lg.row_h - (float)r->font.cell_h) * 0.5f, prompt, 0xf5f5f5);
      const char *hint = "Enter save · Esc cancel";
      float fw = measure_text(r, hint);
      draw_text(r, text, &nlt, cap, lg.x + (lg.w - fw) * 0.5f,
                lg.y + lg.h - lg.pad - (float)r->font.cell_h, hint, 0x7a7a82);
    } else if (layouts->count <= 0) {
      draw_text(r, text, &nlt, cap, lg.x + lg.pad, lg.content_top, "No layouts found",
                0x9a9aa2);
      const char *hint = "s save current · Esc close";
      float fw = measure_text(r, hint);
      draw_text(r, text, &nlt, cap, lg.x + (lg.w - fw) * 0.5f,
                lg.y + lg.h - lg.pad - (float)r->font.cell_h, hint, 0x7a7a82);
    } else {
      int start = 0;
      if (layouts->selected >= lg.visible_rows) {
        start = layouts->selected - lg.visible_rows + 1;
      }
      for (int i = 0; i < lg.visible_rows && start + i < layouts->count; i++) {
        int idx = start + i;
        float ry = lg.content_top + (float)i * lg.row_h;
        int sel = (idx == layouts->selected);
        if (sel) {
          push_round_rect(overlay, &nlo, cap, lg.x + lg.pad, ry, lg.w - lg.pad * 2.0f,
                          lg.row_h - 2.0f * scale, 6.0f * scale, 0x33d17a, 0.95f);
        }
        uint32_t fg = sel ? 0x14181a : 0xe8e8ec;
        draw_text(r, text, &nlt, cap, lg.x + lg.pad + 12.0f * scale,
                  ry + (lg.row_h - (float)r->font.cell_h) * 0.5f, layouts->names[idx].name,
                  fg);
        if (layouts->names[idx].user) {
          const char *tag = "user";
          float tw = measure_text(r, tag);
          draw_text(r, text, &nlt, cap, lg.x + lg.w - lg.pad - tw - 12.0f * scale,
                    ry + (lg.row_h - (float)r->font.cell_h) * 0.5f, tag,
                    sel ? 0x2a2a2e : 0x7a7a82);
        }
      }
      const char *hint = "Enter apply · s save · Del delete · Esc close";
      float fw = measure_text(r, hint);
      draw_text(r, text, &nlt, cap, lg.x + (lg.w - fw) * 0.5f,
                lg.y + lg.h - lg.pad - (float)r->font.cell_h, hint, 0x7a7a82);
    }

    draw_batch(r, overlay, nlo, 0);
    draw_batch(r, text, nlt, 1);
  }

  /* Session overview — live tab/pane preview grid */
  int ov_open = overview && overview->open && !fast_resize;
  if (ov_open) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    int count = traash_overview_overlay_item_count(overview, session);
    TraashOverviewLayout ol;
    traash_overview_overlay_layout(r->fb_w, r->fb_h, scale, r->font.cell_h, count, &ol);
    TraashOverviewOverlay tmp = *overview;
    traash_overview_overlay_clamp(&tmp, session, &ol);

    int nvo = 0;
    int nvt = 0;
    push_quad(overlay, &nvo, cap, 0, 0, (float)r->fb_w, (float)r->fb_h, 0, 0, 0, 0, 0x000000,
              0.45f);
    push_round_rect(overlay, &nvo, cap, ol.x + 4 * scale, ol.y + 8 * scale, ol.w, ol.h, ol.rad,
                    0x000000, 0.22f);
    push_round_rect(overlay, &nvo, cap, ol.x, ol.y, ol.w, ol.h, ol.rad, 0x1e1e24, 0.88f);
    push_quad(overlay, &nvo, cap, ol.x + ol.pad, ol.y + ol.pad + ol.title_h,
              ol.w - ol.pad * 2.0f, 2.0f * scale, 0, 0, 0, 0, 0x33d17a, 0.85f);

    const char *title = overview->mode == TRAASH_OVERVIEW_PANES ? "Panes" : "Tabs";
    draw_text(r, text, &nvt, cap, ol.x + ol.pad, ol.y + ol.pad, title, 0xf5f5f5);
    char sub[96];
    if (overview->mode == TRAASH_OVERVIEW_PANES) {
      TraashWindow *tw = traash_overview_overlay_pane_tab(overview, session);
      snprintf(sub, sizeof(sub), "Tab %s · %d pane%s", tw && tw->name[0] ? tw->name : "?",
               count, count == 1 ? "" : "s");
    } else {
      snprintf(sub, sizeof(sub), "%d open tab%s", count, count == 1 ? "" : "s");
    }
    draw_text(r, text, &nvt, cap, ol.x + ol.pad,
              ol.y + ol.pad + (float)r->font.cell_h + 4.0f * scale, sub, 0x9a9aa2);

    int vis = ol.visible;
    if (vis > count - tmp.scroll) {
      vis = count - tmp.scroll;
    }
    if (vis < 0) {
      vis = 0;
    }
    for (int i = 0; i < vis; i++) {
      int idx = tmp.scroll + i;
      float cx, cy, cw, ch;
      traash_overview_overlay_card_rect(&ol, i, &cx, &cy, &cw, &ch);
      int sel = (idx == tmp.selected);
      push_round_rect(overlay, &nvo, cap, cx, cy, cw, ch, 10.0f * scale,
                      sel ? 0x2a2a32 : 0x24242c, 1.0f);
      if (sel) {
        float bw = fmaxf(1.5f, 2.0f * scale);
        push_quad(overlay, &nvo, cap, cx, cy, cw, bw, 0, 0, 0, 0, 0x33d17a, 1.0f);
        push_quad(overlay, &nvo, cap, cx, cy + ch - bw, cw, bw, 0, 0, 0, 0, 0x33d17a, 1.0f);
        push_quad(overlay, &nvo, cap, cx, cy, bw, ch, 0, 0, 0, 0, 0x33d17a, 1.0f);
        push_quad(overlay, &nvo, cap, cx + cw - bw, cy, bw, ch, 0, 0, 0, 0, 0x33d17a, 1.0f);
      }

      float prev_x = cx + 8.0f * scale;
      float prev_y = cy + 8.0f * scale;
      float prev_w = cw - 16.0f * scale;
      float prev_h = ol.preview_h - 10.0f * scale;
      if (prev_h < 16.0f) {
        prev_h = 16.0f;
      }
      push_round_rect(overlay, &nvo, cap, prev_x, prev_y, prev_w, prev_h, 6.0f * scale,
                      theme->background, 1.0f);

      TraashWindow *w = NULL;
      TraashPane *pane = NULL;
      if (overview->mode == TRAASH_OVERVIEW_PANES) {
        w = traash_overview_overlay_pane_tab(overview, session);
        pane = traash_overview_overlay_nth_pane(w, idx);
        if (pane) {
          draw_screen_preview(r, &pane->screen, theme, prev_x, prev_y, prev_w, prev_h, overlay,
                              &nvo, text, &nvt, cap);
        }
      } else {
        w = traash_overview_overlay_nth_window(session, idx);
        if (w) {
          int zoom = w->zoomed_pane_id >= 0;
          for (TraashPane *p = w->panes; p; p = p->next) {
            if (zoom && p->id != w->zoomed_pane_id) {
              continue;
            }
            float px = prev_x + (zoom ? 0.0f : p->x * prev_w);
            float py = prev_y + (zoom ? 0.0f : p->y * prev_h);
            float pw = zoom ? prev_w : p->w * prev_w;
            float ph = zoom ? prev_h : p->h * prev_h;
            if (pw < 2.0f || ph < 2.0f) {
              continue;
            }
            draw_screen_preview(r, &p->screen, theme, px, py, pw, ph, overlay, &nvo, text,
                                &nvt, cap);
          }
        }
      }

      float meta_y = cy + ol.preview_h;
      float title_x = cx + 10.0f * scale;
      const char *label = "?";
      char meta[96];
      meta[0] = 0;
      if (overview->mode == TRAASH_OVERVIEW_PANES && pane) {
        label = pane->title[0] ? pane->title : (pane->screen.title[0] ? pane->screen.title : "shell");
        if (pane->screen.cwd[0]) {
          snprintf(meta, sizeof(meta), "%s", pane->screen.cwd);
        } else {
          snprintf(meta, sizeof(meta), "pane %d", pane->id);
        }
      } else if (w) {
        label = w->name[0] ? w->name : "tab";
        int pc = traash_window_pane_count(w);
        snprintf(meta, sizeof(meta), "%d pane%s", pc, pc == 1 ? "" : "s");
      }
      draw_text(r, text, &nvt, cap, title_x, meta_y + 4.0f * scale, label, 0xf5f5f5);
      if (meta[0]) {
        draw_text(r, text, &nvt, cap, title_x,
                  meta_y + 4.0f * scale + (float)r->font.cell_h, meta, 0x9a9aa2);
      }
      if (w && w->attention && overview->mode == TRAASH_OVERVIEW_TABS) {
        float d = 8.0f * scale;
        push_round_rect(overlay, &nvo, cap, cx + 8.0f * scale, cy + 8.0f * scale, d, d, d * 0.5f,
                        0xffb454, 1.0f);
      }
      if (pane && traash_pty_has_foreground_process(&pane->pty)) {
        float d = 8.0f * scale;
        push_round_rect(overlay, &nvo, cap, cx + 8.0f * scale, cy + 8.0f * scale, d, d, d * 0.5f,
                        0x33d17a, 1.0f);
      }

      float zx, zy, zw, zh;
      traash_overview_overlay_close_rect(&ol, i, &zx, &zy, &zw, &zh);
      push_round_rect(overlay, &nvo, cap, zx, zy, zw, zh, 4.0f * scale, 0x3a3a44, 0.95f);
      draw_text(r, text, &nvt, cap, zx + 3.0f * scale, zy + (zh - (float)r->font.cell_h) * 0.5f,
                "×", 0xd0d0d8);
    }

    const char *hint = overview->mode == TRAASH_OVERVIEW_PANES
                           ? "Enter focus · × close pane · Esc back"
                           : "Enter open · × close tab · Esc close";
    float fw = measure_text(r, hint);
    draw_text(r, text, &nvt, cap, ol.x + (ol.w - fw) * 0.5f,
              ol.y + ol.h - ol.pad - (float)r->font.cell_h, hint, 0x7a7a82);

    draw_batch(r, overlay, nvo, 0);
    draw_batch(r, text, nvt, 1);
  }

  /* Quit confirm — topmost dialog */
  int qc_open = quit_confirm && quit_confirm->open;
  if (qc_open) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    TraashQuitConfirmGeom qg;
    traash_quit_confirm_geom(r->fb_w, r->fb_h, scale, r->font.cell_h, quit_confirm->count, &qg);

    int nqo = 0;
    int nqt = 0;
    push_quad(overlay, &nqo, cap, 0, 0, (float)r->fb_w, (float)r->fb_h, 0, 0, 0, 0, 0x000000,
              0.55f);
    push_round_rect(overlay, &nqo, cap, qg.x + 4 * scale, qg.y + 8 * scale, qg.w, qg.h, qg.rad,
                    0x000000, 0.25f);
    push_round_rect(overlay, &nqo, cap, qg.x, qg.y, qg.w, qg.h, qg.rad, 0x1e1e24, 0.96f);
    push_quad(overlay, &nqo, cap, qg.x + qg.pad, qg.y + qg.pad + qg.title_h,
              qg.w - qg.pad * 2.0f, 2.0f * scale, 0, 0, 0, 0, 0xe3b341, 0.9f);

    float title_y = qg.y + qg.pad;
    draw_text(r, text, &nqt, cap, qg.x + qg.pad, title_y, "Processes still running", 0xf5f5f5);
    char sub[96];
    snprintf(sub, sizeof(sub), "%d process%s will be killed if you close.",
             quit_confirm->count, quit_confirm->count == 1 ? "" : "es");
    draw_text(r, text, &nqt, cap, qg.x + qg.pad,
              title_y + (float)r->font.cell_h + 6.0f * scale, sub, 0x9a9aa2);

    int start = 0;
    for (int i = 0; i < qg.visible_rows && start + i < quit_confirm->count; i++) {
      const TraashQuitConfirmItem *it = &quit_confirm->items[start + i];
      float ry = qg.content_top + (float)i * qg.row_h;
      push_round_rect(overlay, &nqo, cap, qg.x + qg.pad, ry, qg.w - qg.pad * 2.0f,
                      qg.row_h - 4.0f * scale, 6.0f * scale, 0x2a2a32, 1.0f);
      char line1[96];
      snprintf(line1, sizeof(line1), "Tab %s", it->tab);
      draw_text(r, text, &nqt, cap, qg.x + qg.pad + 12.0f * scale,
                ry + 4.0f * scale, line1, 0x33d17a);
      char line2[192];
      snprintf(line2, sizeof(line2), "%.140s", it->process);
      draw_text(r, text, &nqt, cap, qg.x + qg.pad + 12.0f * scale,
                ry + 4.0f * scale + (float)r->font.cell_h + 2.0f * scale, line2, 0xe8e8ec);
    }

    int cancel_on = quit_confirm->focus == 0;
    int ok_on = quit_confirm->focus == 1;
    push_round_rect(overlay, &nqo, cap, qg.btn_cancel_x, qg.btn_y, qg.btn_w, qg.btn_h,
                    8.0f * scale, cancel_on ? 0x3a3a44 : 0x2a2a32, 1.0f);
    push_round_rect(overlay, &nqo, cap, qg.btn_ok_x, qg.btn_y, qg.btn_w, qg.btn_h,
                    8.0f * scale, ok_on ? 0xc01c28 : 0x6e2128, 1.0f);
    const char *cancel_l = "Cancel";
    const char *ok_l = "Close anyway";
    float cw = measure_text(r, cancel_l);
    float ow = measure_text(r, ok_l);
    float ty = qg.btn_y + (qg.btn_h - (float)r->font.cell_h) * 0.5f;
    draw_text(r, text, &nqt, cap, qg.btn_cancel_x + (qg.btn_w - cw) * 0.5f, ty, cancel_l,
              0xf5f5f5);
    draw_text(r, text, &nqt, cap, qg.btn_ok_x + (qg.btn_w - ow) * 0.5f, ty, ok_l, 0xffffff);

    draw_batch(r, overlay, nqo, 0);
    draw_batch(r, text, nqt, 1);
  }

  if (!fast_resize && r->toast[0] && now < r->toast_until) {
    float scale = r->content_scale > 0.1f ? r->content_scale : 1.0f;
    float pad_x = 14.0f * scale;
    float toast_w = measure_text(r, r->toast) + pad_x * 2.0f;
    float toast_h = (float)r->font.cell_h + 14.0f * scale;
    float toast_x = ((float)r->fb_w - toast_w) * 0.5f;
    float toast_y = (float)r->fb_h - (float)r->status_h - toast_h - 14.0f * scale;
    if (toast_y < (float)r->tab_h + 8.0f * scale) {
      toast_y = (float)r->tab_h + 8.0f * scale;
    }
    int nto = 0;
    int ntt = 0;
    push_round_rect(overlay, &nto, cap, toast_x + 2.0f * scale, toast_y + 3.0f * scale,
                    toast_w, toast_h, 9.0f * scale, 0x000000, 0.35f);
    push_round_rect(overlay, &nto, cap, toast_x, toast_y, toast_w, toast_h, 9.0f * scale,
                    0x24242c, 0.97f);
    draw_text(r, text, &ntt, cap, toast_x + pad_x,
              toast_y + (toast_h - (float)r->font.cell_h) * 0.5f, r->toast, 0xf5f5f5);
    draw_batch(r, overlay, nto, 0);
    draw_batch(r, text, ntt, 1);
  }

  if (r->fb_transparent) {
    traash_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
}
