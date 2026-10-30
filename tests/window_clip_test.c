#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "nv2a_vsh_interp.h"
#include "nv2a_window_clip.h"
#include "xbox_memory_layout.h"
#include "d3d/d3d8_swizzle.h"

typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 0; } \
} while (0)

static uint32_t bounds(unsigned int first, unsigned int last)
{
    return first | (last << 16);
}

static unsigned int swizzled_16(unsigned int x, unsigned int y)
{
    unsigned int i, index = 0;
    for (i = 0; i < 4; ++i) {
        index |= ((x >> i) & 1u) << (2 * i);
        index |= ((y >> i) & 1u) << (2 * i + 1);
    }
    return index;
}

static int test_rectangles(void)
{
    Nv2aWindowClip clip = {0};
    unsigned int i;

    REQUIRE(nv2a_window_clip_allows(&clip, 99, 99, 0, 0),
            "an unprogrammed clip does not discard pixels");
    nv2a_window_clip_write(&clip, 0, 0, bounds(2, 4));
    REQUIRE(nv2a_window_clip_allows(&clip, 99, 99, 0, 0),
            "wait for both axes to be programmed");
    nv2a_window_clip_write(&clip, 1, 0, bounds(3, 5));
    REQUIRE(clip.uniform && nv2a_window_clip_allows(&clip, 2, 3, 0, 0) &&
            nv2a_window_clip_allows(&clip, 4, 5, 0, 0), "inclusive endpoints");
    REQUIRE(!nv2a_window_clip_allows(&clip, 1, 3, 0, 0) &&
            !nv2a_window_clip_allows(&clip, 5, 3, 0, 0) &&
            !nv2a_window_clip_allows(&clip, 2, 6, 0, 0), "outside edges");
    REQUIRE(nv2a_window_clip_allows(&clip, 4, 6, 1, 1) &&
            nv2a_window_clip_allows(&clip, 9, 11, 1, 1) &&
            !nv2a_window_clip_allows(&clip, 10, 11, 1, 1), "AA scaling");

    nv2a_window_clip_write(&clip, 0, 1, bounds(8, 10) | 0xF000F000u);
    nv2a_window_clip_write(&clip, 1, 1, bounds(3, 5));
    REQUIRE(!clip.uniform && clip.horizontal[0] == bounds(2, 4),
            "later rectangle leaves earlier state intact");
    for (i = 1; i < NV2A_WINDOW_CLIP_COUNT; ++i)
        REQUIRE(clip.horizontal[i] == (bounds(8, 10) | 0xF000F000u) &&
                clip.vertical[i] == bounds(3, 5), "write propagates to later slots");
    REQUIRE(nv2a_window_clip_allows(&clip, 10, 5, 0, 0) &&
            !nv2a_window_clip_allows(&clip, 6, 3, 0, 0),
            "rectangle union and 12-bit coordinate masks");
    clip.exclusive = 1;
    REQUIRE(!nv2a_window_clip_allows(&clip, 2, 3, 0, 0) &&
            !nv2a_window_clip_allows(&clip, 8, 3, 0, 0) &&
            nv2a_window_clip_allows(&clip, 6, 3, 0, 0), "exclusive rectangle union");

    nv2a_window_clip_write(&clip, 0, 0, bounds(8, 7));
    nv2a_window_clip_write(&clip, 1, 0, bounds(1, 9));
    REQUIRE(clip.uniform && nv2a_window_clip_allows(&clip, 8, 5, 0, 0),
            "reversed endpoints describe an empty rectangle");
    clip.exclusive = 0;
    REQUIRE(!nv2a_window_clip_allows(&clip, 8, 5, 0, 0),
            "inclusive empty rectangle rejects all pixels");
    return 1;
}

static uint32_t *guest(uint32_t va)
{
    return (uint32_t *)((uintptr_t)va + xbox_GetMemoryOffset());
}

