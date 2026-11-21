#ifndef BRUTEFORCE_GPU_TEXTURE_SAMPLER_H
#define BRUTEFORCE_GPU_TEXTURE_SAMPLER_H

#include <d3d11.h>
#include <stdint.h>

#include "texture_filter.h"

unsigned gpu_texture_sampler_desc(D3D11_SAMPLER_DESC *desc, uint32_t addr_u,
                                  uint32_t addr_v, uint32_t filter, uint32_t control0,
                                  unsigned levels, int enable_mips, int enable_aniso);

#endif
