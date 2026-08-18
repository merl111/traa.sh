#include "mux/protocol.h"

#include <stdlib.h>
#include <string.h>

int traash_proto_encode(uint32_t type, const uint8_t *payload, uint32_t len, uint8_t **out,
                        size_t *out_len) {
  size_t total = 8 + len;
  uint8_t *buf = malloc(total);
  if (!buf) {
    return -1;
  }
  memcpy(buf, &type, 4);
  memcpy(buf + 4, &len, 4);
  if (len && payload) {
    memcpy(buf + 8, payload, len);
  }
  *out = buf;
  *out_len = total;
  return 0;
}

int traash_proto_decode(const uint8_t *buf, size_t n, TraashProtoFrame *frame, size_t *consumed) {
  if (n < 8) {
    return -1;
  }
  uint32_t type = 0;
  uint32_t len = 0;
  memcpy(&type, buf, 4);
  memcpy(&len, buf + 4, 4);
  if (n < 8 + len) {
    return -1;
  }
  frame->type = type;
  frame->len = len;
  frame->payload = NULL;
  if (len) {
    frame->payload = malloc(len);
    if (!frame->payload) {
      return -1;
    }
    memcpy(frame->payload, buf + 8, len);
  }
  if (consumed) {
    *consumed = 8 + len;
  }
  return 0;
}

void traash_proto_frame_free(TraashProtoFrame *frame) {
  if (!frame) {
    return;
  }
  free(frame->payload);
  frame->payload = NULL;
  frame->len = 0;
}

int traash_proto_build_auth(const char *session, const char *password, uint8_t **out,
                            size_t *out_len) {
  if (!session || !password) {
    return -1;
  }
  size_t sl = strlen(session);
  size_t pl = strlen(password);
  if (sl > 65535 || pl > 65535) {
    return -1;
  }
  uint32_t len = 4 + (uint32_t)sl + (uint32_t)pl;
  uint8_t *buf = malloc(len);
  if (!buf) {
    return -1;
  }
  uint16_t sn = (uint16_t)sl;
  uint16_t pn = (uint16_t)pl;
  memcpy(buf, &sn, 2);
  memcpy(buf + 2, session, sl);
  memcpy(buf + 2 + sl, &pn, 2);
  memcpy(buf + 4 + sl, password, pl);
  *out = buf;
  *out_len = len;
  return 0;
}