static int test_texture_cache(void)
{
    static const uint32_t formats[] = {0x0C, 0x0E, 0x0F};
    uint8_t *bytes = (uint8_t *)guest(0x80010000u);
    uint32_t pixels[64], previous[64];
    Nv2aTexture texture = {0};
    unsigned f, block, phase, x, y;

    texture.offset = 0x80010000u;
    texture.width = texture.height = 8;
    for (f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
        unsigned block_bytes = formats[f] == 0x0C ? 8 : 16;
        unsigned color_offset = block_bytes == 8 ? 0 : 8;

        memset(bytes, 0, block_bytes * 4);
        for (block = 0; block < 4; ++block) {
            uint8_t *p = bytes + block * block_bytes;
            if (formats[f] == 0x0E)
                memset(p, 0xFF, 8);
            else if (formats[f] == 0x0F)
                p[0] = 0xFF;
            p[color_offset + 1] = 0xF8;
            p[color_offset + 2] = 0x1F;
        }
        texture.color = formats[f];
        for (phase = 0; phase < 3; ++phase) {
            if (phase == 1)
                bytes[color_offset] ^= 0x1F;
            else if (phase == 2)
                bytes[block_bytes == 8 ? block_bytes : 0] ^= 0x0F;
            REQUIRE(nv2a_backend_decode_texture(&texture, pixels), "decode DXT texture");
            if (phase == 0)
                REQUIRE(pixels[0] == 0xFFFF0000u, "DXT initial red endpoint");
            else if (phase == 1)
                REQUIRE(pixels[0] == 0xFFFF00FFu, "DXT rewritten magenta endpoint");
            else if (formats[f] == 0x0C)
                REQUIRE(pixels[4] == 0xFFFF007Bu, "DXT1 rewritten second block");
            else if (formats[f] == 0x0E)
                REQUIRE(pixels[0] == 0x00FF00FFu, "DXT3 rewritten alpha");
            else
                REQUIRE(pixels[0] == 0xF0FF00FFu, "DXT5 rewritten alpha");
            if (phase)
                REQUIRE(memcmp(previous, pixels, sizeof(pixels)) != 0,
                        "rewriting a cached DXT block changes its decoded pixels");
            for (y = 0; y < 8; ++y)
                for (x = 0; x < 8; ++x) {
                    uint32_t expected;
                    REQUIRE(d3d8_dxt_decode_texel(bytes, formats[f], x, y, 8, &expected) &&
                            pixels[y * 8 + x] == expected, "DXT cache matches direct decode");
                }
            memcpy(previous, pixels, sizeof(pixels));
        }
    }
    memset(bytes, 0, 64);
    for (block = 0; block < 4; ++block) {
        uint8_t *p = bytes + block * 16;
        p[0] = 0xFF;
        p[9] = 0xF8;
        p[10] = 0x1F;
    }
    texture.color = 0x0E;
    REQUIRE(nv2a_backend_decode_texture(&texture, pixels) && pixels[2] == 0x00FF0000u,
            "DXT3 alpha decoding before format change");
    texture.color = 0x0F;
    REQUIRE(nv2a_backend_decode_texture(&texture, pixels) && pixels[2] == 0xFFFF0000u,
            "same bytes at the same address are decoded in the new DXT format");
    return 1;
}

static void method(uint32_t address, uint32_t value)
{
    nv2a_pb_exec_method(0, address, value);
}

static void method_float(uint32_t address, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    method(address, bits);
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

static void screen_vertices(const float points[][2], unsigned int count, uint32_t primitive)
{
    unsigned int i;

    method(0x17FC, primitive);
    method(0x194C, 0xFFFFFFFFu);
    for (i = 0; i < count; ++i) {
        method_float(0x1500, points[i][0]);
        method_float(0x1504, points[i][1]);
        method_float(0x1508, 1);
    }
    method(0x17FC, 0);
}

static void screen_triangle(void)
{
    static const float points[3][2] = {{0, 0}, {16, 0}, {0, 16}};
    screen_vertices(points, 3, 5);
}

static void array_vertices(float *vertices, const float points[][2], unsigned int count,
                           uint32_t primitive, float z)
{
    unsigned int i;

    for (i = 0; i < count; ++i) {
        vertices[4 * i] = points[i][0];
        vertices[4 * i + 1] = points[i][1];
        vertices[4 * i + 2] = z;
        vertices[4 * i + 3] = 1;
    }
    method(0x17FC, primitive);
    method(0x1810, (count - 1) << 24);
    method(0x17FC, 0);
}

static void array_triangle(float *vertices, float z, float side)
{
    const float points[3][2] = {{0, 0}, {side, 0}, {0, side}};
    array_vertices(vertices, points, 3, 5, z);
}

static void clear_color(uint32_t color)
{
    method(0x1D90, color);
    method(0x1D94, 0xF0);
}

static void setup_surface(uint32_t surface)
{
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x105);
    method(0x020C, 64);
    method(0x0210, surface - XBOX_CONTIG_BASE);
}

