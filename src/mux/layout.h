#ifndef TRAASH_LAYOUT_H
#define TRAASH_LAYOUT_H

#include "mux/pane.h"

void traash_layout_split_pair(TraashPane *a, TraashPane *b, int vertical);
void traash_layout_apply_pixel_sizes(TraashPane *panes, int win_cols, int win_rows,
                                     int *out_cols, int *out_rows);
/* Give remaining panes equal horizontal tiles after a close. */
void traash_layout_reflow_equal(TraashPane *panes);

#endif
