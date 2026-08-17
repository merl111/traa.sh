#ifndef TRAASH_SETTINGS_H
#define TRAASH_SETTINGS_H

#include "config/config.h"
#include "input/keymap.h"

typedef enum {
  TRAASH_SETTINGS_TAB_OVERVIEW = 0,
  TRAASH_SETTINGS_TAB_APPEARANCE,
  TRAASH_SETTINGS_TAB_SHORTCUTS,
  TRAASH_SETTINGS_TAB_COUNT
} TraashSettingsTab;

typedef struct {
  int open;
  TraashSettingsTab tab;
  int selected;     /* row within tab */
  int scroll;       /* shortcuts list scroll */
  int capturing;    /* waiting for key chord */
  TraashAction capture_action;
  float panel_x, panel_y, panel_w, panel_h;
  char status_msg[128];
} TraashSettings;

void traash_settings_open(TraashSettings *s);
void traash_settings_close(TraashSettings *s);
int traash_settings_is_open(const TraashSettings *s);

/* Click in framebuffer coords. Returns 1 if consumed. */
int traash_settings_click(TraashSettings *s, TraashConfig *cfg, TraashKeymap *km,
                          const char **themes, int theme_count, const char **statuses,
                          int status_count, float x, float y, float scale);

/* Keyboard while settings open. Returns 1 if consumed. */
int traash_settings_key(TraashSettings *s, TraashConfig *cfg, TraashKeymap *km, int key,
                        int mods);

int traash_settings_row_count(const TraashSettings *s, const TraashKeymap *km);
void traash_settings_row_text(const TraashSettings *s, const TraashConfig *cfg,
                              const TraashKeymap *km, int row, char *left, size_t ln,
                              char *right, size_t rn);

#endif
