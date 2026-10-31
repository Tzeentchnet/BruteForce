#ifndef BRUTEFORCE_GPU_D3D11_DEVICE_STATUS_H
#define BRUTEFORCE_GPU_D3D11_DEVICE_STATUS_H

#include <d3d11.h>

void gpu_d3d11_check_device(ID3D11Device *device, HRESULT result, const char *operation);
HRESULT gpu_d3d11_get_data(ID3D11Device *device, ID3D11DeviceContext *context,
                           ID3D11Asynchronous *query, void *data, UINT size, UINT flags,
                           const char *operation);

#endif
