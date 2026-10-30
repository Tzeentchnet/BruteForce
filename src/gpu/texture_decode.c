#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d/d3d8_swizzle.h"
#include "texture_decode.h"

static const char s_decode_hlsl[] =
"ByteAddressBuffer src : register(t0);\n"
"ByteAddressBuffer palette : register(t1);\n"
"RWTexture2DArray<float4> dst : register(u0);\n"
"cbuffer Params : register(b0) {\n"
"  uint width, height, format, swizzled;\n"
"  uint bpp, pitch, face_stride, faces;\n"
"};\n"
"uint morton(uint x, uint y) {\n"
"  uint result = 0, outbit = 1;\n"
"  for (uint bit = 1; bit < width || bit < height; bit <<= 1) {\n"
"    if (bit < width) { if (x & bit) result |= outbit; outbit <<= 1; }\n"
"    if (bit < height) { if (y & bit) result |= outbit; outbit <<= 1; }\n"
"  }\n"
"  return result;\n"
"}\n"
"uint read_texel(uint offset) {\n"
"  uint aligned = offset & ~3u, shift = (offset & 3u) * 8u;\n"
"  uint value = src.Load(aligned) >> shift;\n"
"  if ((offset & 3u) + bpp > 4u) value |= src.Load(aligned + 4u) << (32u - shift);\n"
"  if (bpp == 1u) value &= 255u;\n"
"  if (bpp == 2u) value &= 65535u;\n"
"  return value;\n"
"}\n"
"[numthreads(8, 8, 1)]\n"
"void main(uint3 id : SV_DispatchThreadID) {\n"
"  if (id.x >= width || id.y >= height || id.z >= faces) return;\n"
"  uint offset = id.z * face_stride + (swizzled != 0u\n"
"      ? morton(id.x, id.y) * bpp : id.y * pitch + id.x * bpp);\n"
"  uint v = read_texel(offset), r = 0, g = 0, b = 0, a = 255;\n"
"  if (format == 0x0Bu) v = palette.Load(v * 4u);\n"
"  if (format == 0x06u || format == 0x12u || format == 0x0Bu) {\n"
"    a = v >> 24; r = (v >> 16) & 255u; g = (v >> 8) & 255u; b = v & 255u;\n"
"  } else if (format == 0x07u || format == 0x1Eu) {\n"
"    r = (v >> 16) & 255u; g = (v >> 8) & 255u; b = v & 255u;\n"
"  } else if (format == 0x05u || format == 0x11u) {\n"
"    r = ((v >> 11) & 31u) * 255u / 31u;\n"
"    g = ((v >> 5) & 63u) * 255u / 63u; b = (v & 31u) * 255u / 31u;\n"
"  } else if (format == 0x02u || format == 0x10u || format == 0x03u || format == 0x1Cu) {\n"
"    if (format == 0x02u || format == 0x10u) a = (v & 0x8000u) ? 255u : 0u;\n"
"    r = ((v >> 10) & 31u) * 255u / 31u;\n"
"    g = ((v >> 5) & 31u) * 255u / 31u; b = (v & 31u) * 255u / 31u;\n"
"  } else if (format == 0x04u || format == 0x1Du) {\n"
"    a = ((v >> 12) & 15u) * 17u; r = ((v >> 8) & 15u) * 17u;\n"
"    g = ((v >> 4) & 15u) * 17u; b = (v & 15u) * 17u;\n"
"  } else if (format == 0x00u || format == 0x13u) {\n"
"    r = g = b = v;\n"
"  } else if (format == 0x19u || format == 0x1Fu) {\n"
"    a = v; r = g = b = 255u;\n"
"  } else if (format == 0x3Fu) {\n"
"    a = v >> 24; b = (v >> 16) & 255u; g = (v >> 8) & 255u; r = v & 255u;\n"
"  } else if (format == 0x40u) {\n"
"    b = v >> 24; g = (v >> 16) & 255u; r = (v >> 8) & 255u; a = v & 255u;\n"
"  } else if (format == 0x41u) {\n"
"    r = v >> 24; g = (v >> 16) & 255u; b = (v >> 8) & 255u; a = v & 255u;\n"
"  }\n"
"  dst[id] = float4(r, g, b, a) / 255.0f;\n"
"}\n";

static unsigned texel_bytes(uint32_t format)
{
    switch (format) {
    case 0x00: case 0x01: case 0x0B: case 0x13: case 0x19: case 0x1B: case 0x1F:
        return 1;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11: case 0x1A:
    case 0x1C: case 0x1D: case 0x20: case 0x27: case 0x28: case 0x29: case 0x30:
        return 2;
    default:
        return 4;
    }
}

static unsigned cube_texel_bytes(uint32_t format)
{
    switch (format) {
    case 0x00: case 0x01: case 0x0B: case 0x13: case 0x19: case 0x1F:
        return 1;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11:
    case 0x1C: case 0x1D:
        return 2;
    default:
        return 4;
    }
}

