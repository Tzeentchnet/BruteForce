#include <string.h>

#include "texture_filter.h"

unsigned gpu_texture_filter_state(GpuTextureFilterState *state,
                                  uint32_t filter, uint32_t control0,
                                  unsigned levels, int enable_mips,
                                  int enable_aniso)
{
    uint32_t mag = (filter >> 24) & 0xFu;
    uint32_t min = (filter >> 16) & 0xFFu;
    unsigned warnings = 0;
    int min_linear;
    int mag_linear;
    int mip_filter;

    memset(state, 0, sizeof *state);
    if (!enable_mips) {
        int linear = mag == 2 || min == 2 || min == 4 || min == 6;
        state->min_filter = linear ? GPU_TEXTURE_FILTER_LINEAR
                                   : GPU_TEXTURE_FILTER_POINT;
        state->mag_filter = state->min_filter;
        state->mip_filter = state->min_filter;
        return 0;
    }

    if (min < 1 || min > 7) {
        warnings |= GPU_SAMPLER_UNKNOWN_MIN;
        min = 1;
    }
    if (mag != 1 && mag != 2 && mag != 4) {
        warnings |= GPU_SAMPLER_UNKNOWN_MAG;
        mag = 1;
    }
    if (min == 7 || mag == 4)
        warnings |= GPU_SAMPLER_CONVOLUTION;
    min_linear = min == 2 || min == 4 || min == 6 || min == 7;
    mag_linear = mag == 2 || mag == 4;
    mip_filter = min >= 3 && min <= 6;
    state->min_filter = min_linear ? GPU_TEXTURE_FILTER_LINEAR
                                   : GPU_TEXTURE_FILTER_POINT;
    state->mag_filter = mag_linear ? GPU_TEXTURE_FILTER_LINEAR
                                   : GPU_TEXTURE_FILTER_POINT;
    state->mip_filter = (min == 5 || min == 6) ? GPU_TEXTURE_FILTER_LINEAR
                                               : GPU_TEXTURE_FILTER_POINT;
    state->max_anisotropy = 1;

    if (mip_filter && levels > 1) {
        float max_level = (float)(levels - 1);
        int32_t bias = (int32_t)(filter & 0x1FFFu);
        state->min_lod = (float)((control0 >> 18) & 0xFFFu) / 256.0f;
        state->max_lod = (float)((control0 >> 6) & 0xFFFu) / 256.0f;
        if (state->min_lod > max_level)
            state->min_lod = max_level;
        if (state->max_lod > max_level)
            state->max_lod = max_level;
        if (state->min_lod > state->max_lod) {
            state->min_lod = state->max_lod;
            warnings |= GPU_SAMPLER_LOD_RANGE;
        }
        if (bias & 0x1000)
            bias -= 0x2000;
        state->lod_bias = (float)bias / 256.0f;
        if (state->lod_bias > 15.99f) {
            state->lod_bias = 15.99f;
            warnings |= GPU_SAMPLER_BIAS_CLAMPED;
        }
        if (enable_aniso && min_linear && mag_linear && state->max_lod > 0)
            state->max_anisotropy = 8;
    }
    return warnings;
}
