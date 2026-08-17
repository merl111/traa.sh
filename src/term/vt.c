#include "term/vt.h"

#include "util/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void traash_vt_init(TraashVt *vt, TraashScreen *screen) {
  memset(vt, 0, sizeof(*vt));
  vt->screen = screen;
  vt->state = VT_GROUND;
  /* Sensible dark defaults until the host theme pushes real colors. */
  vt->report_fg = 0xc0caf5;
  vt->report_bg = 0x1a1b26;
  vt->report_cursor = 0xc0caf5;
}

void traash_vt_set_report_colors(TraashVt *vt, uint32_t fg, uint32_t bg, uint32_t cursor) {
  if (!vt) {
    return;
  }
  vt->report_fg = fg;
  vt->report_bg = bg;
  vt->report_cursor = cursor;
}

static void vt_reply(TraashVt *vt, const char *data, size_t n) {
  if (vt->reply && data && n > 0) {
    vt->reply(vt->ud, data, n);
  }
}

static void reset_csi(TraashVt *vt) {
  vt->csi_len = 0;
  vt->csi_argc = 0;
  vt->csi_collect = 0;
  memset(vt->csi_args, 0, sizeof(vt->csi_args));
  memset(vt->csi_colon, 0, sizeof(vt->csi_colon));
  memset(vt->csi_buf, 0, sizeof(vt->csi_buf));
}

static int csi_arg(TraashVt *vt, int idx, int defv) {
  if (idx >= vt->csi_argc || vt->csi_args[idx] == 0) {
    return defv;
  }
  return vt->csi_args[idx];
}

static void parse_csi_args(TraashVt *vt) {
  vt->csi_argc = 0;
  int val = 0;
  int have = 0;
  int next_colon = 0;
  const int max_args = (int)(sizeof(vt->csi_args) / sizeof(vt->csi_args[0]));
  for (int i = 0; i < vt->csi_len; i++) {
    char c = vt->csi_buf[i];
    if (c >= '0' && c <= '9') {
      val = val * 10 + (c - '0');
      have = 1;
    } else if (c == ';' || c == ':') {
      if (vt->csi_argc < max_args) {
        vt->csi_colon[vt->csi_argc] = (uint8_t)next_colon;
        vt->csi_args[vt->csi_argc++] = have ? val : 0;
      }
      next_colon = (c == ':') ? 1 : 0;
      val = 0;
      have = 0;
    }
  }
  if (vt->csi_argc < max_args) {
    vt->csi_colon[vt->csi_argc] = (uint8_t)next_colon;
    vt->csi_args[vt->csi_argc++] = have ? val : 0;
  }
}

static void apply_truecolor(TraashVt *vt, int *i, int is_bg) {
  TraashScreen *s = vt->screen;
  if (*i + 1 >= vt->csi_argc) {
    return;
  }
  int mode = vt->csi_args[++(*i)];
  if (mode == 5 && *i + 1 < vt->csi_argc) {
    uint32_t idx = (uint32_t)vt->csi_args[++(*i)];
    if (is_bg) {
      s->bg = idx;
      s->attrs &= ~TRAASH_ATTR_TRUECOLOR_BG;
    } else {
      s->fg = idx;
      s->attrs &= ~TRAASH_ATTR_TRUECOLOR_FG;
    }
  } else if (mode == 2) {
    /* Optional color-space id when using colon form: 38:2::R:G:B or 38:2:Cs:R:G:B */
    int remain = vt->csi_argc - (*i + 1);
    if (remain >= 4) {
      (*i)++; /* skip colorspace */
    }
    if (*i + 3 < vt->csi_argc) {
      uint32_t r = (uint32_t)vt->csi_args[++(*i)];
      uint32_t g = (uint32_t)vt->csi_args[++(*i)];
      uint32_t b = (uint32_t)vt->csi_args[++(*i)];
      uint32_t rgb = (r << 16) | (g << 8) | b;
      if (is_bg) {
        s->bg = rgb;
        s->attrs |= TRAASH_ATTR_TRUECOLOR_BG;
      } else {
        s->fg = rgb;
        s->attrs |= TRAASH_ATTR_TRUECOLOR_FG;
      }
    }
  }
}

