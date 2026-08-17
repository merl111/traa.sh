#include "input/actions.h"

#include <string.h>

static const char *NAMES[TRAASH_ACTION_COUNT] = {
    [TRAASH_ACTION_NONE] = "none",
    [TRAASH_ACTION_SPLIT_H] = "split_h",
    [TRAASH_ACTION_SPLIT_V] = "split_v",
    [TRAASH_ACTION_PANE_NEXT] = "pane_next",
    [TRAASH_ACTION_PANE_LEFT] = "pane_left",
    [TRAASH_ACTION_PANE_DOWN] = "pane_down",
    [TRAASH_ACTION_PANE_UP] = "pane_up",
    [TRAASH_ACTION_PANE_RIGHT] = "pane_right",
    [TRAASH_ACTION_ZOOM] = "zoom",
    [TRAASH_ACTION_NEW_WINDOW] = "new_window",
    [TRAASH_ACTION_NEXT_WINDOW] = "next_window",
    [TRAASH_ACTION_PREV_WINDOW] = "prev_window",
    [TRAASH_ACTION_GOTO_WINDOW] = "goto_window",
    [TRAASH_ACTION_DETACH] = "detach",
    [TRAASH_ACTION_RELOAD_CONFIG] = "reload_config",
    [TRAASH_ACTION_COMMAND_PALETTE] = "command_palette",
    [TRAASH_ACTION_THEME_CYCLE] = "theme_cycle",
    [TRAASH_ACTION_STATUS_CYCLE] = "status_cycle",
    [TRAASH_ACTION_FONT_INCREASE] = "font_increase",
    [TRAASH_ACTION_FONT_DECREASE] = "font_decrease",
    [TRAASH_ACTION_DEMO] = "demo",
    [TRAASH_ACTION_COPY_MODE] = "copy_mode",
    [TRAASH_ACTION_COPY] = "copy",
    [TRAASH_ACTION_PASTE] = "paste",
    [TRAASH_ACTION_SEARCH] = "search",
    [TRAASH_ACTION_SETTINGS] = "settings",
    [TRAASH_ACTION_SHORTCUTS] = "shortcuts",
    [TRAASH_ACTION_LAYOUT_PICKER] = "layout_picker",
    [TRAASH_ACTION_OVERVIEW] = "overview",
    [TRAASH_ACTION_QUIT] = "quit",
};

static const char *LABELS[TRAASH_ACTION_COUNT] = {
    [TRAASH_ACTION_NONE] = "None",
    [TRAASH_ACTION_SPLIT_H] = "Split horizontally",
    [TRAASH_ACTION_SPLIT_V] = "Split vertically",
    [TRAASH_ACTION_PANE_NEXT] = "Next pane",
    [TRAASH_ACTION_PANE_LEFT] = "Pane left",
    [TRAASH_ACTION_PANE_DOWN] = "Pane down",
    [TRAASH_ACTION_PANE_UP] = "Pane up",
    [TRAASH_ACTION_PANE_RIGHT] = "Pane right",
    [TRAASH_ACTION_ZOOM] = "Zoom pane",
    [TRAASH_ACTION_NEW_WINDOW] = "New window",
    [TRAASH_ACTION_NEXT_WINDOW] = "Next window",
    [TRAASH_ACTION_PREV_WINDOW] = "Previous window",
    [TRAASH_ACTION_GOTO_WINDOW] = "Go to window by number",
    [TRAASH_ACTION_DETACH] = "Detach session",
    [TRAASH_ACTION_RELOAD_CONFIG] = "Reload config",
    [TRAASH_ACTION_COMMAND_PALETTE] = "Command palette",
    [TRAASH_ACTION_THEME_CYCLE] = "Cycle theme",
    [TRAASH_ACTION_STATUS_CYCLE] = "Cycle status bar",
    [TRAASH_ACTION_FONT_INCREASE] = "Increase font size",
    [TRAASH_ACTION_FONT_DECREASE] = "Decrease font size",
    [TRAASH_ACTION_DEMO] = "Start demo",
    [TRAASH_ACTION_COPY_MODE] = "Copy mode",
    [TRAASH_ACTION_COPY] = "Copy selection",
    [TRAASH_ACTION_PASTE] = "Paste",
    [TRAASH_ACTION_SEARCH] = "Search",
    [TRAASH_ACTION_SETTINGS] = "Open settings",
    [TRAASH_ACTION_SHORTCUTS] = "Keyboard shortcuts",
    [TRAASH_ACTION_LAYOUT_PICKER] = "Layout picker",
    [TRAASH_ACTION_OVERVIEW] = "Session overview",
    [TRAASH_ACTION_QUIT] = "Quit",
};

const char *traash_action_name(TraashAction a) {
  if (a < 0 || a >= TRAASH_ACTION_COUNT) {
    return "none";
  }
  return NAMES[a] ? NAMES[a] : "none";
}

const char *traash_action_label(TraashAction a) {
  if (a < 0 || a >= TRAASH_ACTION_COUNT) {
    return "None";
  }
  return LABELS[a] ? LABELS[a] : traash_action_name(a);
}

TraashAction traash_action_from_name(const char *name) {
  for (int i = 0; i < TRAASH_ACTION_COUNT; i++) {
    if (NAMES[i] && strcmp(NAMES[i], name) == 0) {
      return (TraashAction)i;
    }
  }
  return TRAASH_ACTION_NONE;
}
