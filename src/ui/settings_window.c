#include "ui/settings_window.h"

#include "config/theme.h"
#include "input/actions.h"
#include "input/shortcut_catalog.h"
#include "mux/layout_store.h"
#include "util/log.h"

#include "glad/gl.h"

#include <fontconfig/fontconfig.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear/nuklear.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "nuklear/nuklear_glfw_gl3.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* nk_glfw3_init steals the GLFW user pointer for its own struct; keep ours here. */
static TraashSettingsWindow *g_settings_sw;

/* Adwaita-inspired dark palette */
static const struct nk_color COL_BG = {26, 26, 30, 255};
static const struct nk_color COL_HEADER = {30, 30, 34, 255};
static const struct nk_color COL_CARD = {42, 42, 46, 255};
static const struct nk_color COL_CARD_HOVER = {50, 50, 55, 255};
static const struct nk_color COL_SEP = {55, 55, 60, 255};
static const struct nk_color COL_TEXT = {255, 255, 255, 255};
static const struct nk_color COL_DIM = {154, 154, 154, 255};
static const struct nk_color COL_ACCENT = {51, 209, 122, 255};
static const struct nk_color COL_BTN = {58, 58, 64, 255};
static const struct nk_color COL_BTN_HOVER = {72, 72, 80, 255};
static const struct nk_color COL_BTN_ACTIVE = {45, 140, 90, 255};
static const struct nk_color COL_INPUT = {56, 56, 62, 255};
static const struct nk_color COL_BORDER = {96, 96, 104, 255};

static int str_contains_ci(const char *hay, const char *needle) {
  if (!needle || !needle[0]) {
    return 1;
  }
  if (!hay || !hay[0]) {
    return 0;
  }
  for (const char *h0 = hay; *h0; h0++) {
    const char *h = h0;
    const char *n = needle;
    while (*h && *n &&
           tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
      h++;
      n++;
    }
    if (!*n) {
      return 1;
    }
  }
  return 0;
}

static int shortcut_matches(const TraashKeymap *km, TraashAction a, const char *filter) {
  if (!filter || !filter[0]) {
    return 1;
  }
  if (str_contains_ci(traash_action_label(a), filter) ||
      str_contains_ci(traash_action_name(a), filter)) {
    return 1;
  }
  TraashKeyBind b;
  if (traash_keymap_find_action(km, a, &b)) {
    char chord[64];
    traash_keymap_format(km, &b, chord, sizeof(chord));
    if (str_contains_ci(chord, filter)) {
      return 1;
    }
  }
  return 0;
}

static int count_matching_in_group(const TraashKeymap *km, const TraashShortcutGroup *g,
                                   const char *filter) {
  int n = 0;
  for (int i = 0; i < g->count; i++) {
    if (shortcut_matches(km, g->actions[i], filter)) {
      n++;
    }
  }
  return n;
}

static char *fc_resolve_ui_font(const char *pattern) {
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
  /* Prefer static TTF over variable OTF when possible */
  char *out = strdup((const char *)file);
  FcPatternDestroy(match);
  return out;
}

static const char *pick_ui_font_path(void) {
  static char cached[512];
  static int ready = 0;
  if (ready) {
    return cached[0] ? cached : NULL;
  }
  ready = 1;
  cached[0] = 0;
  /* Prefer fonts stb_truetype handles reliably (avoid VF / unusual CFF). */
  const char *paths[] = {"/usr/share/fonts/TTF/DejaVuSans.ttf",
                         "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                         "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
                         NULL};
  for (int i = 0; paths[i]; i++) {
    if (access(paths[i], R_OK) == 0) {
      snprintf(cached, sizeof(cached), "%s", paths[i]);
      TRAASH_LOGI("settings UI font: %s", cached);
      return cached;
    }
  }
  const char *candidates[] = {"DejaVu Sans:style=Book", "Noto Sans:style=Regular",
                              "Adwaita Sans:style=Regular", "Sans", NULL};
  for (int i = 0; candidates[i]; i++) {
    char *p = fc_resolve_ui_font(candidates[i]);
    if (p && access(p, R_OK) == 0) {
      snprintf(cached, sizeof(cached), "%s", p);
      free(p);
      TRAASH_LOGI("settings UI font: %s", cached);
      return cached;
    }
    free(p);
  }
  return NULL;
}

