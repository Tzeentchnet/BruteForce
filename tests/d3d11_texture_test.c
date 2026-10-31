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
#include "gpu/texture_decode.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);

static void *guest(uint32_t va)
{
    return (void *)((uintptr_t)va + xbox_GetMemoryOffset());
}

static size_t reference_stride(uint32_t w, uint32_t h, unsigned bpp,
                               uint32_t pitch, unsigned levels, int swizzled)
{
    size_t n = 0;
    unsigned i;
    if (!swizzled)
        n = (size_t)pitch * h;
    else {
        for (i = 0; i < (levels ? levels : 1); ++i) {
            n += (size_t)w * h * bpp;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }
    return (n + 127) & ~(size_t)127;
}

static int check_texture(ID3D11Device *device, ID3D11DeviceContext *context,
                         GpuTextureDecoder *decoder, Nv2aTexture *t, unsigned bpp,
                         int swizzled, int faces, unsigned palette_entries)
{
    ID3D11Texture2D *gpu = NULL, *staging = NULL;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    uint32_t *expected = NULL, *p8_decoded = NULL;
    size_t stride = reference_stride(t->width, t->height, bpp, t->pitch, t->levels, swizzled);
    unsigned face, x, y;
    HRESULT hr;
    int ok = 0;

    if (faces == 6 && gpu_texture_face_stride(t) != stride) {
        fprintf(stderr, "FAIL: cube face stride format 0x%02X\n", t->color);
        goto done;
    }
    expected = (uint32_t *)malloc((size_t)t->width * t->height * sizeof *expected);
    if (!expected)
        goto done;
    if (t->color == 0x0B) {
        p8_decoded = (uint32_t *)malloc((size_t)t->width * t->height * faces * sizeof *p8_decoded);
        if (!p8_decoded || !gpu_texture_decode_p8_cpu(t, faces, guest(t->offset),
                                                       guest(t->palette), palette_entries, p8_decoded))
            goto done;
    }
    if (!gpu_texture_decode(decoder, device, context, t, faces, guest(t->offset),
                            t->palette ? guest(t->palette) : NULL, palette_entries, &gpu))
        goto done;
    ID3D11Texture2D_GetDesc(gpu, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: texture staging resource 0x%08lX\n", (unsigned long)hr);
        goto done;
    }
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)gpu);
    for (face = 0; face < (unsigned)faces; ++face) {
        Nv2aTexture reference = *t;
        reference.offset += (uint32_t)(face * stride);
        if (!nv2a_backend_decode_texture(&reference, expected)) {
            fprintf(stderr, "FAIL: CPU texture decode format 0x%02X\n", t->color);
            goto done;
        }
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, face,
                                    D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            fprintf(stderr, "FAIL: map texture 0x%08lX\n", (unsigned long)hr);
            goto done;
        }
        for (y = 0; y < t->height; ++y) {
            const uint8_t *row = (const uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch;
            for (x = 0; x < t->width; ++x) {
                uint32_t argb = expected[(size_t)y * t->width + x];
                const uint8_t *rgba = row + x * 4;
                if (p8_decoded && p8_decoded[((size_t)face * t->height + y) * t->width + x] != argb) {
                    fprintf(stderr, "FAIL: CPU P8 snapshot decoder face %u pixel %u,%u\n",
                            face, x, y);
                    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, face);
                    goto done;
                }
                if (rgba[0] != ((argb >> 16) & 255) || rgba[1] != ((argb >> 8) & 255)
                    || rgba[2] != (argb & 255) || rgba[3] != (argb >> 24)) {
                    fprintf(stderr, "FAIL: format 0x%02X %ux%u face %u pixel %u,%u:"
                                    " expected %08X got %02X%02X%02X%02X\n",
                            t->color, t->width, t->height, face, x, y, argb,
                            rgba[3], rgba[0], rgba[1], rgba[2]);
                    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, face);
                    goto done;
                }
            }
        }
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, face);
    }
    ok = 1;
done:
    free(p8_decoded);
    free(expected);
    if (staging) ID3D11Texture2D_Release(staging);
    if (gpu) ID3D11Texture2D_Release(gpu);
    return ok;
}

