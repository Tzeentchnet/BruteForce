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
    GpuTextureFilterState state;
    unsigned warnings;

    memset(desc, 0, sizeof *desc);
    desc->AddressU = address_mode(addr_u);
    desc->AddressV = address_mode(addr_v);
    desc->AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc->ComparisonFunc = D3D11_COMPARISON_NEVER;
    warnings = gpu_texture_filter_state(&state, filter, control0, levels,
                                        enable_mips, enable_aniso);
    desc->Filter = state.max_anisotropy > 1 ? D3D11_FILTER_ANISOTROPIC
        : D3D11_ENCODE_BASIC_FILTER(
            state.min_filter == GPU_TEXTURE_FILTER_LINEAR
                ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
            state.mag_filter == GPU_TEXTURE_FILTER_LINEAR
                ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
            state.mip_filter == GPU_TEXTURE_FILTER_LINEAR
                ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
            D3D11_FILTER_REDUCTION_TYPE_STANDARD);
    desc->MinLOD = state.min_lod;
    desc->MaxLOD = state.max_lod;
    desc->MipLODBias = state.lod_bias;
    desc->MaxAnisotropy = state.max_anisotropy;
    return warnings;
}
