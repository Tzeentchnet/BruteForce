#include <string.h>

#include "texture_sampler.h"

static D3D11_TEXTURE_ADDRESS_MODE address_mode(uint32_t address)
{
    return address == 1 ? D3D11_TEXTURE_ADDRESS_WRAP
         : address == 2 ? D3D11_TEXTURE_ADDRESS_MIRROR
         : D3D11_TEXTURE_ADDRESS_CLAMP;
}

unsigned gpu_texture_sampler_desc(D3D11_SAMPLER_DESC *desc, uint32_t addr_u,
                                  uint32_t addr_v, uint32_t filter, uint32_t control0,
                                  unsigned levels, int enable_mips, int enable_aniso)
{
    uint32_t mag = (filter >> 24) & 0xF, min = (filter >> 16) & 0xFF;
    unsigned warnings = 0;
    int min_linear, mag_linear, mip_linear, mip_filter;

    memset(desc, 0, sizeof *desc);
    desc->AddressU = address_mode(addr_u);
    desc->AddressV = address_mode(addr_v);
    desc->AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc->ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (!enable_mips) {
        int linear = mag == 2 || min == 2 || min == 4 || min == 6;
        desc->Filter = linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR
                              : D3D11_FILTER_MIN_MAG_MIP_POINT;
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
    mip_linear = min == 5 || min == 6;
    mip_filter = min >= 3 && min <= 6;
    desc->Filter = D3D11_ENCODE_BASIC_FILTER(
        min_linear ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
        mag_linear ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
        mip_linear ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
        D3D11_FILTER_REDUCTION_TYPE_STANDARD);
    desc->MaxAnisotropy = 1;

    if (mip_filter && levels > 1) {
        float max_level = (float)(levels - 1);
        float min_lod = (float)((control0 >> 18) & 0xFFFu) / 256.0f;
        float max_lod = (float)((control0 >> 6) & 0xFFFu) / 256.0f;
        int32_t bias = (int32_t)(filter & 0x1FFFu);
        if (min_lod > max_level) min_lod = max_level;
        if (max_lod > max_level) max_lod = max_level;
        if (min_lod > max_lod) {
            min_lod = max_lod;
            warnings |= GPU_SAMPLER_LOD_RANGE;
        }
        if (bias & 0x1000) bias -= 0x2000;
        desc->MinLOD = min_lod;
        desc->MaxLOD = max_lod;
        desc->MipLODBias = (float)bias / 256.0f;
        if (desc->MipLODBias > D3D11_MIP_LOD_BIAS_MAX) {
            desc->MipLODBias = D3D11_MIP_LOD_BIAS_MAX;
            warnings |= GPU_SAMPLER_BIAS_CLAMPED;
        }
        if (enable_aniso && min_linear && mag_linear && max_lod > 0) {
            desc->Filter = D3D11_FILTER_ANISOTROPIC;
            desc->MaxAnisotropy = 8;
        }
    }
    return warnings;
}
