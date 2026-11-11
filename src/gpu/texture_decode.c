#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d/d3d8_swizzle.h"
#include "d3d11_device_status.h"
#include "texture_decode.h"

static const char s_decode_hlsl[] =
"ByteAddressBuffer src : register(t0);\n"
"ByteAddressBuffer palette : register(t1);\n"
"RWTexture2DArray<float4> dst : register(u0);\n"
"cbuffer Params : register(b0) {\n"
"  uint width, height, format, swizzled;\n"
"  uint bpp, pitch, face_stride, faces;\n"
"  uint level_offset, unused0, unused1, unused2;\n"
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
"  uint offset = level_offset + id.z * face_stride + (swizzled != 0u\n"
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

static int checked_add(size_t a, size_t b, size_t *out)
{
    if (b > SIZE_MAX - a)
        return 0;
    *out = a + b;
    return 1;
}

static int checked_mul(size_t a, size_t b, size_t *out)
{
    if (a && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

int gpu_texture_layout(const Nv2aTexture *t, int faces, int enable_mips,
                       GpuTextureLayout *layout)
{
    GpuTextureSubresource levels[GPU_TEXTURE_MAX_LEVELS];
    size_t total = 0, stride, span = 0;
    uint32_t w, h, count, exposed, natural = 1, i, face, block;

    if (!layout)
        return 0;
    memset(layout, 0, sizeof *layout);
    if (!t || !t->width || !t->height || t->width > 4096 || t->height > 4096
        || t->levels > GPU_TEXTURE_MAX_LEVELS || (faces != 1 && faces != 6)
        || (faces == 6 && t->width != t->height
            && (enable_mips || t->color == 0x0B)))
        return 0;

    block = d3d8_format_dxt_block_bytes(t->color);
    layout->storage = block ? GPU_TEXTURE_BC
                    : d3d8_format_is_swizzled(t->color) ? GPU_TEXTURE_SWIZZLED
                    : GPU_TEXTURE_LINEAR;
    if (layout->storage == GPU_TEXTURE_SWIZZLED
        && ((t->width & (t->width - 1)) || (t->height & (t->height - 1)))) {
        /* Retain legacy level-zero CPU decoding only where its Morton
         * indices stay inside the old width*height source span. */
        if (enable_mips || t->color == 0x0B
            || swizzle_offset(t->width - 1, t->height - 1, t->width, t->height)
               >= (size_t)t->width * t->height)
            return 0;
    }
    if (layout->storage == GPU_TEXTURE_LINEAR && !t->pitch)
        return 0;
    layout->format = t->color;
    layout->faces = (uint32_t)faces;
    layout->declared_levels = t->levels;
    layout->texel_bytes = texel_bytes(t->color);
    layout->mips_enabled = enable_mips != 0;
    w = t->width > t->height ? t->width : t->height;
    while (w > 1) {
        w >>= 1;
        ++natural;
    }
    count = layout->storage == GPU_TEXTURE_LINEAR ? 1 : (t->levels ? t->levels : 1);
    /* D3D cannot expose repeated 1x1 levels, but declared storage still
     * determines the spacing of cube faces, including when falling back. */
    exposed = enable_mips ? (count < natural ? count : natural) : 1;
    layout->levels = exposed;
    w = t->width;
    h = t->height;
    for (i = 0; i < count; ++i) {
        size_t pitch, size, used;
        if (layout->storage == GPU_TEXTURE_BC) {
            if (!checked_mul(((size_t)w + 3) / 4, block, &pitch)
                || !checked_mul(pitch, ((size_t)h + 3) / 4, &size))
                return 0;
        } else if (layout->storage == GPU_TEXTURE_SWIZZLED) {
            if (!checked_mul(w, layout->texel_bytes, &pitch)
                || !checked_mul(pitch, h, &size))
                return 0;
        } else {
            size_t row_bytes;
            pitch = t->pitch;
            if (enable_mips && (t->color == 0x24 || t->color == 0x25)) {
                if (!checked_mul(((size_t)w + 1) / 2, 4, &row_bytes))
                    return 0;
            } else if (!checked_mul(w, layout->texel_bytes, &row_bytes)) {
                return 0;
            }
            if (!checked_mul(pitch, h, &size)
                || !checked_mul(pitch, h - 1, &used)
                || !checked_add(used, row_bytes, &used))
                return 0;
            if (used > size)
                size = used;
        }
        levels[i].width = w;
        levels[i].height = h;
        levels[i].offset = total;
        levels[i].size = size;
        levels[i].row_pitch = pitch;
        if (!checked_add(total, size, &total))
            return 0;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    stride = total;
    if (faces == 6) {
        /* Linear faces retain the legacy pitch*height spacing even when a
         * short pitch makes a row overlap the next face. */
        if (layout->storage == GPU_TEXTURE_LINEAR
            && !checked_mul(t->pitch, t->height, &stride))
            return 0;
        if (!checked_add(stride, 127, &stride))
            return 0;
        stride &= ~(size_t)127;
    }
    layout->face_stride = stride;
    for (face = 0; face < (uint32_t)faces; ++face) {
        size_t face_offset;
        if (!checked_mul(face, stride, &face_offset))
            return 0;
        for (i = 0; i < exposed; ++i) {
            GpuTextureSubresource *sub = &layout->subresource[face][i];
            size_t end;
            *sub = levels[i];
            if (!checked_add(face_offset, sub->offset, &sub->offset)
                || !checked_add(sub->offset, sub->size, &end))
                return 0;
            if (end > span)
                span = end;
        }
    }
    if (!span || span > GPU_TEXTURE_MAX_SPAN)
        return 0;
    layout->span = span;
    return 1;
}

size_t gpu_texture_face_stride(const Nv2aTexture *t)
{
    GpuTextureLayout layout;
    return gpu_texture_layout(t, 6, 0, &layout) ? layout.face_stride : 0;
}

size_t gpu_texture_span(const Nv2aTexture *t, int faces)
{
    GpuTextureLayout layout;
    return gpu_texture_layout(t, faces, 0, &layout) ? layout.span : 0;
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

static int p8_header_valid(const GpuTextureLayout *layout, const void *bytes,
                           size_t byte_count, unsigned palette_entries)
{
    return layout && layout->format == 0x0B && layout->storage == GPU_TEXTURE_SWIZZLED
        && layout->texel_bytes == 1 && bytes && layout->span <= byte_count
        && (layout->faces == 1 || layout->faces == 6)
        && layout->levels && layout->levels <= GPU_TEXTURE_MAX_LEVELS
        && (palette_entries == 32 || palette_entries == 64
            || palette_entries == 128 || palette_entries == 256);
}

static int p8_subresource_valid(const GpuTextureLayout *layout, unsigned face, unsigned level,
                                const uint8_t *src, unsigned entries, unsigned *invalid_index)
{
    const GpuTextureSubresource *sub = &layout->subresource[face][level];
    size_t i;
    if (!sub->width || !sub->height || sub->width > 4096 || sub->height > 4096
        || (sub->width & (sub->width - 1)) || (sub->height & (sub->height - 1))
        || sub->size != (size_t)sub->width * sub->height
        || sub->offset > layout->span || sub->size > layout->span - sub->offset)
        return 0;
    for (i = 0; i < sub->size; ++i)
        if (src[sub->offset + i] >= entries) {
            if (invalid_index)
                *invalid_index = src[sub->offset + i];
            return 0;
        }
    return 1;
}

int gpu_texture_p8_indices_valid(const GpuTextureLayout *layout,
                                 const void *bytes, size_t byte_count,
                                 unsigned palette_entries,
                                 unsigned *invalid_index)
{
    unsigned face, level;
    if (invalid_index)
        *invalid_index = 256;
    if (!p8_header_valid(layout, bytes, byte_count, palette_entries))
        return 0;
    for (face = 0; face < layout->faces; ++face)
        for (level = 0; level < layout->levels; ++level)
            if (!p8_subresource_valid(layout, face, level, (const uint8_t *)bytes,
                                      palette_entries, invalid_index))
                return 0;
    return 1;
}

int gpu_texture_decode_p8_cpu(const GpuTextureLayout *layout, unsigned face,
                              unsigned level, const void *bytes, size_t byte_count,
                              const void *palette, unsigned palette_entries,
                              uint32_t *output, size_t output_texels)
{
    const uint8_t *src = (const uint8_t *)bytes;
    const uint8_t *pal = (const uint8_t *)palette;
    const GpuTextureSubresource *sub;
    uint32_t x, y;

    if (!p8_header_valid(layout, bytes, byte_count, palette_entries)
        || face >= layout->faces || level >= layout->levels || !palette || !output
        || !p8_subresource_valid(layout, face, level, src, palette_entries, NULL))
        return 0;
    sub = &layout->subresource[face][level];
    if (output_texels < (size_t)sub->width * sub->height)
        return 0;
    for (y = 0; y < sub->height; ++y)
        for (x = 0; x < sub->width; ++x) {
            size_t source = sub->offset + swizzle_offset(x, y, sub->width, sub->height);
            size_t dest = (size_t)y * sub->width + x;
            memcpy(&output[dest], pal + (size_t)src[source] * 4, 4);
        }
    return 1;
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
        gpu_d3d11_check_device(device, hr, "creating texture compute shader");
        fprintf(stderr, "[GPU-D3D11] CreateComputeShader failed 0x%08lX\n", (unsigned long)hr);
        decoder->unavailable = 1;
        return 0;
    }
    memset(&desc, 0, sizeof desc);
    desc.ByteWidth = 48;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = ID3D11Device_CreateBuffer(device, &desc, NULL, &decoder->constants);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(device, hr, "creating texture compute constants");
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

static int matching_layout(const Nv2aTexture *t, const GpuTextureLayout *layout,
                           size_t byte_count)
{
    GpuTextureLayout expected;
    unsigned face, level;
    if (!layout || layout->span > byte_count
        || !gpu_texture_layout(t, (int)layout->faces, layout->mips_enabled, &expected)
        || expected.storage != layout->storage || expected.format != layout->format
        || expected.faces != layout->faces || expected.levels != layout->levels
        || expected.declared_levels != layout->declared_levels
        || expected.texel_bytes != layout->texel_bytes
        || expected.face_stride != layout->face_stride || expected.span != layout->span)
        return 0;
    for (face = 0; face < layout->faces; ++face)
        for (level = 0; level < layout->levels; ++level) {
            const GpuTextureSubresource *a = &expected.subresource[face][level];
            const GpuTextureSubresource *b = &layout->subresource[face][level];
            if (a->width != b->width || a->height != b->height
                || a->offset != b->offset || a->size != b->size
                || a->row_pitch != b->row_pitch)
                return 0;
        }
    return 1;
}

int gpu_texture_decode(GpuTextureDecoder *decoder, ID3D11Device *device,
                       ID3D11DeviceContext *context, const Nv2aTexture *t,
                       const GpuTextureLayout *layout, const void *bytes,
                       size_t byte_count, const void *palette, unsigned palette_entries,
                       ID3D11Texture2D **output)
{
    struct {
        uint32_t width, height, format, swizzled, bpp, pitch, face_stride, faces;
        uint32_t level_offset, unused[3];
    } params;
    ID3D11Buffer *input = NULL, *pal_buffer = NULL;
    ID3D11ShaderResourceView *views[2] = {NULL, NULL}, *no_views[2] = {NULL, NULL};
    ID3D11UnorderedAccessView *uav[GPU_TEXTURE_MAX_LEVELS] = {NULL}, *no_uav = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
    size_t span;
    unsigned level;
    UINT padded;
    uint8_t *copy = NULL;
    HRESULT hr = E_FAIL;

    if (!output)
        return 0;
    *output = NULL;
    if (!decoder || !device || !context || !gpu_texture_decode_supported(t) || !bytes
        || !matching_layout(t, layout, byte_count)
        || (t->color == 0x0B
            && (!palette || !gpu_texture_p8_indices_valid(layout, bytes, byte_count,
                                                           palette_entries, NULL)))) {
        fprintf(stderr, "[GPU-D3D11] invalid texture compute input\n");
        return 0;
    }
    span = layout->span;
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
        hr = raw_view(device, palette, palette_entries * 4, &pal_buffer, &views[1]);
        if (FAILED(hr))
            goto failed;
    }
    memset(&td, 0, sizeof td);
    td.Width = t->width;
    td.Height = t->height;
    td.MipLevels = layout->levels;
    td.ArraySize = layout->faces;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    td.MiscFlags = layout->faces == 6 ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, output);
    if (FAILED(hr))
        goto failed;
    memset(&ud, 0, sizeof ud);
    ud.Format = td.Format;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    ud.Texture2DArray.ArraySize = layout->faces;
    for (level = 0; level < layout->levels; ++level) {
        ud.Texture2DArray.MipSlice = level;
        hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)*output,
                                                    &ud, &uav[level]);
        if (FAILED(hr))
            goto failed;
    }
    memset(&params, 0, sizeof params);
    params.format = t->color;
    params.swizzled = d3d8_format_is_swizzled(t->color) ? 1u : 0u;
    params.bpp = layout->texel_bytes;
    params.face_stride = layout->faces == 6 ? (uint32_t)layout->face_stride : 0;
    params.faces = layout->faces;
    ID3D11DeviceContext_CSSetShader(context, decoder->shader, NULL, 0);
    ID3D11DeviceContext_CSSetConstantBuffers(context, 0, 1, &decoder->constants);
    ID3D11DeviceContext_CSSetShaderResources(context, 0, 2, views);
    for (level = 0; level < layout->levels; ++level) {
        const GpuTextureSubresource *sub = &layout->subresource[0][level];
        params.width = sub->width;
        params.height = sub->height;
        params.pitch = (uint32_t)sub->row_pitch;
        params.level_offset = (uint32_t)sub->offset;
        ID3D11DeviceContext_UpdateSubresource(context, (ID3D11Resource *)decoder->constants,
                                             0, NULL, &params, 0, 0);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 0, 1, &uav[level], NULL);
        ID3D11DeviceContext_Dispatch(context, (sub->width + 7) / 8,
                                     (sub->height + 7) / 8, layout->faces);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 0, 1, &no_uav, NULL);
    }
    ID3D11DeviceContext_CSSetShaderResources(context, 0, 2, no_views);
    ID3D11DeviceContext_CSSetShader(context, NULL, NULL, 0);
    gpu_d3d11_check_device(device, S_OK, "submitting texture decode");
    for (level = 0; level < layout->levels; ++level)
        ID3D11UnorderedAccessView_Release(uav[level]);
    if (views[1]) ID3D11ShaderResourceView_Release(views[1]);
    ID3D11ShaderResourceView_Release(views[0]);
    if (pal_buffer) ID3D11Buffer_Release(pal_buffer);
    ID3D11Buffer_Release(input);
    return 1;

failed:
    gpu_d3d11_check_device(device, hr, "creating texture compute resources");
    fprintf(stderr, "[GPU-D3D11] texture compute resource creation failed 0x%08lX\n",
            (unsigned long)hr);
    for (level = 0; level < GPU_TEXTURE_MAX_LEVELS; ++level)
        if (uav[level]) ID3D11UnorderedAccessView_Release(uav[level]);
    if (*output) ID3D11Texture2D_Release(*output);
    *output = NULL;
    if (views[1]) ID3D11ShaderResourceView_Release(views[1]);
    if (views[0]) ID3D11ShaderResourceView_Release(views[0]);
    if (pal_buffer) ID3D11Buffer_Release(pal_buffer);
    if (input) ID3D11Buffer_Release(input);
    return 0;
}
