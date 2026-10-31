#ifndef BRUTEFORCE_GPU_TEXTURE_DECODE_H
#define BRUTEFORCE_GPU_TEXTURE_DECODE_H

#include <d3d11.h>
#include <stddef.h>
#include "nv2a_backend.h"

typedef struct {
    ID3D11ComputeShader *shader;
    ID3D11Buffer *constants;
    int unavailable;
} GpuTextureDecoder;

size_t gpu_texture_face_stride(const Nv2aTexture *texture);
size_t gpu_texture_span(const Nv2aTexture *texture, int faces);
int gpu_texture_decode_supported(const Nv2aTexture *texture);
int gpu_texture_p8_indices_valid(const Nv2aTexture *texture, int faces,
                                 const void *bytes, unsigned palette_entries,
                                 unsigned *invalid_index);
int gpu_texture_decode_p8_cpu(const Nv2aTexture *texture, int faces,
                              const void *bytes, const void *palette,
                              unsigned palette_entries, uint32_t *output);
int gpu_texture_decode(GpuTextureDecoder *decoder, ID3D11Device *device,
                       ID3D11DeviceContext *context, const Nv2aTexture *texture,
                       int faces, const void *bytes, const void *palette,
                       unsigned palette_entries, ID3D11Texture2D **output);

#endif
