#ifndef BRUTEFORCE_GPU_TEXTURE_SAMPLER_H
#define BRUTEFORCE_GPU_TEXTURE_SAMPLER_H

#include <d3d11.h>
#include <stdint.h>

enum {
    GPU_SAMPLER_UNKNOWN_MIN = 1u,
    GPU_SAMPLER_UNKNOWN_MAG = 2u,
    GPU_SAMPLER_CONVOLUTION = 4u,
    GPU_SAMPLER_BIAS_CLAMPED = 8u,
    GPU_SAMPLER_LOD_RANGE = 16u
};

unsigned gpu_texture_sampler_desc(D3D11_SAMPLER_DESC *desc, uint32_t addr_u,
                                  uint32_t addr_v, uint32_t filter, uint32_t control0,
                                  unsigned levels, int enable_mips, int enable_aniso);

#endif