int bruteforce_test_textures(int hardware)
{
    static const struct { uint32_t format; unsigned bpp; int swizzled; } formats[] = {
        {0x00, 1, 1}, {0x02, 2, 1}, {0x03, 2, 1}, {0x04, 2, 1},
        {0x05, 2, 1}, {0x06, 4, 1}, {0x07, 4, 1}, {0x0B, 1, 1},
        {0x10, 2, 0}, {0x11, 2, 0}, {0x12, 4, 0}, {0x13, 1, 0},
        {0x19, 1, 1}, {0x1C, 2, 0}, {0x1D, 2, 0}, {0x1E, 4, 0},
        {0x1F, 1, 0}, {0x3F, 4, 0}, {0x40, 4, 0}, {0x41, 4, 0},
    };
    static const uint32_t dimensions[][2] = {{1, 1}, {2, 8}, {8, 2}, {16, 16}, {64, 32}};
    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0, got;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    GpuTextureDecoder decoder = {0};
    uint32_t source = xbox_ContiguousAlloc(0x10000, 4096);
    uint32_t palette = xbox_ContiguousAlloc(0x1000, 4096);
    uint8_t *bytes;
    uint32_t rng = 0xA5137B9Du;
    unsigned i, j, cases = 0;
    HRESULT hr;
    int ok = 0;

    if (!source || !palette) {
        fprintf(stderr, "FAIL: allocate texture fixture memory\n");
        return 0;
    }
    bytes = (uint8_t *)guest(source);
    for (i = 0; i < 0x10000; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        bytes[i] = (uint8_t)rng;
    }
    bytes = (uint8_t *)guest(palette);
    for (i = 0; i < 1024; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        bytes[i] = (uint8_t)rng;
    }
    hr = D3D11CreateDevice(NULL, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,
                           NULL, D3D11_CREATE_DEVICE_SINGLETHREADED, &want, 1,
                           D3D11_SDK_VERSION, &device, &got, &context);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: create texture test device 0x%08lX\n", (unsigned long)hr);
        return 0;
    }
    for (i = 0; i < sizeof formats / sizeof formats[0]; ++i) {
        Nv2aTexture t = {0};
        t.offset = source;
        t.color = formats[i].format;
        t.palette = t.color == 0x0B ? palette : 0;
        for (j = 0; j < sizeof dimensions / sizeof dimensions[0]; ++j) {
            t.width = dimensions[j][0];
            t.height = dimensions[j][1];
            t.pitch = formats[i].swizzled ? 512 : t.width * formats[i].bpp + 3;
            t.levels = 1;
            if (!check_texture(device, context, &decoder, &t, formats[i].bpp,
                               formats[i].swizzled, 1, t.palette ? 256u : 0u))
                goto done;
            cases++;
        }
        t.width = t.height = 8;
        t.pitch = formats[i].swizzled ? 512 : t.width * formats[i].bpp + 3;
        for (j = 1; j <= 3; j += 2) {
            t.levels = j;
            if (!check_texture(device, context, &decoder, &t, formats[i].bpp,
                               formats[i].swizzled, 6, t.palette ? 256u : 0u))
                goto done;
            cases++;
        }
    }
    for (i = 0; i < 4; ++i) {
        Nv2aTexture t = {0};
        unsigned entries = 256u >> i, face, index, invalid;
        uint32_t allocation = xbox_ContiguousAlloc(0x2000, 4096);
        size_t stride;
        DWORD old_protect;
        uint32_t *table;

        if (!allocation)
            goto done;
        t.offset = source;
        t.palette = allocation + 0x1000 - entries * 4;
        t.width = t.height = 8;
        t.color = 0x0B;
        t.levels = 3;
        stride = gpu_texture_face_stride(&t);
        memset(guest(source), 0xFF, 0x10000);
        for (face = 0; face < 6; ++face)
            for (index = 0; index < 64; ++index)
                ((uint8_t *)guest(source))[face * stride + index] = (uint8_t)((index + face) % entries);
        table = (uint32_t *)guest(t.palette);
        for (index = 0; index < entries; ++index)
            table[index] = 0xFF000000u | index * 0x010101u;
        if (!VirtualProtect(guest(allocation + 0x1000), 0x1000, PAGE_NOACCESS, &old_protect))
            goto done;
        if (!check_texture(device, context, &decoder, &t, 1, 1, 6, entries))
            goto done;
        if (entries < 256) {
            ((uint8_t *)guest(source))[5 * stride + 63] = (uint8_t)entries;
            if (gpu_texture_p8_indices_valid(&t, 6, guest(source), entries, &invalid)
                || invalid != entries) {
                fprintf(stderr, "FAIL: out-of-range P8 index in the last cube face\n");
                goto done;
            }
        }
    }
    puts("short P8 cube cases 4");
    {
        Nv2aTexture no_palette = {0};
        no_palette.width = no_palette.height = 8;
        no_palette.color = 0x0B;
        if (gpu_texture_decode_supported(&no_palette)) {
            fprintf(stderr, "FAIL: palette-less P8 texture accepted\n");
            goto done;
        }
    }
    printf("texture cases %u\n", cases);
    ok = 1;
done:
    if (decoder.constants) ID3D11Buffer_Release(decoder.constants);
    if (decoder.shader) ID3D11ComputeShader_Release(decoder.shader);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    return ok;
}
