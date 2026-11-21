#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gpu/surface_layout.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void)
{
    Nv2aSurface surface;
    GpuSurfaceLayout layout;

    memset(&surface, 0, sizeof surface);
    surface.width = 250;
    surface.height = 480;
    surface.pitch = 640 * 4;
    surface.bytes_per_pixel = 4;
    CHECK(gpu_surface_layout(&surface, &layout));
    CHECK(layout.width == 640);
    CHECK(layout.height == 480);
    CHECK(layout.row_bytes == 2560);
    CHECK(layout.span_bytes == 2560u * 480u);

    surface.width = 320;
    surface.height = 240;
    surface.pitch = 640;
    surface.bytes_per_pixel = 2;
    CHECK(gpu_surface_layout(&surface, &layout));
    CHECK(layout.width == 320);
    CHECK(layout.row_bytes == 640);
    CHECK(layout.span_bytes == 640u * 240u);

    surface.bytes_per_pixel = 3;
    CHECK(!gpu_surface_layout(&surface, &layout));
    surface.bytes_per_pixel = 4;
    surface.pitch = 0;
    CHECK(!gpu_surface_layout(&surface, &layout));
    surface.pitch = 1279;
    CHECK(!gpu_surface_layout(&surface, &layout));
    surface.pitch = 1276;
    CHECK(!gpu_surface_layout(&surface, &layout));
    surface.width = 319;
    surface.pitch = 1279;
    CHECK(gpu_surface_layout(&surface, &layout));
    CHECK(layout.width == 319);
    CHECK(layout.row_bytes == 1276);
    CHECK(layout.span_bytes == 1279u * 239u + 1276u);
    surface.width = 320;
    surface.pitch = 1280;
    surface.height = 0;
    CHECK(!gpu_surface_layout(&surface, &layout));

    puts("Renderer-neutral surface layout: ok");
    return 0;
}
