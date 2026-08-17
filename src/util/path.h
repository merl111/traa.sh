#ifndef TRAASH_PATH_H
#define TRAASH_PATH_H

#include <stddef.h>

/* Resolve config dir into buf. Returns 0 on success. */
int traash_config_dir(char *buf, size_t n);

/* Resolve runtime dir for mux socket. */
int traash_runtime_dir(char *buf, size_t n);

/* Find bundled lua directory (env TRAASH_LUA_PATH, then next to binary, then source). */
int traash_lua_dir(char *buf, size_t n);

#endif
