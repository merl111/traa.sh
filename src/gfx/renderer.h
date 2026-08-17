#ifndef TRAASH_RENDERER_H
#define TRAASH_RENDERER_H

#include "config/theme.h"
#include "config/status.h"
#include "font/font.h"
#include "gfx/atlas.h"
#include "mux/pane.h"
#include "mux/session.h"
#include "ui/context_menu.h"
#include "ui/layout_overlay.h"
#include "ui/overview_overlay.h"
#include "ui/quit_confirm.h"
#include "ui/shortcuts_overlay.h"
#include "input/keymap.h"
#include "term/screen.h"

typedef struct {
  GLuint prog;
  GLuint vao;
  GLuint vbo;
  TraashAtlas atlas;
  TraashFont font;
  int fb_w, fb_h;
  int status_h;
  int tab_h;
  float content_scale;
  float base_font_px;
  char font_family[128];
  int cursor_style; /* 0 block, 1 beam, 2 underline */
  float opacity;    /* 0.3..1 window content alpha */
  int fb_transparent; /* GLFW transparent framebuffer active */
  char toast[96];
  double toast_until;
} TraashRenderer;

typedef struct {
  TraashPane *pane;
  int cell_x;
  int cell_y;
  float pane_x, pane_y, pane_w, pane_h;
} TraashHit;

typedef enum {
  TRAASH_TAB_HIT_NONE = 0,
  TRAASH_TAB_HIT_TAB,
  TRAASH_TAB_HIT_CLOSE,
  TRAASH_TAB_HIT_NEW
} TraashTabHitKind;

typedef struct {
  TraashTabHitKind kind;
  TraashWindow *window;
} TraashTabHit;

/* Optional find-in-pane highlight overlay for the active search. */
typedef struct {
  int active;
  const TraashPane *pane;
  const TraashSearchMatch *matches;
  int match_count;
  int current; /* index into matches, or -1 */
  int match_len;
} TraashSearchDraw;

int traash_renderer_init(TraashRenderer *r, const char *font, int font_px);
void traash_renderer_free(TraashRenderer *r);
void traash_renderer_resize(TraashRenderer *r, int fb_w, int fb_h);
int traash_renderer_set_content_scale(TraashRenderer *r, float scale);
void traash_renderer_show_toast(TraashRenderer *r, const char *text, double until);
int traash_renderer_hit_test(const TraashRenderer *r, TraashSession *session, float mx,
                             float my, TraashHit *out);
int traash_renderer_tab_hit(const TraashRenderer *r, TraashSession *session, float mx,
                            float my, TraashTabHit *out);
void traash_renderer_draw(TraashRenderer *r, TraashSession *session, const TraashTheme *theme,
                          const TraashStatusModel *status, double now,
                          const TraashContextMenu *menu,
                          const TraashShortcutsOverlay *shortcuts,
                          const TraashLayoutOverlay *layouts, const TraashOverviewOverlay *overview,
                          const TraashQuitConfirm *quit_confirm, const TraashKeymap *keymap,
                          const TraashSearchDraw *search);
/* Like draw, but skips per-cell glyph work (for live window drags). */
void traash_renderer_draw_ex(TraashRenderer *r, TraashSession *session, const TraashTheme *theme,
                             const TraashStatusModel *status, double now,
                             const TraashContextMenu *menu,
                             const TraashShortcutsOverlay *shortcuts,
                             const TraashLayoutOverlay *layouts,
                             const TraashOverviewOverlay *overview,
                             const TraashQuitConfirm *quit_confirm, const TraashKeymap *keymap,
                             const TraashSearchDraw *search, int fast_resize);

#endif
