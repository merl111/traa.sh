#include "crypto/secret.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdlib.h>
#include <string.h>

enum {
  TRAASH_SECRET_MAGIC_LEN = 6,
  TRAASH_SECRET_VERSION = 1,
  TRAASH_KDF_ITER = 120000,
};

static const uint8_t TRAASH_SECRET_MAGIC[TRAASH_SECRET_MAGIC_LEN] = {'T', 'R', 'S', 'N', '\x01', '\x00'};

typedef struct {
  uint8_t kdf_salt[TRAASH_KDF_SALT_BYTES];
  uint8_t iv[TRAASH_GCM_IV_BYTES];
  uint8_t tag[TRAASH_GCM_TAG_BYTES];
  uint8_t ct[TRAASH_DEK_BYTES + 1];
} TraashWrapBlock;

typedef struct {
  uint8_t iv[TRAASH_GCM_IV_BYTES];
  uint8_t tag[TRAASH_GCM_TAG_BYTES];
  uint8_t *ct;
  size_t ct_len;
} TraashPayloadBlock;

static int derive_key(const char *password, const uint8_t *salt, uint8_t *key_out) {
  if (!password || !salt || !key_out) {
    return -1;
  }
  if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, TRAASH_KDF_SALT_BYTES,
                        TRAASH_KDF_ITER, EVP_sha256(), TRAASH_DEK_BYTES, key_out) != 1) {
    return -1;
  }
  return 0;
}

static int aes_gcm_encrypt(const uint8_t *key, const uint8_t *iv, const uint8_t *in,
                           size_t in_len, uint8_t *out, uint8_t *tag) {
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    return -1;
  }
  int ok = 0;
  int out_len = 0;
  int total = 0;
  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
    goto done;
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, TRAASH_GCM_IV_BYTES, NULL) != 1) {
    goto done;
  }
  if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
    goto done;
  }
  if (EVP_EncryptUpdate(ctx, out, &out_len, in, (int)in_len) != 1) {
    goto done;
  }
  total = out_len;
  if (EVP_EncryptFinal_ex(ctx, out + total, &out_len) != 1) {
    goto done;
  }
  total += out_len;
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TRAASH_GCM_TAG_BYTES, tag) != 1) {
    goto done;
  }
  ok = total == (int)in_len ? 0 : -1;
done:
  EVP_CIPHER_CTX_free(ctx);
  return ok;
}

static int aes_gcm_decrypt(const uint8_t *key, const uint8_t *iv, const uint8_t *in,
                           size_t in_len, const uint8_t *tag, uint8_t *out) {
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    return -1;
  }
  int ok = -1;
  int out_len = 0;
  int total = 0;
  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
    goto done;
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, TRAASH_GCM_IV_BYTES, NULL) != 1) {
    goto done;
  }
  if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
    goto done;
  }
  if (EVP_DecryptUpdate(ctx, out, &out_len, in, (int)in_len) != 1) {
    goto done;
  }
  total = out_len;
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TRAASH_GCM_TAG_BYTES, (void *)tag) != 1) {
    goto done;
  }
  if (EVP_DecryptFinal_ex(ctx, out + total, &out_len) != 1) {
    goto done;
  }
  total += out_len;
  ok = total == (int)in_len ? 0 : -1;
done:
  EVP_CIPHER_CTX_free(ctx);
  return ok;
}

static int wrap_dek(const char *password, uint8_t role, const uint8_t *dek,
                    TraashWrapBlock *wrap) {
  uint8_t key[TRAASH_DEK_BYTES];
  uint8_t plain[TRAASH_DEK_BYTES + 1];
  if (RAND_bytes(wrap->kdf_salt, TRAASH_KDF_SALT_BYTES) != 1 ||
      RAND_bytes(wrap->iv, TRAASH_GCM_IV_BYTES) != 1) {
    return -1;
  }
  if (derive_key(password, wrap->kdf_salt, key) != 0) {
    return -1;
  }
  memcpy(plain, dek, TRAASH_DEK_BYTES);
  plain[TRAASH_DEK_BYTES] = role;
  return aes_gcm_encrypt(key, wrap->iv, plain, sizeof(plain), wrap->ct, wrap->tag);
}

