#include "input/keymap.h"

#include <stdio.h>
#include <string.h>

enum {
  TK_B = 66,
  TK_C = 67,
  TK_D = 68,
  TK_F = 70,
  TK_H = 72,
  TK_J = 74,
  TK_K = 75,
  TK_L = 76,
  TK_N = 78,
  TK_O = 79,
  TK_P = 80,
  TK_Q = 81,
  TK_R = 82,
  TK_T = 84,
  TK_V = 86,
  TK_W = 87,
  TK_Z = 90,
  TK_5 = 53,
  TK_MINUS = 45,
  TK_EQUAL = 61,
  TK_COMMA = 44,
  TK_PERIOD = 46,
  TK_SLASH = 47,
  TK_SEMICOLON = 59,
  TK_APOSTROPHE = 39,
  TK_LEFT_BRACKET = 91,
  TK_BACKSLASH = 92,
  TK_RIGHT_BRACKET = 93,
  TK_ESCAPE = 256,
  TK_ENTER = 257,
  TK_TAB = 258,
  TK_SPACE = 32,
  /* GLFW modifier keys — must not cancel prefix mode */
  TK_LEFT_SHIFT = 340,
  TK_LEFT_CONTROL = 341,
  TK_LEFT_ALT = 342,
  TK_LEFT_SUPER = 343,
  TK_RIGHT_SHIFT = 344,
  TK_RIGHT_CONTROL = 345,
  TK_RIGHT_ALT = 346,
  TK_RIGHT_SUPER = 347,
  TK_CAPS_LOCK = 280,
  TK_NUM_LOCK = 282
};

static int is_modifier_key(int key) {
  return key == TK_LEFT_SHIFT || key == TK_RIGHT_SHIFT || key == TK_LEFT_CONTROL ||
         key == TK_RIGHT_CONTROL || key == TK_LEFT_ALT || key == TK_RIGHT_ALT ||
         key == TK_LEFT_SUPER || key == TK_RIGHT_SUPER || key == TK_CAPS_LOCK ||
         key == TK_NUM_LOCK;
}

void traash_keymap_init_defaults(TraashKeymap *km) {
  memset(km, 0, sizeof(*km));
  km->prefix_key = TK_B;
  km->prefix_mods = 1; /* Ctrl-b */
  traash_keymap_bind(km, TK_5, 2, 1, TRAASH_ACTION_SPLIT_V);
  traash_keymap_bind(km, TK_APOSTROPHE, 2, 1, TRAASH_ACTION_SPLIT_H);
  traash_keymap_bind(km, TK_O, 0, 1, TRAASH_ACTION_PANE_NEXT);
  traash_keymap_bind(km, TK_H, 0, 1, TRAASH_ACTION_PANE_LEFT);
  traash_keymap_bind(km, TK_J, 0, 1, TRAASH_ACTION_PANE_DOWN);
  traash_keymap_bind(km, TK_K, 0, 1, TRAASH_ACTION_PANE_UP);
  traash_keymap_bind(km, TK_L, 0, 1, TRAASH_ACTION_PANE_RIGHT);
  traash_keymap_bind(km, TK_Z, 0, 1, TRAASH_ACTION_ZOOM);
  /* tmux-style windows: c=new, n=next, p=prev (Tab also next) */
  traash_keymap_bind(km, TK_C, 0, 1, TRAASH_ACTION_NEW_WINDOW);
  traash_keymap_bind(km, TK_N, 0, 1, TRAASH_ACTION_NEXT_WINDOW);
  traash_keymap_bind(km, TK_TAB, 0, 1, TRAASH_ACTION_NEXT_WINDOW);
  traash_keymap_bind(km, TK_P, 0, 1, TRAASH_ACTION_PREV_WINDOW);
  /* Prefix+0..9 → go to window N (representative bind for UI; lookup handles all digits) */
  traash_keymap_bind(km, '1', 0, 1, TRAASH_ACTION_GOTO_WINDOW);
  traash_keymap_bind(km, TK_D, 0, 1, TRAASH_ACTION_DETACH);
  traash_keymap_bind(km, TK_LEFT_BRACKET, 0, 1, TRAASH_ACTION_COPY_MODE);
  /* Vim-like pane motion without prefix */
  traash_keymap_bind(km, TK_H, 1, 0, TRAASH_ACTION_PANE_LEFT);
  traash_keymap_bind(km, TK_J, 1, 0, TRAASH_ACTION_PANE_DOWN);
  traash_keymap_bind(km, TK_K, 1, 0, TRAASH_ACTION_PANE_UP);
  traash_keymap_bind(km, TK_L, 1, 0, TRAASH_ACTION_PANE_RIGHT);
  /* App chords use Ctrl-Shift so plain Ctrl-* reaches the shell (^C ^D ^Z …)
   * except Ctrl-hjkl which are reserved for pane focus above. */
  traash_keymap_bind(km, TK_P, 1 | 2, 0, TRAASH_ACTION_COMMAND_PALETTE);
  traash_keymap_bind(km, TK_R, 1 | 2, 0, TRAASH_ACTION_RELOAD_CONFIG);
  traash_keymap_bind(km, TK_T, 1 | 2, 0, TRAASH_ACTION_THEME_CYCLE);
  traash_keymap_bind(km, TK_F, 1 | 2, 0, TRAASH_ACTION_SEARCH);
  traash_keymap_bind(km, TK_C, 1 | 2, 0, TRAASH_ACTION_COPY);
  traash_keymap_bind(km, TK_V, 1 | 2, 0, TRAASH_ACTION_PASTE);
  traash_keymap_bind(km, TK_COMMA, 1 | 2, 0, TRAASH_ACTION_SETTINGS);
  traash_keymap_bind(km, TK_SLASH, 1 | 2, 0, TRAASH_ACTION_SHORTCUTS);
  traash_keymap_bind(km, TK_SLASH, 2, 1, TRAASH_ACTION_SHORTCUTS); /* prefix+? */
  traash_keymap_bind(km, TK_L, 1 | 2, 0, TRAASH_ACTION_LAYOUT_PICKER);
  traash_keymap_bind(km, TK_O, 1 | 2, 0, TRAASH_ACTION_OVERVIEW);
  traash_keymap_bind(km, TK_EQUAL, 1 | 2, 0, TRAASH_ACTION_FONT_INCREASE);
  traash_keymap_bind(km, TK_MINUS, 1, 0, TRAASH_ACTION_FONT_DECREASE);
  traash_keymap_bind(km, TK_Q, 1 | 2, 0, TRAASH_ACTION_QUIT);
}