static void apply_adwaita_style(struct nk_context *ctx) {
  struct nk_color table[NK_COLOR_COUNT];
  table[NK_COLOR_TEXT] = COL_TEXT;
  table[NK_COLOR_WINDOW] = COL_BG;
  table[NK_COLOR_HEADER] = COL_HEADER;
  table[NK_COLOR_BORDER] = COL_BORDER;
  table[NK_COLOR_BUTTON] = COL_BTN;
  table[NK_COLOR_BUTTON_HOVER] = COL_BTN_HOVER;
  table[NK_COLOR_BUTTON_ACTIVE] = COL_BTN_ACTIVE;
  table[NK_COLOR_TOGGLE] = COL_BTN;
  table[NK_COLOR_TOGGLE_HOVER] = COL_BTN_HOVER;
  table[NK_COLOR_TOGGLE_CURSOR] = COL_ACCENT;
  table[NK_COLOR_SELECT] = COL_CARD_HOVER;
  table[NK_COLOR_SELECT_ACTIVE] = COL_ACCENT;
  table[NK_COLOR_SLIDER] = COL_INPUT;
  table[NK_COLOR_SLIDER_CURSOR] = COL_ACCENT;
  table[NK_COLOR_SLIDER_CURSOR_HOVER] = COL_ACCENT;
  table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = COL_ACCENT;
  table[NK_COLOR_PROPERTY] = COL_INPUT;
  table[NK_COLOR_EDIT] = COL_INPUT;
  table[NK_COLOR_EDIT_CURSOR] = COL_ACCENT;
  table[NK_COLOR_COMBO] = COL_INPUT;
  table[NK_COLOR_CHART] = COL_CARD;
  table[NK_COLOR_CHART_COLOR] = COL_ACCENT;
  table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = COL_TEXT;
  table[NK_COLOR_SCROLLBAR] = COL_CARD;
  table[NK_COLOR_SCROLLBAR_CURSOR] = COL_BTN_HOVER;
  table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = COL_DIM;
  table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = COL_ACCENT;
  table[NK_COLOR_TAB_HEADER] = COL_HEADER;
  nk_style_from_table(ctx, table);

  ctx->style.window.rounding = 0.0f;
  ctx->style.window.padding = nk_vec2(20, 16);
  ctx->style.window.spacing = nk_vec2(10, 8);
  ctx->style.window.group_padding = nk_vec2(14, 10);
  ctx->style.window.group_border = 0;
  ctx->style.window.border = 0;
  ctx->style.window.scrollbar_size = nk_vec2(10, 10);
  ctx->style.window.min_row_height_padding = 6;

  ctx->style.button.rounding = 8.0f;
  ctx->style.button.padding = nk_vec2(14, 8);
  ctx->style.button.border = 0;
  ctx->style.button.text_normal = COL_TEXT;
  ctx->style.button.text_hover = COL_TEXT;
  ctx->style.button.text_active = COL_TEXT;

  ctx->style.combo.rounding = 8.0f;
  ctx->style.combo.border = 1.0f;
  ctx->style.combo.content_padding = nk_vec2(12, 8);
  /* button size = row_h - 2*button_padding.y; keep the triangle small */
  ctx->style.combo.button_padding = nk_vec2(8, 12);
  ctx->style.combo.sym_normal = NK_SYMBOL_CHEVRON_DOWN;
  ctx->style.combo.sym_hover = NK_SYMBOL_CHEVRON_DOWN;
  ctx->style.combo.sym_active = NK_SYMBOL_CHEVRON_DOWN;
  ctx->style.combo.border_color = COL_BORDER;
  ctx->style.combo.label_normal = COL_TEXT;
  ctx->style.combo.label_hover = COL_TEXT;
  ctx->style.combo.label_active = COL_TEXT;
  ctx->style.combo.button.rounding = 6.0f;
  ctx->style.combo.button.padding = nk_vec2(3, 3);

  ctx->style.edit.rounding = 8.0f;
  ctx->style.edit.padding = nk_vec2(10, 8);
  ctx->style.edit.border = 1.0f;
  ctx->style.edit.border_color = COL_BORDER;
  ctx->style.edit.text_normal = COL_TEXT;
  ctx->style.edit.text_hover = COL_TEXT;
  ctx->style.edit.text_active = COL_TEXT;

  ctx->style.property.rounding = 8.0f;
  ctx->style.property.border = 1.0f;
  ctx->style.property.padding = nk_vec2(6, 6);
  ctx->style.property.border_color = COL_BORDER;
  ctx->style.property.label_normal = COL_DIM;
  ctx->style.property.label_hover = COL_TEXT;
  ctx->style.property.label_active = COL_TEXT;

  ctx->style.tab.background = nk_style_item_color(COL_HEADER);
  ctx->style.tab.border = 0;
  ctx->style.tab.rounding = 8.0f;
  ctx->style.tab.padding = nk_vec2(12, 8);
  ctx->style.tab.spacing = nk_vec2(6, 6);

  ctx->style.option.normal = nk_style_item_color(COL_BTN);
  ctx->style.option.hover = nk_style_item_color(COL_BTN_HOVER);
  ctx->style.option.active = nk_style_item_color(COL_ACCENT);
  ctx->style.option.border_color = COL_BORDER;
  ctx->style.option.cursor_normal = nk_style_item_color(COL_TEXT);
  ctx->style.option.cursor_hover = nk_style_item_color(COL_TEXT);
  ctx->style.option.text_normal = COL_TEXT;
  ctx->style.option.text_hover = COL_TEXT;
  ctx->style.option.text_active = COL_TEXT;
  ctx->style.option.padding = nk_vec2(4, 4);
  ctx->style.option.spacing = 8.0f;

  ctx->style.selectable.normal = nk_style_item_color(COL_CARD);
  ctx->style.selectable.hover = nk_style_item_color(COL_CARD_HOVER);
  ctx->style.selectable.pressed = nk_style_item_color(COL_ACCENT);
  ctx->style.selectable.normal_active = nk_style_item_color(COL_BTN_ACTIVE);
  ctx->style.selectable.hover_active = nk_style_item_color(COL_ACCENT);
  ctx->style.selectable.pressed_active = nk_style_item_color(COL_ACCENT);
  ctx->style.selectable.rounding = 8.0f;
  ctx->style.selectable.padding = nk_vec2(12, 8);
  ctx->style.selectable.text_normal = COL_DIM;
  ctx->style.selectable.text_hover = COL_TEXT;
  ctx->style.selectable.text_pressed = COL_TEXT;
  ctx->style.selectable.text_normal_active = COL_TEXT;
  ctx->style.selectable.text_hover_active = COL_TEXT;
  ctx->style.selectable.text_pressed_active = COL_TEXT;

  ctx->style.text.color = COL_TEXT;
}

