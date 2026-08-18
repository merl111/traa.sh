#ifndef TRAASH_CLIENT_H
#define TRAASH_CLIENT_H

#include "crypto/secret.h"
#include "mux/session.h"

#include <stddef.h>
#include <stdint.h>

typedef struct TraashMuxConnection {
  int fd;
  int role;
  int connected;
  TraashSession *session;
  uint8_t *read_buf;
  size_t read_len;
  size_t read_cap;
  int active_pane_id;
} TraashMuxConnection;

int traash_mux_connect_unix(const char *path, TraashMuxConnection *conn);
int traash_mux_connect_tcp(const char *host, int port, TraashMuxConnection *conn);

int traash_mux_client_auth(TraashMuxConnection *conn, const char *session,
                           const char *password, int force_read_only, int *role_out);

int traash_mux_client_send_input(TraashMuxConnection *conn, int pane_id, const uint8_t *data,
                                 size_t n);
int traash_mux_client_send_action(TraashMuxConnection *conn, uint32_t action,
                                  const uint8_t *extra, uint32_t extra_len);

int traash_mux_client_poll(TraashMuxConnection *conn);

void traash_mux_client_close(TraashMuxConnection *conn);

int traash_mux_client_request_json(TraashMuxConnection *conn, uint32_t req_type, const char *json_in,
                                   uint32_t expect_type, char *json_out, size_t json_out_len);

int traash_mux_client_read_event(TraashMuxConnection *conn, char *json_out, size_t json_out_len);

/* Returns 1 if a mux server appears to be listening. */
int traash_mux_server_running(void);

#endif
