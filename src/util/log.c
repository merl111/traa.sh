#include "util/log.h"

#include <stdio.h>
#include <time.h>

static TraashLogLevel g_level = TRAASH_LOG_INFO;

void traash_log_set_level(TraashLogLevel level) {
  g_level = level;
}

void traash_log(TraashLogLevel level, const char *fmt, ...) {
  if (level > g_level) {
    return;
  }
  const char *tag = "INFO";
  switch (level) {
  case TRAASH_LOG_ERROR: tag = "ERROR"; break;
  case TRAASH_LOG_WARN: tag = "WARN"; break;
  case TRAASH_LOG_INFO: tag = "INFO"; break;
  case TRAASH_LOG_DEBUG: tag = "DEBUG"; break;
  }
  fprintf(stderr, "[traash:%s] ", tag);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}
