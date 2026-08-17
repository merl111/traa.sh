#ifndef TRAASH_SETTINGS_WINDOW_H
#define TRAASH_SETTINGS_WINDOW_H

#include "config/config.h"
#include "config/theme.h"
#include "font/font.h"
#include "input/keymap.h"

struct GLFWwindow;

typedef struct {
  struct GLFWwindow *win;
  struct GLFWwindow *share;
  void *nk; /* struct nk_glfw* */
  int open;
  int apply_pending;
  int save_pending;
  int close_pending;
  int theme_dirty;
  int capturing;
  int capturing_leader;
  int capture_leader_armed; /* pressed leader; waiting for follow-up key */
  int capture_as_prefix;    /* 1 = bind as after-leader chord */
  TraashAction capture_action;
  int pending_capture_key;
  int pending_capture_mods;
  TraashTheme preview;
  char status_msg[128];
  char shortcut_filter[64];
  int tab; /* 0 appearance, 1 terminal, 2 shortcuts */
  void *font_ui;
  void *font_title;
  void *font_section;
  void *font_dim;
  char font_families[TRAASH_FONT_MAX_FAMILIES][128];
  int font_family_count;
} TraashSettingsWindow;

int traash_settings_window_open(TraashSettingsWindow *sw, struct GLFWwindow *share,
                                TraashConfig *cfg, void *lua);
void traash_settings_window_close(TraashSettingsWindow *sw);
int traash_settings_window_is_open(const TraashSettingsWindow *sw);

/* Draw one frame; returns 1 if still open. MakeContextCurrent is restored to `main`. */
int traash_settings_window_frame(TraashSettingsWindow *sw, struct GLFWwindow *main,
                                 TraashConfig *cfg, TraashKeymap *km, void *lua,
                                 const char **themes, int theme_count,
                                 const char **statuses, int status_count);

#endif
