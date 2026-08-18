#ifndef TRAASH_PASSWORD_H
#define TRAASH_PASSWORD_H

#include <stddef.h>

/* Prompt on /dev/tty; returns 0 on success. */
int traash_password_prompt(const char *label, char *out, size_t out_len);

/* Read one line from fd (no echo). Returns 0 on success. */
int traash_password_read_fd(int fd, char *out, size_t out_len);

/* Prompt twice and verify match (for setting passwords). */
int traash_password_prompt_pair(const char *label1, const char *label2, char *out,
                                size_t out_len);

#endif
