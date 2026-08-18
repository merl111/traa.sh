#ifndef TRAASH_SCREEN_EXPORT_H
#define TRAASH_SCREEN_EXPORT_H

#include "mux/session.h"
#include "term/screen.h"

#include <stddef.h>

typedef struct {
  int scrollback_lines; /* 0 = live grid only; default 200 */
  int max_bytes;        /* cap total output, default 256KiB */
  int pane_id;          /* 0 = all panes in active window */
} TraashExportOpts;

typedef struct {
  int pane_id;
  char title[256];
  char cwd[512];
  int cols;
  int rows;
  int cursor_x;
  int cursor_y;
  int busy;
  char *text;
  size_t text_len;
} TraashPaneExport;

int traash_screen_export_text(const TraashScreen *s, const TraashExportOpts *opts, char *buf,
                              size_t buf_len);

int traash_session_export_window(const TraashSession *s, const TraashExportOpts *opts,
                                 TraashPaneExport **out, int *count);

void traash_pane_export_free(TraashPaneExport *panes, int count);

#endif
