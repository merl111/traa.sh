#include "util/utf8.h"

int traash_utf8_decode(const uint8_t *s, size_t n, uint32_t *out_cp) {
  if (!s || n == 0 || !out_cp) {
    return 0;
  }
  uint8_t c0 = s[0];
  if (c0 < 0x80) {
    *out_cp = c0;
    return 1;
  }
  if ((c0 & 0xE0) == 0xC0) {
    if (n < 2 || (s[1] & 0xC0) != 0x80) {
      return 0;
    }
    *out_cp = ((uint32_t)(c0 & 0x1F) << 6) | (s[1] & 0x3F);
    return 2;
  }
  if ((c0 & 0xF0) == 0xE0) {
    if (n < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
      return 0;
    }
    *out_cp = ((uint32_t)(c0 & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) |
              (s[2] & 0x3F);
    return 3;
  }
  if ((c0 & 0xF8) == 0xF0) {
    if (n < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 ||
        (s[3] & 0xC0) != 0x80) {
      return 0;
    }
    *out_cp = ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
              ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    return 4;
  }
  return 0;
}

int traash_utf8_encode(uint32_t cp, uint8_t out[4]) {
  if (cp < 0x80) {
    out[0] = (uint8_t)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (uint8_t)(0xC0 | (cp >> 6));
    out[1] = (uint8_t)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (uint8_t)(0xE0 | (cp >> 12));
    out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (uint8_t)(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cp < 0x110000) {
    out[0] = (uint8_t)(0xF0 | (cp >> 18));
    out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
  }
  return 0;
}

int traash_utf8_width(uint32_t cp) {
  if (cp == 0 || (cp >= 0x0300 && cp <= 0x036F)) {
    return 0;
  }
  /* CJK / emoji rough ranges */
  if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
      (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
      (cp >= 0xFE10 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
      (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF)) {
    return 2;
  }
  return 1;
}