static int unwrap_dek(const char *password, const TraashWrapBlock *wrap, uint8_t *dek_out,
                      uint8_t *role_out) {
  uint8_t key[TRAASH_DEK_BYTES];
  uint8_t plain[TRAASH_DEK_BYTES + 1];
  if (derive_key(password, wrap->kdf_salt, key) != 0) {
    return -1;
  }
  if (aes_gcm_decrypt(key, wrap->iv, wrap->ct, sizeof(wrap->ct), wrap->tag, plain) != 0) {
    return -1;
  }
  memcpy(dek_out, plain, TRAASH_DEK_BYTES);
  *role_out = plain[TRAASH_DEK_BYTES];
  return 0;
}

static size_t wrap_block_size(void) {
  return TRAASH_KDF_SALT_BYTES + TRAASH_GCM_IV_BYTES + TRAASH_GCM_TAG_BYTES +
         TRAASH_DEK_BYTES + 1;
}

static void write_wrap(uint8_t **p, const TraashWrapBlock *wrap) {
  memcpy(*p, wrap->kdf_salt, TRAASH_KDF_SALT_BYTES);
  *p += TRAASH_KDF_SALT_BYTES;
  memcpy(*p, wrap->iv, TRAASH_GCM_IV_BYTES);
  *p += TRAASH_GCM_IV_BYTES;
  memcpy(*p, wrap->tag, TRAASH_GCM_TAG_BYTES);
  *p += TRAASH_GCM_TAG_BYTES;
  memcpy(*p, wrap->ct, sizeof(wrap->ct));
  *p += sizeof(wrap->ct);
}

static void read_wrap(const uint8_t **p, TraashWrapBlock *wrap) {
  memcpy(wrap->kdf_salt, *p, TRAASH_KDF_SALT_BYTES);
  *p += TRAASH_KDF_SALT_BYTES;
  memcpy(wrap->iv, *p, TRAASH_GCM_IV_BYTES);
  *p += TRAASH_GCM_IV_BYTES;
  memcpy(wrap->tag, *p, TRAASH_GCM_TAG_BYTES);
  *p += TRAASH_GCM_TAG_BYTES;
  memcpy(wrap->ct, *p, sizeof(wrap->ct));
  *p += sizeof(wrap->ct);
}

int traash_secret_encrypt(const uint8_t *plain, size_t plain_len, const char *write_pw,
                          const char *read_pw, uint8_t **out, size_t *out_len) {
  if (!plain || !write_pw || !read_pw || !out || !out_len || !write_pw[0] || !read_pw[0]) {
    return -1;
  }
  uint8_t dek[TRAASH_DEK_BYTES];
  TraashWrapBlock wrap_rw;
  TraashWrapBlock wrap_ro;
  TraashPayloadBlock payload;
  if (RAND_bytes(dek, TRAASH_DEK_BYTES) != 1) {
    return -1;
  }
  if (wrap_dek(write_pw, TRAASH_ROLE_WRITE, dek, &wrap_rw) != 0 ||
      wrap_dek(read_pw, TRAASH_ROLE_READ, dek, &wrap_ro) != 0) {
    return -1;
  }
  payload.ct = malloc(plain_len);
  if (!payload.ct) {
    return -1;
  }
  payload.ct_len = plain_len;
  if (RAND_bytes(payload.iv, TRAASH_GCM_IV_BYTES) != 1) {
    free(payload.ct);
    return -1;
  }
  if (aes_gcm_encrypt(dek, payload.iv, plain, plain_len, payload.ct, payload.tag) != 0) {
    free(payload.ct);
    return -1;
  }

  size_t total = TRAASH_SECRET_MAGIC_LEN + 4 + wrap_block_size() * 2 + TRAASH_GCM_IV_BYTES +
                 TRAASH_GCM_TAG_BYTES + payload.ct_len;
  uint8_t *blob = malloc(total);
  if (!blob) {
    free(payload.ct);
    return -1;
  }
  uint8_t *p = blob;
  memcpy(p, TRAASH_SECRET_MAGIC, TRAASH_SECRET_MAGIC_LEN);
  p += TRAASH_SECRET_MAGIC_LEN;
  uint32_t ver = TRAASH_SECRET_VERSION;
  memcpy(p, &ver, 4);
  p += 4;
  write_wrap(&p, &wrap_rw);
  write_wrap(&p, &wrap_ro);
  memcpy(p, payload.iv, TRAASH_GCM_IV_BYTES);
  p += TRAASH_GCM_IV_BYTES;
  memcpy(p, payload.tag, TRAASH_GCM_TAG_BYTES);
  p += TRAASH_GCM_TAG_BYTES;
  memcpy(p, payload.ct, payload.ct_len);
  free(payload.ct);
  *out = blob;
  *out_len = total;
  return 0;
}

