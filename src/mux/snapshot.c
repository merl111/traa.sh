#include "mux/snapshot.h"

#include "crypto/secret.h"
#include "mux/pane.h"
#include "mux/window.h"
#include "term/cell.h"
#include "util/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { TRAASH_SNAP_MAGIC = 0x54534e31u }; /* TSn1 */

static void write_u32(uint8_t **p, uint32_t v) {
  memcpy(*p, &v, 4);
  *p += 4;
}

static void write_u16(uint8_t **p, uint16_t v) {
  memcpy(*p, &v, 2);
  *p += 2;
}

static void write_f32(uint8_t **p, float v) {
  memcpy(*p, &v, 4);
  *p += 4;
}

static void write_str(uint8_t **p, const char *s) {
  uint16_t n = (uint16_t)strlen(s ? s : "");
  write_u16(p, n);
  if (n) {
    memcpy(*p, s, n);
    *p += n;
  }
}

static uint32_t read_u32(const uint8_t **p) {
  uint32_t v = 0;
  memcpy(&v, *p, 4);
  *p += 4;
  return v;
}

static uint16_t read_u16(const uint8_t **p) {
  uint16_t v = 0;
  memcpy(&v, *p, 2);
  *p += 2;
  return v;
}

static float read_f32(const uint8_t **p) {
  float v = 0;
  memcpy(&v, *p, 4);
  *p += 4;
  return v;
}

static int read_str(const uint8_t **p, const uint8_t *end, char *out, size_t outn) {
  if ((size_t)(end - *p) < 2) {
    return -1;
  }
  uint16_t n = read_u16(p);
  if ((size_t)(end - *p) < n || n >= outn) {
    return -1;
  }
  if (n) {
    memcpy(out, *p, n);
    *p += n;
  }
  out[n] = 0;
  return 0;
}

static size_t screen_payload_size(const TraashScreen *s) {
  size_t stride = (size_t)(s->scrollback_cols > 0 ? s->scrollback_cols : s->cols);
  return 4 * 8 + 256 + 512 + (size_t)s->cols * (size_t)s->rows * sizeof(TraashCell) +
         (size_t)s->scrollback_len * stride * sizeof(TraashCell) +
         (size_t)s->rows + (size_t)s->scrollback_len;
}

static void write_screen(uint8_t **p, const TraashScreen *s) {
  write_u32(p, (uint32_t)s->cols);
  write_u32(p, (uint32_t)s->rows);
  write_u32(p, (uint32_t)s->cursor_x);
  write_u32(p, (uint32_t)s->cursor_y);
  write_u32(p, (uint32_t)s->scrollback_len);
  write_u32(p, (uint32_t)s->scrollback_cols);
  write_u32(p, (uint32_t)s->scrollback_cap);
  write_u32(p, (uint32_t)s->scroll_offset);
  write_str(p, s->title);
  write_str(p, s->cwd);
  size_t live = (size_t)s->cols * (size_t)s->rows;
  memcpy(*p, s->cells, live * sizeof(TraashCell));
  *p += live * sizeof(TraashCell);
  int stride = s->scrollback_cols > 0 ? s->scrollback_cols : s->cols;
  for (int i = 0; i < s->scrollback_len; i++) {
    const TraashCell *row = &s->scrollback[((s->scrollback_start + i) % s->scrollback_cap) * stride];
    memcpy(*p, row, (size_t)stride * sizeof(TraashCell));
    *p += (size_t)stride * sizeof(TraashCell);
  }
  if (s->row_wrap && s->rows > 0) {
    memcpy(*p, s->row_wrap, (size_t)s->rows);
    *p += (size_t)s->rows;
  }
  if (s->sb_wrap && s->scrollback_len > 0) {
    for (int i = 0; i < s->scrollback_len; i++) {
      **p = s->sb_wrap[((s->scrollback_start + i) % s->scrollback_cap)];
      (*p)++;
    }
  }
}

