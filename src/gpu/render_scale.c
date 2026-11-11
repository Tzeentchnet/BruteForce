/*
 * Render-scale authority and GPU resolves adapted from Blood Wake's bw_vfx.c
 * and bw_vfx_post.c (MIT; THIRD_PARTY_NOTICES.md).
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_scale.h"

static int s_configured;
static int s_requested;
static int s_native;
static int s_fixed_output;
static float s_scale = 1.0f;
static unsigned s_epoch;
static uint64_t s_revision;
static uint32_t s_output_width, s_output_height;

static int picture_size(uint32_t width, uint32_t height, uint32_t *pw, uint32_t *ph)
{
    if (!width || !height || width > 16384 || height > 16384)
        return 0;
    if ((uint64_t)width * 3 <= (uint64_t)height * 4) {
        *pw = width;
        *ph = (uint32_t)((uint64_t)width * 3 / 4);
    } else {
        *pw = (uint32_t)((uint64_t)height * 4 / 3);
        *ph = height;
    }
    return *pw && *ph;
}

static int parse_scale(const char *text, float *value)
{
    char *end;
    float parsed;

    errno = 0;
    parsed = strtof(text, &end);
    if (end == text || *end || errno == ERANGE || !isfinite(parsed) ||
        parsed < 0.5f || parsed > 10.0f)
        return 0;
    *value = parsed;
    return 1;
}

static int parse_output(const char *text, uint32_t *width, uint32_t *height)
{
    uint32_t dimensions[2] = {0, 0};
    int i;

    for (i = 0; i < 2; ++i) {
        if (*text < '0' || *text > '9')
            return 0;
        do {
            unsigned digit = (unsigned)(*text++ - '0');
            if (dimensions[i] > (16384u - digit) / 10u)
                return 0;
            dimensions[i] = dimensions[i] * 10u + digit;
        } while (*text >= '0' && *text <= '9');
        if (!dimensions[i] || (i == 0 ? *text++ != 'x' : *text != '\0'))
            return 0;
    }
    return picture_size(dimensions[0], dimensions[1], width, height);
}

int gpu_scale_prepare_config(GpuScaleConfig *config)
{
    const char *scale = getenv("RECOMP_GPU_SCALE");
    const char *output = getenv("RECOMP_GPU_OUTPUT");

    if (!config)
        return 0;
    memset(config, 0, sizeof *config);
    if (s_configured) {
        config->requested = s_requested;
        config->native = s_native;
        config->fixed_output = s_fixed_output;
        config->scale = s_scale;
        config->width = s_output_width;
        config->height = s_output_height;
        config->valid = 1;
        return 1;
    }
    config->scale = 1.0f;
    if (!scale || !*scale) {
        if (output && *output) {
            fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT is only valid with"
                            " RECOMP_GPU_SCALE=native\n");
            return 0;
        }
    } else if (_stricmp(scale, "native") == 0) {
        config->native = 1;
        config->requested = 1;
        config->scale = 2.0f;
        if (output && *output) {
            if (!parse_output(output, &config->width, &config->height)) {
                fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT must be WxH with dimensions"
                                " from 1 to 16384\n");
                return 0;
            }
            config->scale = (float)config->height / 480.0f;
            if (config->scale < 0.5f || config->scale > 10.0f) {
                fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT produces a native scale"
                                " outside the supported 0.5 to 10 range\n");
                return 0;
            }
            config->fixed_output = 1;
        }
    } else {
        if (output && *output) {
            fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT is only valid with"
                            " RECOMP_GPU_SCALE=native\n");
            return 0;
        }
        if (!parse_scale(scale, &config->scale)) {
            fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_SCALE must be native or a number"
                            " from 0.5 to 10\n");
            return 0;
        }
        config->requested = fabsf(config->scale - 1.0f) > 0.001f;
        if (!config->requested)
            config->scale = 1.0f;
    }
    config->valid = 1;
    return 1;
}

int gpu_scale_commit_config(const GpuScaleConfig *config)
{
    if (!config || s_configured || config->valid != 1 ||
        (config->requested != 0 && config->requested != 1) ||
        (config->native != 0 && config->native != 1) ||
        (config->fixed_output != 0 && config->fixed_output != 1) ||
        !isfinite(config->scale) || config->scale < 0.5f || config->scale > 10.0f ||
        (config->native && !config->requested) ||
        (!config->native && (config->fixed_output || config->width || config->height ||
                             config->requested != (fabsf(config->scale - 1.0f) > 0.001f))) ||
        (config->native && !config->fixed_output &&
         (config->width || config->height || config->scale != 2.0f)) ||
        (config->fixed_output &&
         (!config->width || !config->height || config->width > 16384 ||
          config->height > 16384 || config->scale != (float)config->height / 480.0f))) {
        fprintf(stderr, "[GPU-SCALE] invalid configuration; no scale change committed\n");
        return 0;
    }
    s_native = config->native;
    s_requested = config->requested;
    s_fixed_output = config->fixed_output;
    s_scale = config->scale;
    s_output_width = config->width;
    s_output_height = config->height;
    s_configured = 1;
    ++s_revision;
    if (s_native)
        fprintf(stderr, "[GPU-SCALE] native resolution enabled: initial picture %ux%u%s,"
                        " render scale %.3f (%0.fx%0.f for the 640x480 screen)\n",
                s_output_width, s_output_height, s_fixed_output ? " (fixed)" : "",
                s_scale, 640.0f * s_scale, 480.0f * s_scale);
    else if (s_requested)
        fprintf(stderr, "[GPU-SCALE] fixed render scale %.3f"
                        " (%0.fx%0.f for the 640x480 screen)\n",
                s_scale, 640.0f * s_scale, 480.0f * s_scale);
    return 1;
}

int gpu_scale_requested(void)
{
    return s_requested;
}

int gpu_scale_native(void)
{
    return s_native;
}

float gpu_scale_value(void)
{
    return s_requested ? s_scale : 1.0f;
}

unsigned gpu_scale_epoch(void)
{
    return s_epoch;
}

int gpu_scale_fixed_output(uint32_t *width, uint32_t *height)
{
    if (!s_fixed_output)
        return 0;
    if (width) *width = s_output_width;
    if (height) *height = s_output_height;
    return 1;
}

int gpu_scale_output_size(uint32_t *width, uint32_t *height)
{
    if (!s_output_width || !s_output_height)
        return 0;
    if (width) *width = s_output_width;
    if (height) *height = s_output_height;
    return 1;
}

int gpu_scale_prepare_output(uint32_t width, uint32_t height, GpuScaleUpdate *update)
{
    float scale;

    if (!update || !s_configured)
        return 0;
    update->scale = s_scale;
    update->width = s_output_width;
    update->height = s_output_height;
    update->epoch = s_epoch;
    update->revision = s_revision;
    update->changed = update->scale_changed = 0;
    if (!s_native || s_fixed_output)
        return 1;
    scale = (float)height / 480.0f;
    if (!width || !height || width > 16384 || height > 16384 ||
        scale < 0.5f || scale > 10.0f) {
        fprintf(stderr, "[GPU-SCALE] output picture %ux%u produces unsupported"
                        " native scale %.3f; no scale change committed\n",
                width, height, scale);
        return 0;
    }
    if (width == s_output_width && height == s_output_height)
        return 1;
    update->width = width;
    update->height = height;
    update->changed = 1;
    if (fabsf(scale - s_scale) > 0.001f) {
        if (s_epoch == UINT_MAX) {
            fprintf(stderr, "[GPU-SCALE] scale epoch exhausted; no scale change committed\n");
            return 0;
        }
        update->scale = scale;
        update->epoch = s_epoch + 1;
        update->scale_changed = 1;
    }
    return 1;
}

int gpu_scale_commit_output(const GpuScaleUpdate *update)
{
    float scale;
    int scale_changed;
    if (!update || !s_configured || update->revision != s_revision) {
        fprintf(stderr, "[GPU-SCALE] stale output update; no scale change committed\n");
        return 0;
    }
    if (!update->changed) {
        if (!update->scale_changed && update->width == s_output_width &&
            update->height == s_output_height && update->scale == s_scale &&
            update->epoch == s_epoch)
            return 1;
        fprintf(stderr, "[GPU-SCALE] invalid output update; no scale change committed\n");
        return 0;
    }
    scale = (float)update->height / 480.0f;
    scale_changed = fabsf(scale - s_scale) > 0.001f;
    if (update->changed != 1 || !s_native || s_fixed_output ||
        !update->width || !update->height ||
        update->width > 16384 || update->height > 16384 || scale < 0.5f || scale > 10.0f ||
        (update->width == s_output_width && update->height == s_output_height) ||
        update->scale_changed != scale_changed ||
        (scale_changed && s_epoch == UINT_MAX) ||
        update->epoch != s_epoch + (unsigned)scale_changed ||
        update->scale != (scale_changed ? scale : s_scale)) {
        fprintf(stderr, "[GPU-SCALE] invalid output update; no scale change committed\n");
        return 0;
    }
    s_output_width = update->width;
    s_output_height = update->height;
    s_scale = update->scale;
    s_epoch = update->epoch;
    ++s_revision;
    fprintf(stderr, "[GPU-SCALE] output picture %ux%u: render scale %.3f"
                    " (%0.fx%0.f for the 640x480 screen), epoch %u\n",
            update->width, update->height, s_scale, 640.0f * s_scale,
            480.0f * s_scale, s_epoch);
    return 1;
}

int gpu_scale_surface_size_for(float scale, uint32_t width, uint32_t height,
                               uint32_t aa_sx, uint32_t aa_sy,
                               uint32_t *physical_width, uint32_t *physical_height)
{
    double sx = scale, sy = sx;
    double pw, ph;

    if (!physical_width || !physical_height || !width || !height ||
        !isfinite(scale) || scale < 0.5f || scale > 10.0f)
        return 0;
    if (fabsf(scale - 1.0f) > 0.001f) {
        if (aa_sx > 1)
            sx /= aa_sx;
        if (aa_sy > 1)
            sy /= aa_sy;
    }
    pw = floor(width * sx + 0.5);
    ph = floor(height * sy + 0.5);
    if (pw < 1.0 || ph < 1.0 || pw > 16384.0 || ph > 16384.0) {
        fprintf(stderr, "[GPU-SCALE] logical surface %ux%u at scale %.3f"
                        " exceeds the D3D11 1..16384 texture limit\n",
                width, height, scale);
        return 0;
    }
    *physical_width = (uint32_t)pw;
    *physical_height = (uint32_t)ph;
    return 1;
}

int gpu_scale_surface_size(uint32_t width, uint32_t height,
                           uint32_t aa_sx, uint32_t aa_sy,
                           uint32_t *physical_width, uint32_t *physical_height)
{
    return gpu_scale_surface_size_for(gpu_scale_value(), width, height, aa_sx, aa_sy,
                                      physical_width, physical_height);
}

static ID3D11Device *s_device;
static ID3D11DeviceContext *s_context;
static ID3D11VertexShader *s_vs;
static ID3D11PixelShader *s_ps[3];
static ID3D11SamplerState *s_linear, *s_point;
static ID3D11RasterizerState *s_rasterizer;
static ID3D11BlendState *s_blend;
static ID3D11DepthStencilState *s_depth;
static ID3D11Buffer *s_constants;
static int s_gpu_ok;

#ifdef BRUTEFORCE_D3D11_TESTING
static const char *s_test_gpu_failure;

void gpu_scale_test_fail_init(const char *stage)
{
    s_test_gpu_failure = stage;
}

static int gpu_scale_test_failure(const char *stage)
{
    if (!s_test_gpu_failure || strcmp(stage, s_test_gpu_failure) != 0)
        return 0;
    s_test_gpu_failure = NULL;
    return 1;
}
#else
static int gpu_scale_test_failure(const char *stage) { (void)stage; return 0; }
#endif

static const char s_blit_hlsl[] =
    "cbuffer B : register(b0) { float4 srcsz; float4 dstsz; };\n"
    "Texture2D t0 : register(t0);\n"
    "SamplerState sl : register(s0); SamplerState sp : register(s1);\n"
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs_full(uint id : SV_VertexID) {\n"
    "  V o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "  o.uv = uv; return o; }\n"
    "float4 tap(float2 uv) { return t0.SampleLevel(sl, uv, 0); }\n"
    "float4 ps_bilinear(V i) : SV_Target { return tap(i.uv); }\n"
    "float4 ps_box(V i) : SV_Target {\n"
    "  float2 f = srcsz.xy * dstsz.zw;\n"
    "  int nx = clamp((int)ceil(f.x / 2), 1, 4);\n"
    "  int ny = clamp((int)ceil(f.y / 2), 1, 4);\n"
    "  float2 o = i.uv - 0.5 * dstsz.zw;\n"
    "  float2 st = dstsz.zw / float2(nx, ny); float4 c = 0;\n"
    "  for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)\n"
    "    c += tap(o + (float2(x, y) + 0.5) * st);\n"
    "  return c / (nx * ny); }\n"
    "float4 ps_catmull(V i) : SV_Target {\n"
    "  float2 p = i.uv * srcsz.xy;\n"
    "  float2 t1 = floor(p - 0.5) + 0.5; float2 f = p - t1;\n"
    "  float2 w0 = f * (-0.5 + f * (1 - 0.5 * f));\n"
    "  float2 w1 = 1 + f * f * (-2.5 + 1.5 * f);\n"
    "  float2 w2 = f * (0.5 + f * (2 - 1.5 * f));\n"
    "  float2 w3 = f * f * (-0.5 + 0.5 * f);\n"
    "  float2 w12 = w1 + w2;\n"
    "  float2 t12 = (t1 + w2 / w12) * srcsz.zw;\n"
    "  float2 t0 = (t1 - 1) * srcsz.zw;\n"
    "  float2 t3 = (t1 + 2) * srcsz.zw;\n"
    "  float4 r = tap(float2(t0.x, t0.y)) * w0.x * w0.y\n"
    "    + tap(float2(t12.x, t0.y)) * w12.x * w0.y\n"
    "    + tap(float2(t3.x, t0.y)) * w3.x * w0.y\n"
    "    + tap(float2(t0.x, t12.y)) * w0.x * w12.y\n"
    "    + tap(float2(t12.x, t12.y)) * w12.x * w12.y\n"
    "    + tap(float2(t3.x, t12.y)) * w3.x * w12.y\n"
    "    + tap(float2(t0.x, t3.y)) * w0.x * w3.y\n"
    "    + tap(float2(t12.x, t3.y)) * w12.x * w3.y\n"
    "    + tap(float2(t3.x, t3.y)) * w3.x * w3.y;\n"
    "  return max(r, 0); }\n";

static int compile_shader(const char *entry, const char *target, ID3DBlob **code)
{
    ID3DBlob *errors = NULL;
    HRESULT hr = D3DCompile(s_blit_hlsl, sizeof s_blit_hlsl - 1, "render_scale",
                            NULL, NULL, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3,
                            0, code, &errors);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-SCALE] shader %s failed: %s\n", entry,
                errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "unknown error");
    }
    if (errors)
        ID3D10Blob_Release(errors);
    return SUCCEEDED(hr);
}

int gpu_scale_gpu_init(ID3D11Device *device, ID3D11DeviceContext *context)
{
    static const char *const entries[3] = {"ps_bilinear", "ps_box", "ps_catmull"};
    D3D11_SAMPLER_DESC sampler;
    D3D11_RASTERIZER_DESC rasterizer;
    D3D11_BLEND_DESC blend;
    D3D11_DEPTH_STENCIL_DESC depth;
    D3D11_BUFFER_DESC buffer;
    ID3DBlob *code = NULL;
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps[3] = {NULL, NULL, NULL};
    ID3D11SamplerState *linear = NULL, *point = NULL;
    ID3D11RasterizerState *rs = NULL;
    ID3D11BlendState *bs = NULL;
    ID3D11DepthStencilState *ds = NULL;
    ID3D11Buffer *constants = NULL;
    HRESULT hr = S_OK;
    int i;

    if (s_gpu_ok) {
        if (s_device == device && s_context == context)
            return 1;
        fprintf(stderr, "[GPU-SCALE] resolve resources belong to a different device\n");
        return 0;
    }
    if (!device || !context || !compile_shader("vs_full", "vs_4_0", &code)) {
        hr = E_FAIL;
        goto failed;
    }
    hr = gpu_scale_test_failure("vertex") ? E_OUTOFMEMORY :
         ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(code),
                                         ID3D10Blob_GetBufferSize(code), NULL, &vs);
    ID3D10Blob_Release(code);
    code = NULL;
    for (i = 0; SUCCEEDED(hr) && i < 3; ++i) {
        if (!compile_shader(entries[i], "ps_4_0", &code)) {
            hr = E_FAIL;
            goto failed;
        }
        hr = gpu_scale_test_failure("pixel") ? E_OUTOFMEMORY :
             ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(code),
                                            ID3D10Blob_GetBufferSize(code), NULL, &ps[i]);
        ID3D10Blob_Release(code);
        code = NULL;
    }
    memset(&sampler, 0, sizeof sampler);
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr))
        hr = gpu_scale_test_failure("sampler") ? E_OUTOFMEMORY :
             ID3D11Device_CreateSamplerState(device, &sampler, &linear);
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateSamplerState(device, &sampler, &point);
    memset(&rasterizer, 0, sizeof rasterizer);
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateRasterizerState(device, &rasterizer, &rs);
    memset(&blend, 0, sizeof blend);
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateBlendState(device, &blend, &bs);
    memset(&depth, 0, sizeof depth);
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateDepthStencilState(device, &depth, &ds);
    memset(&buffer, 0, sizeof buffer);
    buffer.ByteWidth = 32;
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr))
        hr = gpu_scale_test_failure("buffer") ? E_OUTOFMEMORY :
             ID3D11Device_CreateBuffer(device, &buffer, NULL, &constants);
    if (FAILED(hr) || !vs || !ps[0] || !ps[1] || !ps[2] ||
        !linear || !point || !rs || !bs || !ds || !constants)
        goto failed;
    s_device = device;
    s_context = context;
    s_vs = vs;
    for (i = 0; i < 3; ++i)
        s_ps[i] = ps[i];
    s_linear = linear;
    s_point = point;
    s_rasterizer = rs;
    s_blend = bs;
    s_depth = ds;
    s_constants = constants;
    s_gpu_ok = 1;
    return 1;

failed:
    fprintf(stderr, "[GPU-SCALE] GPU resolve resources failed 0x%08lX; configuration not committed\n",
            (unsigned long)hr);
    if (code) ID3D10Blob_Release(code);
    if (constants) ID3D11Buffer_Release(constants);
    if (ds) ID3D11DepthStencilState_Release(ds);
    if (bs) ID3D11BlendState_Release(bs);
    if (rs) ID3D11RasterizerState_Release(rs);
    if (point) ID3D11SamplerState_Release(point);
    if (linear) ID3D11SamplerState_Release(linear);
    for (i = 0; i < 3; ++i)
        if (ps[i]) ID3D11PixelShader_Release(ps[i]);
    if (vs) ID3D11VertexShader_Release(vs);
    return 0;
}

int gpu_scale_blit(ID3D11ShaderResourceView *source, UINT source_width, UINT source_height,
                   ID3D11RenderTargetView *target, UINT target_width, UINT target_height,
                   enum GpuScaleBlitMode mode)
{
    ID3D11ShaderResourceView *none[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    ID3D11SamplerState *samplers[2] = {s_linear, s_point};
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D11_VIEWPORT viewport;
    float constants[8];
    HRESULT hr;

    if (!s_gpu_ok || !source || !target || !source_width || !source_height ||
        !target_width || !target_height || mode < GPU_SCALE_BLIT_BILINEAR ||
        mode > GPU_SCALE_BLIT_CATMULL_ROM)
        return 0;
    constants[0] = (float)source_width;
    constants[1] = (float)source_height;
    constants[2] = 1.0f / source_width;
    constants[3] = 1.0f / source_height;
    constants[4] = (float)target_width;
    constants[5] = (float)target_height;
    constants[6] = 1.0f / target_width;
    constants[7] = 1.0f / target_height;
    hr = ID3D11DeviceContext_Map(s_context, (ID3D11Resource *)s_constants, 0,
                                 D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-SCALE] mapping resolve constants failed 0x%08lX\n",
                (unsigned long)hr);
        return 0;
    }
    memcpy(mapped.pData, constants, sizeof constants);
    ID3D11DeviceContext_Unmap(s_context, (ID3D11Resource *)s_constants, 0);

    viewport.TopLeftX = viewport.TopLeftY = 0;
    viewport.Width = (float)target_width;
    viewport.Height = (float)target_height;
    viewport.MinDepth = 0;
    viewport.MaxDepth = 1;
    ID3D11DeviceContext_PSSetShaderResources(s_context, 0, 8, none);
    ID3D11DeviceContext_OMSetRenderTargets(s_context, 1, &target, NULL);
    ID3D11DeviceContext_RSSetViewports(s_context, 1, &viewport);
    ID3D11DeviceContext_IASetInputLayout(s_context, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(s_context, s_vs, NULL, 0);
    ID3D11DeviceContext_GSSetShader(s_context, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(s_context, s_ps[mode], NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(s_context, 0, 1, &s_constants);
    ID3D11DeviceContext_PSSetShaderResources(s_context, 0, 1, &source);
    ID3D11DeviceContext_PSSetSamplers(s_context, 0, 2, samplers);
    ID3D11DeviceContext_RSSetState(s_context, s_rasterizer);
    ID3D11DeviceContext_OMSetBlendState(s_context, s_blend, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_context, s_depth, 0);
    ID3D11DeviceContext_Draw(s_context, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(s_context, 0, 8, none);
    return 1;
}