int traash_secret_decrypt(const uint8_t *blob, size_t blob_len, const char *password,
                          int force_read_only, uint8_t **plain, size_t *plain_len,
                          int *role_out) {
  if (!blob || !password || !plain || !plain_len || !role_out || !password[0]) {
    return -1;
  }
  size_t min = TRAASH_SECRET_MAGIC_LEN + 4 + wrap_block_size() * 2 + TRAASH_GCM_IV_BYTES +
               TRAASH_GCM_TAG_BYTES;
  if (blob_len < min || memcmp(blob, TRAASH_SECRET_MAGIC, TRAASH_SECRET_MAGIC_LEN) != 0) {
    return -1;
  }
  const uint8_t *p = blob + TRAASH_SECRET_MAGIC_LEN;
  uint32_t ver = 0;
  memcpy(&ver, p, 4);
  p += 4;
  if (ver != TRAASH_SECRET_VERSION) {
    return -1;
  }
  TraashWrapBlock wrap_rw;
  TraashWrapBlock wrap_ro;
  read_wrap(&p, &wrap_rw);
  read_wrap(&p, &wrap_ro);
  uint8_t iv[TRAASH_GCM_IV_BYTES];
  uint8_t tag[TRAASH_GCM_TAG_BYTES];
  memcpy(iv, p, TRAASH_GCM_IV_BYTES);
  p += TRAASH_GCM_IV_BYTES;
  memcpy(tag, p, TRAASH_GCM_TAG_BYTES);
  p += TRAASH_GCM_TAG_BYTES;
  size_t ct_len = blob_len - (size_t)(p - blob);
  if (ct_len == 0) {
    return -1;
  }

  uint8_t dek[TRAASH_DEK_BYTES];
  uint8_t role = TRAASH_ROLE_NONE;
  if (unwrap_dek(password, &wrap_rw, dek, &role) == 0) {
    if (role != TRAASH_ROLE_WRITE) {
      return -1;
    }
    *role_out = force_read_only ? TRAASH_ROLE_READ : TRAASH_ROLE_WRITE;
  } else if (unwrap_dek(password, &wrap_ro, dek, &role) == 0) {
    if (role != TRAASH_ROLE_READ) {
      return -1;
    }
    *role_out = TRAASH_ROLE_READ;
  } else {
    return -1;
  }

  uint8_t *out = malloc(ct_len);
  if (!out) {
    return -1;
  }
  if (aes_gcm_decrypt(dek, iv, p, ct_len, tag, out) != 0) {
    free(out);
    return -1;
  }
  *plain = out;
  *plain_len = ct_len;
  return 0;
}

void traash_secret_free(uint8_t *p) {
  free(p);
}
