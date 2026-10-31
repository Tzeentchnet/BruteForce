/*
 * CPU rasteriser versus Direct3D 11 back end (on WARP), same pushbuffer.
 *
 * Draws one synthetic scene through the executor either with the CPU
 * rasteriser ("cpu") or with the D3D11 back end on the WARP device ("gpu"),
 * then writes the presented surface's pixels to a file; test_d3d11.py
 * compares the two. The scene covers combiners (stage 0 times diffuse),
 * affine colour interpolation, a swizzled texture, depth testing, alpha
 * blending, render to texture without readback, and a texture rewritten at
 * the same address between frames. The "-vsh" modes draw the same scene
 * with a vertex program (oPos = v0, oD0 = v3, oT0 = v9) instead of the
 * fixed-function pipeline; "gpu-vsh" runs it on the GPU (draw_vsh) with
 * every batch verified against the interpreter.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "nv2a_vsh_interp.h"
#include "xbox_memory_layout.h"
#include "gpu/d3d11_backend.h"
#include "gpu/d3d11_device_status.h"
#include "gpu/null_backend.h"

typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern void nv2a_pb_exec_poll(void);
extern void nv2a_pb_exec_report(void);
extern int bruteforce_test_textures(int hardware);
extern int bruteforce_test_shader_cache_keys(void);
extern int bruteforce_test_shader_cache_publication(int readonly, int different, int corrupt);
extern int bruteforce_test_shader_cache_temp_collision(int exhausted);

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 0; } \
} while (0)

#define B_SIZE 64u
#define A_SIZE 32u

static void method(uint32_t address, uint32_t value)
{
    nv2a_pb_exec_method(0, address, value);
}

static void method_float(uint32_t address, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    method(address, bits);
}

static void *guest(uint32_t va)
{
    return (void *)((uintptr_t)va + xbox_GetMemoryOffset());
}

static int initialize(const char *path)
{
    FILE *file = fopen(path, "rb");
    unsigned char *image;
    long length;
    int ok;

    REQUIRE(file != NULL, "open synthetic XBE");
    REQUIRE(fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > 0 &&
            fseek(file, 0, SEEK_SET) == 0, "size synthetic XBE");
    image = (unsigned char *)malloc((size_t)length);
    REQUIRE(image != NULL, "allocate synthetic XBE");
    REQUIRE(fread(image, 1, (size_t)length, file) == (size_t)length &&
            fclose(file) == 0, "read synthetic XBE");
    ok = xbox_MemoryLayoutInit(image, (size_t)length);
    free(image);
    REQUIRE(ok, "initialize guest memory");
    return 1;
}

static HRESULT s_mock_removed_reason;
static HRESULT s_mock_query_result;

static HRESULT STDMETHODCALLTYPE mock_removed_reason(ID3D11Device *device)
{
    (void)device;
    return s_mock_removed_reason;
}

static HRESULT STDMETHODCALLTYPE mock_get_data(ID3D11DeviceContext *context,
                                                ID3D11Asynchronous *query, void *data,
                                                UINT size, UINT flags)
{
    (void)context; (void)query; (void)flags;
    if (s_mock_query_result == S_OK && data && size == sizeof(BOOL))
        *(BOOL *)data = TRUE;
    return s_mock_query_result;
}

static int test_device_status(const char *mode, const char *path)
{
    ID3D11DeviceVtbl vtable;
    ID3D11Device device;
    ID3D11DeviceContextVtbl context_vtable;
    ID3D11DeviceContext context;
    HRESULT result = S_OK;
    BOOL complete = FALSE;
    FILE *f = fopen(path, "wb");

    REQUIRE(f != NULL, "open mock device status output");
    memset(&vtable, 0, sizeof vtable);
    vtable.GetDeviceRemovedReason = mock_removed_reason;
    device.lpVtbl = &vtable;
    s_mock_removed_reason = S_OK;
    if (strstr(mode, "result-removed"))
        result = DXGI_ERROR_DEVICE_REMOVED;
    else if (strstr(mode, "result-reset"))
        result = DXGI_ERROR_DEVICE_RESET;
    else if (strstr(mode, "result-hung"))
        result = DXGI_ERROR_DEVICE_HUNG;
    else if (strstr(mode, "result-driver"))
        result = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    else if (strstr(mode, "unrelated"))
        result = E_OUTOFMEMORY;
    else if (strstr(mode, "reason-hung"))
        s_mock_removed_reason = DXGI_ERROR_DEVICE_HUNG;
    else if (strstr(mode, "reason-invalid"))
        s_mock_removed_reason = DXGI_ERROR_INVALID_CALL;
    else if (strcmp(mode, "device-pending") == 0) {
        result = S_FALSE;
        s_mock_removed_reason = DXGI_ERROR_DEVICE_REMOVED;
    }
    REQUIRE(fwrite("pending", 1, 7, f) == 7 && fflush(f) == 0, "write pending marker");
    if (strstr(mode, "device-poll")) {
        memset(&context_vtable, 0, sizeof context_vtable);
        context_vtable.GetData = mock_get_data;
        context.lpVtbl = &context_vtable;
        s_mock_query_result = S_OK;
        if (strstr(mode, "poll-pending") || strstr(mode, "poll-lost"))
            s_mock_query_result = S_FALSE;
        if (strstr(mode, "poll-lost"))
            s_mock_removed_reason = DXGI_ERROR_DEVICE_REMOVED;
        if (strstr(mode, "poll-result-removed"))
            s_mock_query_result = DXGI_ERROR_DEVICE_REMOVED;
        result = gpu_d3d11_get_data(&device, &context, NULL, &complete, sizeof complete, 0,
                                    "synthetic visibility poll");
        if (result == S_FALSE) {
            REQUIRE(fclose(f) == 0, "close pending marker");
            return 1;
        }
        REQUIRE(result == S_OK && complete, "mock query completed");
    } else {
        gpu_d3d11_check_device(&device, result, "synthetic pending GPU work");
    }
    REQUIRE(fwrite(" completed", 1, 10, f) == 10 && fclose(f) == 0,
            "write completed marker");
    return 1;
}

static void surface(uint32_t va, uint32_t size, uint32_t zeta)
{
    method(0x0200, size << 16);
    method(0x0204, size << 16);
    method(0x0208, 0x128);                           /* linear A8R8G8B8, Z24S8 */
    method(0x020C, (size * 4) | (size * 4) << 16);
    method(0x0210, va - XBOX_CONTIG_BASE);
    method(0x0214, zeta ? zeta - XBOX_CONTIG_BASE : 0);
}

static void clear(uint32_t flags, uint32_t color, uint32_t zstencil)
{
    method(0x1D8C, zstencil);
    method(0x1D90, color);
    method(0x1D94, flags);
}

/* Stage 0 times diffuse in one general combiner stage (or diffuse alone
 * when stage 0 is off), written to spare0, no final combiner. */
static void combiners(int textured)
{
    method(0x1E70, textured ? 1u : 0u);              /* stage program */
    method(0x0AC0, (textured ? 0x08u : 0x20u) << 24 | 0x04u << 16);   /* colour ICW */
    method(0x0260, (textured ? 0x18u : 0x30u) << 24 | 0x14u << 16);   /* alpha ICW */
    method(0x1E40, 0x0Cu << 4);                      /* colour OCW: AB -> spare0 */
    method(0x0AA0, 0x0Cu << 4);                      /* alpha OCW */
    method(0x0288, 0);
    method(0x028C, 0);
    method(0x1E60, 1);                               /* one stage */
}

typedef struct { float x, y, z, w; } Pos;

/* A fixed-function batch: positions, D3DCOLOR diffuse and stage-0
 * coordinates in three guest arrays, drawn as DRAW_ARRAYS. */
static void draw_with_texcoords(uint32_t prim, const Pos *p, const uint32_t *c,
                                const float *uv, unsigned components, unsigned n, uint32_t vb)
{
    Pos *pp = (Pos *)guest(vb);
    uint32_t *cc = (uint32_t *)guest(vb + 0x400);
    float *tt = (float *)guest(vb + 0x800);
    unsigned i, j;

    for (i = 0; i < n; ++i) {
        pp[i] = p[i];
        cc[i] = c[i];
        for (j = 0; j < components; ++j)
            tt[i * components + j] = uv[i * components + j];
    }
    method(0x1720, vb - XBOX_CONTIG_BASE);
    method(0x1760, 0x1042);                          /* float4, stride 16 */
    method(0x172C, vb + 0x400 - XBOX_CONTIG_BASE);
    method(0x176C, 0x0440);                          /* D3DCOLOR, stride 4 */
    method(0x1744, vb + 0x800 - XBOX_CONTIG_BASE);
    method(0x1784, (components * 4) << 8 | components << 4 | 2);
    method(0x17FC, prim);
    method(0x1810, (n - 1) << 24);
    method(0x17FC, 0);
}

static void draw(uint32_t prim, const Pos *p, const uint32_t *c, const float (*uv)[2],
                 unsigned n, uint32_t vb)
{
    draw_with_texcoords(prim, p, c, &uv[0][0], 2, n, vb);
}

static void texture_swizzled8(uint32_t va)
{
    method(0x1B00, va - XBOX_CONTIG_BASE);
    method(0x1B04, 2u << 4 | 0x06u << 8 | 1u << 16 | 3u << 20 | 3u << 24);
    method(0x1B08, 1u | 1u << 8);                    /* wrap */
    method(0x1B14, 1u << 24 | 1u << 16);             /* nearest */
}

static void texture_linear_stage(unsigned stage, uint32_t va, unsigned width,
                                 unsigned height, unsigned pitch, unsigned format)
{
    uint32_t base = 0x1B00 + stage * 0x40;
    method(base, va - XBOX_CONTIG_BASE);
    method(base + 0x04, 2u << 4 | format << 8 | 1u << 16);
    method(base + 0x10, pitch << 16);
    method(base + 0x1C, width << 16 | height);
    method(base + 0x08, 3u | 3u << 8);              /* clamp */
    method(base + 0x14, 1u << 24 | 1u << 16);
}