static void apply_sgr(TraashVt *vt) {
  TraashScreen *s = vt->screen;
  if (vt->csi_argc == 0) {
    s->attrs = 0;
    s->fg = 7;
    s->bg = 0;
    return;
  }
  for (int i = 0; i < vt->csi_argc; i++) {
    int p = vt->csi_args[i];
    switch (p) {
    case 0:
      s->attrs = 0;
      s->fg = 7;
      s->bg = 0;
      break;
    case 1:
      s->attrs |= TRAASH_ATTR_BOLD;
      break;
    case 2:
      s->attrs |= TRAASH_ATTR_FAINT;
      break;
    case 3:
      s->attrs |= TRAASH_ATTR_ITALIC;
      break;
    case 4:
      if (i + 1 < vt->csi_argc && vt->csi_colon[i + 1]) {
        int style = vt->csi_args[++i];
        s->attrs &= ~(TRAASH_ATTR_UNDERLINE | TRAASH_ATTR_UNDERCURL |
                      TRAASH_ATTR_UNDERDOUBLE);
        if (style == 1) {
          s->attrs |= TRAASH_ATTR_UNDERLINE;
        } else if (style == 2) {
          s->attrs |= TRAASH_ATTR_UNDERDOUBLE;
        } else if (style == 3 || style == 4 || style == 5) {
          /* curly / dotted / dashed — render as undercurl */
          s->attrs |= TRAASH_ATTR_UNDERCURL;
        }
      } else {
        s->attrs &= ~(TRAASH_ATTR_UNDERCURL | TRAASH_ATTR_UNDERDOUBLE);
        s->attrs |= TRAASH_ATTR_UNDERLINE;
      }
      break;
    case 7:
      s->attrs |= TRAASH_ATTR_INVERSE;
      break;
    case 9:
      s->attrs |= TRAASH_ATTR_STRIKE;
      break;
    case 22:
      s->attrs &= ~(TRAASH_ATTR_BOLD | TRAASH_ATTR_FAINT);
      break;
    case 23:
      s->attrs &= ~TRAASH_ATTR_ITALIC;
      break;
    case 24:
      s->attrs &=
          ~(TRAASH_ATTR_UNDERLINE | TRAASH_ATTR_UNDERCURL | TRAASH_ATTR_UNDERDOUBLE);
      break;
    case 27:
      s->attrs &= ~TRAASH_ATTR_INVERSE;
      break;
    case 39:
      s->fg = 7;
      s->attrs &= ~TRAASH_ATTR_TRUECOLOR_FG;
      break;
    case 49:
      s->bg = 0;
      s->attrs &= ~TRAASH_ATTR_TRUECOLOR_BG;
      break;
    case 38:
      apply_truecolor(vt, &i, 0);
      break;
    case 48:
      apply_truecolor(vt, &i, 1);
      break;
    default:
      if (p >= 30 && p <= 37) {
        s->fg = (uint32_t)(p - 30);
        s->attrs &= ~TRAASH_ATTR_TRUECOLOR_FG;
      } else if (p >= 40 && p <= 47) {
        s->bg = (uint32_t)(p - 40);
        s->attrs &= ~TRAASH_ATTR_TRUECOLOR_BG;
      } else if (p >= 90 && p <= 97) {
        s->fg = (uint32_t)(p - 90 + 8);
        s->attrs &= ~TRAASH_ATTR_TRUECOLOR_FG;
      } else if (p >= 100 && p <= 107) {
        s->bg = (uint32_t)(p - 100 + 8);
        s->attrs &= ~TRAASH_ATTR_TRUECOLOR_BG;
      }
      break;
    }
  }
}

static void reply_osc_color(TraashVt *vt, int code, uint32_t rgb) {
  unsigned r = (rgb >> 16) & 0xffu;
  unsigned g = (rgb >> 8) & 0xffu;
  unsigned b = rgb & 0xffu;
  char buf[64];
  int n;
  if (vt->osc_term_bel) {
    n = snprintf(buf, sizeof(buf), "\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", code, r, r, g, g,
                 b, b);
  } else {
    n = snprintf(buf, sizeof(buf), "\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\033\\", code, r, r, g, g,
                 b, b);
  }
  if (n > 0) {
    vt_reply(vt, buf, (size_t)n);
  }
}

