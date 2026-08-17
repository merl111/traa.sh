#ifndef TRAASH_RING_H
#define TRAASH_RING_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t *data;
  size_t cap;
  size_t head;
  size_t tail;
  size_t len;
} TraashRing;

int traash_ring_init(TraashRing *r, size_t cap);
void traash_ring_free(TraashRing *r);
size_t traash_ring_write(TraashRing *r, const uint8_t *src, size_t n);
size_t traash_ring_read(TraashRing *r, uint8_t *dst, size_t n);
size_t traash_ring_len(const TraashRing *r);

#endif
