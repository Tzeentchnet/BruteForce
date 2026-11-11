#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "xbox_memory_layout.h"
#include "gpu/d3d11_backend.h"
#include "gpu/texture_decode.h"
#include "gpu/texture_sampler.h"

extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 0; } \
} while (0)

static void *guest(uint32_t va)
{
    return (void *)((uintptr_t)va + xbox_GetMemoryOffset());
}

static int test_layout(void)
{
    Nv2aTexture t = {0};
    GpuTextureLayout l, base;
    uint8_t *bytes;
    uint32_t palette[32], out;
    unsigned invalid, i;

    t.width = 8; t.height = 4; t.color = 0x06; t.levels = 4;
    REQUIRE(gpu_texture_layout(&t, 1, 1, &l) && l.levels == 4 && l.span == 172,
            "rectangular swizzled chain span");
    REQUIRE(l.subresource[0][1].offset == 128 && l.subresource[0][1].size == 32 &&
            l.subresource[0][2].offset == 160 && l.subresource[0][2].width == 2 &&
            l.subresource[0][2].height == 1 && l.subresource[0][3].offset == 168 &&
            l.subresource[0][3].size == 4, "rectangular mip offsets through 1x1");
    REQUIRE(gpu_texture_layout(&t, 1, 0, &base) && base.levels == 1 && base.span == 128,
            "disabled mipmaps expose level zero only");

    t.width = t.height = 8;
    REQUIRE(gpu_texture_layout(&t, 6, 1, &l) && l.face_stride == 384 && l.span == 2260 &&
            l.subresource[5][3].offset == 2256 && l.subresource[5][3].size == 4,
            "cube span includes the last face and last level");
    REQUIRE(gpu_texture_layout(&t, 6, 0, &base) && base.face_stride == 384 &&
            base.span == 2176 && base.levels == 1,
            "legacy cube stride retains declared mip padding");
    t.color = 0x0C;
    REQUIRE(gpu_texture_layout(&t, 6, 1, &l) && l.face_stride == 128 && l.span == 696 &&
            l.subresource[5][3].offset == 688 && l.subresource[5][3].size == 8,
            "BC1 small mip blocks and final face");
    t.color = 0x0F;
    REQUIRE(gpu_texture_layout(&t, 6, 1, &l) && l.face_stride == 128 && l.span == 752 &&
            l.subresource[5][3].size == 16, "BC3 small mip blocks and final face");

    t.width = t.height = 4; t.color = 0x06; t.levels = 15;
    REQUIRE(gpu_texture_layout(&t, 6, 1, &l) && l.levels == 3 &&
            l.face_stride == 256 && l.span == 1364 &&
            l.subresource[5][2].offset == 1360 &&
            gpu_texture_layout(&t, 6, 0, &base) && base.face_stride == l.face_stride,
            "capped cube exposure preserves full declared face spacing");
    t.color = 0x0F;
    REQUIRE(gpu_texture_layout(&t, 6, 1, &l) && l.levels == 3 &&
            l.face_stride == 256 && l.span == 1328,
            "capped BC cube preserves full declared block spacing");

    t.width = t.height = 16; t.color = 0x0B; t.levels = 5; t.palette = 1;
    REQUIRE(gpu_texture_layout(&t, 6, 1, &l) && l.face_stride == 384 && l.span == 2261 &&
            l.subresource[5][4].offset == l.span - 1, "P8 complete cube chain");
    bytes = (uint8_t *)calloc(1, l.span);
    REQUIRE(bytes != NULL, "allocate P8 layout fixture");
    for (i = 0; i < 32; ++i)
        palette[i] = 0xFF000000u | i;
    bytes[l.span - 1] = 31;
    REQUIRE(gpu_texture_p8_indices_valid(&l, bytes, l.span, 32, &invalid) &&
            gpu_texture_decode_p8_cpu(&l, 5, 4, bytes, l.span, palette, 32, &out, 1) &&
            out == palette[31], "decode final P8 cube level");
    REQUIRE(!gpu_texture_p8_indices_valid(&l, bytes, l.span - 1, 32, &invalid) &&
            invalid == 256, "reject a truncated final P8 level");
    REQUIRE(!gpu_texture_decode_p8_cpu(&l, 5, 4, bytes, l.span, palette, 32, &out, 0),
            "reject a short P8 output");
    bytes[l.span - 1] = 32;
    REQUIRE(!gpu_texture_p8_indices_valid(&l, bytes, l.span, 32, &invalid) && invalid == 32 &&
            !gpu_texture_decode_p8_cpu(&l, 5, 4, bytes, l.span, palette, 32, &out, 1),
            "reject an invalid index in the final P8 cube level");
    free(bytes);

    t.width = 7; t.height = 3; t.color = 0x12; t.pitch = 32; t.levels = 5;
    REQUIRE(gpu_texture_layout(&t, 1, 1, &l) && l.storage == GPU_TEXTURE_LINEAR &&
            l.levels == 1 && l.span == 96, "linear texture stays at level zero");
    t.color = 0x24; t.pitch = 14;
    REQUIRE(gpu_texture_layout(&t, 1, 1, &l) && l.span == 44 &&
            gpu_texture_layout(&t, 1, 0, &base) && base.span == 56,
            "packed YUV includes a complete odd-width pair, retaining legacy off span");
    t.color = 0x25;
    REQUIRE(gpu_texture_layout(&t, 1, 1, &l) && l.span == 44,
            "both packed YUV formats use paired row bytes");
    t.color = 0x12; t.pitch = 32;
    REQUIRE(!gpu_texture_layout(&t, 6, 1, &l) &&
            gpu_texture_layout(&t, 6, 0, &base),
            "reject non-square cube when enabled; preserve legacy upload path");
    t.width = t.height = 8; t.pitch = 36;
    REQUIRE(gpu_texture_layout(&t, 6, 1, &l) && l.face_stride == 384 && l.span == 2208,
            "padded linear cube spacing");
    t.pitch = UINT32_MAX;
    REQUIRE(!gpu_texture_layout(&t, 1, 1, &l), "reject oversized linear span");
    t.width = UINT32_MAX;
    REQUIRE(!gpu_texture_layout(&t, 1, 1, &l), "reject overflowing dimensions");
    t.width = t.height = 8; t.pitch = 0; t.color = 0x06; t.levels = 16;
    REQUIRE(!gpu_texture_layout(&t, 1, 1, &l), "reject invalid level count");
    t.levels = 15; t.width = t.height = 1;
    REQUIRE(gpu_texture_layout(&t, 1, 1, &l) && l.declared_levels == 15 &&
            l.levels == 1 && l.span == 4, "cap levels at 1x1");
    t.levels = 0;
    REQUIRE(gpu_texture_layout(&t, 1, 1, &l) && l.levels == 1,
            "zero declared levels preserve level-zero fallback");
    t.width = 3;
    REQUIRE(!gpu_texture_layout(&t, 1, 1, &l) && !gpu_texture_layout(&t, 2, 1, &l),
            "reject non-power-of-two swizzles and invalid face counts");
    REQUIRE(gpu_texture_layout(&t, 1, 0, &base) && base.span == 12,
            "preserve a safely bounded legacy level-zero swizzle");
    t.height = 2;
    REQUIRE(!gpu_texture_layout(&t, 1, 0, &base),
            "reject a legacy swizzle whose Morton indices exceed its source span");
    puts("mip layout cases passed");
    return 1;
}

