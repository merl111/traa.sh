#include "app/app.h"

#include "config/config.h"
#include "config/status.h"
#include "config/theme.h"
#include "demo/demo.h"
#include "gfx/gl_loader.h"
#include "gfx/renderer.h"
#include "input/keymap.h"
#include "mux/server.h"
#include "platform/platform.h"
#include "plugin/host.h"
#include "ui/context_menu.h"
#include "ui/layout_overlay.h"
#include "ui/overview_overlay.h"
#include "ui/palette.h"
#include "ui/quit_confirm.h"
#include "ui/settings.h"
#include "ui/settings_window.h"
#include "ui/shortcuts_overlay.h"
#include "ui/status_bar.h"
#include "util/log.h"
#include "util/path.h"
#include "term/screen.h"
#include "assets/icon_embedded.h"
#include "mux/layout_store.h"
#include "mux/client.h"
#include "mux/snapshot.h"
#include "crypto/secret.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  TraashConfig cfg;
  TraashTheme theme;
  TraashMuxServer mux;
  TraashRenderer renderer;
  TraashKeymap keymap;
  TraashPluginHost plugins;
  TraashPalette palette;
  TraashDemo demo;
  TraashContextMenu menu;
  TraashShortcutsOverlay shortcuts;
  TraashLayoutOverlay layouts;
  TraashOverviewOverlay overview;
  TraashQuitConfirm quit_confirm;
  TraashSettings settings;
  TraashSettingsWindow settings_win;
  GLFWwindow *window;
  char status_line[512];
  TraashStatusModel status;
  const char **theme_names;
  int theme_count;
  int theme_index;
  const char **status_names;
  int status_count;
  int status_index;
  int search_mode;
  char search_query[128];
  TraashSearchMatch search_hits[512];
  int search_hit_count;
  int search_hit_index; /* -1 if none */
  int search_match_len;
  int dragging;
  TraashPane *drag_pane;
  double last_click_time;
  int last_click_x;
  int last_click_y;
  TraashPane *last_click_pane;
  int ignore_char; /* keymap consumed key; skip following char_cb */
  GLFWcursor *cursor_ibeam;
  GLFWcursor *cursor_arrow;
  int cursor_shape; /* -1 unset, 0 arrow, 1 ibeam — avoid glfwSetCursor spam */
  int window_focused;
  double sync_hold_since; /* 0 = not holding; else glfw time when sync started */
  double last_fb_change_at; /* glfw time of last framebuffer size change */
  double last_resize_present_at; /* throttle presents during interactive resize */
  int resize_fast_pending; /* 1 = need a full redraw after drag settles */
  int content_scale_pending; /* apply after configure-event batch / resize */
  int client_mode;
  int client_role;
  TraashMuxConnection client;
} TraashApp;

static TraashApp *g_app;

static int app_read_only(const TraashApp *app) {
  return app && app->client_mode && app->client_role == TRAASH_ROLE_READ;
}

static TraashSession *app_session(const TraashApp *app) {
  if (app->client_mode && app->client.session) {
    return app->client.session;
  }
  return app->mux.attached;
}

static void app_write_pane(TraashApp *app, TraashPane *pane, const uint8_t *data, size_t n) {
  if (!app || !pane || !data || n == 0 || app_read_only(app)) {
    return;
  }
  if (app->client_mode) {
    traash_mux_client_send_input(&app->client, pane->id, data, n);
    return;
  }
  if (pane->pty.master_fd >= 0) {
    traash_pty_write(&pane->pty, data, n);
  }
}

static void app_send_action(TraashApp *app, TraashAction action) {
  if (!app->client_mode) {
    return;
  }
  if (app_read_only(app) && traash_mux_action_mutating((uint32_t)action)) {
    return;
  }
  uint32_t extra = 0;
  uint32_t extra_len = 0;
  if (action == TRAASH_ACTION_GOTO_WINDOW) {
    extra = (uint32_t)app->keymap.goto_window;
    extra_len = 4;
  }
  traash_mux_client_send_action(&app->client, (uint32_t)action, (const uint8_t *)&extra,
                                extra_len);
  app->mux.attached = app->client.session;
}

static const char *THEMES[] = {"tokyo-night", "catppuccin-mocha", "dracula",
                               "gruvbox-dark", "nord",           "one-dark",
                               "solarized-dark", "solarized-light", "rose-pine",
                               "github-dark", "traash-dark"};
static const char *STATUSES[] = {"pills", "powerline", "minimal", "tmux", "dev", "compact",
                                 "centered"};

static void refresh_status(TraashApp *app) {
  TraashSession *s = app_session(app);
  traash_status_render(app->cfg.lua, app->cfg.status_bar, s, &app->status);
  traash_status_bar_format(&app->status, app->status_line, sizeof(app->status_line));
  if (app_read_only(app) && app->status.count < 32) {
    TraashStatusSegment *seg = &app->status.segs[app->status.count++];
    snprintf(seg->text, sizeof(seg->text), " READ-ONLY ");
    snprintf(seg->align, sizeof(seg->align), "right");
    seg->fg = 0xffcc66;
    seg->bg = 0x3d2e00;
  }
  if (app->keymap.prefix_active && app->status.count < 32) {
    TraashStatusSegment *seg = &app->status.segs[app->status.count++];
    snprintf(seg->text, sizeof(seg->text), " PREFIX ");
    snprintf(seg->align, sizeof(seg->align), "right");
    seg->fg = 0x1a1a1e;
    seg->bg = 0x33d17a;
  }
}

static void apply_theme(TraashApp *app, const char *name) {
  char theme[64];
  if (!name || !name[0]) {
    name = "tokyo-night";
  }
  /* Copy first — snprintf(dst, "%s", dst) is undefined and can clear the string */
  snprintf(theme, sizeof(theme), "%s", name);
  snprintf(app->cfg.theme, sizeof(app->cfg.theme), "%s", theme);
  traash_theme_load(app->cfg.lua, app->cfg.theme, &app->theme);
  traash_plugins_set_theme(&app->plugins, app->cfg.theme);
  /* Keep OSC 10/11/12 replies in sync with the active theme (Neovim bg detect). */
  for (TraashSession *s = app->mux.sessions; s; s = s->next) {
    for (TraashWindow *w = s->windows; w; w = w->next) {
      for (TraashPane *p = w->panes; p; p = p->next) {
        traash_vt_set_report_colors(&p->vt, app->theme.foreground, app->theme.background,
                                    app->theme.cursor);
      }
    }
  }
}

static void search_close(TraashApp *app) {
  app->search_mode = 0;
  app->search_hit_count = 0;
  app->search_hit_index = -1;
  app->search_match_len = 0;
}

static void search_utf8_delete_last(char *s) {
  size_t n = strlen(s);
  if (n == 0) {
    return;
  }
  while (n > 0 && (s[n - 1] & 0xC0) == 0x80) {
    n--;
  }
  if (n > 0) {
    n--;
  }
  s[n] = 0;
}

static void search_collect(TraashApp *app) {
  TraashPane *pane = traash_session_active_pane(app->mux.attached);
  app->search_hit_count = 0;
  app->search_match_len = traash_screen_query_len(app->search_query);
  if (!pane || !app->search_query[0] || app->search_match_len <= 0) {
    app->search_hit_index = -1;
    return;
  }
  app->search_hit_count = traash_screen_find_all(
      &pane->screen, app->search_query, app->search_hits,
      (int)(sizeof(app->search_hits) / sizeof(app->search_hits[0])));
  if (app->search_hit_count <= 0) {
    app->search_hit_index = -1;
  }
}

static void search_reveal_current(TraashApp *app) {
  TraashPane *pane = traash_session_active_pane(app->mux.attached);
  if (!pane || app->search_hit_index < 0 ||
      app->search_hit_index >= app->search_hit_count) {
    return;
  }
  traash_screen_reveal_row(&pane->screen, app->search_hits[app->search_hit_index].row);
}

static void search_rebuild(TraashApp *app) {
  TraashPane *pane = traash_session_active_pane(app->mux.attached);
  search_collect(app);
  if (!pane || app->search_hit_count <= 0) {
    return;
  }
  int cursor_abs = pane->screen.scrollback_len + pane->screen.cursor_y;
  int pick = 0;
  for (int i = 0; i < app->search_hit_count; i++) {
    if (app->search_hits[i].row > cursor_abs ||
        (app->search_hits[i].row == cursor_abs &&
         app->search_hits[i].col >= pane->screen.cursor_x)) {
      pick = i;
      break;
    }
  }
  app->search_hit_index = pick;
  search_reveal_current(app);
}

static void search_goto(TraashApp *app, int delta) {
  if (!app->search_mode) {
    return;
  }
  int prev = app->search_hit_index;
  search_collect(app);
  if (app->search_hit_count <= 0) {
    return;
  }
  int idx;
  if (prev < 0) {
    idx = (delta >= 0) ? 0 : app->search_hit_count - 1;
  } else {
    idx = (prev + delta) % app->search_hit_count;
    if (idx < 0) {
      idx += app->search_hit_count;
    }
  }
  app->search_hit_index = idx;
  search_reveal_current(app);
}

static void do_copy(TraashApp *app) {
  TraashPane *pane = traash_session_active_pane(app->mux.attached);
  if (!pane || !pane->selection.active) {
    return;
  }
  char buf[65536];
  if (traash_selection_text(&pane->selection, &pane->screen, buf, sizeof(buf)) <= 0) {
    return;
  }
  app->plugins.has_clipboard_override = 0;
  traash_plugins_emit_str(&app->plugins, "on_copy", buf);
  const char *out = buf;
  if (app->plugins.has_clipboard_override) {
    out = app->plugins.clipboard_override;
  }
  glfwSetClipboardString(app->window, out);
  traash_clipboard_set(out);
  /* Also PRIMARY so middle-click paste works like other Linux terminals. */
  traash_primary_set(out);
}

static void apply_named_layout(TraashApp *app, const char *name) {
  if (!app || !name || !name[0] || !app->mux.attached) {
    return;
  }
  TraashLayoutDesc desc;
  if (traash_layout_store_load(name, &desc) != 0) {
    TRAASH_LOGW("layout '%s' not found", name);
    return;
  }
  int cols = 80, rows = 24;
  TraashPane *ap = traash_session_active_pane(app->mux.attached);
  if (ap) {
    cols = ap->screen.cols;
    rows = ap->screen.rows;
  }
  if (traash_layout_apply(app->mux.attached, &desc, cols, rows) != 0) {
    TRAASH_LOGW("failed to apply layout '%s'", name);
  }
}