static void set_font(struct nk_context *ctx, void *font) {
  if (font) {
    nk_style_set_font(ctx, &((struct nk_font *)font)->handle);
  }
}

static void section_header(struct nk_context *ctx, void *font_section, const char *title) {
  set_font(ctx, font_section);
  nk_layout_row_dynamic(ctx, 22, 1);
  nk_label_colored(ctx, title, NK_TEXT_LEFT, COL_TEXT);
}

static float g_card_saved_rounding;

/* Height for a card containing `rows` of `row_h`, optionally with 1px separators between. */
static float card_height_for(struct nk_context *ctx, int rows, float row_h, int with_seps) {
  if (rows < 1) {
    rows = 1;
  }
  float pad = ctx->style.window.group_padding.y * 2.0f;
  float space = ctx->style.window.spacing.y;
  int layouts = with_seps ? (rows * 2 - 1) : rows;
  float h = pad;
  h += (float)rows * row_h;
  if (with_seps && rows > 1) {
    h += (float)(rows - 1) * 1.0f; /* row_sep layout height */
  }
  if (layouts > 1) {
    h += (float)(layouts - 1) * space;
  }
  return h + 8.0f; /* fudge so the last row never clips */
}

static int card_begin(struct nk_context *ctx, const char *id, float height) {
  struct nk_style_item bg = ctx->style.window.fixed_background;
  g_card_saved_rounding = ctx->style.window.rounding;
  ctx->style.window.fixed_background = nk_style_item_color(COL_CARD);
  ctx->style.window.group_border_color = COL_CARD;
  ctx->style.window.rounding = 12.0f;
  nk_layout_row_dynamic(ctx, height, 1);
  int ok = nk_group_begin(ctx, id, NK_WINDOW_NO_SCROLLBAR);
  ctx->style.window.fixed_background = bg;
  return ok;
}

static void card_end(struct nk_context *ctx) {
  nk_group_end(ctx);
  ctx->style.window.rounding = g_card_saved_rounding;
}

static void row_sep(struct nk_context *ctx) {
  struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
  struct nk_rect space;
  nk_layout_row_dynamic(ctx, 1, 1);
  if (nk_widget(&space, ctx) == NK_WIDGET_INVALID) {
    return;
  }
  space.x += 4;
  space.w -= 8;
  space.h = 1;
  nk_fill_rect(canvas, space, 0, COL_SEP);
}

static void setting_row_begin(struct nk_context *ctx, float row_h) {
  /* Template rows work inside groups; dynamic ratio rows often get 0-width columns. */
  nk_layout_row_template_begin(ctx, row_h);
  nk_layout_row_template_push_static(ctx, 168);
  nk_layout_row_template_push_dynamic(ctx);
  nk_layout_row_template_end(ctx);
}

static void setting_row_begin_wide_label(struct nk_context *ctx, float row_h) {
  nk_layout_row_template_begin(ctx, row_h);
  nk_layout_row_template_push_static(ctx, 220);
  nk_layout_row_template_push_dynamic(ctx);
  nk_layout_row_template_end(ctx);
}

static void draw_shortcut_row(struct nk_context *ctx, TraashSettingsWindow *sw, TraashKeymap *km,
                              TraashAction a, int *first_in_card) {
  char chord[64];
  TraashKeyBind b;
  int active = sw->capturing && sw->capture_action == a;
  if (active && sw->capture_leader_armed) {
    snprintf(chord, sizeof(chord), "Leader+…");
  } else if (active) {
    snprintf(chord, sizeof(chord), sw->capture_as_prefix ? "Prefix+key…" : "Press key…");
  } else if (traash_keymap_find_action(km, a, &b)) {
    traash_keymap_format(km, &b, chord, sizeof(chord));
  } else {
    snprintf(chord, sizeof(chord), "Unbound");
  }
  if (!*first_in_card) {
    row_sep(ctx);
  }
  *first_in_card = 0;
  setting_row_begin_wide_label(ctx, 36);
  nk_label_colored(ctx, traash_action_label(a), NK_TEXT_LEFT, COL_TEXT);
  if (nk_button_label(ctx, chord)) {
    sw->capturing = 1;
    sw->capturing_leader = 0;
    sw->capture_leader_armed = 0;
    sw->capture_action = a;
    sw->pending_capture_key = 0;
    sw->theme_dirty = 0;
    sw->capture_as_prefix = 0;
    if (traash_keymap_find_action(km, a, &b)) {
      sw->capture_as_prefix = b.prefix ? 1 : 0;
    }
    snprintf(sw->status_msg, sizeof(sw->status_msg), "Capturing for: %s",
             traash_action_label(a));
    glfwFocusWindow(sw->win);
  }
}

static void close_cb(GLFWwindow *w) {
  (void)w;
  if (g_settings_sw) {
    g_settings_sw->close_pending = 1;
  }
}

static int mods_from_glfw(int mods) {
  mods &= (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER);
  int m = 0;
  if (mods & GLFW_MOD_CONTROL) {
    m |= 1;
  }
  if (mods & GLFW_MOD_SHIFT) {
    m |= 2;
  }
  if (mods & GLFW_MOD_ALT) {
    m |= 4;
  }
  if (mods & GLFW_MOD_SUPER) {
    m |= 8;
  }
  return m;
}

