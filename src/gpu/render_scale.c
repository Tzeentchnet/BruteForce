/*
 * Render-scale authority and GPU resolves adapted from Blood Wake's bw_vfx.c
 * and bw_vfx_post.c (MIT; THIRD_PARTY_NOTICES.md).
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_scale.h"

static int s_configured;
static int s_requested;
static int s_native;
static float s_scale = 1.0f;
static unsigned s_epoch;
static uint32_t s_output_width, s_output_height;

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

int gpu_scale_configure(void)
{
    const char *scale = getenv("RECOMP_GPU_SCALE");
    const char *output = getenv("RECOMP_GPU_OUTPUT");

    if (s_configured)
        return 1;
    s_configured = 1;
    if (!scale || !*scale) {
        if (output && *output) {
            fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT is only valid with"
                            " RECOMP_GPU_SCALE=native\n");
            return 0;
        }
        return 1;
    }
    if (_stricmp(scale, "native") == 0) {
        char extra;
        s_native = 1;
        s_requested = 1;
        s_scale = 2.0f;
        if (output && *output) {
            if (sscanf(output, "%ux%u%c", &s_output_width, &s_output_height, &extra) != 2 ||
                !s_output_width || !s_output_height ||
                s_output_width > 16384 || s_output_height > 16384) {
                fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT must be WxH with dimensions"
                                " from 1 to 16384\n");
                return 0;
            }
            s_scale = (float)s_output_height / 480.0f;
            if (s_scale < 0.5f || s_scale > 10.0f) {
                fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT produces a native scale"
                                " outside the supported 0.5 to 10 range\n");
                return 0;
            }
        }
        fprintf(stderr, "[GPU-SCALE] native resolution enabled: initial output %ux%u,"
                        " render scale %.3f (%0.fx%0.f for the 640x480 screen)\n",
                s_output_width, s_output_height, s_scale, 640.0f * s_scale,
                480.0f * s_scale);
        return 1;
    }
    if (output && *output) {
        fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT is only valid with"
                        " RECOMP_GPU_SCALE=native\n");
        return 0;
    }
    if (!parse_scale(scale, &s_scale)) {
        fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_SCALE must be native or a number"
                        " from 0.5 to 10\n");
        return 0;
    }
    s_requested = fabsf(s_scale - 1.0f) > 0.001f;
    if (s_requested)
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

void gpu_scale_set_output(uint32_t width, uint32_t height)
{
    float scale;

    if (!s_native || !width || !height ||
        (width == s_output_width && height == s_output_height))
        return;
    s_output_width = width;
    s_output_height = height;
    scale = (float)height / 480.0f;
    if (scale < 0.5f || scale > 10.0f) {
        fprintf(stderr, "[GPU-SCALE] output %ux%u produces unsupported native scale %.3f;"
                        " retaining scale %.3f\n", width, height, scale, s_scale);
        return;
    }
    if (fabsf(scale - s_scale) <= 0.001f)
        return;
    s_scale = scale;
    s_epoch++;
    fprintf(stderr, "[GPU-SCALE] output %ux%u: render scale %.3f"
                    " (%0.fx%0.f for the 640x480 screen)\n",
            width, height, scale, 640.0f * scale, 480.0f * scale);
}

int gpu_scale_surface_size(uint32_t width, uint32_t height,
                           uint32_t aa_sx, uint32_t aa_sy,
                           uint32_t *physical_width, uint32_t *physical_height)
{
    double sx = gpu_scale_value(), sy = sx;
    double pw, ph;

    if (!physical_width || !physical_height || !width || !height)
        return 0;
    if (!s_requested || fabsf(s_scale - 1.0f) <= 0.001f) {
        *physical_width = width;
        *physical_height = height;
        return 1;
    }
    if (aa_sx > 1)
        sx /= aa_sx;
    if (aa_sy > 1)
        sy /= aa_sy;
    pw = floor(width * sx + 0.5);
    ph = floor(height * sy + 0.5);
    if (pw < 1.0 || ph < 1.0 || pw > 16384.0 || ph > 16384.0) {
        fprintf(stderr, "[GPU-SCALE] logical surface %ux%u at scale %.3f"
                        " exceeds the D3D11 1..16384 texture limit\n",
                width, height, s_scale);
        return 0;
    }
    *physical_width = (uint32_t)pw;
    *physical_height = (uint32_t)ph;
    return 1;
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
static int s_gpu_initialized;
static int s_gpu_ok;

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
    HRESULT hr = S_OK;
    int i;

    if (s_gpu_initialized)
        return s_gpu_ok;
    s_gpu_initialized = 1;
    s_device = device;
    s_context = context;
    if (!compile_shader("vs_full", "vs_4_0", &code))
        return 0;
    hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(code),
                                         ID3D10Blob_GetBufferSize(code), NULL, &s_vs);
    ID3D10Blob_Release(code);
    code = NULL;
    for (i = 0; SUCCEEDED(hr) && i < 3; ++i) {
        if (!compile_shader(entries[i], "ps_4_0", &code))
            return 0;
        hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(code),
                                            ID3D10Blob_GetBufferSize(code), NULL, &s_ps[i]);
        ID3D10Blob_Release(code);
        code = NULL;
    }
    memset(&sampler, 0, sizeof sampler);
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateSamplerState(device, &sampler, &s_linear);
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateSamplerState(device, &sampler, &s_point);
    memset(&rasterizer, 0, sizeof rasterizer);
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateRasterizerState(device, &rasterizer, &s_rasterizer);
    memset(&blend, 0, sizeof blend);
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateBlendState(device, &blend, &s_blend);
    memset(&depth, 0, sizeof depth);
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateDepthStencilState(device, &depth, &s_depth);
    memset(&buffer, 0, sizeof buffer);
    buffer.ByteWidth = 32;
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateBuffer(device, &buffer, NULL, &s_constants);
    s_gpu_ok = SUCCEEDED(hr) && s_vs && s_ps[0] && s_ps[1] && s_ps[2] &&
               s_linear && s_point && s_rasterizer && s_blend && s_depth && s_constants;
    if (!s_gpu_ok)
        fprintf(stderr, "[GPU-SCALE] GPU resolve resources failed 0x%08lX\n",
                (unsigned long)hr);
    return s_gpu_ok;
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
