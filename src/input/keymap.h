#ifndef TRAASH_KEYMAP_H
#define TRAASH_KEYMAP_H

#include "input/actions.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  int key;      /* GLFW physical key code */
  int mods;     /* internal: ctrl=1 shift=2 alt=4 super=8 */
  int prefix;   /* requires leader/prefix mode */
  TraashAction action;
} TraashKeyBind;

typedef struct {
  TraashKeyBind binds[128];
  int count;
  int prefix_key;  /* leader GLFW key (default KEY_B) */
  int prefix_mods; /* leader mods (default Ctrl=1) */
  bool prefix_active;
  int goto_window; /* set when TRAASH_ACTION_GOTO_WINDOW fires (0–9) */
} TraashKeymap;

void traash_keymap_init_defaults(TraashKeymap *km);
void traash_keymap_set_leader(TraashKeymap *km, int key, int mods);
void traash_keymap_bind(TraashKeymap *km, int key, int mods, int prefix,
                        TraashAction action);
void traash_keymap_rebind(TraashKeymap *km, TraashAction action, int key, int mods,
                          int prefix);
int traash_keymap_find_action(const TraashKeymap *km, TraashAction action,
                              TraashKeyBind *out);
TraashAction traash_keymap_lookup(TraashKeymap *km, int key, int mods);
void traash_keymap_format(const TraashKeymap *km, const TraashKeyBind *b, char *buf,
                          size_t n);
void traash_keymap_format_key(const TraashKeymap *km, int key, int mods, int prefix,
                              char *buf, size_t n);
void traash_keymap_format_leader(const TraashKeymap *km, char *buf, size_t n);
/* Convert legacy printable ASCII binds to GLFW physical keys. */
int traash_keymap_normalize_key(int *key, int *mods);

#endif