static int test_sampler_math(void)
{
    static const D3D11_FILTER expected[6][2] = {
        {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT},
        {D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT},
        {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT},
        {D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT},
        {D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR, D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR},
        {D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR, D3D11_FILTER_MIN_MAG_MIP_LINEAR}
    };
    D3D11_SAMPLER_DESC d;
    unsigned min, mag, warning;
    uint32_t control = (0x100u << 18) | (0x300u << 6);

    for (min = 1; min <= 6; ++min)
        for (mag = 1; mag <= 2; ++mag) {
            warning = gpu_texture_sampler_desc(&d, 1, 2, mag << 24 | min << 16 | 0x1F00u,
                                               control, 4, 1, 0);
            REQUIRE(!warning && d.Filter == expected[min - 1][mag - 1] &&
                    d.AddressU == D3D11_TEXTURE_ADDRESS_WRAP &&
                    d.AddressV == D3D11_TEXTURE_ADDRESS_MIRROR,
                    "independent minification, magnification and mip filters");
            if (min >= 3)
                REQUIRE(d.MinLOD == 1.0f && d.MaxLOD == 3.0f && d.MipLODBias == -1.0f,
                        "CONTROL0 clamps and signed 13-bit LOD bias");
            else
                REQUIRE(d.MinLOD == 0.0f && d.MaxLOD == 0.0f,
                        "non-mip minification stays at level zero");
        }
    gpu_texture_sampler_desc(&d, 3, 3, 2u << 24 | 1u << 16, control, 4, 0, 1);
    REQUIRE(d.Filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR && d.MaxLOD == 0.0f &&
            d.MipLODBias == 0.0f && d.MaxAnisotropy == 0,
            "disabled switch preserves legacy sampler");
    warning = gpu_texture_sampler_desc(&d, 3, 3, 1u << 24 | 3u << 16,
                                       0x300u << 18 | 0x100u << 6, 4, 1, 0);
    REQUIRE((warning & GPU_SAMPLER_LOD_RANGE) && d.MinLOD == 1.0f && d.MaxLOD == 1.0f,
            "inverted clamps are diagnosed and collapse to max");
    gpu_texture_sampler_desc(&d, 3, 3, 1u << 24 | 3u << 16,
                             0x900u << 18 | 0xFFFu << 6, 4, 1, 0);
    REQUIRE(d.MinLOD == 3.0f && d.MaxLOD == 3.0f, "clamps respect declared levels");
    gpu_texture_sampler_desc(&d, 3, 3, 1u << 24 | 3u << 16,
                             0x140u << 18 | 0x280u << 6, 4, 1, 0);
    REQUIRE(d.MinLOD == 1.25f && d.MaxLOD == 2.5f, "fractional 4.8 LOD clamps");
    warning = gpu_texture_sampler_desc(&d, 3, 3, 1u << 24 | 3u << 16 | 0xFFFu,
                                       0xFFFu << 6, 4, 1, 0);
    REQUIRE((warning & GPU_SAMPLER_BIAS_CLAMPED) &&
            d.MipLODBias == D3D11_MIP_LOD_BIAS_MAX, "D3D11 positive bias limit");
    warning = gpu_texture_sampler_desc(&d, 3, 3, 4u << 24 | 7u << 16,
                                       control, 4, 1, 0);
    REQUIRE((warning & GPU_SAMPLER_CONVOLUTION) && d.MaxLOD == 0.0f,
            "convolution approximation is diagnosed");
    warning = gpu_texture_sampler_desc(&d, 3, 3, 0u, control, 4, 1, 0);
    REQUIRE((warning & (GPU_SAMPLER_UNKNOWN_MIN | GPU_SAMPLER_UNKNOWN_MAG)) ==
            (GPU_SAMPLER_UNKNOWN_MIN | GPU_SAMPLER_UNKNOWN_MAG),
            "unknown filters are diagnosed");
    gpu_texture_sampler_desc(&d, 3, 3, 2u << 24 | 6u << 16, control, 4, 1, 1);
    REQUIRE(d.Filter == D3D11_FILTER_ANISOTROPIC && d.MaxAnisotropy == 8,
            "anisotropy is separately enabled for eligible samplers");
    gpu_texture_sampler_desc(&d, 3, 3, 1u << 24 | 6u << 16, control, 4, 1, 1);
    REQUIRE(d.Filter != D3D11_FILTER_ANISOTROPIC, "anisotropy does not override point magnification");
    gpu_texture_sampler_desc(&d, 3, 3, 2u << 24 | 5u << 16, control, 4, 1, 1);
    REQUIRE(d.Filter != D3D11_FILTER_ANISOTROPIC, "anisotropy does not override point minification");
    gpu_texture_sampler_desc(&d, 3, 3, 2u << 24 | 6u << 16, control, 1, 1, 1);
    REQUIRE(d.Filter != D3D11_FILTER_ANISOTROPIC && d.MaxLOD == 0.0f,
            "anisotropy requires an exposed mip chain");
    puts("mip sampler cases passed");
    return 1;
}

