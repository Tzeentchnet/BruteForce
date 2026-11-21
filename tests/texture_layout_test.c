#include <stdio.h>
#include <string.h>

#include "gpu/texture_layout.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void)
{
    Nv2aTexture texture;
    GpuTextureLayout layout;

    memset(&texture, 0, sizeof texture);
    texture.width = 4;
    texture.height = 4;
    texture.pitch = 16;
    texture.color = 0x12;
    texture.levels = 1;
    CHECK(gpu_texture_layout(&texture, 1, 0, &layout));
    CHECK(layout.storage == GPU_TEXTURE_LINEAR);
    CHECK(layout.levels == 1);
    CHECK(layout.span == 64);
    CHECK(layout.subresource[0][0].row_pitch == 16);

    texture.pitch = 0;
    texture.color = 0x06;
    CHECK(gpu_texture_layout(&texture, 1, 0, &layout));
    CHECK(layout.storage == GPU_TEXTURE_SWIZZLED);
    CHECK(layout.span == 64);

    texture.width = 8;
    texture.height = 8;
    texture.color = 0x0C;
    CHECK(gpu_texture_layout(&texture, 1, 0, &layout));
    CHECK(layout.storage == GPU_TEXTURE_BC);
    CHECK(layout.span == 32);
    CHECK(layout.subresource[0][0].row_pitch == 16);

    texture.color = 0x06;
    texture.levels = 4;
    CHECK(gpu_texture_layout(&texture, 1, 1, &layout));
    CHECK(layout.levels == 4);
    CHECK(layout.span == 340);
    CHECK(layout.subresource[0][1].offset == 256);
    CHECK(layout.subresource[0][3].offset == 336);
    CHECK(gpu_texture_layout(&texture, 1, 0, &layout));
    CHECK(layout.levels == 1);
    CHECK(layout.span == 256);

    texture.width = 1;
    texture.height = 1;
    texture.levels = 1;
    CHECK(gpu_texture_layout(&texture, 6, 0, &layout));
    CHECK(layout.face_stride == 128);
    CHECK(layout.span == 644);

    texture.width = 4;
    texture.height = 4;
    texture.color = 0x12;
    texture.pitch = 0;
    CHECK(!gpu_texture_layout(&texture, 1, 0, &layout));
    texture.width = 4097;
    texture.pitch = 4097 * 4;
    CHECK(!gpu_texture_layout(&texture, 1, 0, &layout));

    puts("Renderer-neutral texture layout: ok");
    return 0;
}