static void texture_linear(uint32_t va, uint32_t size)
{
    texture_linear_stage(0, va, size, size, size * 4, 0x12);
}

static void texture_palette8(uint32_t va, uint32_t palette)
{
    method(0x1B00, va - XBOX_CONTIG_BASE);
    method(0x1B04, 2u << 4 | 0x0Bu << 8 | 1u << 16 | 3u << 20 | 3u << 24);
    method(0x1B20, palette - XBOX_CONTIG_BASE);
    method(0x1B08, 1u | 1u << 8);
    method(0x1B14, 1u << 24 | 1u << 16);
}

static void texture_palette_stage(unsigned stage, uint32_t va, uint32_t palette,
                                  unsigned length_code)
{
    uint32_t base = 0x1B00 + stage * 0x40;
    method(base, va - XBOX_CONTIG_BASE);
    method(base + 0x04, 2u << 4 | 0x0Bu << 8 | 1u << 16 | 3u << 20 | 3u << 24);
    method(base + 0x20, (palette - XBOX_CONTIG_BASE) | length_code << 2);
    method(base + 0x08, 1u | 1u << 8);
    method(base + 0x14, 1u << 24 | 1u << 16);
}

static void combiners_stage(unsigned stage)
{
    method(0x1E70, 1u << (stage * 5));
    method(0x0AC0, (0x08u + stage) << 24 | 0x04u << 16);
    method(0x0260, (0x18u + stage) << 24 | 0x14u << 16);
    method(0x1E40, 0x0Cu << 4);
    method(0x0AA0, 0x0Cu << 4);
    method(0x0288, 0);
    method(0x028C, 0);
    method(0x1E60, 1);
}

static void report_at(uint32_t report, unsigned index)
{
    ((volatile uint32_t *)guest(report + index * 16))[3] = 0xFFFFFFFFu;
    method(0x17D0, report + index * 16 - XBOX_CONTIG_BASE);
}

static void flip(void)
{
    method(0x012C, 0);
    method(0x0130, 0);
}

/* mov oD0, v3; mov oT0, v9; mov oPos, v0 (FINAL). */
static void load_program(void)
{
    const uint32_t color_ins[4] = {
        0, (1u << 21) | 0x1Bu | (3u << 9), 2u << 26, (0xFu << 12) | (1u << 11) | (3u << 3)
    };
    const uint32_t tex_ins[4] = {
        0, (1u << 21) | 0x1Bu | (9u << 9), 2u << 26, (0xFu << 12) | (1u << 11) | (9u << 3)
    };
    const uint32_t position_ins[4] = {
        0, (1u << 21) | 0x1Bu, 2u << 26, (0xFu << 12) | (1u << 11) | 1u
    };
    nv2a_vsh_set_instruction(0, color_ins);
    nv2a_vsh_set_instruction(1, tex_ins);
    nv2a_vsh_set_instruction(2, position_ins);
    method(0x1E94, 2);                               /* TRANSFORM_EXECUTION_MODE: program */
}

static void screen_quad(uint32_t color)
{
    static const float points[4][2] = {{0, 0}, {64, 0}, {64, 64}, {0, 64}};
    unsigned i;
    method(0x17FC, 8);
    method(0x194C, color);
    for (i = 0; i < 4; ++i) {
        method_float(0x1500, points[i][0]);
        method_float(0x1504, points[i][1]);
        method_float(0x1508, 1);
    }
    method(0x17FC, 0);
}