static void fill_texture(const Nv2aTexture *t, const GpuTextureLayout *layout)
{
    uint8_t *src = (uint8_t *)guest(t->offset);
    unsigned face, level;
    memset(src, 0, layout->span);
    for (face = 0; face < layout->faces; ++face)
        for (level = 0; level < layout->levels; ++level) {
            const GpuTextureSubresource *sub = &layout->subresource[face][level];
            uint8_t *p = src + sub->offset;
            size_t i;
            uint32_t color = 0xFF000000u | ((face + 1) * 20u << 16)
                           | ((level + 1) * 30u << 8) | (face * 11u + level * 3u);
            if (t->color == 0x0B) {
                memset(p, (int)((face * 4 + level) % 32), sub->size);
            } else if (layout->storage == GPU_TEXTURE_BC) {
                unsigned block_bytes = t->color == 0x0C ? 8 : 16;
                for (i = 0; i < sub->size; i += block_bytes) {
                    unsigned color_offset = block_bytes == 8 ? 0 : 8;
                    if (t->color == 0x0E) memset(p + i, 0xFF, 8);
                    if (t->color == 0x0F) p[i] = 0xFF;
                    p[i + color_offset] = (uint8_t)(face * 3 + level);
                    p[i + color_offset + 1] = (uint8_t)(0xF8u - level * 8u);
                }
            } else if (layout->storage == GPU_TEXTURE_LINEAR) {
                unsigned x, y;
                for (y = 0; y < sub->height; ++y)
                    for (x = 0; x < sub->width; ++x)
                        memcpy(p + (size_t)y * sub->row_pitch + x * 4, &color, 4);
            } else {
                for (i = 0; i < sub->size; i += 4)
                    memcpy(p + i, &color, 4);
            }
        }
}

