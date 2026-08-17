#include "ui/status_bar.h"

#include <stdio.h>
#include <string.h>

void traash_status_bar_format(const TraashStatusModel *model, char *buf, size_t n) {
  size_t o = 0;
  buf[0] = 0;
  for (int i = 0; i < model->count; i++) {
    int m = snprintf(buf + o, n > o ? n - o : 0, "%s%s", i ? "  " : "", model->segs[i].text);
    if (m < 0) {
      break;
    }
    o += (size_t)m;
  }
}