static void settings_key_cb(GLFWwindow *w, int key, int scancode, int action, int mods) {
  TraashSettingsWindow *sw = g_settings_sw;
  struct nk_glfw *nk = sw ? (struct nk_glfw *)sw->nk : NULL;
  /* User pointer is the nk_glfw* installed by nk_glfw3_init — keep that for NK. */
  if (nk) {
    nk_glfw3_key_callback(w, key, scancode, action, mods);
  }
  if (!sw || action != GLFW_PRESS) {
    return;
  }
  if (!sw->capturing && !sw->capturing_leader) {
    return;
  }
  if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL ||
      key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT ||
      key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT ||
      key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER ||
      key == GLFW_KEY_CAPS_LOCK || key == GLFW_KEY_NUM_LOCK) {
    return;
  }
  if (key == GLFW_KEY_ESCAPE) {
    sw->capturing = 0;
    sw->capturing_leader = 0;
    sw->capture_leader_armed = 0;
    sw->pending_capture_key = 0;
    sw->theme_dirty = 0;
    snprintf(sw->status_msg, sizeof(sw->status_msg), "Capture cancelled");
    return;
  }
  sw->pending_capture_key = key;
  sw->pending_capture_mods = mods_from_glfw(mods);
  sw->theme_dirty = 2;
}

static struct nk_font *add_ui_font(struct nk_font_atlas *atlas, const char *path, float height) {
  if (!path || !atlas) {
    return NULL;
  }
  struct nk_font_config cfg = nk_font_config(height);
  /* Supersample the UI atlas, then keep glyph origins on whole pixels. */
  cfg.oversample_h = 2;
  cfg.oversample_v = 2;
  cfg.pixel_snap = 1;
  cfg.range = nk_font_default_glyph_ranges();
  struct nk_font *font = nk_font_atlas_add_from_file(atlas, path, height, &cfg);
  if (!font) {
    TRAASH_LOGW("settings font load failed: %s", path);
  }
  return font;
}

int traash_settings_window_open(TraashSettingsWindow *sw, GLFWwindow *share, TraashConfig *cfg,
                                void *lua) {
  if (!sw) {
    return -1;
  }
  memset(sw, 0, sizeof(*sw));
  sw->share = share;
  g_settings_sw = sw;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  /* Own GL context — sharing with the terminal can leave the font atlas unbound/blank */
  const int prefs_w = 780;
  const int prefs_h = 820;
  sw->win = glfwCreateWindow(prefs_w, prefs_h, "Preferences", NULL, NULL);
  if (!sw->win) {
    TRAASH_LOGE("failed to create settings window");
    g_settings_sw = NULL;
    return -1;
  }
  glfwSetWindowSizeLimits(sw->win, 640, 560, GLFW_DONT_CARE, GLFW_DONT_CARE);
  if (share) {
    int mx = 0, my = 0, mw = 0, mh = 0;
    glfwGetWindowPos(share, &mx, &my);
    glfwGetWindowSize(share, &mw, &mh);
    int px = mx + (mw - prefs_w) / 2;
    int py = my + (mh - prefs_h) / 2;
    if (px < 32) {
      px = 32;
    }
    if (py < 32) {
      py = 32;
    }
    glfwSetWindowPos(sw->win, px, py);
  }
  glfwSetWindowCloseCallback(sw->win, close_cb);

  glfwMakeContextCurrent(sw->win);
  if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
    TRAASH_LOGE("gladLoadGL failed for settings window");
    glfwDestroyWindow(sw->win);
    sw->win = NULL;
    g_settings_sw = NULL;
    if (share) {
      glfwMakeContextCurrent(share);
    }
    return -1;
  }
  glEnable(GL_MULTISAMPLE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  struct nk_glfw *nk = calloc(1, sizeof(*nk));
  if (!nk) {
    glfwDestroyWindow(sw->win);
    sw->win = NULL;
    g_settings_sw = NULL;
    if (share) {
      glfwMakeContextCurrent(share);
    }
    return -1;
  }
  sw->nk = nk;
  /* Sets GLFW user pointer to nk — required by nk_glfw3_* callbacks */
  nk_glfw3_init(nk, sw->win, NK_GLFW3_INSTALL_CALLBACKS);
  /* Replace key callback; keep user pointer as nk for NK's own handlers */
  glfwSetKeyCallback(sw->win, settings_key_cb);
  glfwSetCharCallback(sw->win, nk_glfw3_char_callback);
  glfwFocusWindow(sw->win);

  struct nk_font_atlas *atlas = NULL;
  nk_glfw3_font_stash_begin(nk, &atlas);
  /* Always bake Proggy first so UI never ends up with a null font */
  struct nk_font *font_fallback = nk_font_atlas_add_default(atlas, 15.0f, 0);
  const char *regular = pick_ui_font_path();
  struct nk_font *font_ui = add_ui_font(atlas, regular, 15.0f);
  struct nk_font *font_title = add_ui_font(atlas, regular, 18.0f);
  if (!font_ui) {
    font_ui = font_fallback;
  }
  if (!font_title) {
    font_title = font_ui;
  }
  nk_glfw3_font_stash_end(nk);
  if (font_ui) {
    nk_style_set_font(&nk->ctx, &font_ui->handle);
  }
  sw->font_ui = font_ui;
  sw->font_title = font_title;
  sw->font_section = font_ui;
  sw->font_dim = font_ui;
  apply_adwaita_style(&nk->ctx);
  nk_style_set_font(&nk->ctx, &font_ui->handle);

  const char *theme_name =
      (cfg->theme[0] ? cfg->theme : "tokyo-night");
  traash_theme_load(lua, theme_name, &sw->preview);

  sw->font_family_count =
      traash_font_list_monospace(sw->font_families, TRAASH_FONT_MAX_FAMILIES);
  if (cfg->font_family[0]) {
    int found = 0;
    for (int i = 0; i < sw->font_family_count; i++) {
      if (strcasecmp(sw->font_families[i], cfg->font_family) == 0) {
        found = 1;
        break;
      }
    }
    if (!found && sw->font_family_count < TRAASH_FONT_MAX_FAMILIES) {
      snprintf(sw->font_families[sw->font_family_count],
               sizeof(sw->font_families[0]), "%s", cfg->font_family);
      sw->font_family_count++;
    }
  }

  snprintf(sw->status_msg, sizeof(sw->status_msg),
           "Edit settings, then Apply or Save");
  sw->open = 1;
  sw->tab = 0;

  if (share) {
    glfwMakeContextCurrent(share);
  }
  return 0;
}