void traash_keymap_set_leader(TraashKeymap *km, int key, int mods) {
  if (!km || key <= 0) {
    return;
  }
  km->prefix_key = key;
  km->prefix_mods = mods;
  km->prefix_active = false;
}

int traash_keymap_normalize_key(int *key, int *mods) {
  if (!key || !mods) {
    return 0;
  }
  int k = *key;
  int m = *mods;
  int changed = 0;
  if (k >= 'a' && k <= 'z') {
    k = k - 'a' + 'A'; /* ASCII → GLFW_KEY_A..Z */
    changed = 1;
  } else if (k == '%') {
    k = TK_5;
    m |= 2;
    changed = 1;
  } else if (k == '"') {
    k = TK_APOSTROPHE;
    m |= 2;
    changed = 1;
  } else if (k == '|') {
    k = TK_BACKSLASH;
    m |= 2;
    changed = 1;
  } else if (k == '?') {
    k = TK_SLASH;
    m |= 2;
    changed = 1;
  } else if (k == '+') {
    k = TK_EQUAL;
    m |= 2;
    changed = 1;
  }
  *key = k;
  *mods = m;
  return changed;
}

void traash_keymap_bind(TraashKeymap *km, int key, int mods, int prefix,
                        TraashAction action) {
  if (km->count >= 128) {
    return;
  }
  traash_keymap_normalize_key(&key, &mods);
  km->binds[km->count++] =
      (TraashKeyBind){.key = key, .mods = mods, .prefix = prefix, .action = action};
}

void traash_keymap_rebind(TraashKeymap *km, TraashAction action, int key, int mods,
                          int prefix) {
  traash_keymap_normalize_key(&key, &mods);
  for (int i = 0; i < km->count;) {
    if (km->binds[i].action == action) {
      km->binds[i] = km->binds[km->count - 1];
      km->count--;
      continue;
    }
    i++;
  }
  for (int i = 0; i < km->count;) {
    if (km->binds[i].key == key && km->binds[i].mods == mods &&
        km->binds[i].prefix == prefix) {
      km->binds[i] = km->binds[km->count - 1];
      km->count--;
      continue;
    }
    i++;
  }
  traash_keymap_bind(km, key, mods, prefix, action);
}

