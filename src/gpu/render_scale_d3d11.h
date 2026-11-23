/* Blood Wake-derived D3D11 full-screen render-scale resolves. */
#ifndef BRUTEFORCE_RENDER_SCALE_D3D11_H
#define BRUTEFORCE_RENDER_SCALE_D3D11_H

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>

#include "render_scale.h"

int gpu_scale_gpu_init(ID3D11Device *device, ID3D11DeviceContext *context);
int gpu_scale_blit(ID3D11ShaderResourceView *source, UINT source_width, UINT source_height,
                   ID3D11RenderTargetView *target, UINT target_width, UINT target_height,
                   enum GpuScaleBlitMode mode);

#ifdef BRUTEFORCE_D3D11_TESTING
void gpu_scale_test_fail_init(const char *stage);
#endif

#endif
