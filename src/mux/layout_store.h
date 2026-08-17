#ifndef TRAASH_LAYOUT_STORE_H
#define TRAASH_LAYOUT_STORE_H

#include "mux/session.h"

#include <stddef.h>

#define TRAASH_LAYOUT_MAX_WINDOWS 16
#define TRAASH_LAYOUT_MAX_PANES 16
#define TRAASH_LAYOUT_MAX_NAMES 64
#define TRAASH_LAYOUT_NAME_LEN 64

typedef struct {
  float x, y, w, h;
} TraashLayoutPane;

typedef struct {
  char name[64];
  int active; /* 1-based pane index */
  int pane_count;
  TraashLayoutPane panes[TRAASH_LAYOUT_MAX_PANES];
} TraashLayoutWindow;

typedef struct {
  char name[TRAASH_LAYOUT_NAME_LEN];
  int window_count;
  TraashLayoutWindow windows[TRAASH_LAYOUT_MAX_WINDOWS];
} TraashLayoutDesc;

typedef struct {
  char name[TRAASH_LAYOUT_NAME_LEN];
  int user; /* 1 = ~/.config/traash/layouts, 0 = bundled */
} TraashLayoutName;

/* Scan bundled + user layouts. User names override bundled. Returns count. */
int traash_layout_store_scan(TraashLayoutName *out, int max_out);

/* Load a named layout (user preferred, then bundled). Returns 0 on success. */
int traash_layout_store_load(const char *name, TraashLayoutDesc *out);

/* Save current session as a user layout. Returns 0 on success. */
int traash_layout_store_save(const char *name, const TraashSession *session);

/* Delete a user layout. Returns 0 on success. Bundled layouts cannot be deleted. */
int traash_layout_store_delete(const char *name);

/* Replace session windows with the layout (spawns fresh shells). */
int traash_layout_apply(TraashSession *session, const TraashLayoutDesc *desc, int cols,
                        int rows);

/* True if name looks safe for a filename (alnum, -, _). */
int traash_layout_name_valid(const char *name);

#endif