int traash_keymap_find_action(const TraashKeymap *km, TraashAction action,
                              TraashKeyBind *out) {
  for (int i = 0; i < km->count; i++) {
    if (km->binds[i].action == action) {
      if (out) {
        *out = km->binds[i];
      }
      return 1;
    }
  }
  return 0;
}

static void append_mod(char *buf, size_t n, size_t *o, const char *s) {
  int m = snprintf(buf + *o, n > *o ? n - *o : 0, "%s", s);
  if (m > 0) {
    *o += (size_t)m;
  }
}

static void format_chord(int key, int mods, char *buf, size_t n) {
  size_t o = 0;
  buf[0] = 0;
  if (mods & 1) {
    append_mod(buf, n, &o, "Ctrl-");
  }
  /* Shifted punctuation that has its own glyph — don't also print "Shift-" */
  int slash_as_question = (key == TK_SLASH) && (mods & 2) && ((mods & ~2) == 0);
  int backslash_as_pipe = (key == TK_BACKSLASH) && (mods & 2);
  int shifted_symbol =
      ((mods & 2) && (key == TK_5 || key == TK_APOSTROPHE || key == TK_SEMICOLON ||
                      key == TK_LEFT_BRACKET || key == TK_RIGHT_BRACKET ||
                      key == TK_EQUAL)) ||
      slash_as_question || backslash_as_pipe;
  if ((mods & 2) && !shifted_symbol) {
    append_mod(buf, n, &o, "Shift-");
  }
  if (mods & 4) {
    append_mod(buf, n, &o, "Alt-");
  }
  if (mods & 8) {
    append_mod(buf, n, &o, "Super-");
  }
  char keyname[32];
  if (key >= 65 && key <= 90) {
    snprintf(keyname, sizeof(keyname), "%c", key);
  } else if (key == TK_5 && (mods & 2)) {
    snprintf(keyname, sizeof(keyname), "%%");
  } else if (key == TK_APOSTROPHE && (mods & 2)) {
    snprintf(keyname, sizeof(keyname), "\"");
  } else if (key == TK_APOSTROPHE) {
    snprintf(keyname, sizeof(keyname), "'");
  } else if (slash_as_question) {
    snprintf(keyname, sizeof(keyname), "?");
  } else if (key == TK_SLASH) {
    snprintf(keyname, sizeof(keyname), "/");
  } else if (backslash_as_pipe) {
    snprintf(keyname, sizeof(keyname), "|");
  } else if (key == TK_BACKSLASH) {
    snprintf(keyname, sizeof(keyname), "\\");
  } else if (key == TK_COMMA) {
    snprintf(keyname, sizeof(keyname), ",");
  } else if (key == TK_PERIOD) {
    snprintf(keyname, sizeof(keyname), ".");
  } else if (key == TK_EQUAL && (mods & 2)) {
    snprintf(keyname, sizeof(keyname), "+");
  } else if (key == TK_EQUAL) {
    snprintf(keyname, sizeof(keyname), "=");
  } else if (key == TK_MINUS) {
    snprintf(keyname, sizeof(keyname), "-");
  } else if (key == TK_SEMICOLON && (mods & 2)) {
    snprintf(keyname, sizeof(keyname), ":");
  } else if (key == TK_SEMICOLON) {
    snprintf(keyname, sizeof(keyname), ";");
  } else if (key == TK_LEFT_BRACKET && (mods & 2)) {
    snprintf(keyname, sizeof(keyname), "{");
  } else if (key == TK_LEFT_BRACKET) {
    snprintf(keyname, sizeof(keyname), "[");
  } else if (key == TK_RIGHT_BRACKET && (mods & 2)) {
    snprintf(keyname, sizeof(keyname), "}");
  } else if (key == TK_RIGHT_BRACKET) {
    snprintf(keyname, sizeof(keyname), "]");
  } else if (key == TK_SPACE) {
    snprintf(keyname, sizeof(keyname), "Space");
  } else if (key == TK_ENTER) {
    snprintf(keyname, sizeof(keyname), "Enter");
  } else if (key == TK_ESCAPE) {
    snprintf(keyname, sizeof(keyname), "Esc");
  } else if (key == TK_TAB) {
    snprintf(keyname, sizeof(keyname), "Tab");
  } else if (key >= '0' && key <= '9') {
    snprintf(keyname, sizeof(keyname), "%c", key);
  } else {
    snprintf(keyname, sizeof(keyname), "Key%d", key);
  }
  append_mod(buf, n, &o, keyname);
}

