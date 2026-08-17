#ifndef TRAASH_TEST_HARNESS_H
#define TRAASH_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_traash_tests_failed = 0;
static int g_traash_tests_run = 0;

#define TRAASH_CHECK(cond)                                                     \
  do {                                                                         \
    g_traash_tests_run++;                                                      \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_traash_tests_failed++;                                                 \
    }                                                                          \
  } while (0)

#define TRAASH_CHECK_EQ(a, b) TRAASH_CHECK((a) == (b))
#define TRAASH_CHECK_STREQ(a, b) TRAASH_CHECK(strcmp((a), (b)) == 0)

static inline int traash_test_report(void) {
  fprintf(stderr, "%d tests, %d failed\n", g_traash_tests_run, g_traash_tests_failed);
  return g_traash_tests_failed ? 1 : 0;
}

#endif
