#ifndef BRUTEFORCE_GPU_TEXTURE_LAYOUT_H
#define BRUTEFORCE_GPU_TEXTURE_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include "nv2a_backend.h"

#define GPU_TEXTURE_MAX_FACES 6
#define GPU_TEXTURE_MAX_LEVELS 15
#define GPU_TEXTURE_MAX_SPAN (64u << 20)

typedef enum GpuTextureStorage {
    GPU_TEXTURE_LINEAR,
    GPU_TEXTURE_SWIZZLED,
    GPU_TEXTURE_BC
} GpuTextureStorage;

typedef struct GpuTextureSubresource {
    uint32_t width;
    uint32_t height;
    size_t offset;
    size_t size;
    size_t row_pitch;
} GpuTextureSubresource;

typedef struct GpuTextureLayout {
    GpuTextureStorage storage;
    uint32_t format;
    uint32_t faces;
    uint32_t declared_levels;
    uint32_t levels;
    uint32_t texel_bytes;
    uint32_t mips_enabled;
    size_t face_stride;
    size_t span;
    GpuTextureSubresource subresource[GPU_TEXTURE_MAX_FACES][GPU_TEXTURE_MAX_LEVELS];
} GpuTextureLayout;

int gpu_texture_layout(const Nv2aTexture *texture, int faces, int enable_mips,
                       GpuTextureLayout *layout);
size_t gpu_texture_face_stride(const Nv2aTexture *texture);
size_t gpu_texture_span(const Nv2aTexture *texture, int faces);

#endif
