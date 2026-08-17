#include "util/ring.h"

#include <stdlib.h>
#include <string.h>

int traash_ring_init(TraashRing *r, size_t cap) {
  memset(r, 0, sizeof(*r));
  r->data = (uint8_t *)malloc(cap);
  if (!r->data) {
    return -1;
  }
  r->cap = cap;
  return 0;
}

void traash_ring_free(TraashRing *r) {
  free(r->data);
  memset(r, 0, sizeof(*r));
}

size_t traash_ring_write(TraashRing *r, const uint8_t *src, size_t n) {
  size_t written = 0;
  while (written < n && r->len < r->cap) {
    r->data[r->head] = src[written++];
    r->head = (r->head + 1) % r->cap;
    r->len++;
  }
  return written;
}

size_t traash_ring_read(TraashRing *r, uint8_t *dst, size_t n) {
  size_t readn = 0;
  while (readn < n && r->len > 0) {
    dst[readn++] = r->data[r->tail];
    r->tail = (r->tail + 1) % r->cap;
    r->len--;
  }
  return readn;
}

size_t traash_ring_len(const TraashRing *r) {
  return r->len;
}
