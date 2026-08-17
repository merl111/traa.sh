#include "mux/pane.h"

#include "util/log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void pane_vt_reply(void *ud, const char *data, size_t n) {
  TraashPane *p = ud;
  if (!p || p->pty.master_fd < 0 || !data || n == 0) {
    return;
  }
  traash_pty_write(&p->pty, (const uint8_t *)data, n);
}

TraashPane *traash_pane_create(int id, int cols, int rows) {
  TraashPane *p = calloc(1, sizeof(*p));
  if (!p) {
    return NULL;
  }
  p->id = id;
  p->x = 0;
  p->y = 0;
  p->w = 1;
  p->h = 1;
  if (traash_screen_init(&p->screen, cols, rows, 5000) != 0) {
    free(p);
    return NULL;
  }
  traash_vt_init(&p->vt, &p->screen);
  p->vt.ud = p;
  p->vt.reply = pane_vt_reply;
  p->pty.master_fd = -1;
  snprintf(p->title, sizeof(p->title), "pane-%d", id);
  return p;
}

void traash_pane_destroy(TraashPane *p) {
  if (!p) {
    return;
  }
  traash_pty_close(&p->pty);
  traash_screen_free(&p->screen);
  free(p);
}

int traash_pane_spawn_shell(TraashPane *p) {
  return traash_pty_open(&p->pty, p->screen.cols, p->screen.rows, NULL, NULL);
}

int traash_pane_poll_n(TraashPane *p, int max_chunks) {
  if (p->pty.master_fd < 0) {
    return 1;
  }
  if (max_chunks < 1) {
    max_chunks = 1;
  }
  uint8_t buf[4096];
  int chunks = 0;
  for (; chunks < max_chunks; chunks++) {
    ssize_t n = traash_pty_read(&p->pty, buf, sizeof(buf));
    if (n > 0) {
      traash_vt_feed(&p->vt, buf, (size_t)n);
      p->screen.activity_pending = 1;
      if (p->screen.title[0]) {
        snprintf(p->title, sizeof(p->title), "%s", p->screen.title);
      }
      continue;
    }
    if (n == 0) {
      traash_pty_close(&p->pty);
      return 1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    traash_pty_close(&p->pty);
    return 1;
  }
  if (traash_pty_child_exited(&p->pty)) {
    for (int i = 0; i < max_chunks; i++) {
      ssize_t n = traash_pty_read(&p->pty, buf, sizeof(buf));
      if (n > 0) {
        traash_vt_feed(&p->vt, buf, (size_t)n);
        p->screen.activity_pending = 1;
        continue;
      }
      break;
    }
    traash_pty_close(&p->pty);
    return 1;
  }
  return 0;
}

int traash_pane_poll(TraashPane *p) {
  return traash_pane_poll_n(p, 16);
}

void traash_pane_resize_cells(TraashPane *p, int cols, int rows) {
  if (cols < 1) {
    cols = 1;
  }
  if (rows < 1) {
    rows = 1;
  }
  /* Already at this size and nothing deferred. */
  if (p->screen.cols == cols && p->screen.rows == rows && p->pty_pending_cols < 1 &&
      p->screen.scrollback_cols == cols) {
    return;
  }

  /* Update the visible grid every frame so resize feels continuous. */
  if (p->screen.cols != cols || p->screen.rows != rows) {
    traash_screen_resize_live(&p->screen, cols, rows);
  }

  /* Same pending target — keep the settle timer running (do not reset). */
  if (p->pty_pending_cols == cols && p->pty_pending_rows == rows) {
    return;
  }
  /* Defer scrollback reflow + SIGWINCH until the drag settles. */
  p->pty_pending_cols = cols;
  p->pty_pending_rows = rows;
  p->pty_resize_at = -1.0;
}

void traash_pane_flush_pty_resize(TraashPane *p, double now) {
  if (!p || p->pty.master_fd < 0) {
    return;
  }
  if (p->pty_pending_cols < 1 || p->pty_pending_rows < 1) {
    return;
  }
  if (p->pty_resize_at < 0.0) {
    p->pty_resize_at = now;
    return;
  }
  /* ~150ms settle after last size change — avoid mid-drag scrollback reflow. */
  if (now - p->pty_resize_at < 0.15) {
    return;
  }
  int cols = p->pty_pending_cols;
  int rows = p->pty_pending_rows;
  if (p->screen.cols != cols || p->screen.rows != rows) {
    traash_screen_resize_live(&p->screen, cols, rows);
  }
  if (p->screen.scrollback_cols != cols) {
    traash_screen_sync_scrollback(&p->screen);
  }
  if (p->pty.cols != cols || p->pty.rows != rows) {
    traash_pty_resize(&p->pty, cols, rows);
  }
  p->pty_pending_cols = 0;
  p->pty_pending_rows = 0;
  p->pty_resize_at = 0.0;
}
