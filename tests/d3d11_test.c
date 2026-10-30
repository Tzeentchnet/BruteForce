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

typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern void nv2a_pb_exec_poll(void);
extern int bruteforce_test_textures(int hardware);

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

static void texture_linear(uint32_t va, uint32_t size)
{
    method(0x1B00, va - XBOX_CONTIG_BASE);
    method(0x1B04, 2u << 4 | 0x12u << 8 | 1u << 16);
    method(0x1B10, (size * 4) << 16);
    method(0x1B1C, size << 16 | size);
    method(0x1B08, 3u | 3u << 8);                    /* clamp */
    method(0x1B14, 1u << 24 | 1u << 16);
}

static void texture_palette8(uint32_t va, uint32_t palette)
{
    method(0x1B00, va - XBOX_CONTIG_BASE);
    method(0x1B04, 2u << 4 | 0x0Bu << 8 | 1u << 16 | 3u << 20 | 3u << 24);
    method(0x1B20, palette - XBOX_CONTIG_BASE);
    method(0x1B08, 1u | 1u << 8);
    method(0x1B14, 1u << 24 | 1u << 16);
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
        report_at(report, 1);
        draw(8, p, c, uv, 4, vb);
        report_at(report, 2);
        method(0x17C8, 0);
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
    if (strncmp(argv[2], "textures-", 9) == 0)
        return bruteforce_test_textures(strstr(argv[2], "hardware") != NULL) ? 0 : 1;
    if (strncmp(argv[2], "gpu", 3) == 0) {
        /* gpu-cpuvsh: programs transformed on the CPU, drawn on the GPU. */
        if (_putenv_s("RECOMP_D3D11_WARP", strstr(argv[2], "hardware") ? "0" : "1") != 0 ||
            _putenv_s("RECOMP_GPU_VSH", strstr(argv[2], "cpuvsh") ? "0" : "1") != 0 ||
            _putenv_s("RECOMP_GPU_ZPASS", strstr(argv[2], "async") ? "async" : "sync") != 0 ||
            _putenv_s("RECOMP_GPU_TEX_DECODE", strstr(argv[2], "software-texture") ? "0" : "1") != 0 ||
            (strstr(argv[2], "bc") && _putenv_s("RECOMP_GPU_TEXLOG", "1") != 0) ||
            _putenv_s("RECOMP_GPU_VSH_SYNC", "1") != 0 ||
            _putenv_s("RECOMP_GPU_VSH_VERIFY", "1") != 0 || !bruteforce_d3d11_install())
            return 1;
    } else if (strncmp(argv[2], "cpu", 3) != 0) {
        return 2;
    }
    if (strstr(argv[2], "bc") ? !scene_bc(argv[3]) :
        !scene(argv[3], strstr(argv[2], "vsh") != NULL, strncmp(argv[2], "gpu", 3) == 0))
        return 1;
    puts("scene drawn");
    return 0;
}