static void set_mode(TraashVt *vt, int enable) {
  TraashScreen *s = vt->screen;
  for (int i = 0; i < vt->csi_argc; i++) {
    int mode = vt->csi_args[i];
    if (vt->csi_collect == '?') {
      switch (mode) {
      case 1: /* DECCKM — application cursor keys (tracked only) */
        vt->app_cursor = enable;
        break;
      case 7: /* DECAWM */
        s->auto_wrap = enable;
        break;
      case 25: /* show/hide cursor */
        s->cursor_visible = enable ? 1 : 0;
        break;
      case 1047:
      case 1049:
        if (enable) {
          if (mode == 1049) {
            traash_screen_save_cursor(s);
          }
          traash_screen_set_alt(s, 1);
        } else {
          traash_screen_set_alt(s, 0);
          if (mode == 1049) {
            traash_screen_restore_cursor(s);
          }
        }
        break;
      case 47:
        traash_screen_set_alt(s, enable);
        break;
      case 2004:
        s->bracketed_paste = enable;
        break;
      case 1004:
        s->focus_report = enable;
        break;
      case 2026:
        s->sync_output = enable;
        break;
      default:
        break;
      }
    } else if (vt->csi_collect == 0) {
      if (mode == 4) {
        vt->insert_mode = enable; /* IRM */
      }
    }
  }
}

static void reply_decrm(TraashVt *vt, int mode) {
  /* DECRPM: CSI ? mode ; value $ y  — value 1=set, 2=reset, 0=unknown */
  int value = 0;
  TraashScreen *s = vt->screen;
  switch (mode) {
  case 1:
    value = vt->app_cursor ? 1 : 2;
    break;
  case 7:
    value = s->auto_wrap ? 1 : 2;
    break;
  case 25:
    value = s->cursor_visible ? 1 : 2;
    break;
  case 47:
  case 1047:
  case 1049:
    value = s->alt_screen ? 1 : 2;
    break;
  case 2004:
    value = s->bracketed_paste ? 1 : 2;
    break;
  case 1004:
    value = s->focus_report ? 1 : 2;
    break;
  case 2026:
    value = s->sync_output ? 1 : 2;
    break;
  case 2027:
  case 2031:
  case 2048:
  case 69:
    value = 2;
    break;
  default:
    value = 0;
    break;
  }
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "\033[?%d;%d$y", mode, value);
  if (n > 0) {
    vt_reply(vt, buf, (size_t)n);
  }
}

static void hex_encode_cap(const char *name, char *out, size_t n) {
  size_t o = 0;
  for (const char *p = name; *p && o + 2 < n; p++) {
    static const char *hex = "0123456789ABCDEF";
    out[o++] = hex[(*p >> 4) & 0xF];
    out[o++] = hex[*p & 0xF];
  }
  out[o] = 0;
}

static int hex_match_cap(const char *hex, const char *name) {
  char enc[64];
  hex_encode_cap(name, enc, sizeof(enc));
  return strcmp(hex, enc) == 0;
}

