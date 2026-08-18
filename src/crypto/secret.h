#ifndef TRAASH_SECRET_H
#define TRAASH_SECRET_H

#include <stddef.h>
#include <stdint.h>

enum {
  TRAASH_ROLE_NONE = 0,
  TRAASH_ROLE_READ = 1,
  TRAASH_ROLE_WRITE = 2,
};

enum {
  TRAASH_DEK_BYTES = 32,
  TRAASH_GCM_IV_BYTES = 12,
  TRAASH_GCM_TAG_BYTES = 16,
  TRAASH_KDF_SALT_BYTES = 16,
};

/* Encrypt plaintext with a random DEK; wrap DEK with write and read passwords. */
int traash_secret_encrypt(const uint8_t *plain, size_t plain_len, const char *write_pw,
                          const char *read_pw, uint8_t **out, size_t *out_len);

/* Decrypt blob; sets role to TRAASH_ROLE_WRITE or TRAASH_ROLE_READ. */
int traash_secret_decrypt(const uint8_t *blob, size_t blob_len, const char *password,
                          int force_read_only, uint8_t **plain, size_t *plain_len,
                          int *role_out);

void traash_secret_free(uint8_t *p);

#endif