void traash_settings_window_close(TraashSettingsWindow *sw) {
  if (!sw) {
    return;
  }
  if (sw->win) {
    glfwMakeContextCurrent(sw->win);
  }
  if (sw->nk) {
    nk_glfw3_shutdown((struct nk_glfw *)sw->nk);
    free(sw->nk);
    sw->nk = NULL;
  }
  if (sw->win) {
    glfwDestroyWindow(sw->win);
    sw->win = NULL;
  }
  if (sw->share) {
    glfwMakeContextCurrent(sw->share);
  }
  memset(sw, 0, sizeof(*sw));
  g_settings_sw = NULL;
}

int traash_settings_window_is_open(const TraashSettingsWindow *sw) {
  return sw && sw->open && sw->win;
}

static int theme_index(const char **themes, int n, const char *name) {
  for (int i = 0; i < n; i++) {
    if (strcmp(themes[i], name) == 0) {
      return i;
    }
  }
  return 0;
}

static int tab_button(struct nk_context *ctx, const char *label, int active) {
  struct nk_style_button backup = ctx->style.button;
  if (active) {
    ctx->style.button.normal = nk_style_item_color(COL_ACCENT);
    ctx->style.button.hover = nk_style_item_color(COL_ACCENT);
    ctx->style.button.active = nk_style_item_color(COL_BTN_ACTIVE);
    ctx->style.button.text_normal = nk_rgb(20, 30, 24);
    ctx->style.button.text_hover = nk_rgb(20, 30, 24);
    ctx->style.button.text_active = nk_rgb(10, 20, 14);
  } else {
    ctx->style.button.normal = nk_style_item_color(COL_CARD);
    ctx->style.button.hover = nk_style_item_color(COL_CARD_HOVER);
    ctx->style.button.active = nk_style_item_color(COL_BTN);
  }
  ctx->style.button.rounding = 10.0f;
  int clicked = nk_button_label(ctx, label);
  ctx->style.button = backup;
  return clicked;
}

static int action_button(struct nk_context *ctx, const char *label, int primary) {
  struct nk_style_button backup = ctx->style.button;
  if (primary) {
    ctx->style.button.normal = nk_style_item_color(COL_ACCENT);
    ctx->style.button.hover = nk_style_item_color(nk_rgb(70, 220, 140));
    ctx->style.button.active = nk_style_item_color(COL_BTN_ACTIVE);
    ctx->style.button.text_normal = nk_rgb(20, 30, 24);
    ctx->style.button.text_hover = nk_rgb(20, 30, 24);
    ctx->style.button.text_active = nk_rgb(10, 20, 14);
  }
  ctx->style.button.rounding = 10.0f;
  int clicked = nk_button_label(ctx, label);
  ctx->style.button = backup;
  return clicked;
}