static void setup_array(uint32_t vertex_buffer)
{
    unsigned int i;
    for (i = 0; i < 16; ++i)
        method_float(0x0680 + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
    method(0x1720, vertex_buffer - XBOX_CONTIG_BASE);
    method(0x1760, 0x1042);
}

static void fake_draw(const Nv2aSurface *surface, const Nv2aBatch *batch)
{
    (void)surface;
    (void)batch;
}

static const Nv2aBackend fake_backend = {NULL, fake_draw, NULL};

static int test_rasterization(int enabled, int backend)
{
    uint32_t surface = xbox_ContiguousAlloc(4096, 4096);
    uint32_t vertex_buffer = xbox_ContiguousAlloc(4096, 4096);
    uint32_t *pixels;
    float *vertices;
    REQUIRE(surface && vertex_buffer, "allocate synthetic surfaces");
    pixels = guest(surface);
    vertices = (float *)guest(vertex_buffer);
    setup_surface(surface);
    method(0x02B4, 0);
    method(0x02C0, bounds(2, 4));
    method(0x02E0, bounds(3, 5));
    clear_color(0);
    if (backend)
        nv2a_backend_register(&fake_backend);
    screen_triangle();
    REQUIRE(pixels[3 * 16 + 2] && pixels[5 * 16 + 4], "screen-space clip endpoints");
    REQUIRE((pixels[1 * 16 + 1] == 0) == enabled &&
            (pixels[3 * 16 + 5] == 0) == enabled, "screen-space outside pixels");

    method(0x0208, 0x04040205u);
    clear_color(0);
    screen_triangle();
    REQUIRE(pixels[swizzled_16(2, 3)] &&
            ((pixels[swizzled_16(1, 1)] == 0) == enabled),
            "swizzled target respects the opt-in setting");
    method(0x0208, 0x105);

    if (enabled) {
        method(0x02C4, bounds(8, 10));
        method(0x02E4, bounds(3, 5));
        clear_color(0);
        screen_triangle();
        REQUIRE(pixels[3 * 16 + 2] && pixels[3 * 16 + 8] &&
                !pixels[3 * 16 + 6], "screen-space rectangle union");
        method(0x02B4, 1);
        clear_color(0);
        screen_triangle();
        REQUIRE(!pixels[3 * 16 + 2] && !pixels[3 * 16 + 8] &&
                pixels[3 * 16 + 6], "screen-space exclusive mode");

        clear_color(0x12345678);
        REQUIRE(pixels[3 * 16 + 2] == 0x12345678 &&
                pixels[3 * 16 + 6] == 0x12345678, "surface clear ignores window clips");

        method(0x02B4, 0);
        method(0x02C0, bounds(2, 4));
        method(0x02E0, bounds(3, 5));
        method(0x0208, 0x1105);
        method(0x020C, 128);
        clear_color(0);
        screen_triangle();
        REQUIRE(pixels[3 * 32 + 4] && pixels[3 * 32 + 9] &&
                !pixels[3 * 32 + 3] && !pixels[3 * 32 + 10], "AA clip endpoints");

        method(0x0208, 0x2105);
        clear_color(0);
        screen_triangle();
        REQUIRE(pixels[6 * 32 + 4] && pixels[11 * 32 + 9] &&
                !pixels[5 * 32 + 4] && !pixels[12 * 32 + 9],
                "two-axis AA clip endpoints");

        method(0x0208, 0x105);
        method(0x020C, 64);
    }
    clear_color(0);
    setup_array(vertex_buffer);
    method(0x0214, 0x1000);
    method(0x030C, 1);
    method(0x0354, 0x201);
    method(0x035C, 1);
    array_triangle(vertices, 1, 16);
    REQUIRE(pixels[3 * 16 + 2] && ((pixels[1 * 16 + 1] == 0) == enabled),
            "fixed-function pixels respect the opt-in setting");
    if (enabled) {
        method(0x02C0, bounds(0, 15));
        method(0x02E0, bounds(0, 15));
        array_triangle(vertices, 2, 16);
        REQUIRE(pixels[1 * 16 + 1], "clipped fragments did not write depth");
    }
    surface = xbox_ContiguousAlloc(128u * 128u * 4u, 4096);
    REQUIRE(surface, "allocate parallel rasterization surface");
    pixels = guest(surface);
    method(0x0200, 128u << 16);
    method(0x0204, 128u << 16);
    method(0x020C, 128u * 4u);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    method(0x02C0, bounds(32, 95));
    method(0x02E0, bounds(8, 63));
    method(0x030C, 0);
    method(0x035C, 0);
    clear_color(0);
    array_triangle(vertices, 1, 128);
    REQUIRE(pixels[32 * 128 + 32] &&
            ((pixels[16 * 128 + 16] == 0) == enabled),
            "parallel fixed-function pixels respect the opt-in setting");
    return 1;
}

static int cull_case(uint32_t *pixels, float *vertices, int array, int winding,
                     int expected_first, int expected_second)
{
    static const float clockwise[3][2] = {{0, 0}, {16, 0}, {0, 16}};
    static const float counterclockwise[3][2] = {{0, 0}, {0, 16}, {16, 0}};
    static const float strip[4][2] = {{0, 0}, {16, 0}, {0, 16}, {16, 16}};
    const float (*points)[2] = winding == 0 ? clockwise :
                               winding == 1 ? counterclockwise : strip;
    unsigned int count = winding == 2 ? 4 : 3;
    uint32_t primitive = winding == 2 ? 6 : 5;

    clear_color(0);
    if (array)
        array_vertices(vertices, points, count, primitive, 1);
    else
        screen_vertices(points, count, primitive);
    if ((pixels[3 * 16 + 3] != 0) != expected_first ||
        (winding == 2 && (pixels[13 * 16 + 13] != 0) != expected_second)) {
        fprintf(stderr, "FAIL: cull case array=%d winding=%d expected=%d,%d\n",
                array, winding, expected_first, expected_second);
        return 0;
    }
    return 1;
}

static int cull_quad_case(uint32_t *pixels, float *vertices, int array,
                          uint32_t primitive, int expected)
{
    static const float quad[4][2] = {{0, 0}, {16, 0}, {16, 16}, {0, 16}};
    static const float strip[4][2] = {{0, 0}, {16, 0}, {0, 16}, {16, 16}};
    const float (*points)[2] = primitive == 9 ? strip : quad;

    clear_color(0);
    if (array)
        array_vertices(vertices, points, 4, primitive, 1);
    else
        screen_vertices(points, 4, primitive);
    if ((pixels[3 * 16 + 13] != 0) != expected ||
        (pixels[13 * 16 + 3] != 0) != expected) {
        fprintf(stderr, "FAIL: cull quad array=%d primitive=%u expected=%d\n",
                array, primitive, expected);
        return 0;
    }
    return 1;
}

static int cull_near_case(uint32_t *pixels, float *vertices, int reverse, int expected)
{
    static const float points[3][2] = {{0, 0}, {16, 0}, {0, 16}};
    unsigned int order[3] = {0, reverse ? 2 : 1, reverse ? 1 : 2};
    unsigned int i;

    for (i = 0; i < 3; ++i) {
        vertices[4 * i] = points[order[i]][0];
        vertices[4 * i + 1] = points[order[i]][1];
        vertices[4 * i + 2] = order[i] == 2 ? -1.0f : 1.0f;
        vertices[4 * i + 3] = 1.0f;
    }
    clear_color(0);
    method(0x17FC, 5);
    method(0x1810, 0x02000000u);
    method(0x17FC, 0);
    REQUIRE((pixels[3 * 16 + 3] != 0) == expected,
            "near-clipped triangle preserves face winding");
    return 1;
}

static int cull_swizzled_case(uint32_t *pixels, float *vertices, int array, int enabled)
{
    static const float points[3][2] = {{0, 0}, {0, 16}, {16, 0}};

    method(0x0208, 0x04040205u);
    clear_color(0);
    if (array)
        array_vertices(vertices, points, 3, 5, 1);
    else
        screen_vertices(points, 3, 5);
    REQUIRE((pixels[swizzled_16(3, 3)] != 0) == !enabled,
            "face culling applies to swizzled targets");
    method(0x0208, 0x105);
    return 1;
}

static int test_culling(int enabled, int clipping)
{
    static const float clockwise[3][2] = {{0, 0}, {16, 0}, {0, 16}};
    static const float counterclockwise[3][2] = {{0, 0}, {0, 16}, {16, 0}};
    uint32_t surface = xbox_ContiguousAlloc(4096, 4096);
    uint32_t vertex_buffer = xbox_ContiguousAlloc(4096, 4096);
    uint32_t *pixels;
    float *vertices;
    int array;

    REQUIRE(surface && vertex_buffer, "allocate culling surfaces");
    pixels = guest(surface);
    vertices = (float *)guest(vertex_buffer);
    setup_surface(surface);
    if (clipping) {
        method(0x02B4, 0);
        method(0x02C0, bounds(0, 7));
        method(0x02E0, bounds(0, 15));
        clear_color(0);
        screen_triangle();
        REQUIRE(pixels[3 * 16 + 3] && !pixels[3 * 16 + 9],
                "window clipping remains active with face culling");
        method(0x02C0, bounds(0, 15));
    }
    method(0x0308, 1);
    REQUIRE(cull_case(pixels, vertices, 0, 0, 1, 1) &&
            cull_case(pixels, vertices, 0, 1, 1, 1),
            "culling waits for both face modes");
    method(0x039C, 0x405);
    REQUIRE(cull_case(pixels, vertices, 0, 1, 1, 1),
            "culling waits for a programmed front face");
    nv2a_pb_exec_method(3, 0x039C, 0xFFFFFFFFu);

    for (array = 0; array < 2; ++array) {
        if (array)
            setup_array(vertex_buffer);
        method(0x0308, 1);
        method(0x039C, 0x405);
        method(0x03A0, 0x900);
        REQUIRE(cull_case(pixels, vertices, array, 0, 1, 1) &&
                cull_case(pixels, vertices, array, 1, !enabled, !enabled) &&
                cull_case(pixels, vertices, array, 2, 1, 1) &&
                cull_quad_case(pixels, vertices, array, 7, 1) &&
                cull_quad_case(pixels, vertices, array, 8, 1) &&
                cull_quad_case(pixels, vertices, array, 9, 1) &&
                cull_swizzled_case(pixels, vertices, array, enabled),
                "back-face culling and triangle-strip winding");
        method(0x03A0, 0x901);
        REQUIRE(cull_case(pixels, vertices, array, 0, !enabled, !enabled) &&
                cull_case(pixels, vertices, array, 1, 1, 1) &&
                cull_case(pixels, vertices, array, 2, !enabled, !enabled) &&
                cull_quad_case(pixels, vertices, array, 7, !enabled) &&
                cull_quad_case(pixels, vertices, array, 8, !enabled) &&
                cull_quad_case(pixels, vertices, array, 9, !enabled),
                "counterclockwise front-face selection");
        method(0x039C, 0x404);
        REQUIRE(cull_case(pixels, vertices, array, 0, 1, 1) &&
                cull_case(pixels, vertices, array, 1, !enabled, !enabled),
                "front-face culling");
        method(0x039C, 0x408);
        REQUIRE(cull_case(pixels, vertices, array, 0, !enabled, !enabled) &&
                cull_case(pixels, vertices, array, 1, !enabled, !enabled),
                "front-and-back culling");
        method(0x0308, 0);
        REQUIRE(cull_case(pixels, vertices, array, 0, 1, 1) &&
                cull_case(pixels, vertices, array, 1, 1, 1),
                "disabled culling draws both windings");
        method(0x0308, 2);
        REQUIRE(cull_case(pixels, vertices, array, 0, 1, 1) &&
                cull_case(pixels, vertices, array, 1, 1, 1),
                "cull enable uses only its low bit");
        method(0x039C, 0x405);
        method(0x03A0, 0x900);
        method(0x0308, 3);
        REQUIRE(cull_case(pixels, vertices, array, 0, 1, 1) &&
                cull_case(pixels, vertices, array, 1, !enabled, !enabled),
                "cull enable accepts a set low bit");
    }
    method(0x0308, 1);
    method(0x039C, 0x405);
    method(0x03A0, 0x900);
    REQUIRE(cull_near_case(pixels, vertices, 0, 1) &&
            cull_near_case(pixels, vertices, 1, !enabled),
            "face culling occurs after near-plane clipping");
    method(0x0214, 0x1000);
    method(0x030C, 1);
    method(0x0354, 0x201);
    method(0x035C, 1);
    method(0x0308, 1);
    method(0x039C, 0x405);
    method(0x03A0, 0x900);
    clear_color(0);
    array_vertices(vertices, counterclockwise, 3, 5, 1);
    clear_color(0);
    array_vertices(vertices, clockwise, 3, 5, 2);
    REQUIRE((pixels[3 * 16 + 3] != 0) == enabled,
            "culled triangles do not update depth");
    return 1;
}

static void program_triangle(float *vertices, const float depths[3], const float ws[3])
{
    static const float points[3][2] = {{0, 0}, {16, 0}, {0, 16}};
    unsigned int i;

    for (i = 0; i < 3; ++i) {
        vertices[4 * i] = points[i][0];
        vertices[4 * i + 1] = points[i][1];
        vertices[4 * i + 2] = depths[i];
        vertices[4 * i + 3] = ws[i];
    }
    method(0x17FC, 5);
    method(0x1810, 0x02000000u);
    method(0x17FC, 0);
}

static int test_program_near_plane(int zero, int no_near, int clip, int cull, int trace)
{
    static const float white[4] = {1, 1, 1, 1};
    static const float positive_w[3] = {1, 1, 1};
    static const float screen_quad_w[3] = {240, 240, 240};
    static const float negative_w[3] = {-1, -1, -1};
    static const float crossing_w[3] = {-1, 1, 1};
    static const float crossing_z[3] = {-1, 1, 1};
    static const float screen_quad_z[3] = {0, 0, 0};
    static const float positive_z[3] = {1, 1, 1};
    static const float strip[4][2] = {{0, 0}, {16, 0}, {0, 16}, {16, 16}};
    static const float reverse[3][2] = {{0, 0}, {0, 16}, {16, 0}};
    const uint32_t color_ins[4] = {
        0, (1u << 21) | 0x1Bu, 3u << 26,
        (0xFu << 12) | (1u << 11) | (3u << 3)
    };
    const uint32_t position_ins[4] = {
        0, (1u << 21) | 0x1Bu, 2u << 26,
        (0xFu << 12) | (1u << 11) | 1u
    };
    uint32_t surface = xbox_ContiguousAlloc(4096, 4096);
    uint32_t vertex_buffer = xbox_ContiguousAlloc(4096, 4096);
    uint32_t *pixels;
    float *vertices;

    REQUIRE(surface && vertex_buffer, "allocate programmable near-plane surfaces");
    pixels = guest(surface);
    vertices = (float *)guest(vertex_buffer);
    setup_surface(surface);
    setup_array(vertex_buffer);
    nv2a_vsh_set_instruction(0, color_ins);
    nv2a_vsh_set_instruction(1, position_ins);
    nv2a_vsh_set_constant(0, white);
    method(0x1E94, 2);
    method_float(0x0A28, 0.5f);
    if (trace)
        method(0x012C, 0);

    clear_color(0);
    array_triangle(vertices, 0, 16);
    REQUIRE((pixels[3 * 16 + 3] != 0) == (zero || no_near),
            "zero-depth program output uses the selected near plane");
    clear_color(0);
    program_triangle(vertices, screen_quad_z, screen_quad_w);
    REQUIRE((pixels[3 * 16 + 3] != 0) == (zero || no_near),
            "zero-depth screen output remains visible with large positive W");
    clear_color(0);
    array_triangle(vertices, 0.25f, 16);
    REQUIRE((pixels[3 * 16 + 3] != 0) == (zero || no_near),
            "positive depth below the viewport offset is not unconditionally discarded");
    clear_color(0);
    array_triangle(vertices, -0.25f, 16);
    REQUIRE((pixels[3 * 16 + 3] != 0) == no_near,
            "negative screen depth is clipped unless near clipping is disabled");
    clear_color(0);
    array_triangle(vertices, 1, 16);
    REQUIRE(pixels[3 * 16 + 3], "positive depth beyond the viewport offset draws");
    method_float(0x0A28, -0.5f);
    clear_color(0);
    array_triangle(vertices, -0.25f, 16);
    REQUIRE((pixels[3 * 16 + 3] != 0) == !zero,
            "screen-zero clipping is independent of a negative viewport offset");
    method_float(0x0A28, 1.5f);
    clear_color(0);
    array_triangle(vertices, 0.75f, 16);
    REQUIRE((pixels[3 * 16 + 3] != 0) == (zero || no_near),
            "near-plane selection handles changed viewport constants");
    method_float(0x0A28, 0.5f);
    clear_color(0);
    array_vertices(vertices, strip, 4, 6, 0);
    REQUIRE((pixels[3 * 16 + 3] != 0) == (zero || no_near) &&
            (pixels[13 * 16 + 13] != 0) == (zero || no_near),
            "screen-zero triangle strips draw both triangles");
    method(0x0208, 0x04040205u);
    clear_color(0);
    array_triangle(vertices, 0, 16);
    REQUIRE((pixels[swizzled_16(3, 3)] != 0) == (zero || no_near),
            "the selected near plane applies to swizzled targets");
    method(0x0208, 0x105);

    clear_color(0);
    program_triangle(vertices, crossing_z, positive_w);
    REQUIRE((pixels[4 * 16 + 4] != 0) == (zero || no_near) &&
            (pixels[1 * 16 + 1] != 0) == no_near &&
            pixels[4 * 16 + 8], "triangles crossing the selected near plane are clipped");
    clear_color(0);
    program_triangle(vertices, positive_z, negative_w);
    REQUIRE(!pixels[3 * 16 + 3] && !pixels[13 * 16 + 13],
            "vertices behind the eye remain clipped");
    clear_color(0);
    program_triangle(vertices, positive_z, crossing_w);
    REQUIRE(!pixels[3 * 16 + 3] && pixels[13 * 16 + 13],
            "triangles crossing the eye plane retain their visible portion");

    if (!no_near) {
        method(0x0214, 0x1000);
        method(0x030C, 1);
        method(0x0354, 0x201);
        method(0x035C, 1);
        clear_color(0);
        array_triangle(vertices, -0.25f, 16);
        array_triangle(vertices, 1, 16);
        REQUIRE(pixels[3 * 16 + 3], "near-clipped fragments do not write depth");
        method(0x030C, 0);
        method(0x035C, 0);
    }

    if (zero) {
        method(0x0208, 0x1105);
        method(0x020C, 128);
        clear_color(0);
        array_triangle(vertices, 0, 16);
        REQUIRE(pixels[3 * 32 + 4], "screen-zero clipping handles AA-scaled targets");
        method(0x0208, 0x2105);
        clear_color(0);
        array_triangle(vertices, 0, 16);
        REQUIRE(pixels[6 * 32 + 4], "screen-zero clipping handles two-axis AA");
        method(0x0208, 0x105);
        method(0x020C, 64);
    }
    if (clip) {
        method(0x02B4, 0);
        method(0x02C0, bounds(2, 7));
        method(0x02E0, bounds(0, 15));
        clear_color(0);
        array_triangle(vertices, 0, 16);
        REQUIRE(pixels[3 * 16 + 3] && !pixels[3 * 16 + 9],
                "window clipping remains active with screen-zero clipping");
    }
    if (cull) {
        method(0x0308, 1);
        method(0x039C, 0x405);
        method(0x03A0, 0x900);
        clear_color(0);
        array_vertices(vertices, reverse, 3, 5, 0);
        REQUIRE(!pixels[3 * 16 + 3], "screen-zero clipping retains back-face culling");
        array_triangle(vertices, 0, 16);
        REQUIRE(pixels[3 * 16 + 3], "front-facing screen-zero triangles draw");
    }

    method(0x0308, 0);
    method(0x1E94, 0);
    clear_color(0);
    array_triangle(vertices, -0.25f, 16);
    REQUIRE((pixels[3 * 16 + 3] != 0) == no_near,
            "fixed-function near-plane behavior remains unchanged");
    clear_color(0);
    array_triangle(vertices, 0, 16);
    REQUIRE(pixels[3 * 16 + 3], "fixed-function geometry on its near plane still draws");
    if (trace)
        method(0x012C, 0);
    return 1;
}

int main(int argc, char **argv)
{
    const char *mode;
    int enabled, backend;

    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (argc != 3 || !test_rectangles())
        return 1;
    mode = argv[2];
    if (strcmp(mode, "texture-cache") == 0) {
        if (!initialize(argv[1]) || !test_texture_cache())
            return 1;
        puts("texture cache tests passed");
        return 0;
    }
    if (strncmp(mode, "near-", 5) == 0) {
        int zero = strcmp(mode, "near-off") != 0 && strcmp(mode, "near-unset") != 0 &&
                   strcmp(mode, "near-no-z") != 0 && strcmp(mode, "near-trace-off") != 0;
        int no_near = strcmp(mode, "near-no-z") == 0 || strcmp(mode, "near-conflict") == 0;
        int clip = strcmp(mode, "near-clip") == 0;
        int cull = strcmp(mode, "near-cull") == 0;
        int trace = strncmp(mode, "near-trace-", 11) == 0;

        if (_putenv_s("RECOMP_PROGRAM_NEAR_ZERO", strcmp(mode, "near-invalid") == 0
                                                   ? "invalid" : strcmp(mode, "near-unset") == 0
                                                   ? "" : zero ? "1" : "0") != 0 ||
            _putenv_s("RECOMP_CLIP_NEAR_Z", no_near ? "0" : "1") != 0 ||
            _putenv_s("RECOMP_WINDOW_CLIP", clip ? "1" : "0") != 0 ||
            _putenv_s("RECOMP_CULL_FACE", cull ? "1" : "0") != 0 ||
            _putenv_s("RECOMP_NO_VSH", "") != 0 ||
            _putenv_s("RECOMP_RASTER_THREADS", "4") != 0 ||
            _putenv_s("RECOMP_SWIZZLED_RT", "1") != 0)
            return 1;
        if (strcmp(mode, "near-backend-before") == 0)
            nv2a_backend_register(&fake_backend);
        method(0x0200, 0);
        if (strcmp(mode, "near-invalid") == 0 || strcmp(mode, "near-conflict") == 0)
            return 1;
        if (strcmp(mode, "near-backend-after") == 0)
            nv2a_backend_register(&fake_backend);
        if (!initialize(argv[1]) || !test_program_near_plane(zero, no_near, clip, cull, trace))
            return 1;
        puts("programmable near-plane tests passed");
        return 0;
    }
    if (strncmp(mode, "cull-", 5) == 0) {
        enabled = strcmp(mode, "cull-off") != 0 && strcmp(mode, "cull-unset") != 0;
        if (_putenv_s("RECOMP_WINDOW_CLIP", strcmp(mode, "cull-clip") == 0 ? "1" : "0") != 0 ||
            _putenv_s("RECOMP_CULL_FACE", strcmp(mode, "cull-invalid") == 0
                                          ? "invalid" : strcmp(mode, "cull-unset") == 0
                                          ? "" : enabled ? "1" : "0") != 0 ||
            _putenv_s("RECOMP_PROGRAM_NEAR_ZERO", "0") != 0 ||
            _putenv_s("RECOMP_CLIP_NEAR_Z", "1") != 0 ||
            _putenv_s("RECOMP_RASTER_THREADS", "4") != 0 ||
            _putenv_s("RECOMP_SWIZZLED_RT", "1") != 0)
            return 1;
        if (strcmp(mode, "cull-backend-before") == 0)
            nv2a_backend_register(&fake_backend);
        method(0x0200, 0);
        if (strcmp(mode, "cull-invalid") == 0)
            return 1;
        if (strcmp(mode, "cull-backend-after") == 0)
            nv2a_backend_register(&fake_backend);
        if (strcmp(mode, "cull-invalid-face") == 0) {
            method(0x039C, 0xFFFFFFFFu);
            return 1;
        }
        if (strcmp(mode, "cull-invalid-front") == 0) {
            method(0x03A0, 0xFFFFFFFFu);
            return 1;
        }
        if (!initialize(argv[1]) || !test_culling(enabled, strcmp(mode, "cull-clip") == 0))
            return 1;
        puts("face culling tests passed");
        return 0;
    }
    enabled = strcmp(mode, "disabled") != 0;
    backend = strcmp(mode, "backend-after") == 0;
    if (_putenv_s("RECOMP_WINDOW_CLIP", strcmp(mode, "invalid") == 0
                                         ? "invalid" : enabled ? "1" : "0") != 0 ||
        _putenv_s("RECOMP_CULL_FACE", "0") != 0 ||
        _putenv_s("RECOMP_PROGRAM_NEAR_ZERO", "0") != 0 ||
        _putenv_s("RECOMP_RASTER_THREADS", "4") != 0 ||
        _putenv_s("RECOMP_SWIZZLED_RT", "1") != 0)
        return 1;
    if (strcmp(mode, "invalid") == 0) {
        method(0x0200, 0);
        return 1;
    }
    if (strcmp(mode, "backend-before") == 0)
        nv2a_backend_register(&fake_backend);
    if (!initialize(argv[1]) || !test_rasterization(enabled, backend))
        return 1;
    puts("window clipping tests passed");
    return 0;
}