static int verify_texture(const Nv2aTexture *t, int cube, unsigned stage, int enabled)
{
    ID3D11Device *device = bruteforce_d3d11_test_device();
    ID3D11DeviceContext *context = bruteforce_d3d11_test_context();
    ID3D11ShaderResourceView *srv;
    ID3D11Resource *resource = NULL;
    ID3D11Texture2D *staging = NULL;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    D3D11_TEXTURE2D_DESC desc;
    GpuTextureLayout layout;
    uint32_t *expected = NULL;
    unsigned levels, face, level;
    HRESULT hr;
    int ok = 0;

    if (!gpu_texture_layout(t, cube ? 6 : 1, enabled, &layout))
        return 0;
    srv = bruteforce_d3d11_test_texture(t, cube, stage, &levels);
    if (!srv || levels != layout.levels)
        return 0;
    ID3D11ShaderResourceView_GetDesc(srv, &sv);
    if ((cube && (sv.ViewDimension != D3D11_SRV_DIMENSION_TEXTURECUBE ||
                  sv.TextureCube.MipLevels != levels)) ||
        (!cube && (sv.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
                   sv.Texture2D.MipLevels != levels)))
        return 0;
    ID3D11ShaderResourceView_GetResource(srv, &resource);
    ID3D11Texture2D_GetDesc((ID3D11Texture2D *)resource, &desc);
    if (desc.MipLevels != levels || desc.ArraySize != (cube ? 6u : 1u))
        goto done;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(hr))
        goto done;
    expected = (uint32_t *)malloc((size_t)t->width * t->height * sizeof *expected);
    if (!expected)
        goto done;
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, resource);
    for (face = 0; face < layout.faces; ++face)
        for (level = 0; level < levels; ++level) {
            const GpuTextureSubresource *sub = &layout.subresource[face][level];
            unsigned index = level + face * levels, x, y;
            D3D11_MAPPED_SUBRESOURCE mapped;
            Nv2aTexture reference = *t;
            int compressed = desc.Format == DXGI_FORMAT_BC1_UNORM ||
                             desc.Format == DXGI_FORMAT_BC2_UNORM ||
                             desc.Format == DXGI_FORMAT_BC3_UNORM;
            reference.offset += (uint32_t)sub->offset;
            reference.width = sub->width;
            reference.height = sub->height;
            reference.pitch = layout.storage == GPU_TEXTURE_LINEAR ? (uint32_t)sub->row_pitch : 0;
            reference.levels = 1;
            if (!compressed && !nv2a_backend_decode_texture(&reference, expected))
                goto done;
            hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, index,
                                         D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr))
                goto done;
            for (y = 0; y < (compressed ? (sub->height + 3) / 4 : sub->height); ++y) {
                const uint8_t *row = (const uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch;
                if (compressed) {
                    if (memcmp(row, (uint8_t *)guest(t->offset) + sub->offset
                                    + (size_t)y * sub->row_pitch, sub->row_pitch)) {
                        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, index);
                        goto done;
                    }
                } else for (x = 0; x < sub->width; ++x) {
                    uint32_t argb = expected[(size_t)y * sub->width + x];
                    const uint8_t *px = row + x * 4;
                    uint8_t r = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? px[0] : px[2];
                    uint8_t b = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? px[2] : px[0];
                    if (r != (uint8_t)(argb >> 16) || px[1] != (uint8_t)(argb >> 8) ||
                        b != (uint8_t)argb || px[3] != (uint8_t)(argb >> 24)) {
                        fprintf(stderr, "FAIL: upload format %02X face %u level %u pixel %u,%u\n",
                                t->color, face, level, x, y);
                        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, index);
                        goto done;
                    }
                }
            }
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, index);
        }
    ok = 1;
