#ifndef TRAASH_LOG_H
#define TRAASH_LOG_H

#include <stdarg.h>

typedef enum {
  TRAASH_LOG_ERROR = 0,
  TRAASH_LOG_WARN,
  TRAASH_LOG_INFO,
  TRAASH_LOG_DEBUG
} TraashLogLevel;

void traash_log_set_level(TraashLogLevel level);
void traash_log(TraashLogLevel level, const char *fmt, ...);

#define TRAASH_LOGE(...) traash_log(TRAASH_LOG_ERROR, __VA_ARGS__)
#define TRAASH_LOGW(...) traash_log(TRAASH_LOG_WARN, __VA_ARGS__)
#define TRAASH_LOGI(...) traash_log(TRAASH_LOG_INFO, __VA_ARGS__)
#define TRAASH_LOGD(...) traash_log(TRAASH_LOG_DEBUG, __VA_ARGS__)

#endif