static void exec_dcs(TraashVt *vt) {
  /* XTGETTCAP: DCS + q Pt ST — Pt is hex-encoded name[;name…] */
  if (vt->dcs_len >= 2 && vt->dcs_buf[0] == '+' && vt->dcs_buf[1] == 'q') {
    const char *pt = vt->dcs_buf + 2;
    char reply[512];
    size_t ro = 0;
    reply[ro++] = '\033';
    reply[ro++] = 'P';
    reply[ro++] = '1';
    reply[ro++] = '+';
    reply[ro++] = 'r';
    int any = 0;
    while (*pt && ro + 32 < sizeof(reply)) {
      char name_hex[64];
      size_t nh = 0;
      while (*pt && *pt != ';' && nh + 1 < sizeof(name_hex)) {
        name_hex[nh++] = *pt++;
      }
      name_hex[nh] = 0;
      if (*pt == ';') {
        pt++;
      }
      if (!name_hex[0]) {
        continue;
      }
      const char *val = NULL;
      if (hex_match_cap(name_hex, "Tc") || hex_match_cap(name_hex, "RGB")) {
        val = "1";
      } else if (hex_match_cap(name_hex, "setrgbf") || hex_match_cap(name_hex, "setrgbb")) {
        val = "";
      }
      if (!val) {
        continue;
      }
      if (any) {
        reply[ro++] = ';';
      }
      any = 1;
      for (const char *h = name_hex; *h && ro + 1 < sizeof(reply); h++) {
        reply[ro++] = *h;
      }
      reply[ro++] = '=';
      for (const char *v = val; *v && ro + 1 < sizeof(reply); v++) {
        reply[ro++] = *v;
      }
    }
    if (!any) {
      /* No known caps — still acknowledge with empty success for first request shape */
      ro = 0;
      reply[ro++] = '\033';
      reply[ro++] = 'P';
      reply[ro++] = '0';
      reply[ro++] = '+';
      reply[ro++] = 'r';
    }
    reply[ro++] = '\033';
    reply[ro++] = '\\';
    vt_reply(vt, reply, ro);
    return;
  }

  /* DECRQSS: DCS $ q Pt ST — Pt is "m" for SGR */
  if (vt->dcs_len >= 3 && vt->dcs_buf[0] == '$' && vt->dcs_buf[1] == 'q') {
    const char *pt = vt->dcs_buf + 2;
    if (pt[0] == 'm' && pt[1] == 0) {
      TraashScreen *s = vt->screen;
      char body[128];
      size_t bo = 0;
      body[bo++] = '0';
      if (s->attrs & TRAASH_ATTR_BOLD) {
        bo += (size_t)snprintf(body + bo, sizeof(body) - bo, ";1");
      }
      if (s->attrs & TRAASH_ATTR_ITALIC) {
        bo += (size_t)snprintf(body + bo, sizeof(body) - bo, ";3");
      }
      if (s->attrs & TRAASH_ATTR_UNDERCURL) {
        bo += (size_t)snprintf(body + bo, sizeof(body) - bo, ";4:3");
      } else if (s->attrs & TRAASH_ATTR_UNDERDOUBLE) {
        bo += (size_t)snprintf(body + bo, sizeof(body) - bo, ";4:2");
      } else if (s->attrs & TRAASH_ATTR_UNDERLINE) {
        bo += (size_t)snprintf(body + bo, sizeof(body) - bo, ";4");
      }
      if (s->attrs & TRAASH_ATTR_TRUECOLOR_FG) {
        uint32_t rgb = s->fg;
        bo += (size_t)snprintf(body + bo, sizeof(body) - bo, ";38:2:%u:%u:%u",
                               (rgb >> 16) & 0xffu, (rgb >> 8) & 0xffu, rgb & 0xffu);
      }
      if (s->attrs & TRAASH_ATTR_TRUECOLOR_BG) {
        uint32_t rgb = s->bg;
        bo += (size_t)snprintf(body + bo, sizeof(body) - bo, ";48:2:%u:%u:%u",
                               (rgb >> 16) & 0xffu, (rgb >> 8) & 0xffu, rgb & 0xffu);
      }
      char reply[192];
      int n = snprintf(reply, sizeof(reply), "\033P1$r%sm\033\\", body);
      if (n > 0) {
        vt_reply(vt, reply, (size_t)n);
      }
    }
  }
}