done:
    free(expected);
    if (staging) ID3D11Texture2D_Release(staging);
    if (resource) ID3D11Resource_Release(resource);
    return ok;
}

static int test_uploads_and_cache(int enabled)
{
    static const uint32_t formats[] = {0x06, 0x0B, 0x0C, 0x0E, 0x0F, 0x12};
    unsigned i, cube, levels;
    uint32_t palette = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t *pal = (uint32_t *)guest(palette);
    REQUIRE(palette, "allocate mip fixture palette");
    for (i = 0; i < 256; ++i)
        pal[i] = 0xFF000000u | i * 0x010101u;
    nv2a_pb_exec_method(0, 0x1B60, (palette - XBOX_CONTIG_BASE) | 3u << 2);

    for (i = 0; i < sizeof formats / sizeof formats[0]; ++i)
        for (cube = 0; cube <= 1; ++cube) {
            Nv2aTexture t = {0};
            GpuTextureLayout full;
            ID3D11ShaderResourceView *first, *next;
            t.offset = xbox_ContiguousAlloc(0x4000, 4096);
            t.width = t.height = 8;
            t.color = formats[i];
            t.levels = 4;
            t.pitch = t.color == 0x12 ? 36 : 0;
            t.palette = t.color == 0x0B ? palette : 0;
            t.filter = 1u << 24 | 3u << 16;
            REQUIRE(t.offset && gpu_texture_layout(&t, cube ? 6 : 1, 1, &full),
                    "prepare upload layout");
            fill_texture(&t, &full);
            REQUIRE(verify_texture(&t, cube, 1, enabled), "upload and inspect every subresource");
            first = bruteforce_d3d11_test_texture(&t, cube, 1, &levels);
            REQUIRE(first != NULL, "retrieve cached mip texture");
            ID3D11ShaderResourceView_AddRef(first);
            next = bruteforce_d3d11_test_texture(&t, cube, 1, &levels);
            REQUIRE(next == first, "unchanged texture reuses its cache entry");
            if (full.levels > 1) {
                size_t last = full.subresource[full.faces - 1][full.levels - 1].offset;
                ((uint8_t *)guest(t.offset))[last] ^= 1;
                next = bruteforce_d3d11_test_texture(&t, cube, 1, &levels);
                REQUIRE(next && (enabled ? next != first : next == first),
                        "final face/level rewrite invalidates only an exposed chain");
                REQUIRE(verify_texture(&t, cube, 1, enabled), "rewritten texture contents");
            }
            ID3D11ShaderResourceView_Release(first);
            if (enabled && !cube && full.levels > 1) {
                first = bruteforce_d3d11_test_texture(&t, cube, 1, &levels);
                ID3D11ShaderResourceView_AddRef(first);
                t.levels = 3;
                next = bruteforce_d3d11_test_texture(&t, cube, 1, &levels);
                REQUIRE(next && next != first && levels == 3, "2D declared levels are cache identity");
                ID3D11ShaderResourceView_Release(first);
            }
            if (t.color == 0x0B) {
                first = bruteforce_d3d11_test_texture(&t, cube, 1, &levels);
                ID3D11ShaderResourceView_AddRef(first);
                pal[0] ^= 0x0000FF00u;
                next = bruteforce_d3d11_test_texture(&t, cube, 1, &levels);
                REQUIRE(next && next != first, "P8 palette rewrite invalidates the texture");
                ID3D11ShaderResourceView_Release(first);
            }
        }
    for (cube = 0; cube <= 1; ++cube) {
        Nv2aTexture t = {0};
        GpuTextureLayout layout;
        t.offset = xbox_ContiguousAlloc(0x1000, 4096);
        t.width = t.height = 2; t.color = 0x0C; t.levels = 2;
        REQUIRE(t.offset && gpu_texture_layout(&t, cube ? 6 : 1, 1, &layout),
                "prepare tiny BC texture");
        fill_texture(&t, &layout);
        REQUIRE(verify_texture(&t, cube, 1, enabled), "tiny BC CPU fallback uploads each level");
    }
    for (i = 0; i < 2; ++i) {
        Nv2aTexture t = {0};
        GpuTextureLayout layout;
        t.offset = xbox_ContiguousAlloc(0x1000, 4096);
        t.width = t.height = 4; t.color = i ? 0x0F : 0x06; t.levels = 15;
        REQUIRE(t.offset && gpu_texture_layout(&t, 6, 1, &layout) &&
                layout.levels == 3 && layout.face_stride == 256,
                "prepare capped declared cube chain");
        fill_texture(&t, &layout);
        REQUIRE(verify_texture(&t, 1, 1, enabled),
                "upload capped cube levels at declared face spacing");
    }
    if (!enabled) {
        Nv2aTexture t = {0};
        GpuTextureLayout layout;
        t.offset = xbox_ContiguousAlloc(0x1000, 4096);
        t.width = 3; t.height = 1; t.color = 0x06; t.levels = 1;
        REQUIRE(t.offset && gpu_texture_layout(&t, 1, 0, &layout),
                "prepare safely bounded legacy non-power-of-two swizzle");
        fill_texture(&t, &layout);
        REQUIRE(verify_texture(&t, 0, 1, 0),
                "legacy non-power-of-two CPU fallback remains available");
    }
    puts("mip uploads and cache cases passed");
    return 1;
}