static int read_screen(const uint8_t **p, const uint8_t *end, TraashScreen *s) {
  if ((size_t)(end - *p) < 32) {
    return -1;
  }
  int cols = (int)read_u32(p);
  int rows = (int)read_u32(p);
  int scrollback_cap = 5000;
  if (traash_screen_init(s, cols, rows, scrollback_cap) != 0) {
    return -1;
  }
  s->cursor_x = (int)read_u32(p);
  s->cursor_y = (int)read_u32(p);
  s->scrollback_len = (int)read_u32(p);
  s->scrollback_cols = (int)read_u32(p);
  int sb_cap_file = (int)read_u32(p);
  (void)sb_cap_file;
  s->scroll_offset = (int)read_u32(p);
  if (read_str(p, end, s->title, sizeof(s->title)) != 0 ||
      read_str(p, end, s->cwd, sizeof(s->cwd)) != 0) {
    return -1;
  }
  size_t live = (size_t)cols * (size_t)rows;
  if ((size_t)(end - *p) < live * sizeof(TraashCell)) {
    return -1;
  }
  memcpy(s->cells, *p, live * sizeof(TraashCell));
  *p += live * sizeof(TraashCell);
  int stride = s->scrollback_cols > 0 ? s->scrollback_cols : cols;
  for (int i = 0; i < s->scrollback_len; i++) {
    if ((size_t)(end - *p) < (size_t)stride * sizeof(TraashCell)) {
      return -1;
    }
    int phys = (s->scrollback_start + i) % s->scrollback_cap;
    memcpy(&s->scrollback[phys * stride], *p, (size_t)stride * sizeof(TraashCell));
    *p += (size_t)stride * sizeof(TraashCell);
  }
  if (s->row_wrap && rows > 0 && (size_t)(end - *p) >= (size_t)rows) {
    memcpy(s->row_wrap, *p, (size_t)rows);
    *p += (size_t)rows;
  }
  if (s->sb_wrap && s->scrollback_len > 0 &&
      (size_t)(end - *p) >= (size_t)s->scrollback_len) {
    for (int i = 0; i < s->scrollback_len; i++) {
      int phys = (s->scrollback_start + i) % s->scrollback_cap;
      s->sb_wrap[phys] = **p;
      (*p)++;
    }
  }
  s->dirty = 1;
  return 0;
}

static size_t pane_payload_size(const TraashPane *p) {
  return 4 + 4 * 4 + 256 + screen_payload_size(&p->screen);
}

static void write_pane(uint8_t **p, const TraashPane *pane) {
  write_u32(p, (uint32_t)pane->id);
  write_f32(p, pane->x);
  write_f32(p, pane->y);
  write_f32(p, pane->w);
  write_f32(p, pane->h);
  write_str(p, pane->title);
  write_screen(p, &pane->screen);
}

static TraashPane *read_pane(const uint8_t **p, const uint8_t *end, int cols, int rows) {
  if ((size_t)(end - *p) < 20) {
    return NULL;
  }
  int id = (int)read_u32(p);
  TraashPane *pane = traash_pane_create(id, cols, rows);
  if (!pane) {
    return NULL;
  }
  pane->x = read_f32(p);
  pane->y = read_f32(p);
  pane->w = read_f32(p);
  pane->h = read_f32(p);
  if (read_str(p, end, pane->title, sizeof(pane->title)) != 0) {
    traash_pane_destroy(pane);
    return NULL;
  }
  traash_screen_free(&pane->screen);
  if (read_screen(p, end, &pane->screen) != 0) {
    traash_pane_destroy(pane);
    return NULL;
  }
  traash_vt_init(&pane->vt, &pane->screen);
  pane->vt.ud = pane;
  return pane;
}

int traash_snapshot_encode(const TraashSession *s, uint8_t **out, size_t *out_len) {
  if (!s || !out || !out_len) {
    return -1;
  }
  size_t total = 4 + 64 + 4;
  for (const TraashWindow *w = s->windows; w; w = w->next) {
    total += 4 + 64 + 4 + 4;
    for (const TraashPane *p = w->panes; p; p = p->next) {
      total += pane_payload_size(p);
    }
  }
  uint8_t *buf = malloc(total);
  if (!buf) {
    return -1;
  }
  uint8_t *p = buf;
  write_u32(&p, TRAASH_SNAP_MAGIC);
  write_str(&p, s->name);
  write_u32(&p, (uint32_t)s->id);
  int win_count = 0;
  for (const TraashWindow *w = s->windows; w; w = w->next) {
    win_count++;
  }
  write_u32(&p, (uint32_t)win_count);
  for (const TraashWindow *w = s->windows; w; w = w->next) {
    write_u32(&p, (uint32_t)w->id);
    write_str(&p, w->name);
    int active_idx = 1;
    int idx = 1;
    for (const TraashPane *pp = w->panes; pp; pp = pp->next, idx++) {
      if (pp == w->active) {
        active_idx = idx;
      }
    }
    write_u32(&p, (uint32_t)active_idx);
    int pc = 0;
    for (const TraashPane *pp = w->panes; pp; pp = pp->next) {
      pc++;
    }
    write_u32(&p, (uint32_t)pc);
    for (const TraashPane *pp = w->panes; pp; pp = pp->next) {
      write_pane(&p, pp);
    }
  }
  *out = buf;
  *out_len = (size_t)(p - buf);
  return 0;
}