int traash_settings_window_frame(TraashSettingsWindow *sw, GLFWwindow *main, TraashConfig *cfg,
                                 TraashKeymap *km, void *lua, const char **themes,
                                 int theme_count, const char **statuses, int status_count) {
  if (!traash_settings_window_is_open(sw)) {
    return 0;
  }
  if (glfwWindowShouldClose(sw->win) || sw->close_pending) {
    traash_settings_window_close(sw);
    if (main) {
      glfwMakeContextCurrent(main);
    }
    return 0;
  }

  if (sw->theme_dirty == 2 && sw->pending_capture_key &&
      (sw->capturing || sw->capturing_leader)) {
    int key = sw->pending_capture_key;
    int mods = sw->pending_capture_mods;
    char chord[64];
    if (sw->capturing_leader) {
      traash_keymap_normalize_key(&key, &mods);
      traash_keymap_set_leader(km, key, mods);
      traash_keymap_format_leader(km, chord, sizeof(chord));
      snprintf(sw->status_msg, sizeof(sw->status_msg), "Leader set to %s", chord);
      sw->capturing_leader = 0;
      sw->theme_dirty = 0;
      sw->pending_capture_key = 0;
      sw->pending_capture_mods = 0;
    } else {
      /* Pressing the leader while capturing arms a prefix chord (tmux-style). */
      int is_leader =
          (key == km->prefix_key &&
           (mods == km->prefix_mods ||
            ((mods & km->prefix_mods) == km->prefix_mods &&
             (mods & ~km->prefix_mods) == 0)));
      if (is_leader && !sw->capture_leader_armed) {
        sw->capture_leader_armed = 1;
        sw->capture_as_prefix = 1;
        sw->pending_capture_key = 0;
        sw->pending_capture_mods = 0;
        sw->theme_dirty = 0;
        snprintf(sw->status_msg, sizeof(sw->status_msg),
                 "Leader armed — press the follow-up key (Esc cancels)");
      } else {
        int prefix = sw->capture_as_prefix || sw->capture_leader_armed;
        /* Alt forces after-leader; Shift+Alt forces a direct (non-prefix) bind */
        if ((mods & 4) && (mods & 2)) {
          prefix = 0;
          mods &= ~6;
        } else if (mods & 4) {
          prefix = 1;
          mods &= ~4;
        }
        if (sw->capture_leader_armed) {
          /* Drop sticky leader modifiers held through the chord (e.g. Ctrl still down) */
          mods &= ~km->prefix_mods;
          prefix = 1;
        }
        traash_keymap_normalize_key(&key, &mods);
        traash_keymap_rebind(km, sw->capture_action, key, mods, prefix);
        traash_keymap_format_key(km, key, mods, prefix, chord, sizeof(chord));
        snprintf(sw->status_msg, sizeof(sw->status_msg), "Bound %s → %s",
                 traash_action_label(sw->capture_action), chord);
        sw->capturing = 0;
        sw->capture_leader_armed = 0;
        sw->theme_dirty = 0;
        sw->pending_capture_key = 0;
        sw->pending_capture_mods = 0;
      }
    }
  }

  int win_w = 680, win_h = 640;
  glfwGetWindowSize(sw->win, &win_w, &win_h);

  glfwMakeContextCurrent(sw->win);
  struct nk_glfw *nk = (struct nk_glfw *)sw->nk;
  nk_glfw3_new_frame(nk);
  struct nk_context *ctx = &nk->ctx;
  apply_adwaita_style(ctx);
  set_font(ctx, sw->font_ui);
  if (sw->font_ui) {
    nk_style_set_font(ctx, &((struct nk_font *)sw->font_ui)->handle);
  }

  if (nk_begin(ctx, "Preferences", nk_rect(0, 0, (float)win_w, (float)win_h),
               NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
    /* Tabs only — window manager already shows the title */
    set_font(ctx, sw->font_ui);
    nk_layout_row_dynamic(ctx, 36, 3);
    if (tab_button(ctx, "Appearance", sw->tab == 0)) {
      sw->tab = 0;
    }
    if (tab_button(ctx, "Terminal", sw->tab == 1)) {
      sw->tab = 1;
    }
    if (tab_button(ctx, "Shortcuts", sw->tab == 2)) {
      sw->tab = 2;
    }

    nk_layout_row_dynamic(ctx, 10, 1);
    nk_spacing(ctx, 1);

    /*
     * Reserve footer so Apply/Save/Close are never clipped.
     * Nuklear also subtracts window.padding and inserts spacing between rows.
     */
    const float footer_reserve = 120.0f; /* status + gap + buttons + bottom pad + spacings */
    const float header_used = 36.0f + 10.0f + ctx->style.window.spacing.y * 2.0f;
    float usable =
        (float)win_h - ctx->style.window.padding.y * 2.0f - header_used - footer_reserve;
    float content_h = usable;
    if (content_h < 180.0f) {
      content_h = 180.0f;
    }
    nk_layout_row_dynamic(ctx, content_h, 1);
    if (nk_group_begin(ctx, "prefs_body", 0)) {
      if (sw->tab == 0) {
        section_header(ctx, sw->font_section, "Look");
        if (card_begin(ctx, "card_look", card_height_for(ctx, 4, 38, 1))) {
          set_font(ctx, sw->font_ui);
          setting_row_begin(ctx, 38);
          nk_label_colored(ctx, "Theme", NK_TEXT_LEFT, COL_TEXT);
          {
            int ti = theme_index(themes, theme_count, cfg->theme);
            const char *cur = (theme_count > 0) ? themes[ti] : "(none)";
            if (nk_combo_begin_label(ctx, cur, nk_vec2(280, 240))) {
              nk_layout_row_dynamic(ctx, 28, 1);
              for (int i = 0; i < theme_count; i++) {
                if (nk_combo_item_label(ctx, themes[i], NK_TEXT_LEFT)) {
                  snprintf(cfg->theme, sizeof(cfg->theme), "%s", themes[i]);
                  traash_theme_load(lua, cfg->theme, &sw->preview);
                  sw->theme_dirty = 1;
                  sw->apply_pending = 1;
                }
              }
              nk_combo_end(ctx);
            }
          }
          row_sep(ctx);
          setting_row_begin(ctx, 38);
          nk_label_colored(ctx, "Status bar", NK_TEXT_LEFT, COL_TEXT);
          {
            int si = theme_index(statuses, status_count, cfg->status_bar);
            const char *cur = (status_count > 0) ? statuses[si] : "(none)";
            if (nk_combo_begin_label(ctx, cur, nk_vec2(280, 200))) {
              nk_layout_row_dynamic(ctx, 28, 1);
              for (int i = 0; i < status_count; i++) {
                if (nk_combo_item_label(ctx, statuses[i], NK_TEXT_LEFT)) {
                  snprintf(cfg->status_bar, sizeof(cfg->status_bar), "%s", statuses[i]);
                  sw->apply_pending = 1;
                }
              }
              nk_combo_end(ctx);
            }
          }
          row_sep(ctx);
          setting_row_begin(ctx, 38);
          nk_label_colored(ctx, "Cursor", NK_TEXT_LEFT, COL_TEXT);
          {
            static const char *cursors[] = {"block", "beam", "underline"};
            int ci = cfg->cursor_style % 3;
            if (nk_combo_begin_label(ctx, cursors[ci], nk_vec2(220, 140))) {
              nk_layout_row_dynamic(ctx, 28, 1);
              for (int i = 0; i < 3; i++) {
                if (nk_combo_item_label(ctx, cursors[i], NK_TEXT_LEFT)) {
                  cfg->cursor_style = i;
                  sw->apply_pending = 1;
                }
              }
              nk_combo_end(ctx);
            }
          }
          row_sep(ctx);
          setting_row_begin(ctx, 38);
          nk_label_colored(ctx, "Default layout", NK_TEXT_LEFT, COL_TEXT);
          {
            TraashLayoutName layouts[TRAASH_LAYOUT_MAX_NAMES];
            int n = traash_layout_store_scan(layouts, TRAASH_LAYOUT_MAX_NAMES);
            const char *cur =
                cfg->default_layout[0] ? cfg->default_layout : "(none)";
            if (nk_combo_begin_label(ctx, cur, nk_vec2(280, 220))) {
              nk_layout_row_dynamic(ctx, 28, 1);
              if (nk_combo_item_label(ctx, "(none)", NK_TEXT_LEFT)) {
                cfg->default_layout[0] = 0;
                sw->apply_pending = 1;
              }
              for (int i = 0; i < n; i++) {
                if (nk_combo_item_label(ctx, layouts[i].name, NK_TEXT_LEFT)) {
                  snprintf(cfg->default_layout, sizeof(cfg->default_layout), "%s",
                           layouts[i].name);
                  sw->apply_pending = 1;
                }
              }
              nk_combo_end(ctx);
            }
          }
          card_end(ctx);
        }

        section_header(ctx, sw->font_section, "Font");
        if (card_begin(ctx, "card_font", card_height_for(ctx, 2, 38, 1))) {
          set_font(ctx, sw->font_ui);
          setting_row_begin(ctx, 38);
          nk_label_colored(ctx, "Family", NK_TEXT_LEFT, COL_TEXT);
          {
            const char *cur =
                cfg->font_family[0] ? cfg->font_family : "monospace";
            float popup_h = 28.0f * 12.0f + 16.0f;
            if (popup_h > 360.0f) {
              popup_h = 360.0f;
            }
            if (nk_combo_begin_label(ctx, cur, nk_vec2(320, popup_h))) {
              nk_layout_row_dynamic(ctx, 28, 1);
              for (int i = 0; i < sw->font_family_count; i++) {
                if (nk_combo_item_label(ctx, sw->font_families[i], NK_TEXT_LEFT)) {
                  snprintf(cfg->font_family, sizeof(cfg->font_family), "%s",
                           sw->font_families[i]);
                  sw->apply_pending = 1;
                }
              }
              nk_combo_end(ctx);
            }
          }
          row_sep(ctx);
          setting_row_begin(ctx, 38);
          nk_label_colored(ctx, "Size", NK_TEXT_LEFT, COL_TEXT);
          {
            float prev = cfg->font_size;
            /* ## hides label; single # leaves "fs" visible after hash strip */
            nk_property_float(ctx, "##fs", 8, &cfg->font_size, 48, 1, 0.5f);
            if (cfg->font_size != prev) {
              sw->apply_pending = 1;
            }
          }
          card_end(ctx);
        }

        section_header(ctx, sw->font_section, "Window");
        if (card_begin(ctx, "card_window", card_height_for(ctx, 1, 38, 0))) {
          set_font(ctx, sw->font_ui);
          setting_row_begin(ctx, 38);
          nk_label_colored(ctx, "Opacity", NK_TEXT_LEFT, COL_TEXT);
          {
            float prev = cfg->opacity;
            nk_property_float(ctx, "##op", 0.3f, &cfg->opacity, 1.0f, 0.05f, 0.01f);
            if (cfg->opacity != prev) {
              sw->apply_pending = 1;
            }
          }
          card_end(ctx);
        }
      } else if (sw->tab == 1) {
        section_header(ctx, sw->font_section, "Behavior");
        if (card_begin(ctx, "card_behavior", card_height_for(ctx, 1, 42, 0))) {
          set_font(ctx, sw->font_ui);
          setting_row_begin_wide_label(ctx, 42);
          nk_label_colored(ctx, "Scrollback lines", NK_TEXT_LEFT, COL_TEXT);
          nk_property_int(ctx, "##sb", 100, &cfg->scrollback_lines, 100000, 100, 10);
          card_end(ctx);
        }

        section_header(ctx, sw->font_section, "Plugins");
        int plug_rows = cfg->plugin_count > 0 ? (1 + cfg->plugin_count) : 1;
        float plug_h = card_height_for(ctx, plug_rows, 26, 0) + 8.0f;
        if (card_begin(ctx, "card_plugins", plug_h)) {
          set_font(ctx, sw->font_dim);
          nk_layout_row_dynamic(ctx, 24, 1);
          if (cfg->plugin_count == 0) {
            nk_label_colored(ctx, "No plugins enabled", NK_TEXT_LEFT, COL_DIM);
          } else {
            nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_DIM, "%d enabled", cfg->plugin_count);
            set_font(ctx, sw->font_ui);
            for (int i = 0; i < cfg->plugin_count; i++) {
              nk_layout_row_dynamic(ctx, 26, 1);
              nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_TEXT, "  %s", cfg->plugins[i]);
            }
          }
          card_end(ctx);
        }
      } else {
        const char *filter = sw->shortcut_filter;
        int show_leader = str_contains_ci("leader", filter) || !filter[0];

        set_font(ctx, sw->font_ui);
        setting_row_begin(ctx, 36);
        nk_label_colored(ctx, "Search", NK_TEXT_LEFT, COL_TEXT);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, sw->shortcut_filter,
                                       (int)sizeof(sw->shortcut_filter), nk_filter_default);

        set_font(ctx, sw->font_dim);
        nk_layout_row_dynamic(ctx, 28, 1);
        if (sw->capturing_leader) {
          nk_label_colored(ctx, "Press a new leader key — Esc cancels", NK_TEXT_LEFT, COL_ACCENT);
        } else if (sw->capturing && sw->capture_leader_armed) {
          nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_ACCENT,
                            "Leader armed — press follow-up for %s (Esc cancels)",
                            traash_action_label(sw->capture_action));
        } else if (sw->capturing) {
          nk_labelf_colored(ctx, NK_TEXT_LEFT, COL_ACCENT,
                            "Press a key for %s, or press Leader then a key",
                            traash_action_label(sw->capture_action));
        } else {
          nk_label_colored(ctx,
                           "Click a binding, then press a key — or Leader then key for a prefix chord.",
                           NK_TEXT_LEFT, COL_DIM);
        }

        if (sw->capturing && !sw->capturing_leader) {
          set_font(ctx, sw->font_ui);
          nk_layout_row_dynamic(ctx, 32, 1);
          nk_bool pref = sw->capture_as_prefix ? nk_true : nk_false;
          nk_checkbox_label(ctx, "After leader (prefix chord)", &pref);
          sw->capture_as_prefix = pref ? 1 : 0;
          if (!sw->capture_as_prefix) {
            sw->capture_leader_armed = 0;
          }
        }

        if (show_leader) {
          section_header(ctx, sw->font_section, "Leader");
          if (card_begin(ctx, "card_leader", card_height_for(ctx, 1, 40, 0))) {
            set_font(ctx, sw->font_ui);
            char leader[64];
            traash_keymap_format_leader(km, leader, sizeof(leader));
            setting_row_begin_wide_label(ctx, 40);
            nk_label_colored(ctx, "Leader key", NK_TEXT_LEFT, COL_TEXT);
            if (nk_button_label(ctx, sw->capturing_leader ? "Listening..." : leader)) {
              sw->capturing_leader = 1;
              sw->capturing = 0;
              sw->capture_leader_armed = 0;
              sw->pending_capture_key = 0;
              sw->theme_dirty = 0;
              snprintf(sw->status_msg, sizeof(sw->status_msg), "Capturing leader key");
              glfwFocusWindow(sw->win);
            }
            card_end(ctx);
          }
        }

        int any_shown = show_leader ? 1 : 0;
        int group_count = traash_shortcut_group_count();
        for (int gi = 0; gi < group_count; gi++) {
          const TraashShortcutGroup *g = &TRAASH_SHORTCUT_GROUPS[gi];
          int match_n = count_matching_in_group(km, g, filter);
          if (match_n <= 0) {
            continue;
          }
          any_shown = 1;
          section_header(ctx, sw->font_section, g->title);
          float card_h = card_height_for(ctx, match_n, 36, 1);
          char card_id[64];
          snprintf(card_id, sizeof(card_id), "card_sc_%d", gi);
          if (card_begin(ctx, card_id, card_h)) {
            set_font(ctx, sw->font_ui);
            int first = 1;
            for (int i = 0; i < g->count; i++) {
              TraashAction a = g->actions[i];
              if (!shortcut_matches(km, a, filter)) {
                continue;
              }
              draw_shortcut_row(ctx, sw, km, a, &first);
            }
            card_end(ctx);
          }
        }

        if (!any_shown) {
          set_font(ctx, sw->font_dim);
          nk_layout_row_dynamic(ctx, 36, 1);
          nk_label_colored(ctx, "No matching shortcuts", NK_TEXT_LEFT, COL_DIM);
        }
      }
      nk_group_end(ctx);
    }

    /* Footer — keep this block compact; height is reserved above */
    set_font(ctx, sw->font_dim);
    nk_layout_row_dynamic(ctx, 22, 1);
    nk_label_colored(ctx, sw->status_msg, NK_TEXT_LEFT, COL_DIM);
    set_font(ctx, sw->font_ui);
    nk_layout_row_begin(ctx, NK_STATIC, 40, 3);
    nk_layout_row_push(ctx, 120);
    if (action_button(ctx, "Apply", 0)) {
      sw->apply_pending = 1;
      sw->theme_dirty = 1;
      snprintf(sw->status_msg, sizeof(sw->status_msg), "Applied");
    }
    nk_layout_row_push(ctx, 120);
    if (action_button(ctx, "Save", 1)) {
      sw->save_pending = 1;
      sw->apply_pending = 1;
    }
    nk_layout_row_push(ctx, 120);
    if (action_button(ctx, "Close", 0)) {
      sw->close_pending = 1;
    }
    nk_layout_row_end(ctx);
    nk_layout_row_dynamic(ctx, 12, 1);
    nk_spacing(ctx, 1);
  }
  nk_end(ctx);

  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(sw->win, &fbw, &fbh);
  glViewport(0, 0, fbw, fbh);
  glClearColor(COL_BG.r / 255.0f, COL_BG.g / 255.0f, COL_BG.b / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  nk_glfw3_render(nk, NK_ANTI_ALIASING_ON, 1024 * 1024, 256 * 1024);
  glfwSwapBuffers(sw->win);

  if (main) {
    glfwMakeContextCurrent(main);
  }
  return 1;
}
