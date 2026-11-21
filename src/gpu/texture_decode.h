#ifndef BRUTEFORCE_GPU_TEXTURE_DECODE_H
#define BRUTEFORCE_GPU_TEXTURE_DECODE_H

#include <d3d11.h>
#include "texture_layout.h"

typedef struct {
    ID3D11ComputeShader *shader;
    ID3D11Buffer *constants;
    int unavailable;
} GpuTextureDecoder;

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
