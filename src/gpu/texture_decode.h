#ifndef BRUTEFORCE_GPU_TEXTURE_DECODE_H
#define BRUTEFORCE_GPU_TEXTURE_DECODE_H

#include <d3d11.h>
#include <stddef.h>
#include "nv2a_backend.h"

#define GPU_TEXTURE_MAX_FACES 6
#define GPU_TEXTURE_MAX_LEVELS 15
#define GPU_TEXTURE_MAX_SPAN (64u << 20)

typedef enum {
    GPU_TEXTURE_LINEAR,
    GPU_TEXTURE_SWIZZLED,
    GPU_TEXTURE_BC
} GpuTextureStorage;

typedef struct {
    uint32_t width, height;
    size_t offset, size, row_pitch;
} GpuTextureSubresource;

typedef struct {
    GpuTextureStorage storage;
    uint32_t format, faces, declared_levels, levels, texel_bytes, mips_enabled;
    size_t face_stride, span;
    GpuTextureSubresource subresource[GPU_TEXTURE_MAX_FACES][GPU_TEXTURE_MAX_LEVELS];
} GpuTextureLayout;

typedef struct {
    ID3D11ComputeShader *shader;
    ID3D11Buffer *constants;
    int unavailable;
} GpuTextureDecoder;

int gpu_texture_layout(const Nv2aTexture *texture, int faces, int enable_mips,
                       GpuTextureLayout *layout);
size_t gpu_texture_face_stride(const Nv2aTexture *texture);
size_t gpu_texture_span(const Nv2aTexture *texture, int faces);
int gpu_texture_decode_supported(const Nv2aTexture *texture);
int gpu_texture_p8_indices_valid(const GpuTextureLayout *layout,
                                 const void *bytes, size_t byte_count,
                                 unsigned palette_entries,
                                 unsigned *invalid_index);
int gpu_texture_decode_p8_cpu(const GpuTextureLayout *layout, unsigned face,
                              unsigned level, const void *bytes, size_t byte_count,
                              const void *palette, unsigned palette_entries,
                              uint32_t *output, size_t output_texels);
int gpu_texture_decode(GpuTextureDecoder *decoder, ID3D11Device *device,
                       ID3D11DeviceContext *context, const Nv2aTexture *texture,
                       const GpuTextureLayout *layout, const void *bytes,
                       size_t byte_count, const void *palette, unsigned palette_entries,
                       ID3D11Texture2D **output);

#endif