static int test_sampler_cache(int enabled)
{
    uint32_t filter = 1u << 24 | 3u << 16;
    const char *aniso_option = getenv("RECOMP_GPU_ANISO");
    int aniso_enabled = aniso_option && strcmp(aniso_option, "1") == 0;
    D3D11_SAMPLER_DESC desc;
    ID3D11SamplerState *a = bruteforce_d3d11_test_sampler(3, 3, filter, 0x300u << 6, 4);
    ID3D11SamplerState *b = bruteforce_d3d11_test_sampler(3, 3, filter, 0x300u << 6, 4);
    ID3D11SamplerState *bias = bruteforce_d3d11_test_sampler(3, 3, filter | 0x1F00u, 0x300u << 6, 4);
    ID3D11SamplerState *clamp = bruteforce_d3d11_test_sampler(3, 3, filter, 0x100u << 6, 4);
    ID3D11SamplerState *mip = bruteforce_d3d11_test_sampler(3, 3, 1u << 24 | 5u << 16, 0x300u << 6, 4);
    REQUIRE(a && a == b && bias && clamp && mip, "sampler cache reuses equal states");
    REQUIRE(enabled ? (a != bias && a != clamp && a != mip)
                    : (a == bias && a == clamp && a == mip),
            "sampler cache identity includes effective bias, clamps and mip filter");
    a = bruteforce_d3d11_test_sampler(3, 3, 2u << 24 | 6u << 16, 0x300u << 6, 4);
    REQUIRE(a != NULL, "create anisotropic-option sampler");
    ID3D11SamplerState_GetDesc(a, &desc);
    REQUIRE((desc.Filter == D3D11_FILTER_ANISOTROPIC) == (enabled && aniso_enabled),
            "anisotropy switch changes only eligible enabled samplers");
    if (enabled && aniso_enabled)
        REQUIRE(desc.MaxAnisotropy == 8, "anisotropic sampler uses 8x");
    if (enabled) {
        REQUIRE(bruteforce_d3d11_test_sampler(3, 3, 0, 0x300u << 6, 4) &&
                bruteforce_d3d11_test_sampler(3, 3, 4u << 24 | 7u << 16,
                                              0x300u << 6, 4) &&
                bruteforce_d3d11_test_sampler(3, 3, filter | 0xFFFu,
                                              0x300u << 6, 4) &&
                bruteforce_d3d11_test_sampler(3, 3, filter,
                                              0x300u << 18 | 0x100u << 6, 4),
                "fallback sampler diagnostics retain valid D3D states");
    }
    puts("mip sampler cache cases passed");
    return 1;
}