static void layout_apply_selected(TraashApp *app) {
  if (!app->layouts.open || app->layouts.count <= 0) {
    return;
  }
  if (app->layouts.selected < 0 || app->layouts.selected >= app->layouts.count) {
    return;
  }
  apply_named_layout(app, app->layouts.names[app->layouts.selected].name);
  traash_layout_overlay_close(&app->layouts);
}

static void layout_save_current(TraashApp *app) {
  if (!app->layouts.save_name[0] || !traash_layout_name_valid(app->layouts.save_name)) {
    return;
  }
  if (!app->mux.attached) {
    return;
  }
  if (traash_layout_store_save(app->layouts.save_name, app->mux.attached) == 0) {
    traash_layout_overlay_refresh(&app->layouts);
    /* Select the saved layout */
    for (int i = 0; i < app->layouts.count; i++) {
      if (strcmp(app->layouts.names[i].name, app->layouts.save_name) == 0) {
        app->layouts.selected = i;
        break;
      }
    }
  }
  app->layouts.save_mode = 0;
  app->layouts.save_name[0] = 0;
}

static void layout_delete_selected(TraashApp *app) {
  if (!app->layouts.open || app->layouts.count <= 0) {
    return;
  }
  int i = app->layouts.selected;
  if (i < 0 || i >= app->layouts.count) {
    return;
  }
  if (!app->layouts.names[i].user) {
    return; /* bundled */
  }
  if (traash_layout_store_delete(app->layouts.names[i].name) == 0) {
    traash_layout_overlay_refresh(&app->layouts);
  }
}

static void paste_text(TraashApp *app, const char *clip) {
  TraashPane *pane = traash_session_active_pane(app_session(app));
  if (!pane || !clip || !clip[0] || app_read_only(app)) {
    return;
  }
  if (app->client_mode) {
    if (pane->screen.bracketed_paste) {
      app_write_pane(app, pane, (const uint8_t *)"\x1b[200~", 6);
    }
    app_write_pane(app, pane, (const uint8_t *)clip, strlen(clip));
    if (pane->screen.bracketed_paste) {
      app_write_pane(app, pane, (const uint8_t *)"\x1b[201~", 6);
    }
    return;
  }
  if (pane->pty.master_fd < 0) {
    return;
  }
  if (pane->screen.bracketed_paste) {
    traash_pty_write(&pane->pty, (const uint8_t *)"\x1b[200~", 6);
  }
  traash_pty_write(&pane->pty, (const uint8_t *)clip, strlen(clip));
  if (pane->screen.bracketed_paste) {
    traash_pty_write(&pane->pty, (const uint8_t *)"\x1b[201~", 6);
  }
}

static void do_paste(TraashApp *app) {
  const char *clip = glfwGetClipboardString(app->window);
  char fallback[65536];
  if (!clip || !clip[0]) {
    if (traash_clipboard_get(fallback, sizeof(fallback)) <= 0) {
      return;
    }
    clip = fallback;
  }
  paste_text(app, clip);
}

/* Middle-click: PRIMARY first, then clipboard (Linux terminal convention). */
static void do_paste_primary(TraashApp *app) {
  char buf[65536];
  if (traash_primary_get(buf, sizeof(buf)) > 0 && buf[0]) {
    paste_text(app, buf);
    return;
  }
  do_paste(app);
}

static void cursor_to_framebuffer(TraashApp *app, double *fx, double *fy) {
  double mx = 0, my = 0;
  glfwGetCursorPos(app->window, &mx, &my);
  int win_w = 1, win_h = 1, fb_w = 1, fb_h = 1;
  glfwGetWindowSize(app->window, &win_w, &win_h);
  glfwGetFramebufferSize(app->window, &fb_w, &fb_h);
  *fx = mx * ((double)fb_w / (double)(win_w > 0 ? win_w : 1));
  *fy = my * ((double)fb_h / (double)(win_h > 0 ? win_h : 1));
}

static void set_mouse_cursor(TraashApp *app, GLFWwindow *w, int ibeam) {
  int shape = ibeam ? 1 : 0;
  if (app->cursor_shape == shape) {
    return;
  }
  app->cursor_shape = shape;
  glfwSetCursor(w, ibeam ? app->cursor_ibeam : app->cursor_arrow);
}

static void apply_settings_live(TraashApp *app) {
  apply_theme(app, app->cfg.theme);
  app->renderer.cursor_style = app->cfg.cursor_style;
  app->renderer.base_font_px = app->cfg.font_size;
  snprintf(app->renderer.font_family, sizeof(app->renderer.font_family), "%s",
           app->cfg.font_family);
  /* Force font rebuild at current scale */
  float scale = app->renderer.content_scale;
  app->renderer.content_scale = 0;
  traash_renderer_set_content_scale(&app->renderer, scale);
  float opacity = app->cfg.opacity;
  if (opacity < 0.3f) {
    opacity = 0.3f;
  }
  if (opacity > 1.0f) {
    opacity = 1.0f;
  }
  app->renderer.opacity = opacity;
  /* Wayland has no glfwSetWindowOpacity — transparent FB + shader alpha instead */
  if (!app->renderer.fb_transparent) {
    glfwSetWindowOpacity(app->window, opacity);
  }
}

static void change_font_size(TraashApp *app, float delta) {
  float size = app->cfg.font_size + delta;
  if (size < 8.0f) {
    size = 8.0f;
  } else if (size > 48.0f) {
    size = 48.0f;
  }
  if (size != app->cfg.font_size) {
    app->cfg.font_size = size;
    app->renderer.base_font_px = size;
    float scale = app->renderer.content_scale;
    app->renderer.content_scale = 0.0f;
    traash_renderer_set_content_scale(&app->renderer, scale);
  }
  char message[64];
  snprintf(message, sizeof(message), "Font size  %.0f px", app->cfg.font_size);
  traash_renderer_show_toast(&app->renderer, message, glfwGetTime() + 1.35);
}

static void request_app_quit(TraashApp *app) {
  if (!app || !app->window) {
    return;
  }
  if (app->quit_confirm.open) {
    return;
  }
  if (traash_quit_confirm_needed(&app->quit_confirm, &app->mux)) {
    traash_context_menu_close(&app->menu);
    if (app->palette.open) {
      app->palette.open = false;
    }
    traash_shortcuts_overlay_close(&app->shortcuts);
    traash_layout_overlay_close(&app->layouts);
    traash_overview_overlay_close(&app->overview);
    return;
  }
  glfwSetWindowShouldClose(app->window, GLFW_TRUE);
}

static TraashWindow *find_window_id(TraashSession *s, int id) {
  if (!s || id < 0) {
    return NULL;
  }
  for (TraashWindow *w = s->windows; w; w = w->next) {
    if (w->id == id) {
      return w;
    }
  }
  return NULL;
}

static TraashPane *find_pane_id(TraashWindow *w, int id) {
  if (!w || id < 0) {
    return NULL;
  }
  for (TraashPane *p = w->panes; p; p = p->next) {
    if (p->id == id) {
      return p;
    }
  }
  return NULL;
}

static void ensure_one_window(TraashApp *app) {
  TraashSession *s = app->mux.attached;
  if (s && !s->windows) {
    traash_session_new_window(s, NULL, 80, 24);
  }
}

static void overview_clamp_now(TraashApp *app) {
  if (!app->overview.open) {
    return;
  }
  TraashSession *s = app->mux.attached;
  int count = traash_overview_overlay_item_count(&app->overview, s);
  TraashOverviewLayout lay;
  float scale = app->renderer.content_scale > 0.1f ? app->renderer.content_scale : 1.0f;
  traash_overview_overlay_layout(app->renderer.fb_w, app->renderer.fb_h, scale,
                                 app->renderer.font.cell_h, count, &lay);
  traash_overview_overlay_clamp(&app->overview, s, &lay);
}

static void do_close_window(TraashApp *app, TraashWindow *w) {
  TraashSession *s = app->mux.attached;
  if (!s || !w) {
    return;
  }
  traash_session_close_window(s, w);
  ensure_one_window(app);
  if (app->overview.open && app->overview.mode == TRAASH_OVERVIEW_PANES &&
      !traash_overview_overlay_pane_tab(&app->overview, s)) {
    traash_overview_overlay_open_tabs(&app->overview, s);
  }
  overview_clamp_now(app);
}

static void do_close_pane(TraashApp *app, TraashWindow *w, TraashPane *p) {
  if (!w || !p) {
    return;
  }
  if (traash_window_pane_count(w) <= 1) {
    do_close_window(app, w);
    return;
  }
  traash_window_close_pane(w, p);
  if (!w->panes) {
    do_close_window(app, w);
    return;
  }
  overview_clamp_now(app);
}

static void request_close_window(TraashApp *app, TraashWindow *w) {
  if (!app || !w) {
    return;
  }
  if (traash_quit_confirm_needed_window(&app->quit_confirm, w)) {
    return;
  }
  do_close_window(app, w);
}

static void request_close_pane(TraashApp *app, TraashWindow *w, TraashPane *p) {
  if (!app || !w || !p) {
    return;
  }
  if (traash_window_pane_count(w) <= 1) {
    request_close_window(app, w);
    return;
  }
  if (traash_quit_confirm_needed_pane(&app->quit_confirm, p)) {
    app->quit_confirm.target_window_id = w->id;
    return;
  }
  do_close_pane(app, w, p);
}

static void overview_focus_window(TraashApp *app, TraashWindow *w, TraashPane *p) {
  TraashSession *s = app->mux.attached;
  if (!s || !w) {
    return;
  }
  traash_session_select_window(s, w);
  if (p) {
    traash_window_focus_pane(w, p);
  }
  traash_overview_overlay_close(&app->overview);
}

static void overview_activate_selected(TraashApp *app) {
  TraashSession *s = app->mux.attached;
  if (!s) {
    return;
  }
  if (app->overview.mode == TRAASH_OVERVIEW_TABS) {
    TraashWindow *w = traash_overview_overlay_selected_window(&app->overview, s);
    if (!w) {
      return;
    }
    if (traash_window_pane_count(w) > 1) {
      traash_overview_overlay_drill_panes(&app->overview, w);
      overview_clamp_now(app);
    } else {
      overview_focus_window(app, w, w->active);
    }
    return;
  }
  TraashWindow *w = traash_overview_overlay_pane_tab(&app->overview, s);
  TraashPane *p = traash_overview_overlay_selected_pane(&app->overview, s);
  overview_focus_window(app, w, p);
}

