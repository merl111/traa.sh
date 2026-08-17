#ifndef TRAASH_DEMO_H
#define TRAASH_DEMO_H

#include "mux/server.h"

#include <stdbool.h>

typedef struct {
  bool active;
  bool auto_mode;
  int step;
  double next_tick;
  TraashSession *prev;
} TraashDemo;

int traash_demo_start(TraashDemo *demo, TraashMuxServer *srv, void *lua, bool auto_mode);
void traash_demo_stop(TraashDemo *demo, TraashMuxServer *srv);
int traash_demo_update(TraashDemo *demo, TraashMuxServer *srv, void *lua, double now);
int traash_demo_key(TraashDemo *demo, TraashMuxServer *srv, void *lua, int key);

#endif