static void exec_csi(TraashVt *vt, char final) {
  TraashScreen *s = vt->screen;
  /* DECRQM: CSI ? Ps $ p — final is p, intermediate $ in buffer */
  int decrqm = 0;
  if (final == 'p' && vt->csi_collect == '?' && vt->csi_len > 0 &&
      vt->csi_buf[vt->csi_len - 1] == '$') {
    vt->csi_buf[--vt->csi_len] = 0;
    decrqm = 1;
  }
  parse_csi_args(vt);
  if (decrqm) {
    reply_decrm(vt, csi_arg(vt, 0, 0));
    return;
  }
  switch (final) {
  case 'm':
    apply_sgr(vt);
    break;
  case 'H':
  case 'f':
    traash_screen_move_cursor(s, csi_arg(vt, 1, 1) - 1, csi_arg(vt, 0, 1) - 1);
    break;
  case 'A':
    traash_screen_move_cursor(s, s->cursor_x, s->cursor_y - csi_arg(vt, 0, 1));
    break;
  case 'B':
    traash_screen_move_cursor(s, s->cursor_x, s->cursor_y + csi_arg(vt, 0, 1));
    break;
  case 'C':
    traash_screen_move_cursor(s, s->cursor_x + csi_arg(vt, 0, 1), s->cursor_y);
    break;
  case 'D':
    traash_screen_move_cursor(s, s->cursor_x - csi_arg(vt, 0, 1), s->cursor_y);
    break;
  case 'G':
    traash_screen_move_cursor(s, csi_arg(vt, 0, 1) - 1, s->cursor_y);
    break;
  case 'd': /* VPA */
    traash_screen_move_cursor(s, s->cursor_x, csi_arg(vt, 0, 1) - 1);
    break;
  case 'J':
    traash_screen_clear(s, csi_arg(vt, 0, 0));
    break;
  case 'K':
    traash_screen_clear_line(s, csi_arg(vt, 0, 0));
    break;
  case 'L':
    traash_screen_insert_lines(s, csi_arg(vt, 0, 1));
    break;
  case 'M':
    traash_screen_delete_lines(s, csi_arg(vt, 0, 1));
    break;
  case '@':
    traash_screen_insert_cells(s, csi_arg(vt, 0, 1));
    break;
  case 'P':
    traash_screen_delete_cells(s, csi_arg(vt, 0, 1));
    break;
  case 'X':
    traash_screen_erase_chars(s, csi_arg(vt, 0, 1));
    break;
  case 'S':
    traash_screen_scroll_up(s, csi_arg(vt, 0, 1));
    break;
  case 'T':
    traash_screen_scroll_down(s, csi_arg(vt, 0, 1));
    break;
  case 'r':
    traash_screen_set_scroll_region(s, csi_arg(vt, 0, 1) - 1,
                                    csi_arg(vt, 1, s->rows) - 1);
    break;
  case 's':
    if (vt->csi_collect == 0) {
      traash_screen_save_cursor(s);
    }
    break;
  case 'u':
    if (vt->csi_collect == 0) {
      traash_screen_restore_cursor(s);
    }
    /* CSI ? u — kitty keyboard query; ignore */
    break;
  case 'h':
    set_mode(vt, 1);
    break;
  case 'l':
    set_mode(vt, 0);
    break;
  case 'n':
    if (vt->csi_collect == 0) {
      int mode = csi_arg(vt, 0, 0);
      if (mode == 5) {
        vt_reply(vt, "\033[0n", 4);
      } else if (mode == 6) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "\033[%d;%dR", s->cursor_y + 1, s->cursor_x + 1);
        if (n > 0) {
          vt_reply(vt, buf, (size_t)n);
        }
      }
    }
    break;
  case 'c':
    if (vt->csi_collect == 0) {
      /* Modest DA — don't claim VT420-level features we lack. */
      vt_reply(vt, "\033[?62;1;6;22c", 13);
    } else if (vt->csi_collect == '>') {
      vt_reply(vt, "\033[>0;100;0c", 11);
    }
    break;
  case 'q':
    /* DECSCUSR: CSI Ps SP q — space is in csi_buf */
    if (vt->csi_len > 0 && vt->csi_buf[vt->csi_len - 1] == ' ') {
      int style = csi_arg(vt, 0, 0);
      /* 0/1 block blink, 2 block, 3/4 underline, 5/6 bar */
      if (style <= 2) {
        s->cursor_style = 0;
      } else if (style <= 4) {
        s->cursor_style = 2;
      } else {
        s->cursor_style = 1;
      }
    }
    break;
  default:
    break;
  }
}

