/*
 * D3D11 full-screen resolves adapted from Blood Wake's bw_vfx_post.c
 * (MIT; THIRD_PARTY_NOTICES.md).
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

#include "render_scale_d3d11.h"
#include "d3d11_device_status.h"

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
    gpu_d3d11_check_device(device, hr, "creating scaled resolve resources");
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
    gpu_d3d11_check_device(s_device, S_OK, "starting scaled resolve");
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
        gpu_d3d11_check_device(s_device, hr, "mapping scaled resolve constants");
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
