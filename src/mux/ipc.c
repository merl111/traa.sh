#include "mux/ipc.h"

#include <string.h>

int traash_ipc_encode(const TraashIpcMsg *msg, uint8_t *buf, size_t n, size_t *out) {
  size_t need = 8 + msg->len;
  if (n < need) {
    return -1;
  }
  buf[0] = (uint8_t)(msg->type & 0xff);
  buf[1] = (uint8_t)((msg->type >> 8) & 0xff);
  buf[2] = (uint8_t)((msg->type >> 16) & 0xff);
  buf[3] = (uint8_t)((msg->type >> 24) & 0xff);
  buf[4] = (uint8_t)(msg->len & 0xff);
  buf[5] = (uint8_t)((msg->len >> 8) & 0xff);
  buf[6] = (uint8_t)((msg->len >> 16) & 0xff);
  buf[7] = (uint8_t)((msg->len >> 24) & 0xff);
  if (msg->len) {
    memcpy(buf + 8, msg->payload, msg->len);
  }
  if (out) {
    *out = need;
  }
  return 0;
}

int traash_ipc_decode(const uint8_t *buf, size_t n, TraashIpcMsg *msg) {
  if (n < 8) {
    return -1;
  }
  msg->type = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
              ((uint32_t)buf[3] << 24);
  msg->len = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6] << 16) |
             ((uint32_t)buf[7] << 24);
  if (msg->len > sizeof(msg->payload) || n < 8 + msg->len) {
    return -1;
  }
  memset(msg->payload, 0, sizeof(msg->payload));
  if (msg->len) {
    memcpy(msg->payload, buf + 8, msg->len);
  }
  return 0;
}
