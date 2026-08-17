#ifndef TRAASH_UTF8_H
#define TRAASH_UTF8_H

#include <stddef.h>
#include <stdint.h>

/* Decode one UTF-8 codepoint. Returns bytes consumed (0 on error). */
int traash_utf8_decode(const uint8_t *s, size_t n, uint32_t *out_cp);

/* Encode codepoint into buf (max 4). Returns bytes written. */
int traash_utf8_encode(uint32_t cp, uint8_t out[4]);

/* Display width: 0 combining, 1 narrow, 2 wide (simplified). */
int traash_utf8_width(uint32_t cp);

#endif
