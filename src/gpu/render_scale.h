/* Blood Wake-derived render scaling and full-screen resolve helpers. */
#ifndef BRUTEFORCE_RENDER_SCALE_H
#define BRUTEFORCE_RENDER_SCALE_H

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>

enum GpuScaleBlitMode {
    GPU_SCALE_BLIT_BILINEAR,
    GPU_SCALE_BLIT_BOX,
    GPU_SCALE_BLIT_CATMULL_ROM
};

typedef struct GpuScaleConfig {
    float scale;
    uint32_t width, height;
    int requested, native, fixed_output, valid;
} GpuScaleConfig;

typedef struct GpuScaleUpdate {
    float scale;
    uint32_t width, height;
    unsigned epoch;
    uint64_t revision;
    int changed, scale_changed;
} GpuScaleUpdate;

int gpu_scale_prepare_config(GpuScaleConfig *config);
int gpu_scale_commit_config(const GpuScaleConfig *config);
int gpu_scale_requested(void);
int gpu_scale_native(void);
float gpu_scale_value(void);
unsigned gpu_scale_epoch(void);
int gpu_scale_fixed_output(uint32_t *width, uint32_t *height);
int gpu_scale_output_size(uint32_t *width, uint32_t *height);
int gpu_scale_prepare_output(uint32_t width, uint32_t height, GpuScaleUpdate *update);
int gpu_scale_commit_output(const GpuScaleUpdate *update);
int gpu_scale_surface_size_for(float scale, uint32_t width, uint32_t height,
                               uint32_t aa_sx, uint32_t aa_sy,
                               uint32_t *physical_width, uint32_t *physical_height);
int gpu_scale_surface_size(uint32_t width, uint32_t height,
                           uint32_t aa_sx, uint32_t aa_sy,
                           uint32_t *physical_width, uint32_t *physical_height);

int gpu_scale_gpu_init(ID3D11Device *device, ID3D11DeviceContext *context);
int gpu_scale_blit(ID3D11ShaderResourceView *source, UINT source_width, UINT source_height,
                   ID3D11RenderTargetView *target, UINT target_width, UINT target_height,
                   enum GpuScaleBlitMode mode);

#ifdef BRUTEFORCE_D3D11_TESTING
void gpu_scale_test_fail_init(const char *stage);
#endif

#endif