static int format_sized(uint32_t format)
{
    return d3d8_format_is_swizzled(format) || d3d8_format_dxt_block_bytes(format);
}

static size_t level_bytes(const Nv2aTexture *t)
{
    unsigned block = d3d8_format_dxt_block_bytes(t->color);
    if (block)
        return (size_t)((t->width + 3) / 4) * ((t->height + 3) / 4) * block;
    if (d3d8_format_is_swizzled(t->color))
        return (size_t)t->width * t->height * texel_bytes(t->color);
    return (size_t)t->pitch * t->height;
}

size_t gpu_texture_face_stride(const Nv2aTexture *t)
{
    size_t total = 0;
    uint32_t w, h, levels, i, block;
    if (!t || !t->width || !t->height || t->levels > 15)
        return 0;
    if (!format_sized(t->color))
        total = (size_t)t->pitch * t->height;
    else {
        w = t->width;
        h = t->height;
        levels = t->levels ? t->levels : 1;
        block = d3d8_format_dxt_block_bytes(t->color);
        for (i = 0; i < levels; ++i) {
            total += block ? (size_t)((w + 3) / 4) * ((h + 3) / 4) * block
                           : (size_t)w * h * cube_texel_bytes(t->color);
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }
    return (total + 127u) & ~(size_t)127;
}

size_t gpu_texture_span(const Nv2aTexture *t, int faces)
{
    size_t n, stride;
    if (!t || !t->width || !t->height || (faces != 1 && faces != 6))
        return 0;
    n = level_bytes(t);
    if (!format_sized(t->color) && t->pitch) {
        size_t used = (size_t)(t->height - 1) * t->pitch
                    + (size_t)t->width * texel_bytes(t->color);
        if (used > n)
            n = used;
    }
    if (faces == 6) {
        stride = gpu_texture_face_stride(t);
        if (!stride)
            return 0;
        n += stride * 5;
    }
    return n && n <= (64u << 20) ? n : 0;
}

int gpu_texture_decode_supported(const Nv2aTexture *t)
{
    if (!t || !t->width || !t->height || t->width > 4096 || t->height > 4096)
        return 0;
    switch (t->color) {
    case 0x00: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x19: case 0x1C: case 0x1D:
    case 0x1E: case 0x1F: case 0x3F: case 0x40: case 0x41:
        break;
    case 0x0B:
        if (!t->palette)
            return 0;
        break;
    default:
        return 0;
    }
    if (d3d8_format_is_swizzled(t->color))
        return !(t->width & (t->width - 1)) && !(t->height & (t->height - 1));
    return t->pitch != 0;
}

static int init_decoder(GpuTextureDecoder *decoder, ID3D11Device *device)
{
    ID3DBlob *code = NULL, *errors = NULL;
    D3D11_BUFFER_DESC desc;
    HRESULT hr;
    if (decoder->shader)
        return 1;
    if (decoder->unavailable)
        return 0;
    hr = D3DCompile(s_decode_hlsl, sizeof s_decode_hlsl - 1, "texture_decode",
                    NULL, NULL, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
                    0, &code, &errors);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] texture compute shader failed 0x%08lX: %s\n",
                (unsigned long)hr, errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "?");
        if (errors) ID3D10Blob_Release(errors);
        decoder->unavailable = 1;
        return 0;
    }
    if (errors) ID3D10Blob_Release(errors);
    hr = ID3D11Device_CreateComputeShader(device, ID3D10Blob_GetBufferPointer(code),
                                          ID3D10Blob_GetBufferSize(code), NULL,
                                          &decoder->shader);
    ID3D10Blob_Release(code);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] CreateComputeShader failed 0x%08lX\n", (unsigned long)hr);
        decoder->unavailable = 1;
        return 0;
    }
    memset(&desc, 0, sizeof desc);
    desc.ByteWidth = 32;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = ID3D11Device_CreateBuffer(device, &desc, NULL, &decoder->constants);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] texture constants failed 0x%08lX\n", (unsigned long)hr);
        ID3D11ComputeShader_Release(decoder->shader);
        decoder->shader = NULL;
        decoder->unavailable = 1;
        return 0;
    }
    return 1;
}

static HRESULT raw_view(ID3D11Device *device, const void *bytes, UINT size,
                        ID3D11Buffer **buffer, ID3D11ShaderResourceView **view)
{
    D3D11_BUFFER_DESC bd;
    D3D11_SUBRESOURCE_DATA init;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd;
    HRESULT hr;
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = size;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    memset(&init, 0, sizeof init);
    init.pSysMem = bytes;
    hr = ID3D11Device_CreateBuffer(device, &bd, &init, buffer);
    if (FAILED(hr))
        return hr;
    memset(&sd, 0, sizeof sd);
    sd.Format = DXGI_FORMAT_R32_TYPELESS;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    sd.BufferEx.NumElements = size / 4;
    sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    return ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)*buffer, &sd, view);
}