TraashSession *traash_snapshot_decode(const uint8_t *buf, size_t len, int cols, int rows) {
  if (!buf || len < 8) {
    return NULL;
  }
  const uint8_t *p = buf;
  const uint8_t *end = buf + len;
  if (read_u32(&p) != TRAASH_SNAP_MAGIC) {
    return NULL;
  }
  char name[64];
  if (read_str(&p, end, name, sizeof(name)) != 0) {
    return NULL;
  }
  int sid = (int)read_u32(&p);
  TraashSession *s = calloc(1, sizeof(*s));
  if (!s) {
    return NULL;
  }
  s->id = sid;
  snprintf(s->name, sizeof(s->name), "%s", name);
  s->next_window_id = 2;
  s->next_pane_id = 2;
  int win_count = (int)read_u32(&p);
  TraashWindow *last_w = NULL;
  for (int wi = 0; wi < win_count; wi++) {
    if ((size_t)(end - p) < 12) {
      traash_session_destroy(s);
      return NULL;
    }
    int wid = (int)read_u32(&p);
    char wname[64];
    if (read_str(&p, end, wname, sizeof(wname)) != 0) {
      traash_session_destroy(s);
      return NULL;
    }
    int active_idx = (int)read_u32(&p);
    int pc = (int)read_u32(&p);
    TraashWindow *w = calloc(1, sizeof(*w));
    if (!w) {
      traash_session_destroy(s);
      return NULL;
    }
    w->id = wid;
    snprintf(w->name, sizeof(w->name), "%s", wname);
    TraashPane *last_p = NULL;
    TraashPane *active_p = NULL;
    for (int pi = 0; pi < pc; pi++) {
      TraashPane *pane = read_pane(&p, end, cols, rows);
      if (!pane) {
        traash_window_destroy(w);
        traash_session_destroy(s);
        return NULL;
      }
      if (!w->panes) {
        w->panes = pane;
      } else {
        last_p->next = pane;
      }
      last_p = pane;
      if (pi + 1 == active_idx) {
        active_p = pane;
      }
      if (pane->id >= s->next_pane_id) {
        s->next_pane_id = pane->id + 1;
      }
    }
    w->active = active_p ? active_p : w->panes;
    if (!s->windows) {
      s->windows = w;
    } else {
      last_w->next = w;
    }
    last_w = w;
    if (wid >= s->next_window_id) {
      s->next_window_id = wid + 1;
    }
  }
  s->active = s->windows;
  for (TraashWindow *w = s->windows; w; w = w->next) {
    for (TraashPane *pane = w->panes; pane; pane = pane->next) {
      traash_pane_spawn_shell(pane);
    }
  }
  return s;
}

int traash_session_file_path(const char *name, char *buf, size_t n) {
  char data[512];
  if (traash_data_dir(data, sizeof(data)) != 0 || !name || !name[0]) {
    return -1;
  }
  snprintf(buf, n, "%s/sessions/%s.tsn", data, name);
  return 0;
}

int traash_session_file_exists(const char *name) {
  char path[768];
  if (traash_session_file_path(name, path, sizeof(path)) != 0) {
    return 0;
  }
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int traash_snapshot_save_encrypted(const char *name, const TraashSession *s,
                                   const char *write_pw, const char *read_pw) {
  uint8_t *plain = NULL;
  size_t plain_len = 0;
  if (traash_snapshot_encode(s, &plain, &plain_len) != 0) {
    return -1;
  }
  uint8_t *blob = NULL;
  size_t blob_len = 0;
  int r = traash_secret_encrypt(plain, plain_len, write_pw, read_pw, &blob, &blob_len);
  free(plain);
  if (r != 0) {
    return -1;
  }
  char path[768];
  char dir[512];
  if (traash_session_file_path(name, path, sizeof(path)) != 0) {
    traash_secret_free(blob);
    return -1;
  }
  snprintf(dir, sizeof(dir), "%s", path);
  char *slash = strrchr(dir, '/');
  if (slash) {
    *slash = 0;
    mkdir(dir, 0700);
  }
  FILE *f = fopen(path, "wb");
  if (!f) {
    traash_secret_free(blob);
    return -1;
  }
  size_t w = fwrite(blob, 1, blob_len, f);
  fclose(f);
  traash_secret_free(blob);
  if (w != blob_len) {
    return -1;
  }
  chmod(path, 0600);
  return 0;
}

int traash_snapshot_load_encrypted(const char *name, const char *password,
                                   int force_read_only, TraashSession **out, int *role_out) {
  char path[768];
  if (traash_session_file_path(name, path, sizeof(path)) != 0) {
    return -1;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) {
    fclose(f);
    return -1;
  }
  uint8_t *blob = malloc((size_t)sz);
  if (!blob) {
    fclose(f);
    return -1;
  }
  if (fread(blob, 1, (size_t)sz, f) != (size_t)sz) {
    free(blob);
    fclose(f);
    return -1;
  }
  fclose(f);
  uint8_t *plain = NULL;
  size_t plain_len = 0;
  int role = TRAASH_ROLE_NONE;
  if (traash_secret_decrypt(blob, (size_t)sz, password, force_read_only, &plain, &plain_len,
                            &role) != 0) {
    free(blob);
    return -1;
  }
  free(blob);
  TraashSession *s = traash_snapshot_decode(plain, plain_len, 80, 24);
  traash_secret_free(plain);
  if (!s) {
    return -1;
  }
  *out = s;
  if (role_out) {
    *role_out = role;
  }
  return 0;
}