static void identity_matrix(void)
{
    unsigned i;
    for (i = 0; i < 16; ++i)
        method_float(0x0680 + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
    method(0x0314, 0);
}

static int save_frame(FILE *f, uint32_t color)
{
    flip();
    return fwrite(guest(color), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE;
}

typedef struct PackedVshVertex {
    int16_t position[4];
    uint8_t diffuse[4];
    uint32_t normal;
} PackedVshVertex;

static int scene_vsh_packed(const char *out_path, int short3, int ubyte4)
{
    static const int16_t positions[3][4] = {
        {-4, 4, 1, 1}, {24, 4, 1, 1}, {-4, 28, 1, 1}
    };
    static const int normal[3][3] = {
        {-1023, 511, -511}, {1023, -1023, 511}, {-1, 1, 0}
    };
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    PackedVshVertex *vertices;
    FILE *f;
    unsigned i;

    REQUIRE(sizeof(PackedVshVertex) == 16 && b && vb, "allocate packed VSH fixture");
    vertices = (PackedVshVertex *)guest(vb);
    for (i = 0; i < 3; ++i) {
        memcpy(vertices[i].position, positions[i], sizeof positions[i]);
        if (short3)
            vertices[i].position[3] = -123;
        vertices[i].diffuse[0] = 32;
        vertices[i].diffuse[1] = 128;
        vertices[i].diffuse[2] = 224;
        vertices[i].diffuse[3] = ubyte4 ? 255 : 0x5D;
        vertices[i].normal = ((uint32_t)normal[i][0] & 0x7FFu) |
                             (((uint32_t)normal[i][1] & 0x7FFu) << 11) |
                             (((uint32_t)normal[i][2] & 0x3FFu) << 22);
    }
    identity_matrix();
    load_program();
    surface(b, B_SIZE, 0);
    combiners(0);
    method(0x030C, 0);
    method(0x0358, 0x01010101);
    clear(0xF0, 0xFF000000u, 0);
    method(0x1720, vb - XBOX_CONTIG_BASE);
    method(0x1760, short3 ? 0x1035 : 0x1045);
    method(0x172C, vb + 8 - XBOX_CONTIG_BASE);
    method(0x176C, ubyte4 ? 0x1044 : 0x1034);
    method(0x1744, vb + 12 - XBOX_CONTIG_BASE);
    method(0x1784, 0x1016);
    method(0x17FC, 5);
    method(0x1810, 2u << 24);
    method(0x17FC, 0);
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open packed VSH output");
    REQUIRE(save_frame(f, b) && fclose(f) == 0, "write packed VSH output");
    nv2a_pb_exec_report();
    puts("packed VSH fixture drawn");
    return 1;
}

static void load_relative_program(void)
{
    const uint32_t arl[4] = {
        0, (13u << 21) | 0x1Bu | (9u << 9), 2u << 26, 0
    };
    const uint32_t color[4] = {
        0, (1u << 21) | (3u << 13) | 0x1Bu, 3u << 26,
        (0xFu << 12) | (1u << 11) | (3u << 3) | (1u << 1)
    };
    const uint32_t position[4] = {
        0, (1u << 21) | 0x1Bu, 2u << 26, (0xFu << 12) | (1u << 11) | 1u
    };
    nv2a_vsh_set_instruction(0, arl);
    nv2a_vsh_set_instruction(1, color);
    nv2a_vsh_set_instruction(2, position);
    method(0x1E94, 2);
}

static int scene_vsh_relative(const char *out_path)
{
    static const float offsets[8] = {-1.75f, -1.0f, 0.0f, 1.25f,
                                     2.0f, 188.0f, 189.0f, -4.0f};
    static const float constants[6][4] = {
        {1, 1, 1, 1}, {1, 0, 1, 1}, {1, 0, 0, 1},
        {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1}
    };
    static const float cyan[4] = {0, 1, 1, 1};
    static const uint32_t white[4] = {
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
    };
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    FILE *f;
    unsigned i;

    REQUIRE(b && vb, "allocate relative VSH fixture");
    identity_matrix();
    load_relative_program();
    for (i = 0; i < 6; ++i)
        nv2a_vsh_set_constant(i, constants[i]);
    nv2a_vsh_set_constant(191, cyan);
    surface(b, B_SIZE, 0);
    combiners(0);
    method(0x030C, 0);
    method(0x0358, 0x01010101);
    clear(0xF0, 0xFF000000u, 0);
    for (i = 0; i < 8; ++i) {
        float x = 2.0f + (float)(i % 4) * 16.0f;
        float y = 2.0f + (float)(i / 4) * 20.0f;
        Pos p[4] = {{x, y, 1, 1}, {x + 12, y, 1, 1},
                    {x + 12, y + 12, 1, 1}, {x, y + 12, 1, 1}};
        float uv[4][2] = {{offsets[i], 0}, {offsets[i], 0},
                          {offsets[i], 0}, {offsets[i], 0}};
        draw(8, p, white, uv, 4, vb);
    }
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open relative VSH output");
    REQUIRE(save_frame(f, b) && fclose(f) == 0, "write relative VSH output");
    nv2a_pb_exec_report();
    puts("relative VSH fixture drawn");
    return 1;
}

static int scene_vsh_culling(const char *out_path)
{
    static const struct { uint32_t enable, face, front; } cases[] = {
        {0, 0x405, 0x900}, {1, 0x405, 0x900}, {1, 0x405, 0x901},
        {1, 0x404, 0x900}, {1, 0x408, 0x900}, {2, 0x405, 0x900},
        {3, 0x405, 0x900}
    };
    static const Pos clockwise[3] = {{4, 4, 1, 1}, {24, 4, 1, 1}, {4, 24, 1, 1}};
    static const Pos counter[3] = {{36, 4, 1, 1}, {36, 24, 1, 1}, {56, 4, 1, 1}};
    static const Pos strip[4] = {{4, 36, 1, 1}, {24, 36, 1, 1},
                                  {4, 56, 1, 1}, {24, 56, 1, 1}};
    static const uint32_t red[4] = {0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u};
    static const uint32_t green[4] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
    static const uint32_t blue[4] = {0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu};
    static const float uv[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    FILE *f;
    unsigned i;

    REQUIRE(b && vb, "allocate VSH culling fixture");
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open VSH culling output");
    identity_matrix();
    load_program();
    surface(b, B_SIZE, 0);
    combiners(0);
    method(0x030C, 0);
    method(0x0358, 0x01010101);
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        method(0x039C, cases[i].face);
        method(0x03A0, cases[i].front);
        method(0x0308, cases[i].enable);
        clear(0xF0, 0xFF000000u, 0);
        draw(6, strip, blue, uv, 4, vb);
        draw(5, clockwise, red, uv, 3, vb);
        draw(5, counter, green, uv, 3, vb);
        REQUIRE(save_frame(f, b), "save VSH culling frame");
    }
    REQUIRE(fclose(f) == 0, "close VSH culling output");
    nv2a_pb_exec_report();
    puts("VSH culling fixture drawn");
    return 1;
}

static void sample_quad(float x, float y, float u, float v, uint32_t vb)
{
    const Pos p[4] = {{x, y, 0, 1}, {x + 10, y, 0, 1},
                      {x + 10, y + 10, 0, 1}, {x, y + 10, 0, 1}};
    static const uint32_t white[4] = {
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
    };
    const float uv[4][2] = {{u, v}, {u, v}, {u, v}, {u, v}};
    draw(8, p, white, uv, 4, vb);
}

static int scene_unsupported_cache(const char *out_path)
{
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t tex = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t *pixels;
    DWORD old_protect, ignored;
    unsigned i;
    FILE *f;

    REQUIRE(b && tex && vb, "allocate unsupported texture fixture");
    pixels = (uint32_t *)guest(tex);
    for (i = 0; i < B_SIZE * B_SIZE; ++i)
        pixels[i] = 0xFF00FF00u;
    identity_matrix();
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF000000u, 0);
    texture_linear_stage(0, tex, B_SIZE, B_SIZE, B_SIZE * 4, 0x2E);
    combiners(1);
    for (i = 0; i < 3; ++i)
        sample_quad(2, 2, 0.03f, 0.03f, vb);

    REQUIRE(VirtualProtect(guest(tex), B_SIZE * B_SIZE * 4, PAGE_NOACCESS, &old_protect),
            "protect unsupported texture");
    sample_quad(16, 2, 0.03f, 0.03f, vb);
    REQUIRE(VirtualProtect(guest(tex), B_SIZE * B_SIZE * 4, old_protect, &ignored),
            "restore unsupported texture");

    texture_linear(tex, B_SIZE);
    sample_quad(30, 2, 0.03f, 0.03f, vb);
    surface(tex, B_SIZE, 0);
    clear(0xF0, 0xFF0000FFu, 0);
    surface(b, B_SIZE, 0);
    texture_linear_stage(0, tex, B_SIZE, B_SIZE, B_SIZE * 4, 0x2E);
    sample_quad(44, 2, 0.03f, 0.03f, vb);
    flip();
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open unsupported texture output");
    REQUIRE(fwrite(guest(b), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE &&
            fclose(f) == 0, "write unsupported texture output");
    puts("unsupported texture cache fixture drawn");
    return 1;
}

static int scene_failed_upload_cache(const char *out_path)
{
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t good = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t bad = xbox_ContiguousAlloc(0x4000, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    unsigned i;
    FILE *f;

    REQUIRE(b && good && bad && vb, "allocate failed upload fixture");
    for (i = 0; i < B_SIZE * B_SIZE; ++i)
        ((uint32_t *)guest(good))[i] = 0xFF00FF00u;
    memset(guest(bad), 0, 0x4000);
    identity_matrix();
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF000000u, 0);
    texture_linear(good, B_SIZE);
    combiners(1);
    sample_quad(2, 2, 0.03f, 0.03f, vb);

    for (i = 0; i < 4; ++i) {
        texture_linear_stage(0, bad + (i / 2) * 4, 16, 8, 64, 0x12);
        method(0x1B04, 2u << 4 | 0x12u << 8 | 1u << 16 | 1u << 2);
        method(0x1E70, 3);
        sample_quad(2, 16, 0.03f, 0.03f, vb);
    }

    texture_linear(good, B_SIZE);
    combiners(1);
    sample_quad(16, 2, 0.03f, 0.03f, vb);
    flip();
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open failed upload output");
    REQUIRE(fwrite(guest(b), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE &&
            fclose(f) == 0, "write failed upload output");
    puts("failed upload cache fixture drawn");
    return 1;
}

static int scene_cache_rewrite(const char *out_path)
{
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t tex = xbox_ContiguousAlloc(64 * 64 * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t *pixels;
    unsigned i, generation = 1;
    FILE *f;

    REQUIRE(b && tex && vb, "allocate texture-cache fixture");
    pixels = (uint32_t *)guest(tex);
    for (i = 0; i < 64 * 64; ++i)
        pixels[i] = 0xFFFF0000u;
    identity_matrix();
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF000000u, 0);
    texture_linear(tex, 64);
    combiners(1);
    sample_quad(2, 2, 2.03f, 0.03f, vb);
    for (i = 0; i < 64 * 64; ++i)
        pixels[i] = 0xFF00FF00u;
    sample_quad(16, 2, 2.03f, 0.03f, vb);
    flip();
    generation++;

    /* Stabilise this entry and then alter a pixel outside all 128 sparse
     * eight-byte samples in the old cache. Avoid its periodic full check. */
    for (i = 0; i < 36; ++i) {
        sample_quad(2, 18, 2.03f, 0.03f, vb);
        flip();
        generation++;
    }
    if (((generation + (tex >> 12)) & 31u) == 0) {
        sample_quad(2, 18, 2.03f, 0.03f, vb);
        flip();
    }
    pixels[2] = 0xFF0000FFu;             /* bytes 8..11 are not sampled */
    sample_quad(16, 18, 2.03f, 0.03f, vb);
    flip();
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open texture-cache output");
    REQUIRE(fwrite(guest(b), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE &&
            fclose(f) == 0, "write texture-cache output");
    puts("texture-cache rewrites drawn");
    return 1;
}

static int scene_short_palettes(const char *out_path, int invalid_index,
                                int short_allocation, unsigned stage)
{
    static const uint32_t colors[4] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu,
                                       0xFFFFFF00u};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    unsigned code;
    FILE *f;

    REQUIRE(b && vb, "allocate P8 fixture surface");
    identity_matrix();
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF000000u, 0);
    for (code = 0; code < 4; ++code) {
        unsigned entries = 256u >> code;
        uint32_t tex = xbox_ContiguousAlloc(0x1000, 4096);
        uint32_t allocation = xbox_ContiguousAlloc(short_allocation ? 0x1000 : 0x2000, 4096);
        uint32_t palette;
        DWORD old_protect;
        uint32_t *table;

        REQUIRE(tex && allocation, "allocate P8 fixture texture and palette");
        if (short_allocation || invalid_index) {
            entries = 32;
            code = 3;
        }
        palette = allocation + 0x1000 - (short_allocation ? 64 : entries * 4);
        memset(guest(tex), invalid_index ? entries : entries - 1, 64);
        table = (uint32_t *)guest(palette);
        if (short_allocation)
            table[0] = 0xFFFF0000u;
        else {
            memset(table, 0, entries * 4);
            table[entries - 1] = colors[code];
            REQUIRE(VirtualProtect(guest(allocation + 0x1000), 0x1000,
                                   PAGE_NOACCESS, &old_protect), "guard P8 palette end");
        }
        texture_palette_stage(stage, tex, palette, code);
        combiners_stage(stage);
        sample_quad((float)(2 + code * 14), 2, 0.03f, 0.03f, vb);
        if (short_allocation || invalid_index)
            break;
    }
    flip();
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open P8 output");
    REQUIRE(fwrite(guest(b), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE &&
            fclose(f) == 0, "write P8 output");
    puts("short palettes drawn");
    return 1;
}

static int scene_p8_rewrite(const char *out_path, int invalid_index, int shrink,
                            unsigned stage)
{
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t tex = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t palette = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t *table;
    FILE *f;

    REQUIRE(b && vb && tex && palette, "allocate P8 rewrite fixture");
    memset(guest(tex), shrink ? 32 : 0, 64);
    table = (uint32_t *)guest(palette);
    memset(table, 0, 256 * 4);
    table[0] = 0xFFFF0000u;
    table[1] = 0xFF00FF00u;
    table[32] = 0xFF0000FFu;
    identity_matrix();
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF000000u, 0);
    texture_palette_stage(stage, tex, palette, shrink ? 0u : 3u);
    combiners_stage(stage);
    sample_quad(2, 2, 0.03f, 0.03f, vb);
    if (shrink)
        method(0x1B20 + stage * 0x40, (palette - XBOX_CONTIG_BASE) | 3u << 2);
    else
        memset(guest(tex), invalid_index ? 32 : 1, 64);
    sample_quad(16, 2, 0.03f, 0.03f, vb);
    if (!invalid_index && !shrink) {
        table[1] = 0xFF0000FFu;
        sample_quad(30, 2, 0.03f, 0.03f, vb);
    }
    flip();
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open P8 rewrite output");
    REQUIRE(fwrite(guest(b), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE &&
            fclose(f) == 0, "write P8 rewrite output");
    puts("P8 rewrites drawn");
    return 1;
}

static int scene_p8_reprotect(int protect_palette, unsigned stage)
{
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t tex = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t palette = xbox_ContiguousAlloc(0x1000, 4096);
    DWORD old_protect;

    REQUIRE(b && vb && tex && palette, "allocate P8 reprotection fixture");
    memset(guest(tex), 0, 64);
    ((uint32_t *)guest(palette))[0] = 0xFFFF0000u;
    identity_matrix();
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF000000u, 0);
    texture_palette_stage(stage, tex, palette, 3);
    combiners_stage(stage);
    sample_quad(2, 2, 0.03f, 0.03f, vb);
    REQUIRE(VirtualProtect(guest(protect_palette ? palette : tex), 0x1000,
                           PAGE_NOACCESS, &old_protect), "protect cached P8 range");
    sample_quad(16, 2, 0.03f, 0.03f, vb);
    return 0;
}

static int scene_p8_bad_range(const char *mode)
{
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    int bad_texture = strstr(mode, "texture") != NULL;
    int short_texture = strstr(mode, "short-texture") != NULL;
    uint32_t tex_alloc = xbox_ContiguousAlloc(short_texture ? 0x1000 : 0x2000, 4096);
    uint32_t pal_alloc = xbox_ContiguousAlloc(0x2000, 4096);
    uint32_t tex, palette;
    DWORD old_protect;

    REQUIRE(b && vb && tex_alloc && pal_alloc, "allocate invalid P8 range fixture");
    tex = bad_texture ? tex_alloc + 0x1000 - 128 : tex_alloc;
    palette = bad_texture ? pal_alloc : pal_alloc + 0x1000 - 64;
    memset(guest(tex), 0, bad_texture ? 128 : 64);
    *(uint32_t *)guest(palette) = 0xFFFF0000u;
    if (!short_texture) {
        uint32_t guard = bad_texture ? tex_alloc + 0x1000 : pal_alloc + 0x1000;
        REQUIRE(VirtualProtect(guest(guard), 0x1000, PAGE_NOACCESS, &old_protect),
                "guard invalid P8 range");
    }
    identity_matrix();
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF000000u, 0);
    texture_palette_stage(0, tex, palette, 3);
    if (bad_texture)
        method(0x1B04, 2u << 4 | 0x0Bu << 8 | 1u << 16 | 4u << 20 | 4u << 24);
    combiners_stage(0);
    sample_quad(2, 2, 0.03f, 0.03f, vb);
    return 0;
}

static uint32_t bounds(unsigned first, unsigned last)
{
    return first | last << 16;
}

static void clear_rect(unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    method(0x1D98, bounds(x0, x1));
    method(0x1D9C, bounds(y0, y1));
}

static int scene_clear_rect(const char *out_path, int program, int vertical_first)
{
    static const Pos near_quad[4] = {{0, 0, 1000, 1}, {64, 0, 1000, 1},
                                     {64, 64, 1000, 1}, {0, 64, 1000, 1}};
    static const Pos mid_quad[4] = {{0, 0, 8388608.0f, 1}, {64, 0, 8388608.0f, 1},
                                    {64, 64, 8388608.0f, 1}, {0, 64, 8388608.0f, 1}};
    static const Pos z16_quad[4] = {{0, 0, 32768, 1}, {64, 0, 32768, 1},
                                    {64, 64, 32768, 1}, {0, 64, 32768, 1}};
    static const uint32_t green[4] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
    static const float uv[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    FILE *f = fopen(out_path, "wb");

    REQUIRE(b && z && vb && f, "initialize rectangular clear fixture");
    identity_matrix();
    if (program)
        load_program();
    surface(b, B_SIZE, z);
    combiners(0);

    method(vertical_first ? 0x1D9C : 0x1D98, bounds(10, 19));
    clear(0xF3, 0xAA112233u, 0xFFFFFF00u);
    REQUIRE(save_frame(f, b), "save incomplete-clear-rectangle frame");

    clear_rect(0, 0, 63, 63);
    clear(0xF3, 0xFF2674A1u, 0xFFFFFF00u);
    clear_rect(0, 0, 63, 7);
    clear(0xF3, 0xFF000000u, 0);
    clear_rect(0, 56, 63, 63);
    clear(0xF3, 0xFF000000u, 0);
    method(0x030C, 1);
    method(0x0354, 0x201);
    method(0x035C, 0);
    draw(8, near_quad, green, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save depth-tested letterbox frame");

    clear_rect(0, 0, 63, 63);
    clear(0xF3, 0x7F102030u, 0xFFFFFF00u);
    clear_rect(10, 20, 19, 29);
    clear(0x10, 0xFFCCBBAAu, 0);
    clear(0x40, 0xFFFFEE77u, 0);
    clear(0x80, 0x66123456u, 0);
    clear_rect(20, 20, 29, 29);
    clear(0x20, 0xFF004400u, 0);
    REQUIRE(save_frame(f, b), "save per-channel clear frame");

    clear_rect(0, 0, 63, 63);
    clear(0x10, 0xFF551122u, 0);
    REQUIRE(save_frame(f, b), "save full-surface masked clear frame");

    clear(0xF3, 0xFF101010u, 0xFFFFFF7Bu);
    clear_rect(60, 60, 80, 80);
    clear(0xF0, 0xFFF0A000u, 0);
    clear_rect(20, 0, 18, 63);
    clear(0xF3, 0, 0);
    clear_rect(0, 30, 63, 28);
    clear(0xF3, 0, 0);
    clear_rect(65535, 0, 65535, 63);
    clear(0xF3, 0, 0);
    REQUIRE(save_frame(f, b), "save clipped and empty clear frame");

    method(0x032C, 1);
    method(0x0364, 0x202);
    method(0x0368, 0x7B);
    method(0x036C, 0xFF);
    method(0x0360, 0);
    draw(8, near_quad, green, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save empty-clear depth and stencil frame");

    clear_rect(0, 0, 63, 63);
    clear(0xF3, 0xFF000000u, 0xFFFFFF55u);
    clear_rect(16, 16, 31, 31);
    clear(1, 0xFFFFFFFFu, 0x40000099u);
    method(0x0368, 0x55);
    draw(8, mid_quad, green, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save partial-depth frame");

    clear_rect(0, 0, 63, 63);
    clear(0xF0, 0xFF000000u, 0);
    method(0x030C, 0);
    draw(8, near_quad, green, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save depth-clear-preserved-stencil frame");

    clear(0xF3, 0xFF000000u, 0xFFFFFF44u);
    clear_rect(16, 16, 31, 31);
    clear(2, 0xFFFFFFFFu, 0x00000033u);
    method(0x030C, 1);
    method(0x0368, 0x33);
    draw(8, mid_quad, green, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save partial-stencil frame");

    clear_rect(0, 0, 63, 63);
    clear(0xF0, 0xFF000000u, 0);
    method(0x0368, 0x44);
    draw(8, mid_quad, green, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save stencil outside clear frame");
    method(0x032C, 0);
    method(0x030C, 0);

    clear(0xF0, 0xFF223344u, 0);
    method(0x0200, 32u << 16);
    method(0x0204, 32u << 16);
    method(0x0208, 0x2128);
    clear_rect(3, 5, 6, 7);
    clear(0xF0, 0xFFEEEEEEu, 0);
    surface(b, B_SIZE, z);
    REQUIRE(save_frame(f, b), "save two-axis AA clear frame");

    clear_rect(0, 0, 63, 63);
    clear(0xF0, 0xFF223344u, 0);
    method(0x0200, 32u << 16);
    method(0x0204, 64u << 16);
    method(0x0208, 0x1128);
    clear_rect(3, 5, 6, 7);
    clear(0xF0, 0xFFEEEEEEu, 0);
    surface(b, B_SIZE, z);
    REQUIRE(save_frame(f, b), "save horizontal AA clear frame");

    clear_rect(0, 0, 63, 63);
    clear(0xF0, 0xFF334455u, 0);
    method(0x0200, 32u << 16);
    method(0x0204, 32u << 16);
    clear_rect(40, 40, 47, 47);
    clear(0xF0, 0xFF0000FFu, 0);
    surface(b, B_SIZE, z);
    REQUIRE(save_frame(f, b), "save clear beyond draw clip frame");

    method(0x0208, 0x118);
    clear_rect(0, 0, 63, 63);
    clear(0xF1, 0xFF000000u, 0xFFFFu);
    clear_rect(16, 16, 31, 31);
    clear(1, 0, 0x4000u);
    method(0x030C, 1);
    draw(8, z16_quad, green, uv, 4, vb);
    surface(b, B_SIZE, z);
    REQUIRE(save_frame(f, b), "save partial Z16 clear frame");
    method(0x030C, 0);

    method(0x02B4, 0);
    method(0x02C0, bounds(0, 3));
    method(0x02E0, bounds(0, 3));
    clear_rect(0, 0, 63, 63);
    clear(0xF0, 0xFF000000u, 0);
    clear_rect(10, 10, 19, 19);
    clear(0xF0, 0xFFFF0000u, 0);
    draw(8, near_quad, green, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save clear and extended window clip frame");

    clear_rect(0, 0, 63, 63);
    clear(0xF0, 0xFF000000u, 0);
    clear_rect(40, 40, 47, 47);
    clear(0xF0, 0xFF0000FFu, 0);
    screen_quad(0xFF0000FFu);
    REQUIRE(save_frame(f, b), "save clear and base window clip frame");

    REQUIRE(fclose(f) == 0, "close rectangular clear frames");
    puts("rectangular clear frames 16");
    return 1;
}

static int scene_window_clip(const char *out_path, int program)
{
    static const Pos p[4] = {{0, 0, 1, 1}, {64, 0, 1, 1},
                             {64, 64, 1, 1}, {0, 64, 1, 1}};
    static const uint32_t white[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    static const uint32_t green[4] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
    static const float uv[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    FILE *f = fopen(out_path, "wb");
    unsigned i;

    REQUIRE(b && vb && f, "initialize clipping fixture");
    identity_matrix();
    if (program)
        load_program();
    surface(b, B_SIZE, 0);
    combiners(0);

    method(0x02C0, bounds(4, 11));                   /* one axis is not enough */
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save incomplete-axis frame");

    method(0x02E0, bounds(4, 11));
    clear(0xF0, 0xFF112233u, 0);                   /* clears ignore window clipping */
    REQUIRE(save_frame(f, b), "save clear frame");

    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save base inclusive frame");

    method(0x02C4, bounds(20, 27));
    method(0x02E4, bounds(4, 11));
    clear(0xF0, 0xFF000000u, 0);
    draw(8, p, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save extended union frame");

    method(0x02B4, 1);
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save base exclusive frame");

    method(0x02B4, 0);
    method(0x02C0, bounds(4, 11));
    method(0x02E0, bounds(4, 11));
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFF0000FFu);                       /* immediate colour is ABGR */
    method(0x02C0, bounds(20, 27));
    draw(8, p, green, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save changed-state frame");

    method(0x0200, 32u << 16);
    method(0x0204, 64u << 16);
    method(0x0208, 0x1128);                         /* horizontal 2x AA */
    method(0x020C, B_SIZE * 4);
    method(0x02C0, bounds(4, 7));
    method(0x02E0, bounds(5, 7));
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save horizontal AA frame");

    method(0x0204, 32u << 16);
    method(0x0208, 0x2128);                         /* horizontal and vertical 2x AA */
    clear(0xF0, 0xFF000000u, 0);
    draw(8, p, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save two-axis AA frame");

    surface(b, B_SIZE, 0);
    for (i = 0; i < 8; ++i) {
        method(0x02C0 + i * 4, bounds(2 + 7 * i, 4 + 7 * i));
        method(0x02E0 + i * 4, bounds(20, 22));
    }
    clear(0xF0, 0xFF000000u, 0);
    draw(8, p, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save eight-rectangle frame");
    method(0x02B4, 1);
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save eight-rectangle exclusive frame");

    method(0x02B4, 0);
    method(0x02C0, bounds(30, 29));
    method(0x02E0, bounds(0, 63));
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save empty inclusive frame");
    method(0x02B4, 1);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, p, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save empty exclusive frame");
    REQUIRE(fclose(f) == 0, "close clipping frames");
    puts("window clip frames 12");
    return 1;
}

static void texture_depth(uint32_t va, unsigned width, unsigned height, unsigned pitch)
{
    texture_linear_stage(0, va, width, height, pitch, 0x2E);
}

static void texture_packed_depth(uint32_t va, unsigned width, unsigned height, unsigned pitch)
{
    texture_linear_stage(0, va, width, height, pitch, 0x41);
}

static int scene_hud_near(const char *out_path)
{
    static const Pos glow[4] = {{0, 0, 0, 240}, {64, 0, 0, 240},
                                {64, 64, 0, 240}, {0, 64, 0, 240}};
    static const Pos hud[4] = {{4, 4, 0, 120}, {20, 4, 0, 120},
                               {20, 20, 0, 120}, {4, 20, 0, 120}};
    static const Pos positive[4] = {{44, 4, 1, 1}, {60, 4, 1, 1},
                                    {60, 20, 1, 1}, {44, 20, 1, 1}};
    static const Pos negative[4] = {{44, 44, -0.25f, 1}, {60, 44, -0.25f, 1},
                                    {60, 60, -0.25f, 1}, {44, 60, -0.25f, 1}};
    static const uint32_t gray[4] = {0xFF404040u, 0xFF404040u, 0xFF404040u, 0xFF404040u};
    static const uint32_t red[4] = {0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u};
    static const uint32_t green[4] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
    static const uint32_t blue[4] = {0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu};
    static const float uv[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    static const float offsets[2] = {0.5f, -0.5f};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    FILE *f = fopen(out_path, "wb");
    unsigned i;

    REQUIRE(b && vb && f, "initialize HUD and glow near-plane fixture");
    identity_matrix();
    load_program();
    surface(b, B_SIZE, 0);
    combiners(0);
    method(0x030C, 0);
    method(0x0358, 0x01010101);
    for (i = 0; i < 2; i++) {
        method_float(0x0A28, offsets[i]);
        clear(0xF0, 0xFF606060u, 0);
        method(0x0304, 1);
        method(0x0344, 1);
        method(0x0348, 1);
        draw(8, glow, gray, uv, 4, vb);
        method(0x0304, 0);
        draw(8, hud, red, uv, 4, vb);
        draw(8, positive, green, uv, 4, vb);
        draw(8, negative, blue, uv, 4, vb);
        REQUIRE(save_frame(f, b), "save HUD and glow near-plane frame");
    }
    REQUIRE(fclose(f) == 0, "close HUD and glow near-plane frames");
    puts("HUD and glow near-plane frames 2");
    return 1;
}

static void glare_combiners(void)
{
    static const uint32_t color_icw[5] = {
        0xC1C80000u, 0xD9209D40u, 0xC820C020u, 0xCCD80000u, 0xCC200000u
    };
    static const uint32_t alpha_icw[5] = {
        0, 0xD9309D50u, 0xD830D030u, 0xDCD81010u, 0
    };
    static const uint32_t color_ocw[5] = {
        0x000820D0u, 0x00000C00u, 0x00004C00u, 0x000000C0u, 0x000820D0u
    };
    static const uint32_t alpha_ocw[5] = {
        0, 0x00000C00u, 0x00004C00u, 0x000000C0u, 0
    };
    unsigned i;

    for (i = 0; i < 5; i++) {
        method(0x0AC0 + i * 4, color_icw[i]);
        method(0x0260 + i * 4, alpha_icw[i]);
        method(0x1E40 + i * 4, color_ocw[i]);
        method(0x0AA0 + i * 4, alpha_ocw[i]);
        method(0x0A60 + i * 4, i == 0 ? 0x004C961Du : 0);
        method(0x0A80 + i * 4, 0);
    }
    method(0x0288, 0x0C040000u);
    method(0x028C, 0x00001D80u);
    method(0x1E60, 0x00011105u);
    method(0x1E70, 0x00000021u);
}

static int scene_glare_threshold(const char *out_path, int program)
{
    static const struct { uint32_t argb; uint8_t stencil; } cases[] = {
        {0xFF202020u, 0xC0}, {0xFFA0A0A0u, 0xC0}, {0xFFA0A0A0u, 0x40},
        {0xFFE0E0E0u, 0xC0}, {0xFFA0A0A0u, 0xA1}, {0xFFA0A0A0u, 0x9F},
        {0xFFE02010u, 0x60}, {0xFFE02010u, 0x50}, {0x80A0A0A0u, 0x40}
    };
    static const Pos full[4] = {{0, 0, 0, 1}, {64, 0, 0, 1},
                                 {64, 64, 0, 1}, {0, 64, 0, 1}};
    static const uint32_t white[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    static const float uv[4][2] = {{0, 0}, {64, 0}, {64, 64}, {0, 64}};
    uint32_t scene = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t bright = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    FILE *f = fopen(out_path, "wb");
    unsigned i;

    REQUIRE(scene && bright && z && vb && f, "initialize glare threshold fixture");
    memset(guest(z), 0, B_SIZE * B_SIZE * 4);
    identity_matrix();
    if (program)
        load_program();
    method(0x030C, 0);
    method(0x035C, 0);
    method(0x0358, 0x01010101);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        surface(scene, B_SIZE, z);
        clear(0xF3, cases[i].argb, 0xFFFFFF00u | cases[i].stencil);
        REQUIRE(((const uint32_t *)guest(z))[0] == 0,
                "fixture requires stale zero guest depth and stencil");

        surface(bright, B_SIZE, 0);
        clear(0xF0, 0xFF000000u, 0);
        texture_linear(scene, B_SIZE);
        texture_linear_stage(1, z, B_SIZE, B_SIZE, B_SIZE * 4, 0x41);
        glare_combiners();
        method(0x0304, 1);
        method(0x0344, 1);
        method(0x0348, 0);
        draw(8, full, white, uv, 4, vb);
        REQUIRE(save_frame(f, bright), "save glare bright-pass frame");

        surface(scene, B_SIZE, z);
        texture_linear(bright, B_SIZE);
        combiners(1);
        method(0x0304, 1);
        method(0x0344, 1);
        method(0x0348, 1);
        draw(8, full, white, uv, 4, vb);
        REQUIRE(save_frame(f, scene), "save glare composite frame");
    }
    REQUIRE(fclose(f) == 0, "close glare threshold frames");
    puts("glare threshold frames 18");
    return 1;
}

static int scene_stencil_texture(const char *out_path, int program)
{
    static const Pos full[4] = {{0, 0, 0, 1}, {64, 0, 0, 1},
                                 {64, 64, 0, 1}, {0, 64, 0, 1}};
    static const Pos left[4] = {{0, 0, 0, 1}, {32, 0, 0, 1},
                                 {32, 64, 0, 1}, {0, 64, 0, 1}};
    static const uint32_t white[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    static const float uv[4][2] = {{0, 0}, {64, 0}, {64, 64}, {0, 64}};
    static const float middle[4][2] = {{32, 32}, {32, 32}, {32, 32}, {32, 32}};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t other_z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t growth_z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t small_b = xbox_ContiguousAlloc(A_SIZE * A_SIZE * 4, 4096);
    uint32_t ordinary = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    FILE *f = fopen(out_path, "wb");
    unsigned x, y;

    REQUIRE(b && z && other_z && growth_z && small_b && ordinary && vb && f,
            "initialize packed depth fixture");
    for (y = 0; y < B_SIZE; ++y)
        for (x = 0; x < B_SIZE; ++x) {
            ((uint32_t *)guest(z))[y * B_SIZE + x] =
                x < B_SIZE / 2 ? 0x12345600u : 0x89ABCD5Fu;
            ((uint32_t *)guest(ordinary))[y * B_SIZE + x] = 0x11223344u;
        }
    identity_matrix();
    if (program)
        load_program();
    method(0x030C, 0);
    method(0x035C, 0);
    surface(b, B_SIZE, z);
    texture_packed_depth(z, B_SIZE, B_SIZE, B_SIZE * 4);
    combiners(1);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save guest-seeded packed depth frame");

    clear(3, 0, 0xC040807Bu);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save GPU-cleared packed depth frame");

    clear_rect(0, 0, 31, 63);
    clear(2, 0, 0x0000005Fu);
    clear_rect(0, 0, 63, 63);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save partial stencil clear frame");

    surface(b, B_SIZE, other_z);
    clear(0xF3, 0xFF000000u, 0x444444FFu);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save unbound packed depth frame");
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save reused packed depth frame");

    surface(b, B_SIZE, z);
    combiners(0);
    method(0x032C, 1);
    method(0x0360, 0xFF);
    method(0x0364, 0x207);
    method(0x0368, 0x33);
    method(0x036C, 0xFF);
    method(0x0370, 0x1E00);
    method(0x0374, 0x1E00);
    method(0x0378, 0x1E01);
    draw(8, left, white, uv, 4, vb);
    method(0x032C, 0);
    surface(b, B_SIZE, other_z);
    combiners(1);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save GPU-written stencil frame");

    method(0x1B14, 2u << 24 | 2u << 16);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, middle, 4, vb);
    REQUIRE(save_frame(f, b), "save filtered packed depth frame");

    texture_packed_depth(ordinary, B_SIZE, B_SIZE, B_SIZE * 4);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save ordinary 0x41 texture frame");

    texture_packed_depth(z + 4, B_SIZE, B_SIZE, B_SIZE * 4);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save overlapping packed depth rejection frame");
    texture_packed_depth(z, B_SIZE, B_SIZE, B_SIZE * 4 + 4);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save packed depth pitch rejection frame");

    surface(small_b, A_SIZE, growth_z);
    clear(0xF3, 0xFF000000u, 0xAABBCCDDu);
    surface(b, B_SIZE, growth_z);
    clear(1, 0, 0x44556600u);
    texture_packed_depth(growth_z, B_SIZE, B_SIZE, B_SIZE * 4);
    clear(0xF0, 0xFF000000u, 0);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save uninitialized stencil rejection frame");
    clear(2, 0, 0x000000FFu);
    draw(8, full, white, uv, 4, vb);
    REQUIRE(save_frame(f, b), "save separately initialized depth and stencil frame");

    if (program)
        method(0x1E94, 0);
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save base packed depth frame");
    REQUIRE(fclose(f) == 0, "close packed depth frames");
    puts("packed depth frames 13");
    return 1;
}

static int scene_depth_texture(const char *out_path, int program)
{
    static const uint32_t depths[3] = {0xC0000000u, 0x40000000u, 0x80000000u};
    static const Pos p[4] = {{0, 0, 0, 1}, {64, 0, 0, 1},
                             {64, 64, 0, 1}, {0, 64, 0, 1}};
    static const uint32_t white[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    static const float uv[4][4] = {{0, 0, 8388608.0f, 1}, {64, 0, 8388608.0f, 1},
                                   {64, 64, 8388608.0f, 1}, {0, 64, 8388608.0f, 1}};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t other_z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t cold_z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t growth_z = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t small_b = xbox_ContiguousAlloc(A_SIZE * A_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    FILE *f = fopen(out_path, "wb");
    unsigned func, depth, x, y;

    REQUIRE(b && z && other_z && cold_z && growth_z && small_b && vb && f,
            "initialize depth texture fixture");
    identity_matrix();
    if (program)
        load_program();
    surface(b, B_SIZE, z);
    texture_depth(z, B_SIZE, B_SIZE, B_SIZE * 4);
    combiners(1);
    method(0x1E70, 2);                               /* PROJECT3D */
    method(0x1E6C, 4);                               /* GREATER */
    method(0x030C, 1);
    method(0x0354, 0x207);                           /* ALWAYS: draw writes depth 0 */
    method(0x035C, 1);
    clear(0xF3, 0xFF000000u, 0xC0000000u);         /* depth 0.75 */
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save active-depth frame");

    method(0x030C, 0);
    method(0x035C, 0);
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save updated-depth frame");

    for (depth = 0; depth < 3; ++depth) {
        for (func = 0; func < 8; ++func) {
            clear(0xF3, 0xFF000000u, depths[depth]);
            method(0x1E6C, func);
            draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
            REQUIRE(save_frame(f, b), "save shadow comparison frame");
        }
    }

    method(0x1E70, 1);                               /* PROJECT2D compares against zero */
    method(0x1E6C, 4);
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save base depth frame");

    method(0x1E6C, 1);                               /* LESS than zero is false */
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save base depth false frame");
    method(0x1E6C, 2);                               /* zero equals zero */
    clear(0xF3, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save base depth equal frame");

    clear(0xF3, 0xFF000000u, 0xC0000000u);
    surface(b, B_SIZE, other_z);                      /* sample a non-bound depth surface */
    clear(0xF3, 0xFF000000u, 0xC0000000u);
    method(0x1E70, 2);
    method(0x1E6C, 4);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save unbound-depth frame");

    texture_depth(z, B_SIZE, B_SIZE, B_SIZE * 4 + 4);
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save incompatible-pitch frame");

    texture_depth(z, B_SIZE, B_SIZE - 1, B_SIZE * 4);
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save incompatible-size frame");

    texture_depth(z, B_SIZE, B_SIZE, B_SIZE * 4 + 4);
    method(0x1E70, 1);
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save base incompatible-pitch frame");

    texture_depth(z, B_SIZE, B_SIZE, B_SIZE * 4);
    method(0x1E70, 3);                               /* unsupported CUBEMAP stage */
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save unsupported-depth-mode frame");

    texture_depth(z + 4, B_SIZE, B_SIZE, B_SIZE * 4);
    method(0x1E70, 2);
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save overlapping-depth-alias frame");

    texture_depth(z, B_SIZE, B_SIZE, B_SIZE * 4);
    method(0x1B04, 2u << 4 | 0x2Eu << 8 | 2u << 16);
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save depth-mipmap frame");

    method(0x1B04, 2u << 4 | 0x2Eu << 8 | 1u << 16 | 1u << 2);
    method(0x1E70, 1);
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save extended-depth-cube frame");
    if (program)
        method(0x1E94, 0);                           /* force the screen-space base path */
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save base-depth-cube frame");

    texture_depth(z, B_SIZE, B_SIZE, B_SIZE * 4);
    method(0x1E70, 2);
    clear(0xF0, 0xFF000000u, 0);
    screen_quad(0xFFFFFFFFu);
    REQUIRE(save_frame(f, b), "save unsupported-base-project3d frame");
    if (program)
        method(0x1E94, 2);

    surface(b, B_SIZE, z);
    method(0x0208, 0x118);                           /* Z16 cannot alias D24 texture */
    texture_depth(z, B_SIZE, B_SIZE, B_SIZE * 4);
    method(0x1E70, 2);
    clear(0xF3, 0xFF000000u, 0xC0000000u);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save incompatible-format frame");

    for (y = 0; y < B_SIZE; ++y)
        for (x = 0; x < B_SIZE; ++x)
            ((uint32_t *)guest(cold_z))[y * B_SIZE + x] =
                x < B_SIZE / 2 ? 0x4000005Au : 0xC00000A5u;
    surface(b, B_SIZE, cold_z);
    texture_depth(cold_z, B_SIZE, B_SIZE, B_SIZE * 4);
    method(0x1E6C, 4);
    clear(0xF0, 0xFF000000u, 0);                   /* seed from guest; no depth clear */
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save guest-seeded depth frame");

    combiners(0);
    method(0x032C, 1);                               /* stencil EQUAL 0x5A */
    method(0x0364, 0x202);
    method(0x0368, 0x5A);
    method(0x036C, 0xFF);
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save guest-seeded stencil frame");
    method(0x032C, 0);

    surface(small_b, A_SIZE, growth_z);
    clear(0xF3, 0xFF000000u, 0xC0000000u);
    surface(b, B_SIZE, growth_z);
    texture_depth(growth_z, B_SIZE, B_SIZE, B_SIZE * 4);
    combiners(1);
    method(0x1E70, 2);
    method(0x1E6C, 4);
    clear_rect(0, 0, 31, 31);
    clear(1, 0, 0xC0000000u);
    clear_rect(0, 0, 63, 63);
    clear(0xF0, 0xFF000000u, 0);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save partially-cleared resized-depth rejection frame");

    clear(0xF3, 0xFF000000u, 0xC0000000u);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save fully-cleared resized-depth frame");

    surface(b, B_SIZE, growth_z);
    method(0x0208, 0x228);                           /* swizzled surface is not a linear alias */
    clear(0xF3, 0xFF000000u, 0xC0000000u);
    draw_with_texcoords(8, p, white, &uv[0][0], 4, 4, vb);
    REQUIRE(save_frame(f, b), "save swizzled-depth rejection frame");
    REQUIRE(fclose(f) == 0, "close depth texture frames");
    puts("depth texture frames 45");
    return 1;
}

static int test_surface_guard(const char *report, int invalid)
{
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    MEMORY_BASIC_INFORMATION first, excluded, last;
    char page[32];

    REQUIRE(b != 0, "allocate guarded surface");
    snprintf(page, sizeof page, "0x%08X", b + 4096);
    REQUIRE(_putenv_s("BRUTEFORCE_SURFACE_GUARD", report) == 0 &&
            _putenv_s("BRUTEFORCE_SURFACE_GUARD_EXCLUDE_PAGE", invalid ? "not-a-page" : page) == 0,
            "configure surface guard");
    if (invalid)
        return bruteforce_null_backend_install() == 0;
    REQUIRE(bruteforce_null_backend_install(), "install surface guard");
    surface(b, B_SIZE, 0);
    clear(0xF0, 0, 0);
    flip();
    REQUIRE(VirtualQuery(guest(b), &first, sizeof first) == sizeof first &&
            VirtualQuery(guest(b + 4096), &excluded, sizeof excluded) == sizeof excluded &&
            VirtualQuery(guest(b + 8192), &last, sizeof last) == sizeof last,
            "query guarded surface pages");
    REQUIRE((first.Protect & PAGE_NOACCESS) && !(excluded.Protect & PAGE_NOACCESS) &&
            (last.Protect & PAGE_NOACCESS), "exclude only the requested page");
    *(volatile uint32_t *)guest(b) = 0x12345678u;
    *(volatile uint32_t *)guest(b + 4096) = 0xABCDEF01u;
    Sleep(2100);
    flip();
    puts("surface guard exclusion passed");
    return 1;
}

static int scene(const char *out_path, int program, int gpu)
{
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t bz = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t a = xbox_ContiguousAlloc(A_SIZE * A_SIZE * 4, 4096);
    uint32_t tex = xbox_ContiguousAlloc(8 * 8 * 4, 4096);
    uint32_t p8 = xbox_ContiguousAlloc(8 * 8, 4096);
    uint32_t palette = xbox_ContiguousAlloc(256 * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t report = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t *texels;
    unsigned i;
    FILE *f;

    REQUIRE(b && bz && a && tex && p8 && palette && vb && report, "allocate guest memory");
    texels = (uint32_t *)guest(tex);
    for (i = 0; i < 64; ++i)                         /* swizzled order: any pattern */
        texels[i] = (i & 1) ? 0xFFF08020u : (i & 2) ? 0xFF20C040u : 0xFF4060E0u;
    for (i = 0; i < 16; ++i)
        method_float(0x0680 + i * 4, i % 5 == 0 ? 1.0f : 0.0f);   /* composite = I */
    method(0x0314, 0);                               /* no lighting */
    if (program)
        load_program();

    /* Render to texture: A is red with a green triangle. */
    surface(a, A_SIZE, 0);
    clear(0xF0, 0xFFFF0000u, 0);
    combiners(0);
    {
        static const Pos p[3] = {{2, 2, 0, 1}, {30, 4, 0, 1}, {6, 28, 0, 1}};
        static const uint32_t c[3] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00C000u};
        static const float uv[3][2] = {{0, 0}, {0, 0}, {0, 0}};
        draw(5, p, c, uv, 3, vb);
    }

    /* B: cleared, depth on. */
    surface(b, B_SIZE, bz);
    clear(0xF3, 0xFF102040u, 0xFFFFFF00u);
    method(0x030C, 1);                               /* depth test */
    method(0x0354, 0x201);                           /* LESS */
    method(0x035C, 1);                               /* depth write */

    /* A textured quad with interpolated colours, counted by a visibility
     * test: 36 x 36 pixels pass the depth test. */
    texture_swizzled8(tex);
    combiners(1);
    method(0x17C8, 0);                               /* CLEAR_REPORT_VALUE */
    method(0x17CC, 1);                               /* count */
    {
        static const Pos p[4] = {{4, 4, 1000, 1}, {40, 4, 1000, 1}, {40, 40, 1000, 1},
                                 {4, 40, 1000, 1}};
        static const uint32_t c[4] = {0xFFFFFFFFu, 0xFFFF8080u, 0xFF80FF80u, 0xFF8080FFu};
        static const float uv[4][2] = {{0.03f, 0.03f}, {2.03f, 0.03f}, {2.03f, 2.03f},
                                       {0.03f, 2.03f}};
        draw(8, p, c, uv, 4, vb);
    }
    method(0x17CC, 0);
    ((uint32_t *)guest(report))[3] = 0xFFFFFFFFu;    /* incomplete, as D3D leaves it */
    method(0x17D0, report - XBOX_CONTIG_BASE);       /* GET_REPORT */
    for (i = 0; i < 100000 && ((volatile uint32_t *)guest(report))[3] != 0; ++i) {
        nv2a_pb_exec_poll();                         /* asynchronous reports */
        Sleep(0);
    }
    printf("zpass %u\n", ((uint32_t *)guest(report))[2]);
    REQUIRE(((volatile uint32_t *)guest(report))[3] == 0, "visibility report completed");

    /* Queue reports across active counting, clears, empty intervals, and more
     * intervals than the query pool can hold without waiting. */
    {
        static const Pos p[4] = {{40, 40, 0, 1}, {44, 40, 0, 1},
                                 {44, 44, 0, 1}, {40, 44, 0, 1}};
        static const uint32_t c[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        static const float uv[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
        static const uint32_t cpu_counts[6] = {20, 40, 20, 20, 0, 1400};
        static const uint32_t gpu_counts[6] = {16, 32, 16, 16, 0, 1120};
        const uint32_t *expected = gpu ? gpu_counts : cpu_counts;
        unsigned j;
        int complete;
        method(0x030C, 0);
        combiners(0);
        method(0x17C8, 0);
        method(0x17CC, 1);
        draw(8, p, c, uv, 4, vb);
        if (gpu) {
            clear_rect(8, 8, 15, 15);
            clear(2, 0, 0x5A);
            clear_rect(0, 0, 63, 63);
            clear(2, 0, 0x11);
        }
        report_at(report, 1);
        draw(8, p, c, uv, 4, vb);
        report_at(report, 2);
        method(0x17C8, 0);
        if (gpu) {
            clear_rect(8, 8, 15, 15);
            clear(2, 0, 0xA5);
            clear_rect(0, 0, 63, 63);
        }
        draw(8, p, c, uv, 4, vb);
        method(0x17CC, 0);
        report_at(report, 3);
        report_at(report, 4);
        method(0x17C8, 0);
        report_at(report, 5);
        method(0x17C8, 0);
        for (j = 0; j < 70; ++j) {
            method(0x17CC, 1);
            draw(8, p, c, uv, 4, vb);
            if (gpu && j == 63) {
                clear_rect(8, 8, 15, 15);
                clear(2, 0, 0x33);
                clear_rect(0, 0, 63, 63);
            }
            method(0x17CC, 0);
        }
        report_at(report, 6);
        for (i = 0; i < 100000; ++i) {
            complete = 1;
            nv2a_pb_exec_poll();
            for (j = 1; j <= 6; ++j)
                if (((volatile uint32_t *)guest(report + j * 16))[3] != 0)
                    complete = 0;
            if (complete)
                break;
            Sleep(0);
        }
        REQUIRE(i < 100000, "queued visibility reports completed");
        for (j = 1; j <= 6; ++j) {
            uint32_t *r = (uint32_t *)guest(report + j * 16);
            if (r[2] != expected[j - 1]) {
                fprintf(stderr, "FAIL: visibility report %u: expected %u, got %u\n",
                        j, expected[j - 1], r[2]);
                return 0;
            }
            if (j > 1)
                REQUIRE(*(uint64_t *)guest(report + (j - 1) * 16) < *(uint64_t *)r,
                        "visibility timestamps stay ordered");
        }
        printf("visibility %u %u %u %u %u %u\n", expected[0], expected[1], expected[2],
               expected[3], expected[4], expected[5]);
        method(0x030C, 1);
    }

    /* A blended, nearer triangle partly over it. */
    method(0x0304, 1);                               /* blend */
    method(0x0344, 0x302);                           /* SRC_ALPHA */
    method(0x0348, 0x303);                           /* ONE_MINUS_SRC_ALPHA */
    method(0x0350, 0x8006);                          /* ADD */
    combiners(0);
    {
        static const Pos p[3] = {{20, 20, 500, 1}, {62, 24, 500, 1}, {24, 62, 500, 1}};
        static const uint32_t c[3] = {0x80FFFF00u, 0x80FFFF00u, 0x8000FFFFu};
        static const float uv[3][2] = {{0, 0}, {0, 0}, {0, 0}};
        draw(5, p, c, uv, 3, vb);
    }
    method(0x0304, 0);

    /* A, the render target, sampled in B's bottom-left corner. */
    texture_linear(a, A_SIZE);
    combiners(1);
    {
        static const Pos p[4] = {{2, 42, 100, 1}, {22, 42, 100, 1}, {22, 62, 100, 1},
                                 {2, 62, 100, 1}};
        static const uint32_t c[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        static const float uv[4][2] = {{0.03f, 0.03f}, {32.03f, 0.03f},
                                       {32.03f, 32.03f}, {0.03f, 32.03f}};
        draw(8, p, c, uv, 4, vb);
    }
    flip();

    /* The texture rewritten at the same address, then drawn again. */
    surface(b, B_SIZE, bz);
    for (i = 0; i < 64; ++i)
        texels[i] = (i & 4) ? 0xFFFFFFFFu : 0xFF000000u;
    texture_swizzled8(tex);
    combiners(1);
    method(0x0354, 0x207);                           /* ALWAYS */
    {
        static const Pos p[4] = {{44, 2, 10, 1}, {62, 2, 10, 1}, {62, 20, 10, 1},
                                 {44, 20, 10, 1}};
        static const uint32_t c[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        draw(8, p, c, uv, 4, vb);
    }

    /* A palette rewrite at the same address in the same frame must invalidate
     * the cached P8 texture. */
    memset(guest(p8), 0, 64);
    memset(guest(palette), 0, 256 * 4);
    ((uint32_t *)guest(palette))[0] = 0xFFFF0000u;
    texture_palette8(p8, palette);
    combiners(1);
    {
        static const Pos left[4] = {{44, 22, 1, 1}, {52, 22, 1, 1},
                                    {52, 30, 1, 1}, {44, 30, 1, 1}};
        static const Pos right[4] = {{54, 22, 1, 1}, {62, 22, 1, 1},
                                     {62, 30, 1, 1}, {54, 30, 1, 1}};
        static const uint32_t c[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        draw(8, left, c, uv, 4, vb);
        ((uint32_t *)guest(palette))[0] = 0xFF00FF00u;
        draw(8, right, c, uv, 4, vb);
    }
    flip();

    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open output");
    REQUIRE(fwrite(guest(b), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE && fclose(f) == 0,
            "write output");
    return 1;
}

static int scene_cpu_frame(const char *out_path, int overlay)
{
    static const Pos quad[4] = {{20, 20, 1, 1}, {44, 20, 1, 1},
                                {44, 44, 1, 1}, {20, 44, 1, 1}};
    static const uint32_t red[4] = {
        0xFFFF2020u, 0xFFFF2020u, 0xFFFF2020u, 0xFFFF2020u
    };
    static const float uv[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    uint32_t *pixels;
    unsigned x, y;
    FILE *f;

    REQUIRE(b && vb, "allocate CPU framebuffer fixture");
    identity_matrix();
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF000000u, 0);
    pixels = (uint32_t *)guest(b);
    for (y = 0; y < B_SIZE; ++y)
        for (x = 0; x < B_SIZE; ++x)
            pixels[y * B_SIZE + x] = x < B_SIZE / 2 ? 0xFFC04020u : 0xFF40C020u;
    if (overlay) {
        combiners(0);
        draw(8, quad, red, uv, 4, vb);
    }
    flip();
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open CPU framebuffer output");
    REQUIRE(fwrite(guest(b), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE &&
            fclose(f) == 0, "write CPU framebuffer output");
    puts("CPU framebuffer frames 1");
    return 1;
}

static void fill_bc(uint32_t va, uint32_t format, uint16_t color)
{
    unsigned block_size = format == 0x0C ? 8 : 16;
    uint8_t *base = (uint8_t *)guest(va);
    unsigned i;
    for (i = 0; i < 16; ++i) {
        uint8_t *block = base + i * block_size;
        uint8_t *rgb = block + (block_size == 8 ? 0 : 8);
        memset(block, 0, block_size);
        if (format == 0x0E)
            memset(block, 0xAA, 8);
        else if (format == 0x0F)
            block[0] = block[1] = 0x80;
        memcpy(rgb, &color, sizeof color);
        memcpy(rgb + 2, &color, sizeof color);
    }
}

static void texture_bc(uint32_t va, uint32_t format, int cube)
{
    method(0x1B00, va - XBOX_CONTIG_BASE);
    method(0x1B10, 512u << 16);                     /* stale linear pitch */
    method(0x1B04, 2u << 4 | format << 8 | (cube ? 5u : 1u) << 16
                    | 4u << 20 | 4u << 24 | (cube ? 1u << 2 : 0));
    method(0x1B08, 3u | 3u << 8);
    method(0x1B14, 1u << 24 | 1u << 16);
}

static int scene_bc(const char *out_path)
{
    static const uint32_t formats[3] = {0x0C, 0x0E, 0x0F};
    static const uint16_t colors[6] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF};
    static const float directions[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };
    static const uint32_t white[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    static const float uv[4][2] = {{0.03f, 0.03f}, {0.97f, 0.03f},
                                   {0.97f, 0.97f}, {0.03f, 0.97f}};
    uint32_t b = xbox_ContiguousAlloc(B_SIZE * B_SIZE * 4, 4096);
    uint32_t source[3];
    uint32_t vb = xbox_ContiguousAlloc(0x1000, 4096);
    unsigned i, face, vertex, component;
    FILE *f;

    REQUIRE(b && vb, "allocate BC fixture output");
    for (i = 0; i < 3; ++i) {
        uint32_t stride = formats[i] == 0x0C ? 256 : 384;
        source[i] = xbox_ContiguousAlloc(0x1000, 4096);
        REQUIRE(source[i], "allocate BC fixture texture");
        memset(guest(source[i]), 0, 0x1000);
        fill_bc(source[i], formats[i], 0xFFFF);
        for (face = 0; face < 6; ++face)
            fill_bc(source[i] + 0x200 + face * stride, formats[i], colors[face]);
    }
    for (i = 0; i < 16; ++i)
        method_float(0x0680 + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
    method(0x0314, 0);
    surface(b, B_SIZE, 0);
    clear(0xF0, 0xFF102040u, 0);
    for (i = 0; i < 3; ++i) {
        float y = (float)(2 + i * 12);
        Pos p[4] = {{1, y, 0, 1}, {8, y, 0, 1}, {8, y + 7, 0, 1}, {1, y + 7, 0, 1}};
        texture_bc(source[i], formats[i], 0);
        combiners(1);
        draw(8, p, white, uv, 4, vb);
        texture_bc(source[i] + 0x200, formats[i], 1);
        method(0x1E70, 3);                          /* CUBEMAP */
        for (face = 0; face < 6; ++face) {
            float direction[4][3];
            float x = (float)(10 + face * 9);
            p[0].x = p[3].x = x;
            p[1].x = p[2].x = x + 7;
            for (vertex = 0; vertex < 4; ++vertex)
                for (component = 0; component < 3; ++component)
                    direction[vertex][component] = directions[face][component];
            draw_with_texcoords(8, p, white, &direction[0][0], 3, 4, vb);
        }
    }
    flip();
    f = fopen(out_path, "wb");
    REQUIRE(f != NULL, "open BC output");
    REQUIRE(fwrite(guest(b), 4, B_SIZE * B_SIZE, f) == B_SIZE * B_SIZE && fclose(f) == 0,
            "write BC output");
    puts("BC scene drawn");
    return 1;
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if (argc != 4)
        return 2;
    if (_putenv_s("RECOMP_RASTER_THREADS", "4") != 0 || _putenv_s("RECOMP_SWIZZLED_RT", "1") != 0)
        return 1;
    if (!initialize(argv[1]))
        return 1;
    if (strncmp(argv[2], "device-", 7) == 0)
        return test_device_status(argv[2], argv[3]) ? 0 : 1;
    if (strncmp(argv[2], "textures-", 9) == 0)
        return bruteforce_test_textures(strstr(argv[2], "hardware") != NULL) ? 0 : 1;
    if (strncmp(argv[2], "guard", 5) == 0)
        return test_surface_guard(argv[3], strstr(argv[2], "invalid") != NULL) ? 0 : 1;
    if ((strstr(argv[2], "clip") || strstr(argv[2], "clear")) &&
        _putenv_s("RECOMP_WINDOW_CLIP", "1") != 0)
        return 1;
    if (strstr(argv[2], "vsh-cull") &&
        _putenv_s("RECOMP_CULL_FACE", strstr(argv[2], "cull-off") ? "0" : "1") != 0)
        return 1;
    if (strncmp(argv[2], "gpu", 3) == 0) {
        /* gpu-cpuvsh: programs transformed on the CPU, drawn on the GPU. */
        if (_putenv_s("RECOMP_D3D11_WARP", strstr(argv[2], "hardware") ? "0" : "1") != 0 ||
            (strstr(argv[2], "scale-invalid") &&
             _putenv_s("RECOMP_GPU_SCALE", "invalid") != 0) ||
            (strstr(argv[2], "scale2") &&
             _putenv_s("RECOMP_GPU_SCALE", "2") != 0) ||
            (strstr(argv[2], "scale-output-invalid") &&
             (_putenv_s("RECOMP_GPU_SCALE", "native") != 0 ||
              _putenv_s("RECOMP_GPU_OUTPUT", "100x100") != 0)) ||
            _putenv_s("RECOMP_GPU_VSH", strstr(argv[2], "cpuvsh") ? "0" : "1") != 0 ||
            _putenv_s("RECOMP_GPU_ZPASS", strstr(argv[2], "async") ? "async" : "sync") != 0 ||
            _putenv_s("RECOMP_GPU_TEX_DECODE", strstr(argv[2], "software-texture") ? "0" : "1") != 0 ||
            (strstr(argv[2], "depth") &&
             _putenv_s("RECOMP_GPU_DEPTH_TEXTURE", strstr(argv[2], "invalid") ? "invalid" : "1") != 0) ||
            (strstr(argv[2], "stencil") &&
             _putenv_s("RECOMP_GPU_STENCIL_ALIAS", strstr(argv[2], "invalid") ? "invalid" : "1") != 0) ||
            (strstr(argv[2], "direct") &&
             _putenv_s("RECOMP_GPU_DIRECT_PRESENT",
                       strstr(argv[2], "invalid") ? "invalid" :
                       strstr(argv[2], "off") ? "0" : "1") != 0) ||
            (strstr(argv[2], "bc") && _putenv_s("RECOMP_GPU_TEXLOG", "1") != 0) ||
            _putenv_s("RECOMP_GPU_VSH_SYNC", "1") != 0 ||
            _putenv_s("RECOMP_GPU_VSH_VERIFY", "1") != 0 || !bruteforce_d3d11_install())
            return 1;
    } else if (strncmp(argv[2], "cpu", 3) != 0) {
        return 2;
    }
    if (strcmp(argv[2], "gpu-cache-keys") == 0)
        return bruteforce_test_shader_cache_keys() ? 0 : 1;
    if (strncmp(argv[2], "gpu-cache-temp-", 15) == 0)
        return bruteforce_test_shader_cache_temp_collision(strstr(argv[2], "exhausted") != NULL)
                   ? 0 : 1;
    if (strncmp(argv[2], "gpu-cache-publish-", 18) == 0)
        return bruteforce_test_shader_cache_publication(strstr(argv[2], "readonly") != NULL,
                                                        strstr(argv[2], "different") != NULL,
                                                        strstr(argv[2], "corrupt") != NULL) ? 0 : 1;
    if (strstr(argv[2], "vsh-packed") ?
        !scene_vsh_packed(argv[3], strstr(argv[2], "short3") != NULL,
                          strstr(argv[2], "ubyte4") != NULL) :
        strstr(argv[2], "vsh-relative") ? !scene_vsh_relative(argv[3]) :
        strstr(argv[2], "vsh-cull") ? !scene_vsh_culling(argv[3]) :
        strstr(argv[2], "p8-bad-range") ?
        !scene_p8_bad_range(argv[2]) :
        strstr(argv[2], "p8-reprotect") ?
        !scene_p8_reprotect(strstr(argv[2], "palette") != NULL,
                            strstr(argv[2], "stage1") ? 1u : 0u) :
        strstr(argv[2], "p8-rewrite") ?
        !scene_p8_rewrite(argv[3], strstr(argv[2], "invalid-index") != NULL,
                          strstr(argv[2], "shrink") != NULL,
                          strstr(argv[2], "stage1") ? 1u : 0u) :
        strstr(argv[2], "p8") ?
        !scene_short_palettes(argv[3], strstr(argv[2], "invalid-index") != NULL,
                              strstr(argv[2], "short-allocation") != NULL,
                              strstr(argv[2], "stage1") ? 1u : 0u) :
        strstr(argv[2], "unsupported-cache") ? !scene_unsupported_cache(argv[3]) :
        strstr(argv[2], "failed-upload-cache") ? !scene_failed_upload_cache(argv[3]) :
        strstr(argv[2], "cache-rewrite") ? !scene_cache_rewrite(argv[3]) :
        strstr(argv[2], "cpu-frame") ?
        !scene_cpu_frame(argv[3], strstr(argv[2], "overlay") != NULL) :
        strstr(argv[2], "near-hud") ? !scene_hud_near(argv[3]) :
        strstr(argv[2], "clear") ? !scene_clear_rect(argv[3], strstr(argv[2], "vsh") != NULL,
                                                     strstr(argv[2], "vertical") != NULL) :
        strstr(argv[2], "clip") ? !scene_window_clip(argv[3], strstr(argv[2], "vsh") != NULL) :
        strstr(argv[2], "glare") ? !scene_glare_threshold(argv[3], strstr(argv[2], "vsh") != NULL) :
        strstr(argv[2], "stencil") ? !scene_stencil_texture(argv[3], strstr(argv[2], "vsh") != NULL) :
        strstr(argv[2], "depth") ? !scene_depth_texture(argv[3], strstr(argv[2], "vsh") != NULL) :
        strstr(argv[2], "bc") ? !scene_bc(argv[3]) :
        !scene(argv[3], strstr(argv[2], "vsh") != NULL, strncmp(argv[2], "gpu", 3) == 0))
        return 1;
    puts("scene drawn");
    return 0;
}