static void overview_close_selected(TraashApp *app) {
  TraashSession *s = app->mux.attached;
  if (!s) {
    return;
  }
  if (app->overview.mode == TRAASH_OVERVIEW_PANES) {
    request_close_pane(app, traash_overview_overlay_pane_tab(&app->overview, s),
                       traash_overview_overlay_selected_pane(&app->overview, s));
    return;
  }
  request_close_window(app, traash_overview_overlay_selected_window(&app->overview, s));
}

static void confirm_app_quit(TraashApp *app) {
  if (!app || !app->window) {
    return;
  }
  TraashQuitKind kind = app->quit_confirm.kind;
  int win_id = app->quit_confirm.target_window_id;
  int pane_id = app->quit_confirm.target_pane_id;
  traash_quit_confirm_close(&app->quit_confirm);
  if (kind == TRAASH_QUIT_KIND_TAB) {
    TraashWindow *w = find_window_id(app->mux.attached, win_id);
    if (w) {
      do_close_window(app, w);
    }
    return;
  }
  if (kind == TRAASH_QUIT_KIND_PANE) {
    TraashWindow *w = find_window_id(app->mux.attached, win_id);
    TraashPane *p = find_pane_id(w, pane_id);
    if (w && p) {
      do_close_pane(app, w, p);
    }
    return;
  }
  glfwSetWindowShouldClose(app->window, GLFW_TRUE);
}

static void cancel_app_quit(TraashApp *app) {
  if (!app) {
    return;
  }
  traash_quit_confirm_close(&app->quit_confirm);
  if (app->window) {
    glfwSetWindowShouldClose(app->window, GLFW_FALSE);
  }
}

static void run_action(TraashApp *app, TraashAction action) {
  TraashSession *sess = app_session(app);
  TraashWindow *win = sess ? sess->active : NULL;
  if (app->client_mode) {
    switch (action) {
    case TRAASH_ACTION_COMMAND_PALETTE:
      traash_palette_toggle(&app->palette);
      return;
    case TRAASH_ACTION_RELOAD_CONFIG:
      traash_config_reload(&app->cfg);
      apply_theme(app, app->cfg.theme);
      app->renderer.cursor_style = app->cfg.cursor_style;
      return;
    case TRAASH_ACTION_THEME_CYCLE:
      app->theme_index = (app->theme_index + 1) % app->theme_count;
      apply_theme(app, app->theme_names[app->theme_index]);
      return;
    case TRAASH_ACTION_STATUS_CYCLE:
      app->status_index = (app->status_index + 1) % app->status_count;
      return;
    case TRAASH_ACTION_SEARCH:
      app->search_mode = !app->search_mode;
      return;
    case TRAASH_ACTION_SETTINGS:
      if (!traash_settings_window_is_open(&app->settings_win)) {
        traash_settings_window_open(&app->settings_win, app->window, &app->cfg, app->cfg.lua);
      }
      return;
    case TRAASH_ACTION_SHORTCUTS:
      traash_shortcuts_overlay_toggle(&app->shortcuts);
      return;
    case TRAASH_ACTION_OVERVIEW:
      traash_overview_overlay_toggle(&app->overview, sess);
      overview_clamp_now(app);
      return;
    case TRAASH_ACTION_LAYOUT_PICKER:
      traash_layout_overlay_toggle(&app->layouts);
      return;
    case TRAASH_ACTION_QUIT:
      glfwSetWindowShouldClose(app->window, GLFW_TRUE);
      return;
    case TRAASH_ACTION_COPY:
    case TRAASH_ACTION_FONT_INCREASE:
    case TRAASH_ACTION_FONT_DECREASE:
      break;
    default:
      app_send_action(app, action);
      return;
    }
  }
  switch (action) {
  case TRAASH_ACTION_SPLIT_H:
    if (win) {
      traash_window_split(win, 0, &sess->next_pane_id, 80, 24);
    }
    break;
  case TRAASH_ACTION_SPLIT_V:
    if (win) {
      traash_window_split(win, 1, &sess->next_pane_id, 80, 24);
    }
    break;
  case TRAASH_ACTION_PANE_NEXT:
    if (win) {
      traash_window_focus_next(win);
    }
    break;
  case TRAASH_ACTION_PANE_LEFT:
    if (win) {
      traash_window_focus_dir(win, -1, 0);
    }
    break;
  case TRAASH_ACTION_PANE_DOWN:
    if (win) {
      traash_window_focus_dir(win, 0, 1);
    }
    break;
  case TRAASH_ACTION_PANE_UP:
    if (win) {
      traash_window_focus_dir(win, 0, -1);
    }
    break;
  case TRAASH_ACTION_PANE_RIGHT:
    if (win) {
      traash_window_focus_dir(win, 1, 0);
    }
    break;
  case TRAASH_ACTION_ZOOM:
    if (win) {
      traash_window_zoom_toggle(win);
    }
    break;
  case TRAASH_ACTION_NEW_WINDOW:
    if (sess) {
      traash_session_new_window(sess, NULL, 80, 24);
    }
    break;
  case TRAASH_ACTION_NEXT_WINDOW:
    if (sess) {
      traash_session_next_window(sess);
    }
    break;
  case TRAASH_ACTION_PREV_WINDOW:
    if (sess) {
      traash_session_prev_window(sess);
    }
    break;
  case TRAASH_ACTION_GOTO_WINDOW:
    if (sess) {
      traash_session_goto_window(sess, app->keymap.goto_window);
    }
    break;
  case TRAASH_ACTION_DETACH:
    traash_mux_detach(&app->mux);
    break;
  case TRAASH_ACTION_RELOAD_CONFIG:
    traash_config_reload(&app->cfg);
    apply_theme(app, app->cfg.theme);
    app->renderer.cursor_style = app->cfg.cursor_style;
    break;
  case TRAASH_ACTION_COMMAND_PALETTE:
    traash_palette_toggle(&app->palette);
    break;
  case TRAASH_ACTION_THEME_CYCLE:
    app->theme_index = (app->theme_index + 1) % app->theme_count;
    apply_theme(app, app->theme_names[app->theme_index]);
    break;
  case TRAASH_ACTION_STATUS_CYCLE:
    app->status_index = (app->status_index + 1) % app->status_count;
    snprintf(app->cfg.status_bar, sizeof(app->cfg.status_bar), "%s",
             app->status_names[app->status_index]);
    break;
  case TRAASH_ACTION_FONT_INCREASE:
    change_font_size(app, 1.0f);
    break;
  case TRAASH_ACTION_FONT_DECREASE:
    change_font_size(app, -1.0f);
    break;
  case TRAASH_ACTION_DEMO:
    traash_demo_start(&app->demo, &app->mux, app->cfg.lua, false);
    break;
  case TRAASH_ACTION_COPY:
    do_copy(app);
    break;
  case TRAASH_ACTION_PASTE:
    do_paste(app);
    break;
  case TRAASH_ACTION_SETTINGS:
    if (!traash_settings_window_is_open(&app->settings_win)) {
      traash_settings_window_open(&app->settings_win, app->window, &app->cfg, app->cfg.lua);
    }
    break;
  case TRAASH_ACTION_SHORTCUTS:
    traash_context_menu_close(&app->menu);
    if (app->palette.open) {
      app->palette.open = false;
    }
    traash_layout_overlay_close(&app->layouts);
    traash_overview_overlay_close(&app->overview);
    traash_shortcuts_overlay_toggle(&app->shortcuts);
    break;
  case TRAASH_ACTION_LAYOUT_PICKER:
    traash_context_menu_close(&app->menu);
    if (app->palette.open) {
      app->palette.open = false;
    }
    traash_shortcuts_overlay_close(&app->shortcuts);
    traash_overview_overlay_close(&app->overview);
    traash_layout_overlay_toggle(&app->layouts);
    break;
  case TRAASH_ACTION_OVERVIEW:
    traash_context_menu_close(&app->menu);
    if (app->palette.open) {
      app->palette.open = false;
    }
    traash_shortcuts_overlay_close(&app->shortcuts);
    traash_layout_overlay_close(&app->layouts);
    traash_overview_overlay_toggle(&app->overview, sess);
    overview_clamp_now(app);
    break;
  case TRAASH_ACTION_SEARCH:
    if (app->search_mode) {
      search_close(app);
    } else {
      app->search_mode = 1;
      app->search_query[0] = 0;
      app->search_hit_count = 0;
      app->search_hit_index = -1;
      app->search_match_len = 0;
    }
    break;
  case TRAASH_ACTION_QUIT:
    request_app_quit(app);
    break;
  default:
    break;
  }
}

static int glfw_mods_to_internal(int mods) {
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

static int font_zoom_delta(int key, int scancode, int mods) {
  if (!(mods & 1) || (mods & (4 | 8))) {
    return 0;
  }
  if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
    return 1;
  }
  if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
    return -1;
  }
  /* GLFW key tokens follow a US layout. glfwGetKeyName resolves the actual
   * printable key on non-US layouts, where + and - can have different tokens. */
  const char *name = glfwGetKeyName(key, scancode);
  if (name && (strcmp(name, "+") == 0 || strcmp(name, "=") == 0)) {
    return 1;
  }
  if (name && strcmp(name, "-") == 0) {
    return -1;
  }
  return 0;
}

