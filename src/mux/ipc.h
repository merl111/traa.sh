#ifndef TRAASH_IPC_H
#define TRAASH_IPC_H

#include <stddef.h>
#include <stdint.h>

enum {
  TRAASH_IPC_LIST = 1,
  TRAASH_IPC_ATTACH = 2,
  TRAASH_IPC_DETACH = 3,
  TRAASH_IPC_CREATE = 4,
  TRAASH_IPC_OK = 100,
  TRAASH_IPC_ERR = 101,
  TRAASH_IPC_SESSION_LIST = 102
};

typedef struct {
  uint32_t type;
  uint32_t len;
  char payload[512];
} TraashIpcMsg;

int traash_ipc_encode(const TraashIpcMsg *msg, uint8_t *buf, size_t n, size_t *out);
int traash_ipc_decode(const uint8_t *buf, size_t n, TraashIpcMsg *msg);

#endif
