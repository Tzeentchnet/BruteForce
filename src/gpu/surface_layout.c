#include "surface_layout.h"

#include <limits.h>
#include <string.h>

int gpu_surface_layout(const Nv2aSurface *surface, GpuSurfaceLayout *layout)
{
    GpuSurfaceLayout result;
    uint64_t row_bytes;
    uint64_t span_bytes;
    uint32_t pitched_width;

    if (!surface || !layout || !surface->width || !surface->height || !surface->pitch)
        return 0;
    memset(&result, 0, sizeof result);
    result.bytes_per_pixel = surface->bytes_per_pixel ? surface->bytes_per_pixel : 4;
    if (result.bytes_per_pixel != 2 && result.bytes_per_pixel != 4)
        return 0;
    row_bytes = (uint64_t)surface->width * result.bytes_per_pixel;
    if (row_bytes > surface->pitch)
        return 0;
    pitched_width = surface->pitch / result.bytes_per_pixel;
    result.width = pitched_width;
    result.height = surface->height;
    result.pitch = surface->pitch;
    row_bytes = (uint64_t)result.width * result.bytes_per_pixel;
    span_bytes = (uint64_t)(result.height - 1) * result.pitch + row_bytes;
    if (!row_bytes || row_bytes > SIZE_MAX || span_bytes > SIZE_MAX)
        return 0;
    result.row_bytes = (size_t)row_bytes;
    result.span_bytes = (size_t)span_bytes;
    *layout = result;
    return 1;
}
