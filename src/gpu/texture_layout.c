#include <stdint.h>
#include <string.h>

#include "d3d/d3d8_swizzle.h"
#include "texture_layout.h"

static unsigned texel_bytes(uint32_t format)
{
    switch (format) {
    case 0x00: case 0x01: case 0x0B: case 0x13: case 0x19: case 0x1B: case 0x1F:
        return 1;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11: case 0x1A:
    case 0x1C: case 0x1D: case 0x20: case 0x27: case 0x28: case 0x29: case 0x30:
        return 2;
    default:
        return 4;
    }
}

static int checked_add(size_t a, size_t b, size_t *out)
{
    if (b > SIZE_MAX - a)
        return 0;
    *out = a + b;
    return 1;
}

static int checked_mul(size_t a, size_t b, size_t *out)
{
    if (a && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

int gpu_texture_layout(const Nv2aTexture *texture, int faces, int enable_mips,
                       GpuTextureLayout *layout)
{
    GpuTextureSubresource levels[GPU_TEXTURE_MAX_LEVELS];
    size_t total = 0;
    size_t stride;
    size_t span = 0;
    uint32_t width;
    uint32_t height;
    uint32_t count;
    uint32_t exposed;
    uint32_t natural = 1;
    uint32_t i;
    uint32_t face;
    uint32_t block;

    if (!layout)
        return 0;
    memset(layout, 0, sizeof *layout);
    if (!texture || !texture->width || !texture->height ||
        texture->width > 4096 || texture->height > 4096 ||
        texture->levels > GPU_TEXTURE_MAX_LEVELS ||
        (faces != 1 && faces != 6) ||
        (faces == 6 && texture->width != texture->height &&
         (enable_mips || texture->color == 0x0B)))
        return 0;

    block = d3d8_format_dxt_block_bytes(texture->color);
    layout->storage = block ? GPU_TEXTURE_BC
                    : d3d8_format_is_swizzled(texture->color)
                    ? GPU_TEXTURE_SWIZZLED : GPU_TEXTURE_LINEAR;
    if (layout->storage == GPU_TEXTURE_SWIZZLED &&
        ((texture->width & (texture->width - 1)) ||
         (texture->height & (texture->height - 1)))) {
        if (enable_mips || texture->color == 0x0B ||
            swizzle_offset(texture->width - 1, texture->height - 1,
                           texture->width, texture->height) >=
                (size_t)texture->width * texture->height)
            return 0;
    }
    if (layout->storage == GPU_TEXTURE_LINEAR && !texture->pitch)
        return 0;
    layout->format = texture->color;
    layout->faces = (uint32_t)faces;
    layout->declared_levels = texture->levels;
    layout->texel_bytes = texel_bytes(texture->color);
    layout->mips_enabled = enable_mips != 0;
    width = texture->width > texture->height ? texture->width : texture->height;
    while (width > 1) {
        width >>= 1;
        ++natural;
    }
    count = layout->storage == GPU_TEXTURE_LINEAR
          ? 1 : (texture->levels ? texture->levels : 1);
    exposed = enable_mips ? (count < natural ? count : natural) : 1;
    layout->levels = exposed;
    width = texture->width;
    height = texture->height;
    for (i = 0; i < count; ++i) {
        size_t pitch;
        size_t size;
        size_t used;

        if (layout->storage == GPU_TEXTURE_BC) {
            if (!checked_mul(((size_t)width + 3) / 4, block, &pitch) ||
                !checked_mul(pitch, ((size_t)height + 3) / 4, &size))
                return 0;
        } else if (layout->storage == GPU_TEXTURE_SWIZZLED) {
            if (!checked_mul(width, layout->texel_bytes, &pitch) ||
                !checked_mul(pitch, height, &size))
                return 0;
        } else {
            size_t row_bytes;

            pitch = texture->pitch;
            if (enable_mips && (texture->color == 0x24 || texture->color == 0x25)) {
                if (!checked_mul(((size_t)width + 1) / 2, 4, &row_bytes))
                    return 0;
            } else if (!checked_mul(width, layout->texel_bytes, &row_bytes)) {
                return 0;
            }
            if (!checked_mul(pitch, height, &size) ||
                !checked_mul(pitch, height - 1, &used) ||
                !checked_add(used, row_bytes, &used))
                return 0;
            if (used > size)
                size = used;
        }
        levels[i].width = width;
        levels[i].height = height;
        levels[i].offset = total;
        levels[i].size = size;
        levels[i].row_pitch = pitch;
        if (!checked_add(total, size, &total))
            return 0;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    stride = total;
    if (faces == 6) {
        if (layout->storage == GPU_TEXTURE_LINEAR &&
            !checked_mul(texture->pitch, texture->height, &stride))
            return 0;
        if (!checked_add(stride, 127, &stride))
            return 0;
        stride &= ~(size_t)127;
    }
    layout->face_stride = stride;
    for (face = 0; face < (uint32_t)faces; ++face) {
        size_t face_offset;

        if (!checked_mul(face, stride, &face_offset))
            return 0;
        for (i = 0; i < exposed; ++i) {
            GpuTextureSubresource *subresource = &layout->subresource[face][i];
            size_t end;

            *subresource = levels[i];
            if (!checked_add(face_offset, subresource->offset, &subresource->offset) ||
                !checked_add(subresource->offset, subresource->size, &end))
                return 0;
            if (end > span)
                span = end;
        }
    }
    if (!span || span > GPU_TEXTURE_MAX_SPAN)
        return 0;
    layout->span = span;
    return 1;
}

size_t gpu_texture_face_stride(const Nv2aTexture *texture)
{
    GpuTextureLayout layout;

    return gpu_texture_layout(texture, 6, 0, &layout) ? layout.face_stride : 0;
}

size_t gpu_texture_span(const Nv2aTexture *texture, int faces)
{
    GpuTextureLayout layout;

    return gpu_texture_layout(texture, faces, 0, &layout) ? layout.span : 0;
}