static void exec_osc(TraashVt *vt) {
  int code = 0;
  const char *semi = strchr(vt->osc_buf, ';');
  if (!semi) {
    /* Some queries are bare codes; still allow numeric-only. */
    for (const char *p = vt->osc_buf; *p; p++) {
      if (*p < '0' || *p > '9') {
        return;
      }
      code = code * 10 + (*p - '0');
    }
    return;
  }
  for (const char *p = vt->osc_buf; p < semi; p++) {
    if (*p < '0' || *p > '9') {
      break;
    }
    code = code * 10 + (*p - '0');
  }
  const char *data = semi + 1;
  if (code == 0 || code == 2) {
    snprintf(vt->screen->title, sizeof(vt->screen->title), "%s", data);
  } else if (code == 7) {
    /* OSC 7 — current working directory (file://host/path or file:///path) */
    const char *path = data;
    if (strncmp(data, "file://", 7) == 0) {
      path = data + 7;
      const char *slash = strchr(path, '/');
      if (slash) {
        path = slash;
      }
    }
    snprintf(vt->screen->cwd, sizeof(vt->screen->cwd), "%s", path);
  } else if ((code == 10 || code == 11 || code == 12) && data[0] == '?' && data[1] == 0) {
    uint32_t color = vt->report_bg;
    if (code == 10) {
      color = vt->report_fg;
    } else if (code == 12) {
      color = vt->report_cursor;
    }
    reply_osc_color(vt, code, color);
  } else if (code == 133 && data[0] == 'D') {
    /* FinalTerm / iTerm2 shell integration: command finished (optional ;exit). */
    vt->screen->command_done_pending = 1;
  }
  if (vt->on_osc) {
    vt->on_osc(vt->ud, code, data);
  }
}

static void feed_ground_utf8(TraashVt *vt, const uint8_t **pp, const uint8_t *end) {
  uint32_t cp = 0;
  int n = traash_utf8_decode(*pp, (size_t)(end - *pp), &cp);
  if (n <= 0) {
    (*pp)++;
    return;
  }
  *pp += n;
  if (cp == 0x7F) {
    return;
  }
  traash_screen_put_codepoint(vt->screen, cp);
}

void traash_vt_feed_byte(TraashVt *vt, uint8_t b) {
  traash_vt_feed(vt, &b, 1);
}

