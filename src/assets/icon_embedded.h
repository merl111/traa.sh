#ifndef TRAASH_ICON_EMBEDDED_H
#define TRAASH_ICON_EMBEDDED_H

struct GLFWwindow;

/* Set window icon from pixels baked into the binary (X11). No-op on Wayland. */
void traash_window_set_icon(struct GLFWwindow *window);

#endif