static void key_cb(GLFWwindow *w, int key, int scancode, int action, int mods) {
  TraashApp *app = g_app;
  if (!app) {
    return;
  }
  /* Ctrl-hjkl (and similar) do not generate a char event, so ignore_char would
   * otherwise stay set and swallow the next real key. CHAR arrives before
   * RELEASE for keys that do produce text. */
  if (action == GLFW_RELEASE) {
    app->ignore_char = 0;
    return;
  }
  if (action != GLFW_PRESS && action != GLFW_REPEAT) {
    return;
  }

  /* Modifier-only events must not cancel a pending leader chord */
  if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT ||
      key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL ||
      key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT ||
      key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER ||
      key == GLFW_KEY_CAPS_LOCK || key == GLFW_KEY_NUM_LOCK) {
    return;
  }

  /* Treat the numpad Enter as the regular Enter in shells and every overlay. */
  if (key == GLFW_KEY_KP_ENTER) {
    key = GLFW_KEY_ENTER;
  }

  int m = glfw_mods_to_internal(mods);
  int font_delta = font_zoom_delta(key, scancode, m);
  if (font_delta != 0) {
    change_font_size(app, (float)font_delta);
    app->ignore_char = 1;
    return;
  }
  int is_leader =
      (key == app->keymap.prefix_key &&
       (m == app->keymap.prefix_mods ||
        ((m & app->keymap.prefix_mods) == app->keymap.prefix_mods &&
         (m & ~app->keymap.prefix_mods) == 0)));

  if (app->demo.active) {
    if (traash_demo_key(&app->demo, &app->mux, app->cfg.lua, key)) {
      return;
    }
  }
  if (app->quit_confirm.open) {
    app->ignore_char = 1;
    if (key == GLFW_KEY_ESCAPE) {
      cancel_app_quit(app);
      return;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
      if (app->quit_confirm.focus == 1) {
        confirm_app_quit(app);
      } else {
        cancel_app_quit(app);
      }
      return;
    }
    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_H || key == GLFW_KEY_TAB ||
        key == GLFW_KEY_RIGHT || key == GLFW_KEY_L) {
      app->quit_confirm.focus = 1 - app->quit_confirm.focus;
      return;
    }
    if (key == GLFW_KEY_Y) {
      confirm_app_quit(app);
      return;
    }
    if (key == GLFW_KEY_N) {
      cancel_app_quit(app);
      return;
    }
    return;
  }
  if (app->menu.open && key == GLFW_KEY_ESCAPE) {
    traash_context_menu_close(&app->menu);
    return;
  }
  if (app->overview.open) {
    app->ignore_char = 1;
    if (key == GLFW_KEY_ESCAPE) {
      if (app->overview.mode == TRAASH_OVERVIEW_PANES) {
        traash_overview_overlay_back(&app->overview, app->mux.attached);
        overview_clamp_now(app);
      } else {
        traash_overview_overlay_close(&app->overview);
        app->keymap.prefix_active = false;
      }
      return;
    }
    if (action == GLFW_PRESS) {
      TraashAction act = traash_keymap_lookup(&app->keymap, key, m);
      if (act == TRAASH_ACTION_OVERVIEW) {
        run_action(app, act);
        return;
      }
    }
    TraashOverviewLayout lay;
    int count = traash_overview_overlay_item_count(&app->overview, app->mux.attached);
    float scale = app->renderer.content_scale > 0.1f ? app->renderer.content_scale : 1.0f;
    traash_overview_overlay_layout(app->renderer.fb_w, app->renderer.fb_h, scale,
                                   app->renderer.font.cell_h, count, &lay);
    if (key == GLFW_KEY_ENTER) {
      overview_activate_selected(app);
      return;
    }
    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_H) {
      traash_overview_overlay_move(&app->overview, app->mux.attached, -1, 0, &lay);
      return;
    }
    if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_L) {
      traash_overview_overlay_move(&app->overview, app->mux.attached, 1, 0, &lay);
      return;
    }
    if (key == GLFW_KEY_UP || key == GLFW_KEY_K) {
      traash_overview_overlay_move(&app->overview, app->mux.attached, 0, -1, &lay);
      return;
    }
    if (key == GLFW_KEY_DOWN || key == GLFW_KEY_J) {
      traash_overview_overlay_move(&app->overview, app->mux.attached, 0, 1, &lay);
      return;
    }
    if (key == GLFW_KEY_BACKSPACE || key == GLFW_KEY_U) {
      if (app->overview.mode == TRAASH_OVERVIEW_PANES) {
        traash_overview_overlay_back(&app->overview, app->mux.attached);
        overview_clamp_now(app);
      }
      return;
    }
    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_X) {
      overview_close_selected(app);
      return;
    }
    return;
  }
  if (app->shortcuts.open) {
    if (key == GLFW_KEY_ESCAPE) {
      traash_shortcuts_overlay_close(&app->shortcuts);
      app->keymap.prefix_active = false;
      return;
    }
    /* Allow toggle chord (incl. leader then ?); swallow everything else */
    if (action == GLFW_PRESS) {
      TraashAction act = traash_keymap_lookup(&app->keymap, key, m);
      if (act == TRAASH_ACTION_SHORTCUTS) {
        run_action(app, act);
        app->ignore_char = 1;
        return;
      }
    }
    app->ignore_char = 1;
    return;
  }
  if (app->layouts.open) {
    app->ignore_char = 1;
    if (app->layouts.save_mode) {
      if (key == GLFW_KEY_ESCAPE) {
        app->layouts.save_mode = 0;
        app->layouts.save_name[0] = 0;
        return;
      }
      if (key == GLFW_KEY_ENTER) {
        layout_save_current(app);
        return;
      }
      if (key == GLFW_KEY_BACKSPACE) {
        size_t n = strlen(app->layouts.save_name);
        if (n > 0) {
          app->layouts.save_name[n - 1] = 0;
        }
        return;
      }
      return; /* printable via char_cb */
    }
    if (key == GLFW_KEY_ESCAPE) {
      traash_layout_overlay_close(&app->layouts);
      app->keymap.prefix_active = false;
      return;
    }
    if (action == GLFW_PRESS) {
      TraashAction act = traash_keymap_lookup(&app->keymap, key, m);
      if (act == TRAASH_ACTION_LAYOUT_PICKER) {
        run_action(app, act);
        return;
      }
    }
    if (key == GLFW_KEY_ENTER) {
      layout_apply_selected(app);
      return;
    }
    if (key == GLFW_KEY_DOWN || key == GLFW_KEY_J) {
      if (app->layouts.count > 0) {
        app->layouts.selected = (app->layouts.selected + 1) % app->layouts.count;
      }
      return;
    }
    if (key == GLFW_KEY_UP || key == GLFW_KEY_K) {
      if (app->layouts.count > 0) {
        app->layouts.selected =
            (app->layouts.selected - 1 + app->layouts.count) % app->layouts.count;
      }
      return;
    }
    if (key == GLFW_KEY_S && m == 0) {
      /* s — start save name entry */
      app->layouts.save_mode = 1;
      app->layouts.save_name[0] = 0;
      return;
    }
    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) {
      layout_delete_selected(app);
      return;
    }
    return;
  }
  if (app->palette.open) {
    if (key == GLFW_KEY_ESCAPE) {
      app->palette.open = false;
      return;
    }
    if (key == GLFW_KEY_ENTER) {
      run_action(app, traash_palette_activate(&app->palette));
      return;
    }
    if (key == GLFW_KEY_DOWN) {
      app->palette.selected = (app->palette.selected + 1) % app->palette.count;
      return;
    }
    if (key == GLFW_KEY_UP) {
      app->palette.selected =
          (app->palette.selected - 1 + app->palette.count) % app->palette.count;
      return;
    }
  }

  if (app->search_mode) {
    app->ignore_char = 1;
    if (key == GLFW_KEY_ESCAPE) {
      search_close(app);
      return;
    }
    if (key == GLFW_KEY_BACKSPACE) {
      search_utf8_delete_last(app->search_query);
      search_rebuild(app);
      return;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_F3 || key == GLFW_KEY_DOWN) {
      search_goto(app, (m & 2) ? -1 : 1);
      return;
    }
    if (key == GLFW_KEY_UP) {
      search_goto(app, -1);
      return;
    }
    /* Allow Ctrl-Shift-F to toggle search closed via keymap below */
    if (action == GLFW_PRESS) {
      TraashAction act = traash_keymap_lookup(&app->keymap, key, m);
      if (act == TRAASH_ACTION_SEARCH) {
        run_action(app, act);
        return;
      }
      /* Swallow other app chords while finding */
      if (act != TRAASH_ACTION_NONE) {
        return;
      }
    }
    /* Printable input is handled in char_cb; clear ignore for that path */
    if (!(m & (1 | 4 | 8)) && key != GLFW_KEY_ENTER && key != GLFW_KEY_F3 &&
        key != GLFW_KEY_UP && key != GLFW_KEY_DOWN && key != GLFW_KEY_BACKSPACE &&
        key != GLFW_KEY_ESCAPE) {
      app->ignore_char = 0;
    }
    return;
  }

  /* Keymap / leader: PRESS for chords; REPEAT for non-prefix motion (Ctrl-hjkl) */
  int awaiting = app->keymap.prefix_active;
  if (action == GLFW_REPEAT && is_leader) {
    return; /* don't leak ^B into the shell */
  }
  if (action == GLFW_REPEAT && awaiting) {
    return; /* wait for a real follow-up key */
  }

  if (action == GLFW_PRESS) {
    TraashAction act = traash_keymap_lookup(&app->keymap, key, m);
    /* Hard fallback: Ctrl-Shift-L always opens the layout picker if unbound. */
    if (act == TRAASH_ACTION_NONE && key == GLFW_KEY_L && (m & 1) && (m & 2) &&
        !(m & ~3)) {
      act = TRAASH_ACTION_LAYOUT_PICKER;
    }
    if (act == TRAASH_ACTION_NONE && key == GLFW_KEY_O && (m & 1) && (m & 2) &&
        !(m & ~3)) {
      act = TRAASH_ACTION_OVERVIEW;
    }
    if (act != TRAASH_ACTION_NONE) {
      run_action(app, act);
      /* Ctrl+letter does not generate char_cb; arming ignore would eat the next key. */
      if (!((m & 1) && !(m & 2) && key >= GLFW_KEY_A && key <= GLFW_KEY_Z)) {
        app->ignore_char = 1;
      }
      return;
    }
    if (is_leader || awaiting) {
      app->ignore_char = 1;
      return;
    }
  } else {
    /* REPEAT: non-prefix binds only */
    for (int i = 0; i < app->keymap.count; i++) {
      TraashKeyBind *b = &app->keymap.binds[i];
      if (!b->prefix && b->key == key && b->mods == m) {
        run_action(app, b->action);
        if (!((m & 1) && !(m & 2) && key >= GLFW_KEY_A && key <= GLFW_KEY_Z)) {
          app->ignore_char = 1;
        }
        return;
      }
    }
  }

  TraashPane *pane = traash_session_active_pane(app_session(app));
  if (!pane) {
    return;
  }
  if (!app->client_mode && pane->pty.master_fd < 0) {
    return;
  }

  /* xterm-style CSI / control sequences for keys terminals always expect */
  if (key == GLFW_KEY_ENTER) {
    app_write_pane(app, pane, (const uint8_t *)"\r", 1);
  } else if (key == GLFW_KEY_BACKSPACE) {
    app_write_pane(app, pane, (const uint8_t *)"\x7f", 1);
  } else if (key == GLFW_KEY_TAB) {
    app_write_pane(app, pane, (const uint8_t *)"\t", 1);
  } else if (key == GLFW_KEY_ESCAPE) {
    app_write_pane(app, pane, (const uint8_t *)"\x1b", 1);
  } else if (key == GLFW_KEY_DELETE) {
    app_write_pane(app, pane, (const uint8_t *)"\x1b[3~", 4);
  } else if (key == GLFW_KEY_INSERT) {
    app_write_pane(app, pane, (const uint8_t *)"\x1b[2~", 4);
  } else if (key == GLFW_KEY_HOME) {
    const char *seq = pane->vt.app_cursor ? "\x1bOH" : "\x1b[H";
    app_write_pane(app, pane, (const uint8_t *)seq, strlen(seq));
  } else if (key == GLFW_KEY_END) {
    const char *seq = pane->vt.app_cursor ? "\x1bOF" : "\x1b[F";
    app_write_pane(app, pane, (const uint8_t *)seq, strlen(seq));
  } else if (key == GLFW_KEY_PAGE_UP) {
    app_write_pane(app, pane, (const uint8_t *)"\x1b[5~", 4);
  } else if (key == GLFW_KEY_PAGE_DOWN) {
    app_write_pane(app, pane, (const uint8_t *)"\x1b[6~", 4);
  } else if (key == GLFW_KEY_UP) {
    const char *seq =
        (m & 1)   ? "\x1b[1;5A"
        : (m & 4) ? "\x1b[1;3A"
        : pane->vt.app_cursor ? "\x1bOA"
                              : "\x1b[A";
    app_write_pane(app, pane, (const uint8_t *)seq, strlen(seq));
  } else if (key == GLFW_KEY_DOWN) {
    const char *seq =
        (m & 1)   ? "\x1b[1;5B"
        : (m & 4) ? "\x1b[1;3B"
        : pane->vt.app_cursor ? "\x1bOB"
                              : "\x1b[B";
    app_write_pane(app, pane, (const uint8_t *)seq, strlen(seq));
  } else if (key == GLFW_KEY_RIGHT) {
    const char *seq =
        (m & 1)   ? "\x1b[1;5C"
        : (m & 4) ? "\x1b[1;3C"
        : pane->vt.app_cursor ? "\x1bOC"
                              : "\x1b[C";
    app_write_pane(app, pane, (const uint8_t *)seq, strlen(seq));
  } else if (key == GLFW_KEY_LEFT) {
    const char *seq =
        (m & 1)   ? "\x1b[1;5D"
        : (m & 4) ? "\x1b[1;3D"
        : pane->vt.app_cursor ? "\x1bOD"
                              : "\x1b[D";
    app_write_pane(app, pane, (const uint8_t *)seq, strlen(seq));
  } else if ((m & 1) && key == GLFW_KEY_BACKSLASH) {
    uint8_t c = 0x1c; /* Ctrl-\ = SIGQUIT */
    app_write_pane(app, pane, &c, 1);
  } else if ((m & 1) && key == GLFW_KEY_LEFT_BRACKET) {
    uint8_t c = 0x1b; /* Ctrl-[ = ESC */
    app_write_pane(app, pane, &c, 1);
  } else if ((m & 1) && key == GLFW_KEY_MINUS) {
    uint8_t c = 0x1f; /* Ctrl-_ / Ctrl-- */
    app_write_pane(app, pane, &c, 1);
  } else if ((m & 1) && !(m & 2) && key == GLFW_KEY_SLASH) {
    uint8_t c = 0x1f; /* Ctrl-/ often maps to same as Ctrl-_ */
    app_write_pane(app, pane, &c, 1);
  } else if ((m & 1) && !(m & 2) && key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
    /* Plain Ctrl+letter → shell (^C ^D ^Z …). Ctrl-Shift stays for app binds. */
    if (key == app->keymap.prefix_key &&
        (m & app->keymap.prefix_mods) == app->keymap.prefix_mods &&
        app->keymap.prefix_mods == 1) {
      return;
    }
    uint8_t c = (uint8_t)(key - GLFW_KEY_A + 1);
    app_write_pane(app, pane, &c, 1);
  } else if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F4) {
    char seq[8];
    int n = snprintf(seq, sizeof(seq), "\x1bO%c", 'P' + (key - GLFW_KEY_F1));
    app_write_pane(app, pane, (const uint8_t *)seq, (size_t)n);
  } else if (key >= GLFW_KEY_F5 && key <= GLFW_KEY_F12) {
    static const int fcodes[] = {15, 17, 18, 19, 20, 21, 23, 24};
    char seq[16];
    int n = snprintf(seq, sizeof(seq), "\x1b[%d~", fcodes[key - GLFW_KEY_F5]);
    app_write_pane(app, pane, (const uint8_t *)seq, (size_t)n);
  }
  (void)w;
}

