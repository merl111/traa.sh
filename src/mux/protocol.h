#ifndef TRAASH_PROTOCOL_H
#define TRAASH_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

enum {
  TRAASH_PROTO_AUTH = 1,
  TRAASH_PROTO_AUTH_OK = 2,
  TRAASH_PROTO_AUTH_ERR = 3,
  TRAASH_PROTO_SNAPSHOT = 4,
  TRAASH_PROTO_PTY_OUT = 5,
  TRAASH_PROTO_INPUT = 6,
  TRAASH_PROTO_ACTION = 7,
  TRAASH_PROTO_ACTION_DENIED = 8,
  TRAASH_PROTO_DETACH = 9,
  TRAASH_PROTO_PING = 10,
  TRAASH_PROTO_PONG = 11,
  TRAASH_PROTO_RESIZE = 12,
  TRAASH_PROTO_LIST = 13,
  TRAASH_PROTO_SESSION_LIST = 14,
  TRAASH_PROTO_CREATE = 15,
  TRAASH_PROTO_GET_STATE = 16,
  TRAASH_PROTO_STATE = 17,
  TRAASH_PROTO_WAIT_IDLE = 18,
  TRAASH_PROTO_IDLE = 19,
  TRAASH_PROTO_WAIT_TIMEOUT = 20,
  TRAASH_PROTO_SUBSCRIBE = 21,
  TRAASH_PROTO_UNSUBSCRIBE = 22,
  TRAASH_PROTO_EVENT = 23,
  TRAASH_PROTO_OK = 100,
  TRAASH_PROTO_ERR = 101,
};

typedef struct {
  uint32_t type;
  uint32_t len;
  uint8_t *payload;
} TraashProtoFrame;

int traash_proto_encode(uint32_t type, const uint8_t *payload, uint32_t len, uint8_t **out,
                        size_t *out_len);
int traash_proto_decode(const uint8_t *buf, size_t n, TraashProtoFrame *frame, size_t *consumed);

void traash_proto_frame_free(TraashProtoFrame *frame);

/* Build AUTH payload: u16 name_len + name + u16 pw_len + password */
int traash_proto_build_auth(const char *session, const char *password, uint8_t **out,
                            size_t *out_len);

#endif