void traash_keymap_format_leader(const TraashKeymap *km, char *buf, size_t n) {
  if (!km) {
    snprintf(buf, n, "Ctrl-B");
    return;
  }
  format_chord(km->prefix_key, km->prefix_mods, buf, n);
}

void traash_keymap_format_key(const TraashKeymap *km, int key, int mods, int prefix,
                              char *buf, size_t n) {
  if (prefix) {
    char leader[64];
    char chord[64];
    traash_keymap_format_leader(km, leader, sizeof(leader));
    format_chord(key, mods, chord, sizeof(chord));
    snprintf(buf, n, "%s %s", leader, chord);
  } else {
    format_chord(key, mods, buf, n);
  }
}

void traash_keymap_format(const TraashKeymap *km, const TraashKeyBind *b, char *buf,
                          size_t n) {
  if (!b) {
    snprintf(buf, n, "(unbound)");
    return;
  }
  if (b->action == TRAASH_ACTION_GOTO_WINDOW) {
    char leader[64];
    traash_keymap_format_leader(km, leader, sizeof(leader));
    snprintf(buf, n, "%s 0…9", leader);
    return;
  }
  traash_keymap_format_key(km, b->key, b->mods, b->prefix, buf, n);
}

static int prefix_mods_match(const TraashKeyBind *b, int mods, int leader_mods) {
  int effective = mods & ~leader_mods;
  /* Letter chords: ignore incidental Shift (prefix+N vs prefix+n). */
  if (!(b->mods & 2) && b->key >= 'A' && b->key <= 'Z') {
    effective &= ~2;
  }
  if (b->mods == mods) {
    return 1;
  }
  return b->mods == effective;
}

TraashAction traash_keymap_lookup(TraashKeymap *km, int key, int mods) {
  /* Shift/Ctrl/Alt alone must not arm, fire, or cancel the leader */
  if (is_modifier_key(key)) {
    return TRAASH_ACTION_NONE;
  }

  if (key == km->prefix_key && mods == km->prefix_mods) {
    km->prefix_active = !km->prefix_active;
    return TRAASH_ACTION_NONE;
  }
  /* Leader while sticky mods still held (e.g. Ctrl still down, press b again) */
  if (key == km->prefix_key && (mods & km->prefix_mods) == km->prefix_mods &&
      (mods & ~km->prefix_mods) == 0) {
    km->prefix_active = !km->prefix_active;
    return TRAASH_ACTION_NONE;
  }

  if (km->prefix_active) {
    /* tmux-style: prefix + digit selects window with that number */
    int digit = -1;
    if (key >= '0' && key <= '9') {
      digit = key - '0';
    } else if (key >= 320 && key <= 329) { /* GLFW_KEY_KP_0..KP_9 */
      digit = key - 320;
    }
    if (digit >= 0) {
      /* Digits only — Shift+5 is prefix+% (split). Sticky leader mods OK. */
      int extra = mods & ~km->prefix_mods;
      if (extra == 0) {
        km->prefix_active = false;
        km->goto_window = digit;
        return TRAASH_ACTION_GOTO_WINDOW;
      }
    }
    for (int i = 0; i < km->count; i++) {
      TraashKeyBind *b = &km->binds[i];
      if (b->prefix && b->key == key && prefix_mods_match(b, mods, km->prefix_mods)) {
        km->prefix_active = false;
        return b->action;
      }
    }
    /* Unmatched follow-up cancels leader mode (key is eaten by caller) */
    km->prefix_active = false;
    return TRAASH_ACTION_NONE;
  }

  for (int i = 0; i < km->count; i++) {
    TraashKeyBind *b = &km->binds[i];
    if (!b->prefix && b->key == key && b->mods == mods) {
      return b->action;
    }
  }
  /* Ctrl-Shift letter chords: accept sticky Caps/Num already stripped; also
   * match when only Ctrl+Shift bits are set among modifiers. */
  if ((mods & (1 | 2)) == (1 | 2) && key >= 'A' && key <= 'Z') {
    int want = 1 | 2;
    for (int i = 0; i < km->count; i++) {
      TraashKeyBind *b = &km->binds[i];
      if (!b->prefix && b->key == key && b->mods == want) {
        return b->action;
      }
    }
  }
  return TRAASH_ACTION_NONE;
}
