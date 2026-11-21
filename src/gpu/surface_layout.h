#ifndef BRUTEFORCE_GPU_SURFACE_LAYOUT_H
#define BRUTEFORCE_GPU_SURFACE_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include "nv2a_backend.h"

typedef struct GpuSurfaceLayout {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bytes_per_pixel;
    size_t row_bytes;
    size_t span_bytes;
} GpuSurfaceLayout;

int gpu_surface_layout(const Nv2aSurface *surface, GpuSurfaceLayout *layout);

#endif