int gpu_texture_decode(GpuTextureDecoder *decoder, ID3D11Device *device,
                       ID3D11DeviceContext *context, const Nv2aTexture *t,
                       int faces, const void *bytes, const void *palette,
                       ID3D11Texture2D **output)
{
    struct {
        uint32_t width, height, format, swizzled, bpp, pitch, face_stride, faces;
    } params;
    ID3D11Buffer *input = NULL, *pal_buffer = NULL;
    ID3D11ShaderResourceView *views[2] = {NULL, NULL}, *no_views[2] = {NULL, NULL};
    ID3D11UnorderedAccessView *uav = NULL, *no_uav = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
    size_t span = gpu_texture_span(t, faces), stride;
    UINT padded;
    uint8_t *copy = NULL;
    HRESULT hr = E_FAIL;

    *output = NULL;
    if (!gpu_texture_decode_supported(t) || !bytes || !span
        || (t->color == 0x0B && !palette) || (faces == 6 && t->width != t->height)) {
        fprintf(stderr, "[GPU-D3D11] invalid texture compute input\n");
        return 0;
    }
    if (!init_decoder(decoder, device))
        return 0;
    padded = (UINT)((span + 3) & ~(size_t)3);
    if (padded != span) {
        copy = (uint8_t *)calloc(1, padded);
        if (!copy) {
            fprintf(stderr, "[GPU-D3D11] cannot pad texture upload (%u bytes)\n", padded);
            return 0;
        }
        memcpy(copy, bytes, span);
        bytes = copy;
    }
    hr = raw_view(device, bytes, padded, &input, &views[0]);
    free(copy);
    if (FAILED(hr))
        goto failed;
    if (t->color == 0x0B) {
        hr = raw_view(device, palette, 256 * 4, &pal_buffer, &views[1]);
        if (FAILED(hr))
            goto failed;
    }
    memset(&td, 0, sizeof td);
    td.Width = t->width;
    td.Height = t->height;
    td.MipLevels = 1;
    td.ArraySize = (UINT)faces;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    td.MiscFlags = faces == 6 ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, output);
    if (FAILED(hr))
        goto failed;
    memset(&ud, 0, sizeof ud);
    ud.Format = td.Format;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    ud.Texture2DArray.ArraySize = (UINT)faces;
    hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)*output, &ud, &uav);
    if (FAILED(hr))
        goto failed;
    stride = faces == 6 ? gpu_texture_face_stride(t) : 0;
    params.width = t->width;
    params.height = t->height;
    params.format = t->color;
    params.swizzled = d3d8_format_is_swizzled(t->color) ? 1u : 0u;
    params.bpp = texel_bytes(t->color);
    params.pitch = t->pitch;
    params.face_stride = (uint32_t)stride;
    params.faces = (uint32_t)faces;
    ID3D11DeviceContext_UpdateSubresource(context, (ID3D11Resource *)decoder->constants,
                                         0, NULL, &params, 0, 0);
    ID3D11DeviceContext_CSSetShader(context, decoder->shader, NULL, 0);
    ID3D11DeviceContext_CSSetConstantBuffers(context, 0, 1, &decoder->constants);
    ID3D11DeviceContext_CSSetShaderResources(context, 0, 2, views);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 0, 1, &uav, NULL);
    ID3D11DeviceContext_Dispatch(context, (t->width + 7) / 8, (t->height + 7) / 8, (UINT)faces);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 0, 1, &no_uav, NULL);
    ID3D11DeviceContext_CSSetShaderResources(context, 0, 2, no_views);
    ID3D11DeviceContext_CSSetShader(context, NULL, NULL, 0);
    ID3D11UnorderedAccessView_Release(uav);
    if (views[1]) ID3D11ShaderResourceView_Release(views[1]);
    ID3D11ShaderResourceView_Release(views[0]);
    if (pal_buffer) ID3D11Buffer_Release(pal_buffer);
    ID3D11Buffer_Release(input);
    return 1;

failed:
    fprintf(stderr, "[GPU-D3D11] texture compute resource creation failed 0x%08lX\n",
            (unsigned long)hr);
    if (uav) ID3D11UnorderedAccessView_Release(uav);
    if (*output) ID3D11Texture2D_Release(*output);
    *output = NULL;
    if (views[1]) ID3D11ShaderResourceView_Release(views[1]);
    if (views[0]) ID3D11ShaderResourceView_Release(views[0]);
    if (pal_buffer) ID3D11Buffer_Release(pal_buffer);
    if (input) ID3D11Buffer_Release(input);
    return 0;
}
