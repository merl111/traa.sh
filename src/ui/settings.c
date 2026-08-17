#include "ui/settings.h"

#include "config/config.h"

#include <stdio.h>
#include <string.h>

static const char *CURSOR_NAMES[] = {"block", "beam", "underline"};

void traash_settings_open(TraashSettings *s) {
  memset(s, 0, sizeof(*s));
  s->open = 1;
  s->tab = TRAASH_SETTINGS_TAB_OVERVIEW;
  snprintf(s->status_msg, sizeof(s->status_msg), "Click a shortcut row, then press a new key");
}

void traash_settings_close(TraashSettings *s) {
  memset(s, 0, sizeof(*s));
}

int traash_settings_is_open(const TraashSettings *s) {
  return s && s->open;
}

static TraashAction shortcut_actions[] = {
    TRAASH_ACTION_SPLIT_H,         TRAASH_ACTION_SPLIT_V,     TRAASH_ACTION_PANE_NEXT,
    TRAASH_ACTION_PANE_LEFT,       TRAASH_ACTION_PANE_DOWN,   TRAASH_ACTION_PANE_UP,
    TRAASH_ACTION_PANE_RIGHT,      TRAASH_ACTION_ZOOM,        TRAASH_ACTION_NEW_WINDOW,
    TRAASH_ACTION_NEXT_WINDOW,     TRAASH_ACTION_PREV_WINDOW, TRAASH_ACTION_GOTO_WINDOW,
    TRAASH_ACTION_DETACH,
    TRAASH_ACTION_COPY,            TRAASH_ACTION_PASTE,       TRAASH_ACTION_COMMAND_PALETTE,
    TRAASH_ACTION_THEME_CYCLE,     TRAASH_ACTION_STATUS_CYCLE, TRAASH_ACTION_FONT_INCREASE,
    TRAASH_ACTION_FONT_DECREASE,   TRAASH_ACTION_RELOAD_CONFIG,
    TRAASH_ACTION_SEARCH,          TRAASH_ACTION_DEMO,        TRAASH_ACTION_SETTINGS,
    TRAASH_ACTION_SHORTCUTS,       TRAASH_ACTION_LAYOUT_PICKER, TRAASH_ACTION_OVERVIEW,
    TRAASH_ACTION_QUIT};

static int shortcut_count(void) {
  return (int)(sizeof(shortcut_actions) / sizeof(shortcut_actions[0]));
}

int traash_settings_row_count(const TraashSettings *s, const TraashKeymap *km) {
  (void)km;
  if (s->tab == TRAASH_SETTINGS_TAB_OVERVIEW) {
    return 7;
  }
  if (s->tab == TRAASH_SETTINGS_TAB_APPEARANCE) {
    return 5;
  }
  return shortcut_count();
}