static void char_cb(GLFWwindow *w, unsigned int codepoint) {
  (void)w;
  TraashApp *app = g_app;
  if (!app || app->palette.open || app->demo.active || app->shortcuts.open ||
      app->overview.open || app->quit_confirm.open) {
    return;
  }
  if (app->layouts.open) {
    if (app->layouts.save_mode) {
      if (app->ignore_char) {
        app->ignore_char = 0;
      }
      char ch = 0;
      if (codepoint >= 'a' && codepoint <= 'z') {
        ch = (char)codepoint;
      } else if (codepoint >= 'A' && codepoint <= 'Z') {
        ch = (char)codepoint;
      } else if (codepoint >= '0' && codepoint <= '9') {
        ch = (char)codepoint;
      } else if (codepoint == '-' || codepoint == '_') {
        ch = (char)codepoint;
      } else {
        return;
      }
      size_t n = strlen(app->layouts.save_name);
      if (n + 1 < sizeof(app->layouts.save_name)) {
        app->layouts.save_name[n] = ch;
        app->layouts.save_name[n + 1] = 0;
      }
      return;
    }
    return;
  }
  if (app->search_mode) {
    if (app->ignore_char) {
      app->ignore_char = 0;
      return;
    }
    if (codepoint < 32 || codepoint == 127) {
      return;
    }
    uint8_t enc[4];
    int n = 0;
    if (codepoint < 0x80) {
      enc[0] = (uint8_t)codepoint;
      n = 1;
    } else if (codepoint < 0x800) {
      enc[0] = (uint8_t)(0xC0 | (codepoint >> 6));
      enc[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
      n = 2;
    } else if (codepoint < 0x10000) {
      enc[0] = (uint8_t)(0xE0 | (codepoint >> 12));
      enc[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
      enc[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
      n = 3;
    } else {
      enc[0] = (uint8_t)(0xF0 | (codepoint >> 18));
      enc[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
      enc[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
      enc[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
      n = 4;
    }
    size_t cur = strlen(app->search_query);
    if (cur + (size_t)n < sizeof(app->search_query)) {
      memcpy(app->search_query + cur, enc, (size_t)n);
      app->search_query[cur + (size_t)n] = 0;
      search_rebuild(app);
    }
    return;
  }
  if (app->ignore_char) {
    app->ignore_char = 0;
    return;
  }
  /* While waiting for a tmux-style chord, don't leak the follow-up char */
  if (app->keymap.prefix_active) {
    return;
  }
  TraashPane *pane = traash_session_active_pane(app_session(app));
  if (!pane) {
    return;
  }
  if (!app->client_mode && pane->pty.master_fd < 0) {
    return;
  }
  uint8_t buf[4];
  int n = 0;
  if (codepoint < 0x80) {
    buf[0] = (uint8_t)codepoint;
    n = 1;
  } else if (codepoint < 0x800) {
    buf[0] = (uint8_t)(0xC0 | (codepoint >> 6));
    buf[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
    n = 2;
  } else if (codepoint < 0x10000) {
    buf[0] = (uint8_t)(0xE0 | (codepoint >> 12));
    buf[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
    buf[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
    n = 3;
  } else {
    buf[0] = (uint8_t)(0xF0 | (codepoint >> 18));
    buf[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
    buf[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
    buf[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
    n = 4;
  }
  app_write_pane(app, pane, buf, (size_t)n);
}

static void scroll_cb(GLFWwindow *w, double xoff, double yoff) {
  (void)w;
  (void)xoff;
  TraashApp *app = g_app;
  if (!app || app->overview.open || app->shortcuts.open || app->layouts.open ||
      app->quit_confirm.open) {
    return;
  }
  TraashPane *pane = traash_session_active_pane(app->mux.attached);
  if (!pane || pane->screen.alt_screen) {
    return;
  }
  /* GLFW: positive yoff = scroll up → older history (higher offset). */
  pane->screen.scroll_offset += (int)(yoff * 3);
  if (pane->screen.scroll_offset < 0) {
    pane->screen.scroll_offset = 0;
  }
  if (pane->screen.scroll_offset > pane->screen.scrollback_len) {
    pane->screen.scroll_offset = pane->screen.scrollback_len;
  }
}

static void mouse_button_cb(GLFWwindow *w, int button, int action, int mods) {
  (void)w;
  (void)mods;
  TraashApp *app = g_app;
  if (!app) {
    return;
  }
  double fx, fy;
  cursor_to_framebuffer(app, &fx, &fy);

  if (app->quit_confirm.open && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    float scale = app->renderer.content_scale > 0.1f ? app->renderer.content_scale : 1.0f;
    int btn = traash_quit_confirm_hit_button(&app->quit_confirm, (float)fx, (float)fy,
                                            app->renderer.fb_w, app->renderer.fb_h, scale,
                                            app->renderer.font.cell_h);
    if (btn == 0) {
      cancel_app_quit(app);
      return;
    }
    if (btn == 1) {
      confirm_app_quit(app);
      return;
    }
    if (traash_quit_confirm_hit_backdrop(&app->quit_confirm, (float)fx, (float)fy,
                                         app->renderer.fb_w, app->renderer.fb_h, scale,
                                         app->renderer.font.cell_h)) {
      cancel_app_quit(app);
    }
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
    if (app->shortcuts.open || app->overview.open || app->layouts.open) {
      return;
    }
    float scale = app->renderer.content_scale > 0.1f ? app->renderer.content_scale : 1.0f;
    float mw = traash_context_menu_width(scale);
    float mh = traash_context_menu_height(scale);
    float mx = (float)fx;
    float my = (float)fy;
    if (mx + mw > (float)app->renderer.fb_w) {
      mx = (float)app->renderer.fb_w - mw - 4.0f * scale;
    }
    if (my + mh > (float)app->renderer.fb_h) {
      my = (float)app->renderer.fb_h - mh - 4.0f * scale;
    }
    if (mx < 4.0f * scale) {
      mx = 4.0f * scale;
    }
    if (my < 4.0f * scale) {
      my = 4.0f * scale;
    }
    traash_context_menu_open(&app->menu, mx, my);
    app->menu.hover = traash_context_menu_hit(&app->menu, (float)fx, (float)fy, scale);
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
    /* Middle-click tab = close (browser-style); elsewhere = paste. */
    TraashTabHit th;
    if (traash_renderer_tab_hit(&app->renderer, app->mux.attached, (float)fx, (float)fy, &th) &&
        (th.kind == TRAASH_TAB_HIT_TAB || th.kind == TRAASH_TAB_HIT_CLOSE) && th.window) {
      TraashSession *sess = app->mux.attached;
      if (sess) {
        traash_session_close_window(sess, th.window);
        if (!sess->windows) {
          traash_session_new_window(sess, NULL, 80, 24);
        }
      }
      return;
    }
    if (app->shortcuts.open || app->menu.open || app->overview.open) {
      return;
    }
    TraashHit hit;
    if (traash_renderer_hit_test(&app->renderer, app->mux.attached, (float)fx, (float)fy,
                                 &hit) &&
        hit.pane) {
      if (app->mux.attached && app->mux.attached->active) {
        app->mux.attached->active->active = hit.pane;
      }
    }
    do_paste_primary(app);
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    if (app->overview.open) {
      float scale = app->renderer.content_scale > 0.1f ? app->renderer.content_scale : 1.0f;
      TraashOverviewHit hit = traash_overview_overlay_hit(
          &app->overview, app->mux.attached, (float)fx, (float)fy, app->renderer.fb_w,
          app->renderer.fb_h, scale, app->renderer.font.cell_h);
      if (hit.kind == TRAASH_OVERVIEW_HIT_BACKDROP) {
        traash_overview_overlay_close(&app->overview);
        return;
      }
      if (hit.kind == TRAASH_OVERVIEW_HIT_CLOSE) {
        if (app->overview.mode == TRAASH_OVERVIEW_PANES) {
          request_close_pane(app, hit.window, hit.pane);
        } else {
          request_close_window(app, hit.window);
        }
        return;
      }
      if (hit.kind == TRAASH_OVERVIEW_HIT_CARD) {
        app->overview.selected = hit.index;
        overview_activate_selected(app);
      }
      return;
    }
    if (app->shortcuts.open) {
      float scale = app->renderer.content_scale > 0.1f ? app->renderer.content_scale : 1.0f;
      if (traash_shortcuts_overlay_hit_backdrop(&app->shortcuts, (float)fx, (float)fy,
                                               app->renderer.fb_w, app->renderer.fb_h, scale,
                                               app->renderer.font.cell_h)) {
        traash_shortcuts_overlay_close(&app->shortcuts);
      }
      return;
    }
    if (app->layouts.open) {
      float scale = app->renderer.content_scale > 0.1f ? app->renderer.content_scale : 1.0f;
      if (traash_layout_overlay_hit_backdrop(&app->layouts, (float)fx, (float)fy,
                                             app->renderer.fb_w, app->renderer.fb_h, scale,
                                             app->renderer.font.cell_h)) {
        traash_layout_overlay_close(&app->layouts);
        return;
      }
      int row = traash_layout_overlay_hit_row(&app->layouts, (float)fx, (float)fy,
                                              app->renderer.fb_w, app->renderer.fb_h, scale,
                                              app->renderer.font.cell_h);
      if (row >= 0) {
        app->layouts.selected = row;
        layout_apply_selected(app);
      }
      return;
    }
    if (app->menu.open) {
      int hit = traash_context_menu_hit(&app->menu, (float)fx, (float)fy,
                                       app->renderer.content_scale);
      traash_context_menu_close(&app->menu);
      if (hit == TRAASH_MENU_COPY) {
        do_copy(app);
      } else if (hit == TRAASH_MENU_PASTE) {
        do_paste(app);
      } else if (hit == TRAASH_MENU_SPLIT_H) {
        run_action(app, TRAASH_ACTION_SPLIT_H);
      } else if (hit == TRAASH_MENU_SPLIT_V) {
        run_action(app, TRAASH_ACTION_SPLIT_V);
      } else if (hit == TRAASH_MENU_SETTINGS) {
        if (!traash_settings_window_is_open(&app->settings_win)) {
          traash_settings_window_open(&app->settings_win, app->window, &app->cfg,
                                      app->cfg.lua);
        }
      }
      return;
    }

    /* Tab bar: select / close / new */
    {
      TraashTabHit th;
      if (traash_renderer_tab_hit(&app->renderer, app->mux.attached, (float)fx, (float)fy,
                                  &th)) {
        TraashSession *sess = app->mux.attached;
        if (th.kind == TRAASH_TAB_HIT_NEW) {
          run_action(app, TRAASH_ACTION_NEW_WINDOW);
        } else if (th.kind == TRAASH_TAB_HIT_TAB && th.window && sess) {
          traash_session_select_window(sess, th.window);
        } else if (th.kind == TRAASH_TAB_HIT_CLOSE && th.window && sess) {
          traash_session_close_window(sess, th.window);
          if (!sess->windows) {
            traash_session_new_window(sess, NULL, 80, 24);
          }
        }
        return;
      }
    }

    TraashHit hit;
    if (traash_renderer_hit_test(&app->renderer, app->mux.attached, (float)fx, (float)fy,
                                 &hit) &&
        hit.pane) {
      if (app->mux.attached && app->mux.attached->active) {
        app->mux.attached->active->active = hit.pane;
      }
      /* Clear selection on other panes */
      if (app->mux.attached && app->mux.attached->active) {
        for (TraashPane *p = app->mux.attached->active->panes; p; p = p->next) {
          if (p != hit.pane) {
            traash_selection_clear(&p->selection);
          }
        }
      }
      double now = glfwGetTime();
      int is_double =
          (now - app->last_click_time) < 0.45 && app->last_click_pane == hit.pane &&
          app->last_click_x == hit.cell_x && app->last_click_y == hit.cell_y;
      app->last_click_time = now;
      app->last_click_x = hit.cell_x;
      app->last_click_y = hit.cell_y;
      app->last_click_pane = hit.pane;
      if (is_double) {
        /* Select to separators: /, ., :, whitespace, punctuation, … */
        traash_selection_select_word(&hit.pane->selection, &hit.pane->screen, hit.cell_x,
                                     hit.cell_y);
        app->dragging = 0;
        app->drag_pane = NULL;
        if (hit.pane->selection.active) {
          do_copy(app);
        }
        /* Reset so a third click starts fresh instead of another "double" */
        app->last_click_time = 0;
        return;
      }
      traash_selection_begin(&hit.pane->selection, hit.cell_x, hit.cell_y);
      app->dragging = 1;
      app->drag_pane = hit.pane;
    }
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
    TraashPane *pane = app->drag_pane ? app->drag_pane
                                      : traash_session_active_pane(app->mux.attached);
    if (pane && app->dragging) {
      TraashHit hit;
      if (traash_renderer_hit_test(&app->renderer, app->mux.attached, (float)fx, (float)fy,
                                   &hit)) {
        int cx = hit.cell_x;
        int cy = hit.cell_y;
        if (hit.pane != pane) {
          /* Clamp to edge of drag pane when leaving it */
          cx = hit.cell_x;
          cy = hit.cell_y;
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
        }
        traash_selection_update(&pane->selection, cx, cy);
      }
      traash_selection_end(&pane->selection);
      if (pane->selection.active &&
          !(pane->selection.ax == pane->selection.bx &&
            pane->selection.ay == pane->selection.by)) {
        do_copy(app);
      }
    }
    app->dragging = 0;
    app->drag_pane = NULL;
  }
}

static void cursor_pos_cb(GLFWwindow *w, double x, double y) {
  (void)x;
  (void)y;
  TraashApp *app = g_app;
  if (!app) {
    return;
  }
  double fx, fy;
  cursor_to_framebuffer(app, &fx, &fy);

  if (app->menu.open) {
    app->menu.hover =
        traash_context_menu_hit(&app->menu, (float)fx, (float)fy, app->renderer.content_scale);
    set_mouse_cursor(app, w, 0);
  } else if (app->settings.open) {
    set_mouse_cursor(app, w, 0);
  } else {
    TraashHit hit;
    int over_term = traash_renderer_hit_test(&app->renderer, app->mux.attached, (float)fx,
                                            (float)fy, &hit);
    set_mouse_cursor(app, w, over_term);
  }

  if (!app->dragging || !app->drag_pane) {
    return;
  }

  TraashPane *pane = app->drag_pane;
  int cw = app->renderer.font.cell_w;
  int ch = app->renderer.font.cell_h;
  if (cw < 1 || ch < 1) {
    return;
  }
  int term_top = app->renderer.tab_h;
  int term_h = app->renderer.fb_h - app->renderer.tab_h - app->renderer.status_h;
  float px = (float)pane->x * (float)app->renderer.fb_w;
  float py = (float)term_top + (float)pane->y * (float)term_h;
  int nx = (int)floor(((float)fx - px - 2.0f) / (float)cw);
  int ny = (int)floor(((float)fy - py - 2.0f) / (float)ch);
  if (nx < 0) {
    nx = 0;
  }
  if (ny < 0) {
    ny = 0;
  }
  if (nx >= pane->screen.cols) {
    nx = pane->screen.cols - 1;
  }
  if (ny >= pane->screen.rows) {
    ny = pane->screen.rows - 1;
  }
  traash_selection_update(&pane->selection, nx, ny);
}

static float detect_content_scale(GLFWwindow *window) {
  float xscale = 1.0f, yscale = 1.0f;
  glfwGetWindowContentScale(window, &xscale, &yscale);
  float scale = xscale > yscale ? xscale : yscale;
  if (scale < 0.5f) {
    scale = 0.5f;
  }

  /* Fallback only when GLFW reports ~1x but the framebuffer is clearly HiDPI.
   * Never take max(scale, ratio) — that sticks at 2x after monitor moves when
   * content-scale drops to 1 while fb/window is briefly still high. */
  int win_w = 0, win_h = 0, fb_w = 0, fb_h = 0;
  glfwGetWindowSize(window, &win_w, &win_h);
  glfwGetFramebufferSize(window, &fb_w, &fb_h);
  if (win_w > 0 && win_h > 0 && fb_w > 0 && fb_h > 0) {
    float rx = (float)fb_w / (float)win_w;
    float ry = (float)fb_h / (float)win_h;
    float ratio = rx > ry ? rx : ry;
    if (scale < 1.05f && ratio > 1.25f) {
      scale = ratio;
    }
  }
  if (scale > 4.0f) {
    scale = 4.0f;
  }
  return scale;
}

static void apply_content_scale(TraashApp *app) {
  if (!app || !app->window) {
    return;
  }
  traash_renderer_set_content_scale(&app->renderer, detect_content_scale(app->window));
}

/* Compositor still sending configure/refresh for a live drag. */
static int resizing_now(const TraashApp *app) {
  if (!app || app->last_fb_change_at <= 0.0) {
    return 0;
  }
  double dt = glfwGetTime() - app->last_fb_change_at;
  return dt >= 0.0 && dt < 0.40;
}

static int g_presenting;

/* Draw + swap. Safe to call from framebuffer/refresh callbacks (X11/Wayland
 * block the main loop during interactive resize; without this the UI freezes).
 * fast_resize: skip per-cell glyphs (chrome + pane fills only). */
static void present_frame_ex(TraashApp *app, int fast_resize) {
  if (!app || !app->window || g_presenting) {
    return;
  }
  g_presenting = 1;

  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(app->window, &fbw, &fbh);
  if (fbw < 1) {
    fbw = 1;
  }
  if (fbh < 1) {
    fbh = 1;
  }
  traash_renderer_resize(&app->renderer, fbw, fbh);

  double now = glfwGetTime();
  TraashStatusModel draw_status = app->status;
  if (!fast_resize) {
    if (app->keymap.prefix_active) {
      int found = 0;
      for (int i = 0; i < draw_status.count; i++) {
        if (strcmp(draw_status.segs[i].text, " PREFIX ") == 0) {
          found = 1;
          break;
        }
      }
      if (!found && draw_status.count < 32) {
        TraashStatusSegment *seg = &draw_status.segs[draw_status.count++];
        snprintf(seg->text, sizeof(seg->text), " PREFIX ");
        snprintf(seg->align, sizeof(seg->align), "right");
        seg->fg = 0x1a1a1e;
        seg->bg = 0x33d17a;
      }
    }
    if (app->palette.open) {
      memset(&draw_status, 0, sizeof(draw_status));
      draw_status.count = 1;
      snprintf(draw_status.segs[0].text, sizeof(draw_status.segs[0].text),
               "palette> %s  [%s]", app->palette.query,
               traash_action_name(app->palette.actions[app->palette.selected]));
      snprintf(draw_status.segs[0].align, sizeof(draw_status.segs[0].align), "left");
      draw_status.segs[0].fg = app->theme.status_bar_fg;
      draw_status.segs[0].bg = app->theme.status_bar_bg;
    } else if (app->search_mode) {
      memset(&draw_status, 0, sizeof(draw_status));
      draw_status.count = 1;
      if (app->search_query[0] == 0) {
        snprintf(draw_status.segs[0].text, sizeof(draw_status.segs[0].text),
                 "Find: _  Enter next  Shift+Enter prev  Esc");
      } else if (app->search_hit_count <= 0) {
        snprintf(draw_status.segs[0].text, sizeof(draw_status.segs[0].text),
                 "Find: %.80s  (no matches)", app->search_query);
      } else {
        snprintf(draw_status.segs[0].text, sizeof(draw_status.segs[0].text),
                 "Find: %.64s  [%d/%d]", app->search_query, app->search_hit_index + 1,
                 app->search_hit_count);
      }
      snprintf(draw_status.segs[0].align, sizeof(draw_status.segs[0].align), "left");
      draw_status.segs[0].fg = 0x0d1117;
      draw_status.segs[0].bg = 0xe3b341;
    } else if (app->demo.active) {
      memset(&draw_status, 0, sizeof(draw_status));
      draw_status.count = 1;
      snprintf(draw_status.segs[0].text, sizeof(draw_status.segs[0].text),
               "DEMO step %d  (n/p/q)  | %s", app->demo.step, app->status_line);
      snprintf(draw_status.segs[0].align, sizeof(draw_status.segs[0].align), "left");
      draw_status.segs[0].fg = app->theme.status_bar_fg;
      draw_status.segs[0].bg = app->theme.status_bar_bg;
    }
  }

  TraashSearchDraw search_draw = {0};
  if (!fast_resize) {
    TraashPane *search_pane = traash_session_active_pane(app->mux.attached);
    if (app->search_mode && search_pane && app->search_hit_count > 0) {
      search_draw.active = 1;
      search_draw.pane = search_pane;
      search_draw.matches = app->search_hits;
      search_draw.match_count = app->search_hit_count;
      search_draw.current = app->search_hit_index;
      search_draw.match_len = app->search_match_len;
    }
  }

  traash_renderer_draw_ex(&app->renderer, app_session(app), &app->theme, &draw_status, now,
                          &app->menu, &app->shortcuts, &app->layouts, &app->overview,
                          &app->quit_confirm, &app->keymap, &search_draw, fast_resize);
  glfwSwapBuffers(app->window);
  g_presenting = 0;
}

static void present_frame(TraashApp *app) {
  /* Refresh/content-scale callbacks must not full-redraw mid-drag. */
  present_frame_ex(app, resizing_now(app));
}

static void present_resize_frame(TraashApp *app) {
  if (!app) {
    return;
  }
  double now = glfwGetTime();
  /* libdecor may block the main loop during a drag, so callbacks must commit
   * frames. Cap them to compositor cadence instead of swapping per event. */
  if (app->last_resize_present_at > 0.0 &&
      now - app->last_resize_present_at < (1.0 / 60.0)) {
    return;
  }
  present_frame_ex(app, 1);
  app->last_resize_present_at = now;
}

static void content_scale_cb(GLFWwindow *w, float xscale, float yscale) {
  (void)xscale;
  (void)yscale;
  TraashApp *app = g_app;
  if (!app || app->window != w) {
    return;
  }
  /* Never rebuild the font atlas or swap while GLFW is dispatching Wayland
   * configure events. The main loop applies this after the event batch. */
  app->content_scale_pending = 1;
}

static void framebuffer_size_cb(GLFWwindow *w, int width, int height) {
  TraashApp *app = g_app;
  if (!app || app->window != w) {
    return;
  }
  double now = glfwGetTime();
  app->last_fb_change_at = now;
  app->resize_fast_pending = 1;
  if (width < 1) {
    width = 1;
  }
  if (height < 1) {
    height = 1;
  }
  traash_renderer_resize(&app->renderer, width, height);
  /* Wayland/libdecor can hold the main loop during an interactive drag.
   * Commit only a throttled chrome-only frame from this callback. */
  present_resize_frame(app);
}

static void window_refresh_cb(GLFWwindow *w) {
  TraashApp *app = g_app;
  if (!app || app->window != w) {
    return;
  }
  if (resizing_now(app)) {
    present_resize_frame(app);
  } else {
    present_frame_ex(app, 0);
  }
}

static void window_close_cb(GLFWwindow *w) {
  TraashApp *app = g_app;
  if (!app || app->window != w) {
    return;
  }
  /* GLFW sets should-close before this runs — clear it until the user confirms. */
  glfwSetWindowShouldClose(w, GLFW_FALSE);
  request_app_quit(app);
}

static void focus_cb(GLFWwindow *w, int focused) {
  TraashApp *app = g_app;
  if (!app || app->window != w) {
    return;
  }
  app->window_focused = focused;
  TraashPane *pane = traash_session_active_pane(app_session(app));
  if (!pane || !pane->screen.focus_report) {
    return;
  }
  if (!app->client_mode && pane->pty.master_fd < 0) {
    return;
  }
  const char *seq = focused ? "\033[I" : "\033[O";
  app_write_pane(app, pane, (const uint8_t *)seq, 3);
}

int traash_app_run(const TraashCli *cli) {
  TraashApp app;
  memset(&app, 0, sizeof(app));
  g_app = &app;
  app.theme_names = THEMES;
  app.theme_count = (int)(sizeof(THEMES) / sizeof(THEMES[0]));
  app.status_names = STATUSES;
  app.status_count = (int)(sizeof(STATUSES) / sizeof(STATUSES[0]));

  if (traash_config_init(&app.cfg) != 0) {
    return 1;
  }
  traash_config_load(&app.cfg);
  traash_keymap_init_defaults(&app.keymap);
  traash_config_apply_keys(&app.cfg, &app.keymap);
  traash_palette_init(&app.palette);

  if (cli->attach && (cli->host || traash_mux_server_running())) {
    if (cli->host) {
      if (traash_mux_connect_tcp(cli->host, cli->port, &app.client) != 0) {
        TRAASH_LOGE("tcp connect failed");
        return 1;
      }
    } else {
      char runtime[400];
      char sock[512];
      if (traash_runtime_dir(runtime, sizeof(runtime)) != 0) {
        TRAASH_LOGE("runtime dir failed");
        return 1;
      }
      snprintf(sock, sizeof(sock), "%s/mux.sock", runtime);
      if (traash_mux_connect_unix(sock, &app.client) != 0) {
        TRAASH_LOGE("mux connect failed");
        return 1;
      }
    }
    int role = TRAASH_ROLE_WRITE;
    if (traash_mux_client_auth(&app.client, cli->attach, cli->password, cli->read_only,
                               &role) != 0) {
      TRAASH_LOGE("authentication failed");
      traash_mux_client_close(&app.client);
      return 1;
    }
    app.client_mode = 1;
    app.client_role = role;
    app.mux.attached = app.client.session;
    app.mux.cols = 120;
    app.mux.rows = 40;
  } else {
    if (traash_mux_init(&app.mux, 120, 40) != 0) {
      TRAASH_LOGE("mux init failed");
      return 1;
    }
    if (!traash_mux_server_running()) {
      traash_mux_start_listener(&app.mux);
    }
    if (cli->attach) {
      if (traash_session_file_exists(cli->attach) && cli->password[0]) {
        TraashSession *loaded = NULL;
        int role = TRAASH_ROLE_NONE;
        if (traash_snapshot_load_encrypted(cli->attach, cli->password, cli->read_only,
                                           &loaded, &role) != 0) {
          TRAASH_LOGE("failed to unlock encrypted session");
          traash_mux_shutdown(&app.mux);
          return 1;
        }
        loaded->encrypted = 1;
        loaded->dek_valid = role == TRAASH_ROLE_WRITE;
        loaded->pw_cached = role == TRAASH_ROLE_WRITE;
        if (loaded->pw_cached) {
          snprintf(loaded->write_pw, sizeof(loaded->write_pw), "%s", cli->password);
        }
        loaded->next = app.mux.sessions;
        app.mux.sessions = loaded;
        app.mux.attached = loaded;
        if (cli->read_only) {
          app.client_role = TRAASH_ROLE_READ;
        }
      } else {
        traash_mux_attach(&app.mux, cli->attach);
      }
    } else if (cli->create && cli->encrypt) {
      traash_mux_create_encrypted_session(&app.mux, cli->create, cli->write_pw, cli->read_pw);
      traash_mux_attach(&app.mux, cli->create);
    } else if (app.cfg.default_layout[0]) {
      apply_named_layout(&app, app.cfg.default_layout);
    }
  }
  apply_theme(&app, app.cfg.theme);
  for (int i = 0; i < app.theme_count; i++) {
    if (strcmp(THEMES[i], app.cfg.theme) == 0) {
      app.theme_index = i;
    }
  }
  traash_plugins_init(&app.plugins, &app.cfg);
  traash_plugins_set_mux(&app.plugins, &app.mux);
  traash_plugins_set_theme(&app.plugins, app.cfg.theme);

  if (!glfwInit()) {
    TRAASH_LOGE("glfwInit failed");
    return 1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  /* Antialias vector chrome (rounded tabs, panels, icons, thin details). */
  glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  /* Needed for opacity on Wayland (glfwSetWindowOpacity is unavailable there). */
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
#ifdef GLFW_WAYLAND_APP_ID
  /* Matches assets/linux/sh.traa.traash.desktop Icon= / StartupWMClass= */
  glfwWindowHintString(GLFW_WAYLAND_APP_ID, "sh.traa.traash");
#endif
  app.window = glfwCreateWindow(1100, 720, "traa.sh", NULL, NULL);
  if (!app.window) {
    TRAASH_LOGE("window create failed");
    glfwTerminate();
    return 1;
  }
  traash_window_set_icon(app.window);
  glfwMakeContextCurrent(app.window);
  /* 0 = lower input latency; we pace the loop ourselves (~120 Hz). */
  glfwSwapInterval(0);
  app.cursor_ibeam = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
  app.cursor_arrow = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
  app.cursor_shape = -1;
  if (!traash_gl_load()) {
    TRAASH_LOGE("GL load failed");
    return 1;
  }
  traash_glEnable(GL_MULTISAMPLE);
  if (traash_renderer_init(&app.renderer, app.cfg.font_family, (int)app.cfg.font_size) !=
      0) {
    TRAASH_LOGE("renderer init failed");
    return 1;
  }
  app.renderer.cursor_style = app.cfg.cursor_style;
  app.renderer.fb_transparent =
      glfwGetWindowAttrib(app.window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;
  apply_content_scale(&app);
  {
    float opacity = app.cfg.opacity;
    if (opacity < 0.3f) {
      opacity = 0.3f;
    }
    if (opacity > 1.0f) {
      opacity = 1.0f;
    }
    app.renderer.opacity = opacity;
    if (!app.renderer.fb_transparent) {
      glfwSetWindowOpacity(app.window, opacity);
    }
  }
  glfwSetKeyCallback(app.window, key_cb);
  glfwSetCharCallback(app.window, char_cb);
  glfwSetScrollCallback(app.window, scroll_cb);
  glfwSetMouseButtonCallback(app.window, mouse_button_cb);
  glfwSetCursorPosCallback(app.window, cursor_pos_cb);
  glfwSetWindowContentScaleCallback(app.window, content_scale_cb);
  glfwSetFramebufferSizeCallback(app.window, framebuffer_size_cb);
  glfwSetWindowRefreshCallback(app.window, window_refresh_cb);
  glfwSetWindowCloseCallback(app.window, window_close_cb);
  glfwSetWindowFocusCallback(app.window, focus_cb);
  app.window_focused = glfwGetWindowAttrib(app.window, GLFW_FOCUSED);

  if (cli->demo) {
    traash_demo_start(&app.demo, &app.mux, app.cfg.lua, cli->demo_auto);
  }

  double last_status = 0;
  const double frame_budget = 1.0 / 120.0; /* soft cap; keeps typing snappy without pegging CPU */
  while (!glfwWindowShouldClose(app.window)) {
    double frame_start = glfwGetTime();

    /* Input first so keystrokes reach the PTY before we read/draw this frame */
    glfwPollEvents();

    int fbw, fbh;
    glfwGetFramebufferSize(app.window, &fbw, &fbh);
    int size_changed = (fbw != app.renderer.fb_w || fbh != app.renderer.fb_h);
    if (size_changed) {
      traash_renderer_resize(&app.renderer, fbw, fbh);
      app.last_fb_change_at = glfwGetTime();
      app.resize_fast_pending = 1;
    }

    /* Read shell echo/output after handling input */
    int interactive_resize = resizing_now(&app);
    if (app.client_mode) {
      if (traash_mux_client_poll(&app.client) != 0) {
        glfwSetWindowShouldClose(app.window, GLFW_TRUE);
      }
      app.mux.attached = app.client.session;
    } else if (!interactive_resize) {
      traash_mux_poll_ex(&app.mux, 0);
      if (!app.client_mode) {
        traash_mux_flush_encrypted(&app.mux, glfwGetTime());
      }
    }

    if (!interactive_resize && app.content_scale_pending) {
      app.content_scale_pending = 0;
      apply_content_scale(&app);
    }

    if (!app_session(&app)) {
      /* Last shell exited (e.g. Ctrl-D) — close the window like other terminals */
      glfwSetWindowShouldClose(app.window, GLFW_TRUE);
    }

    /* Plugin hooks driven by terminal state */
    {
      TraashSession *sess = app.mux.attached;
      if (sess) {
        for (TraashWindow *w = sess->windows; w; w = w->next) {
          for (TraashPane *p = w->panes; p; p = p->next) {
            int bell = p->screen.bell_pending;
            int done = p->screen.command_done_pending;
            int activity = p->screen.activity_pending;
            if (!bell && !done && !activity) {
              continue;
            }
            p->screen.bell_pending = 0;
            p->screen.command_done_pending = 0;
            p->screen.activity_pending = 0;
            /* Badge inactive tabs on output / BEL / OSC 133;D (e.g. sleep finishes → prompt). */
            if (w != sess->active && (bell || done || activity)) {
              w->attention = 1;
            }
            if (bell && !app.window_focused) {
              traash_plugins_emit(&app.plugins, "on_bell");
            }
            if (done) {
              traash_plugins_emit(&app.plugins, "on_command_finished");
            }
          }
        }
        TraashPane *ap = traash_session_active_pane(sess);
        int pid = ap ? ap->id : -1;
        if (pid != app.plugins.last_focus_pane_id) {
          app.plugins.last_focus_pane_id = pid;
          traash_plugins_emit_str(&app.plugins, "on_pane_focus",
                                  ap && ap->title[0] ? ap->title : "");
        }
      }
      if (app.plugins.reload_theme) {
        app.plugins.reload_theme = 0;
        apply_theme(&app, app.cfg.theme);
        refresh_status(&app);
      }
      if (app.plugins.pending_action[0]) {
        TraashAction a = traash_action_from_name(app.plugins.pending_action);
        app.plugins.pending_action[0] = 0;
        if (a != TRAASH_ACTION_NONE) {
          run_action(&app, a);
        }
      }
    }

    double now = glfwGetTime();
    traash_demo_update(&app.demo, &app.mux, app.cfg.lua, now);
    /* Settle deferred PTY resizes after interactive window drags */
    if (!interactive_resize && app.mux.attached) {
      for (TraashWindow *w = app.mux.attached->windows; w; w = w->next) {
        for (TraashPane *p = w->panes; p; p = p->next) {
          traash_pane_flush_pty_resize(p, now);
        }
      }
    }
    if (!interactive_resize && now - last_status > 0.5) {
      refresh_status(&app);
      last_status = now;
      traash_plugins_emit(&app.plugins, "on_tick");
    }
    if (traash_settings_window_is_open(&app.settings_win)) {
      /* Settings UI needs a frame before we present the terminal. */
      present_frame(&app);
      traash_settings_window_frame(&app.settings_win, app.window, &app.cfg, &app.keymap,
                                   app.cfg.lua, app.theme_names, app.theme_count,
                                   app.status_names, app.status_count);
      if (app.settings_win.apply_pending) {
        apply_settings_live(&app);
        refresh_status(&app);
        app.settings_win.apply_pending = 0;
        if (app.settings_win.theme_dirty == 1) {
          memcpy(&app.theme, &app.settings_win.preview, sizeof(app.theme));
          app.settings_win.theme_dirty = 0;
        }
      }
      if (app.settings_win.save_pending) {
        if (traash_config_save(&app.cfg, &app.keymap) == 0) {
          snprintf(app.settings_win.status_msg, sizeof(app.settings_win.status_msg),
                   "Saved ~/.config/traash/config.lua");
        } else {
          snprintf(app.settings_win.status_msg, sizeof(app.settings_win.status_msg),
                   "Failed to save config");
        }
        app.settings_win.save_pending = 0;
      }
    } else if (interactive_resize) {
      present_resize_frame(&app);
    } else {
      if (app.resize_fast_pending) {
        app.resize_fast_pending = 0;
      }
      int hold_present = 0;
      TraashPane *ap = traash_session_active_pane(app.mux.attached);
      if (ap && ap->screen.sync_output) {
        if (app.sync_hold_since <= 0.0) {
          app.sync_hold_since = now;
        }
        if (now - app.sync_hold_since < 0.12) {
          hold_present = 1;
        } else {
          ap->screen.sync_output = false;
          app.sync_hold_since = 0.0;
        }
      } else {
        app.sync_hold_since = 0.0;
      }
      if (!hold_present) {
        present_frame_ex(&app, 0);
      }
    }

    double elapsed = glfwGetTime() - frame_start;
    double rem = frame_budget - elapsed;
    /* Don't sleep while the user is dragging the window — keeps resize fluid. */
    if (!interactive_resize && rem > 0.0004) {
      struct timespec ts;
      ts.tv_sec = (time_t)rem;
      ts.tv_nsec = (long)((rem - (double)ts.tv_sec) * 1e9);
      if (ts.tv_nsec < 0) {
        ts.tv_nsec = 0;
      }
      nanosleep(&ts, NULL);
    }
  }

  traash_settings_window_close(&app.settings_win);
  traash_renderer_free(&app.renderer);
  if (app.cursor_ibeam) {
    glfwDestroyCursor(app.cursor_ibeam);
  }
  if (app.cursor_arrow) {
    glfwDestroyCursor(app.cursor_arrow);
  }
  glfwDestroyWindow(app.window);
  glfwTerminate();
  traash_plugins_shutdown(&app.plugins);
  if (app.client_mode) {
    traash_mux_client_close(&app.client);
  } else {
    traash_mux_shutdown(&app.mux);
  }
  traash_config_shutdown(&app.cfg);
  g_app = NULL;
  return 0;
}
