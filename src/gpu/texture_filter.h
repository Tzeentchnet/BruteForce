#ifndef BRUTEFORCE_GPU_TEXTURE_FILTER_H
#define BRUTEFORCE_GPU_TEXTURE_FILTER_H

#include <stdint.h>

enum {
    GPU_SAMPLER_UNKNOWN_MIN = 1u,
    GPU_SAMPLER_UNKNOWN_MAG = 2u,
    GPU_SAMPLER_CONVOLUTION = 4u,
    GPU_SAMPLER_BIAS_CLAMPED = 8u,
    GPU_SAMPLER_LOD_RANGE = 16u
};

typedef enum GpuTextureFilterMode {
    GPU_TEXTURE_FILTER_POINT,
    GPU_TEXTURE_FILTER_LINEAR
} GpuTextureFilterMode;

typedef struct GpuTextureFilterState {
    GpuTextureFilterMode min_filter;
    GpuTextureFilterMode mag_filter;
    GpuTextureFilterMode mip_filter;
    float min_lod;
    float max_lod;
    float lod_bias;
    unsigned max_anisotropy;
} GpuTextureFilterState;

unsigned gpu_texture_filter_state(GpuTextureFilterState *state,
                                  uint32_t filter, uint32_t control0,
                                  unsigned levels, int enable_mips,
                                  int enable_aniso);

#endif