void traash_settings_row_text(const TraashSettings *s, const TraashConfig *cfg,
                              const TraashKeymap *km, int row, char *left, size_t ln,
                              char *right, size_t rn) {
  left[0] = right[0] = 0;
  if (s->tab == TRAASH_SETTINGS_TAB_OVERVIEW) {
    switch (row) {
    case 0:
      snprintf(left, ln, "Theme");
      snprintf(right, rn, "%s", cfg->theme);
      break;
    case 1:
      snprintf(left, ln, "Status bar");
      snprintf(right, rn, "%s", cfg->status_bar);
      break;
    case 2:
      snprintf(left, ln, "Font");
      snprintf(right, rn, "%s", cfg->font_family);
      break;
    case 3:
      snprintf(left, ln, "Font size");
      snprintf(right, rn, "%.0f", cfg->font_size);
      break;
    case 4:
      snprintf(left, ln, "Cursor");
      snprintf(right, rn, "%s", CURSOR_NAMES[cfg->cursor_style % 3]);
      break;
    case 5:
      snprintf(left, ln, "Scrollback");
      snprintf(right, rn, "%d lines", cfg->scrollback_lines);
      break;
    case 6:
      snprintf(left, ln, "Plugins");
      snprintf(right, rn, "%d enabled", cfg->plugin_count);
      break;
    }
    return;
  }
  if (s->tab == TRAASH_SETTINGS_TAB_APPEARANCE) {
    switch (row) {
    case 0:
      snprintf(left, ln, "Theme");
      snprintf(right, rn, "%s  (click to cycle)", cfg->theme);
      break;
    case 1:
      snprintf(left, ln, "Status bar");
      snprintf(right, rn, "%s  (click to cycle)", cfg->status_bar);
      break;
    case 2:
      snprintf(left, ln, "Font size");
      snprintf(right, rn, "%.0f  (click + / -)", cfg->font_size);
      break;
    case 3:
      snprintf(left, ln, "Cursor style");
      snprintf(right, rn, "%s  (click to cycle)", CURSOR_NAMES[cfg->cursor_style % 3]);
      break;
    case 4:
      snprintf(left, ln, "Save config");
      snprintf(right, rn, "Write ~/.config/traash/config.lua");
      break;
    }
    return;
  }
  if (row < 0 || row >= shortcut_count()) {
    return;
  }
  TraashAction a = shortcut_actions[row];
  snprintf(left, ln, "%s", traash_action_label(a));
  TraashKeyBind b;
  if (s->capturing && s->capture_action == a) {
    snprintf(right, rn, "Press new shortcut...");
  } else if (traash_keymap_find_action(km, a, &b)) {
    traash_keymap_format(km, &b, right, rn);
  } else {
    snprintf(right, rn, "(unbound)");
  }
}

static int index_of(const char **list, int n, const char *v) {
  for (int i = 0; i < n; i++) {
    if (strcmp(list[i], v) == 0) {
      return i;
    }
  }
  return 0;
}

int traash_config_save(const TraashConfig *cfg, const TraashKeymap *km);

static void appearance_activate(TraashSettings *s, TraashConfig *cfg, TraashKeymap *km,
                                const char **themes, int theme_count, const char **statuses,
                                int status_count, int row, int which) {
  (void)s;
  if (row == 0 && theme_count > 0) {
    int i = (index_of(themes, theme_count, cfg->theme) + 1) % theme_count;
    snprintf(cfg->theme, sizeof(cfg->theme), "%s", themes[i]);
  } else if (row == 1 && status_count > 0) {
    int i = (index_of(statuses, status_count, cfg->status_bar) + 1) % status_count;
    snprintf(cfg->status_bar, sizeof(cfg->status_bar), "%s", statuses[i]);
  } else if (row == 2) {
    if (which < 0) {
      cfg->font_size -= 1.0f;
    } else {
      cfg->font_size += 1.0f;
    }
    if (cfg->font_size < 8) {
      cfg->font_size = 8;
    }
    if (cfg->font_size > 48) {
      cfg->font_size = 48;
    }
  } else if (row == 3) {
    cfg->cursor_style = (cfg->cursor_style + 1) % 3;
  } else if (row == 4) {
    if (traash_config_save(cfg, km) == 0) {
      snprintf(s->status_msg, sizeof(s->status_msg), "Saved config.lua");
    } else {
      snprintf(s->status_msg, sizeof(s->status_msg), "Failed to save config");
    }
  }
}