static int test_guarded_fallback(int enabled, int cube, int tracked_short, int excess_levels)
{
    Nv2aTexture t = {0};
    GpuTextureLayout base, full;
    ID3D11ShaderResourceView *first, *next;
    uint32_t allocation;
    unsigned levels;
    DWORD old_protect;
    t.width = t.height = excess_levels ? 4 : 8;
    t.color = 0x06;
    t.levels = excess_levels ? 15 : 4;
    REQUIRE(gpu_texture_layout(&t, cube ? 6 : 1, 0, &base) &&
            gpu_texture_layout(&t, cube ? 6 : 1, 1, &full) && base.span < 4096 &&
            full.span > base.span, "prepare final-level guard");
    allocation = xbox_ContiguousAlloc(tracked_short ? 0x1000 : 0x2000, 4096);
    REQUIRE(allocation, "allocate guarded chain");
    t.offset = allocation + 0x1000 - (uint32_t)base.span;
    memset(guest(t.offset), 0x4A, base.span);
    if (!tracked_short) {
        memset(guest(allocation + 0x1000), 0x4A, 0x1000);
        first = bruteforce_d3d11_test_texture(&t, cube, 0, &levels);
        REQUIRE(first && levels == (enabled ? full.levels : 1u), "upload before guard is installed");
        ID3D11ShaderResourceView_AddRef(first);
        REQUIRE(VirtualProtect(guest(allocation + 0x1000), 0x1000, PAGE_NOACCESS, &old_protect),
                "guard the missing final levels");
    } else {
        first = NULL;
    }
    next = bruteforce_d3d11_test_texture(&t, cube, 0, &levels);
    REQUIRE(next && levels == 1 && (!enabled || !first || next != first),
            "unreadable or tracked-short chain falls back to readable level zero");
    if (enabled) {
        ID3D11ShaderResourceView *changed;
        ID3D11ShaderResourceView_AddRef(next);
        ((uint8_t *)guest(t.offset))[0] ^= 1;
        changed = bruteforce_d3d11_test_texture(&t, cube, 0, &levels);
        REQUIRE(changed && changed != next && levels == 1,
                "fallback base rewrite invalidates even in approximate-cache mode");
        ID3D11ShaderResourceView_Release(next);
    }
    if (!tracked_short) {
        DWORD ignored;
        REQUIRE(VirtualProtect(guest(allocation + 0x1000), 0x1000, old_protect, &ignored),
                "restore guarded pages");
        if (enabled) {
            ID3D11ShaderResourceView *restored =
                bruteforce_d3d11_test_texture(&t, cube, 0, &levels);
            REQUIRE(restored && levels == full.levels, "restored mip chain is retried");
        }
        ID3D11ShaderResourceView_Release(first);
    }
    return 1;
}

static int test_guarded_yuv(void)
{
    Nv2aTexture t = {0};
    GpuTextureLayout layout;
    uint32_t allocation;
    unsigned format;
    DWORD old_protect, ignored;
    t.width = 7; t.height = 3; t.pitch = 14; t.levels = 1;
    t.color = 0x24;
    REQUIRE(gpu_texture_layout(&t, 1, 1, &layout) && layout.span == 44,
            "prepare odd-width YUV guard");
    allocation = xbox_ContiguousAlloc(0x2000, 4096);
    REQUIRE(allocation, "allocate guarded YUV");
    t.offset = allocation + 0x1000 - (uint32_t)layout.span;
    memset(guest(t.offset), 128, layout.span);
    REQUIRE(VirtualProtect(guest(allocation + 0x1000), 0x1000, PAGE_NOACCESS, &old_protect),
            "guard bytes after final YUV pair");
    for (format = 0x24; format <= 0x25; ++format) {
        t.color = format;
        REQUIRE(verify_texture(&t, 0, 0, 1), "guarded odd-width YUV upload and CPU parity");
    }
    REQUIRE(VirtualProtect(guest(allocation + 0x1000), 0x1000, old_protect, &ignored),
            "restore guarded YUV pages");
    puts("guarded odd-width YUV cases passed");
    return 1;
}

