#ifndef TRAASH_STATUS_H
#define TRAASH_STATUS_H

#include "mux/session.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char text[128];
  char align[16]; /* left, center, right */
  uint32_t fg, bg;
} TraashStatusSegment;

typedef struct {
  TraashStatusSegment segs[32];
  int count;
  /* Pill layout (0 = classic flush rectangles / powerline) */
  float gap;    /* space between pills */
  float radius; /* corner radius */
  float pad_x;  /* horizontal text padding inside pill */
  float v_pad;  /* vertical inset from status bar edges */
} TraashStatusModel;

int traash_status_render(void *lua_state, const char *style, const TraashSession *session,
                         TraashStatusModel *out);

#endif