int traash_settings_click(TraashSettings *s, TraashConfig *cfg, TraashKeymap *km,
                          const char **themes, int theme_count, const char **statuses,
                          int status_count, float x, float y, float scale) {
  if (!s->open) {
    return 0;
  }
  float px = s->panel_x, py = s->panel_y, pw = s->panel_w, ph = s->panel_h;
  if (x < px || y < py || x > px + pw || y > py + ph) {
    traash_settings_close(s);
    return 1;
  }

  float header = 44.0f * scale;
  float tab_h = 34.0f * scale;
  float row_h = 32.0f * scale;

  /* Close button */
  if (x > px + pw - 40 * scale && y < py + header) {
    traash_settings_close(s);
    return 1;
  }

  /* Tabs */
  if (y >= py + header && y < py + header + tab_h) {
    float tw = pw / 3.0f;
    int tab = (int)((x - px) / tw);
    if (tab < 0) {
      tab = 0;
    }
    if (tab > 2) {
      tab = 2;
    }
    s->tab = (TraashSettingsTab)tab;
    s->selected = 0;
    s->scroll = 0;
    s->capturing = 0;
    return 1;
  }

  /* Footer is not clickable for rows */
  if (y >= py + ph - 36 * scale) {
    return 1;
  }

  float list_top = py + header + tab_h + 10 * scale;
  int row = (int)((y - list_top) / row_h) + s->scroll;
  int rows = traash_settings_row_count(s, km);
  if (row < 0 || row >= rows) {
    return 1;
  }
  s->selected = row;

  if (s->tab == TRAASH_SETTINGS_TAB_APPEARANCE) {
    int which = (x > px + pw * 0.7f) ? 1 : ((x < px + pw * 0.45f) ? -1 : 1);
    appearance_activate(s, cfg, km, themes, theme_count, statuses, status_count, row, which);
    return 1;
  }
  if (s->tab == TRAASH_SETTINGS_TAB_SHORTCUTS) {
    s->capturing = 1;
    s->capture_action = shortcut_actions[row];
    snprintf(s->status_msg, sizeof(s->status_msg), "Capturing for: %s",
             traash_action_label(s->capture_action));
    return 1;
  }
  /* overview: switch to appearance on click */
  if (s->tab == TRAASH_SETTINGS_TAB_OVERVIEW) {
    s->tab = TRAASH_SETTINGS_TAB_APPEARANCE;
    s->selected = row < 4 ? row : 0;
  }
  return 1;
}

int traash_settings_key(TraashSettings *s, TraashConfig *cfg, TraashKeymap *km, int key,
                        int mods) {
  (void)cfg;
  if (!s->open) {
    return 0;
  }
  if (key == 256 /* ESC */) {
    if (s->capturing) {
      s->capturing = 0;
      snprintf(s->status_msg, sizeof(s->status_msg), "Capture cancelled");
      return 1;
    }
    traash_settings_close(s);
    return 1;
  }
  if (s->capturing) {
    if (key == 341 || key == 345 || key == 340 || key == 344 || key == 342 || key == 346 ||
        key == 343 || key == 347) {
      return 1;
    }
    int prefix = 0;
    TraashKeyBind old;
    if (traash_keymap_find_action(km, s->capture_action, &old)) {
      prefix = old.prefix;
    }
    /* Alt forces a Ctrl-b prefix chord; Shift+Alt clears prefix */
    if ((mods & 4) && (mods & 2)) {
      prefix = 0;
      mods &= ~6;
    } else if (mods & 4) {
      prefix = 1;
      mods &= ~4;
    }
    traash_keymap_rebind(km, s->capture_action, key, mods, prefix);
    s->capturing = 0;
    char chord[64];
    traash_keymap_format_key(km, key, mods, prefix, chord, sizeof(chord));
    snprintf(s->status_msg, sizeof(s->status_msg), "Bound %s → %s",
             traash_action_label(s->capture_action), chord);
    return 1;
  }
  if (key == 265 /* up */) {
    if (s->selected > 0) {
      s->selected--;
    }
    if (s->selected < s->scroll) {
      s->scroll = s->selected;
    }
    return 1;
  }
  if (key == 264 /* down */) {
    int rows = traash_settings_row_count(s, km);
    if (s->selected + 1 < rows) {
      s->selected++;
    }
    return 1;
  }
  if (key == 263 /* left */ && s->tab > 0) {
    s->tab--;
    s->selected = 0;
    return 1;
  }
  if (key == 262 /* right */ && s->tab + 1 < TRAASH_SETTINGS_TAB_COUNT) {
    s->tab++;
    s->selected = 0;
    return 1;
  }
  return 1;
}
