#ifndef TRAASH_VT_H
#define TRAASH_VT_H

#include "term/screen.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
  VT_GROUND = 0,
  VT_ESC,
  VT_ESC_INTERMEDIATE, /* ESC ( ) * + # % … — consume one final byte */
  VT_CSI,
  VT_OSC,
  VT_DCS,
  VT_APC, /* ESC _ … ST — kitty graphics etc (swallowed) */
  VT_PM   /* ESC ^ … ST */
} TraashVtState;

typedef struct {
  TraashScreen *screen;
  TraashVtState state;
  char csi_buf[128];
  int csi_len;
  int csi_args[24];
  int csi_argc;
  uint8_t csi_colon[24]; /* 1 if this arg was preceded by ':' */
  int csi_collect; /* private marker like ? */
  char osc_buf[512];
  int osc_len;
  int osc_term_bel; /* 1 if OSC ended with BEL, 0 if ST */
  char dcs_buf[512];
  int dcs_len;
  /* Colors reported for OSC 10/11/12 queries (0xRRGGBB). */
  uint32_t report_fg;
  uint32_t report_bg;
  uint32_t report_cursor;
  int app_cursor;  /* DECCKM */
  int insert_mode; /* IRM */
  void (*on_osc)(void *ud, int code, const char *data);
  void (*reply)(void *ud, const char *data, size_t n);
  void *ud;
} TraashVt;

void traash_vt_init(TraashVt *vt, TraashScreen *screen);
void traash_vt_set_report_colors(TraashVt *vt, uint32_t fg, uint32_t bg, uint32_t cursor);
void traash_vt_feed(TraashVt *vt, const uint8_t *data, size_t n);
void traash_vt_feed_byte(TraashVt *vt, uint8_t b);

#endif