static int test_guarded_p8_cube(int enabled, int tracked_short)
{
    Nv2aTexture t = {0};
    GpuTextureLayout base, full;
    ID3D11ShaderResourceView *first = NULL, *next;
    uint32_t allocation, palette;
    unsigned levels;
    DWORD old_protect, ignored;
    t.width = t.height = 16; t.color = 0x0B; t.levels = 5;
    REQUIRE(gpu_texture_layout(&t, 6, 0, &base) &&
            gpu_texture_layout(&t, 6, 1, &full) && base.span < 4096 &&
            full.span > base.span, "prepare final P8 cube-level guard");
    allocation = xbox_ContiguousAlloc(tracked_short ? 0x1000 : 0x2000, 4096);
    palette = xbox_ContiguousAlloc(128, 4096);
    REQUIRE(allocation && palette, "allocate guarded P8 cube");
    t.offset = allocation + 0x1000 - (uint32_t)base.span;
    t.palette = palette;
    memset(guest(t.offset), 31, base.span);
    memset(guest(palette), 0xFF, 128);
    nv2a_pb_exec_method(0, 0x1B20, (palette - XBOX_CONTIG_BASE) | (3u << 2));
    if (!tracked_short) {
        memset(guest(allocation + 0x1000), 31, 0x1000);
        first = bruteforce_d3d11_test_texture(&t, 1, 0, &levels);
        REQUIRE(first && levels == (enabled ? 5u : 1u), "upload P8 cube before guard");
        ID3D11ShaderResourceView_AddRef(first);
        REQUIRE(VirtualProtect(guest(allocation + 0x1000), 0x1000, PAGE_NOACCESS, &old_protect),
                "guard final P8 cube mip levels");
    }
    next = bruteforce_d3d11_test_texture(&t, 1, 0, &levels);
    REQUIRE(next && levels == 1 && (!enabled || !first || next != first),
            "guarded or tracked-short P8 cube falls back to readable level zero");
    if (enabled) {
        ID3D11ShaderResourceView *changed;
        ID3D11ShaderResourceView_AddRef(next);
        ((uint8_t *)guest(t.offset))[0] = 30;
        changed = bruteforce_d3d11_test_texture(&t, 1, 0, &levels);
        REQUIRE(changed && changed != next && levels == 1,
                "fallback P8 rewrite invalidates even in approximate-cache mode");
        ID3D11ShaderResourceView_Release(next);
    }
    if (!tracked_short) {
        REQUIRE(VirtualProtect(guest(allocation + 0x1000), 0x1000, old_protect, &ignored),
                "restore final P8 cube mip pages");
        if (enabled)
            REQUIRE(bruteforce_d3d11_test_texture(&t, 1, 0, &levels) && levels == 5,
                    "restored P8 cube chain is retried");
        ID3D11ShaderResourceView_Release(first);
    }
    return 1;
}

static int test_guest_range_overflow(void)
{
    Nv2aTexture t = {0};
    unsigned levels;
    t.width = t.height = 4;
    t.color = 0x06;
    t.levels = 3;
    t.offset = UINT32_MAX - 7;
    REQUIRE(!bruteforce_d3d11_test_texture(&t, 0, 0, &levels),
            "reject a mip chain crossing the 32-bit guest address limit");
    t.offset = XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE - 16;
    REQUIRE(!bruteforce_d3d11_test_texture(&t, 0, 0, &levels),
            "reject a mip chain crossing the guest contiguous window");
    return 1;
}

int bruteforce_test_mipmaps(void)
{
    const char *option = getenv("RECOMP_GPU_MIPS");
    int enabled = option && strcmp(option, "1") == 0;
    if (!test_layout() || !test_sampler_math() || !test_sampler_cache(enabled) ||
        !test_uploads_and_cache(enabled) || !test_guarded_fallback(enabled, 0, 0, 0) ||
        !test_guarded_fallback(enabled, 1, 0, 0) || !test_guarded_fallback(enabled, 0, 1, 0) ||
        !test_guarded_fallback(enabled, 1, 0, 1) ||
        !test_guarded_p8_cube(enabled, 0) || !test_guarded_p8_cube(enabled, 1) ||
        (enabled && (!test_guarded_yuv() || !test_guest_range_overflow())))
        return 0;
    puts("guarded mip fallback cases passed");
    return 1;
}

int bruteforce_test_mip_p8_invalid(void)
{
    Nv2aTexture t = {0};
    GpuTextureLayout layout;
    uint32_t tex, pal;
    unsigned levels;
    t.width = t.height = 16;
    t.color = 0x0B;
    t.levels = 5;
    REQUIRE(gpu_texture_layout(&t, 6, 1, &layout) && layout.faces == 6 &&
            layout.levels == 5, "invalid P8 cube layout");
    tex = xbox_ContiguousAlloc((uint32_t)layout.span, 4096);
    pal = xbox_ContiguousAlloc(128, 4096);
    REQUIRE(tex && pal, "allocate invalid P8 cube");
    memset(guest(tex), 0, layout.span);
    memset(guest(pal), 0xFF, 128);
    ((uint8_t *)guest(tex))[layout.subresource[5][4].offset] = 32;
    t.offset = tex;
    t.palette = pal;
    nv2a_pb_exec_method(0, 0x1B20, (pal - XBOX_CONTIG_BASE) | (3u << 2));
    REQUIRE(bruteforce_d3d11_test_texture(&t, 1, 0, &levels) != NULL && levels == 1,
            "disabled mip path ignores invalid unused lower level");
    puts("P8 level zero remained valid");
    return 1;
}
