/* Persistent combiner pixel-shader bytecode cache; see shader_cache.c. */
#ifndef GPU_SHADER_CACHE_H
#define GPU_SHADER_CACHE_H

#include <stddef.h>

int gpu_pscache_init(void);
int gpu_pscache_enabled(void);
int gpu_pscache_load(const char *source, size_t length, const char *name, const char *entry,
                     const char *target, unsigned flags, void **bytes, size_t *size);
void gpu_pscache_store(const char *source, size_t length, const char *name, const char *entry,
                       const char *target, unsigned flags, const void *bytes, size_t size);

#endif
