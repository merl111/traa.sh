#ifndef TRAASH_PLATFORM_H
#define TRAASH_PLATFORM_H

#include <stddef.h>

void traash_clipboard_set(const char *text);
int traash_clipboard_get(char *buf, size_t n);
/* X11/Wayland PRIMARY selection (select-to-copy / middle-click paste). */
void traash_primary_set(const char *text);
int traash_primary_get(char *buf, size_t n);
void traash_open_url(const char *url);

#endif