void traash_vt_feed(TraashVt *vt, const uint8_t *data, size_t n) {
  const uint8_t *p = data;
  const uint8_t *end = data + n;
  while (p < end) {
    uint8_t b = *p;
    switch (vt->state) {
    case VT_GROUND:
      if (b == 0x1B) {
        vt->state = VT_ESC;
        p++;
      } else if (b == '\n' || b == 0x0B || b == 0x0C) {
        traash_screen_newline(vt->screen);
        p++;
      } else if (b == '\r') {
        traash_screen_cr(vt->screen);
        p++;
      } else if (b == '\b') {
        traash_screen_backspace(vt->screen);
        p++;
      } else if (b == '\t') {
        int nx = (vt->screen->cursor_x + 8) & ~7;
        if (nx >= vt->screen->cols) {
          nx = vt->screen->cols - 1;
        }
        traash_screen_move_cursor(vt->screen, nx, vt->screen->cursor_y);
        p++;
      } else if (b == 0x07) {
        vt->screen->bell_pending = 1;
        p++;
      } else if (b < 0x20) {
        p++;
      } else {
        feed_ground_utf8(vt, &p, end);
      }
      break;
    case VT_ESC:
      if (b == '[') {
        reset_csi(vt);
        vt->state = VT_CSI;
        p++;
      } else if (b == ']') {
        vt->osc_len = 0;
        vt->osc_buf[0] = 0;
        vt->osc_term_bel = 1;
        vt->state = VT_OSC;
        p++;
      } else if (b == 'P') {
        vt->dcs_len = 0;
        vt->dcs_buf[0] = 0;
        vt->state = VT_DCS;
        p++;
      } else if (b == '_') {
        vt->state = VT_APC;
        p++;
      } else if (b == '^') {
        vt->state = VT_PM;
        p++;
      } else if (b == 'c') {
        /* RIS rough reset */
        traash_screen_clear(vt->screen, 2);
        traash_screen_move_cursor(vt->screen, 0, 0);
        vt->screen->attrs = 0;
        vt->screen->fg = 7;
        vt->screen->bg = 0;
        vt->state = VT_GROUND;
        p++;
      } else if (b == '7' || b == '8' || b == 'D' || b == 'E' || b == 'M') {
        if (b == '7') {
          traash_screen_save_cursor(vt->screen);
        } else if (b == '8') {
          traash_screen_restore_cursor(vt->screen);
        } else if (b == 'D') {
          traash_screen_newline(vt->screen);
        } else if (b == 'E') {
          traash_screen_cr(vt->screen);
          traash_screen_newline(vt->screen);
        } else if (b == 'M') {
          if (vt->screen->cursor_y == vt->screen->scroll_top) {
            traash_screen_scroll_down(vt->screen, 1);
          } else {
            traash_screen_move_cursor(vt->screen, vt->screen->cursor_x,
                                      vt->screen->cursor_y - 1);
          }
        }
        vt->state = VT_GROUND;
        p++;
      } else if (b == '=' || b == '>') {
        /* Application / normal keypad — accept and ignore. */
        vt->state = VT_GROUND;
        p++;
      } else if (b == '(' || b == ')' || b == '*' || b == '+' || b == '-' || b == '.' ||
                 b == '/' || b == '#' || b == '%' || b == ' ') {
        /* Charset designation / alignment / UTF-8 — consume the final byte. */
        vt->state = VT_ESC_INTERMEDIATE;
        p++;
      } else {
        vt->state = VT_GROUND;
        p++;
      }
      break;
    case VT_ESC_INTERMEDIATE:
      /* Final byte of ESC intermediate sequences (e.g. ESC ( B). */
      vt->state = VT_GROUND;
      p++;
      break;
    case VT_CSI:
      if (b >= 0x40 && b <= 0x7E) {
        exec_csi(vt, (char)b);
        vt->state = VT_GROUND;
        p++;
      } else if (b == '?' || b == '>' || b == '!' || b == '=') {
        vt->csi_collect = b;
        p++;
      } else if (vt->csi_len + 1 < (int)sizeof(vt->csi_buf)) {
        vt->csi_buf[vt->csi_len++] = (char)b;
        p++;
      } else {
        vt->state = VT_GROUND;
        p++;
      }
      break;
    case VT_OSC:
      if (b == 0x07) {
        vt->osc_buf[vt->osc_len] = 0;
        vt->osc_term_bel = 1;
        exec_osc(vt);
        vt->state = VT_GROUND;
        p++;
      } else if (b == 0x1B) {
        p++;
        if (p < end && *p == '\\') {
          vt->osc_buf[vt->osc_len] = 0;
          vt->osc_term_bel = 0;
          exec_osc(vt);
          vt->state = VT_GROUND;
          p++;
        } else {
          /* Nested ESC — terminate OSC and re-process. */
          vt->osc_buf[vt->osc_len] = 0;
          vt->osc_term_bel = 0;
          exec_osc(vt);
          vt->state = VT_ESC;
        }
      } else if (vt->osc_len + 1 < (int)sizeof(vt->osc_buf)) {
        vt->osc_buf[vt->osc_len++] = (char)b;
        p++;
      } else {
        p++;
      }
      break;
    case VT_DCS:
      if (b == 0x1B) {
        p++;
        if (p < end && *p == '\\') {
          vt->dcs_buf[vt->dcs_len] = 0;
          exec_dcs(vt);
          vt->state = VT_GROUND;
          p++;
        } else {
          vt->dcs_buf[vt->dcs_len] = 0;
          exec_dcs(vt);
          vt->state = VT_ESC;
        }
      } else if (b == 0x07) {
        /* BEL as ST (rare) */
        vt->dcs_buf[vt->dcs_len] = 0;
        exec_dcs(vt);
        vt->state = VT_GROUND;
        p++;
      } else if (vt->dcs_len + 1 < (int)sizeof(vt->dcs_buf)) {
        vt->dcs_buf[vt->dcs_len++] = (char)b;
        p++;
      } else {
        p++;
      }
      break;
    case VT_APC:
    case VT_PM:
      /* Swallow kitty graphics / privacy messages until ST. */
      if (b == 0x1B) {
        p++;
        if (p < end && *p == '\\') {
          vt->state = VT_GROUND;
          p++;
        } else {
          vt->state = VT_ESC;
        }
      } else if (b == 0x07) {
        vt->state = VT_GROUND;
        p++;
      } else {
        p++;
      }
      break;
    }
  }
}
