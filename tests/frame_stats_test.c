#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "nv2a_backend_ext.h"
#include "nv2a_census.h"
#include "nv2a_frame_stats.h"
#include "xbox_memory_layout.h"

typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern void nv2a_pb_exec_report(void);

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 0; } \
} while (0)

static int test_hash(void)
{
    uint8_t a[8 * 64], b[8 * 64];
    uint64_t h;
    unsigned i;

    for (i = 0; i < sizeof a; ++i)
        a[i] = (uint8_t)(i * 7 + 3);
    memcpy(b, a, sizeof a);
    h = nv2a_frame_hash(a, 64, 1, 1, 10, 6, 4);
    REQUIRE(h != 0, "a hash is never the unknown value");
    REQUIRE(h == nv2a_frame_hash(b, 64, 1, 1, 10, 6, 4), "equal pictures hash equally");
    b[64 * 7 + 63] ^= 0xFF;                          /* outside the rectangle */
    b[64 * 1 + 0] ^= 0xFF;                           /* left of the rectangle */
    b[64 * 1 + 44] ^= 0xFF;                          /* right of the rectangle */
    REQUIRE(h == nv2a_frame_hash(b, 64, 1, 1, 10, 6, 4),
            "pixels outside the visible rectangle do not contribute");
    b[64 * 6 + 4 * 10 + 3] ^= 1;                     /* last visible byte */
    REQUIRE(h != nv2a_frame_hash(b, 64, 1, 1, 10, 6, 4), "the last visible byte counts");
    memcpy(b, a, sizeof a);
    b[64 * 1 + 4] ^= 1;                              /* first visible byte */
    REQUIRE(h != nv2a_frame_hash(b, 64, 1, 1, 10, 6, 4), "the first visible byte counts");
    REQUIRE(nv2a_frame_hash(a, 64, 0, 0, 3, 1, 2) != nv2a_frame_hash(a, 64, 0, 0, 1, 3, 2),
            "the same bytes in a different shape hash differently");
    memset(a, 0, sizeof a);
    REQUIRE(nv2a_frame_hash(a, 64, 0, 0, 16, 2, 4) != nv2a_frame_hash(a, 64, 0, 0, 16, 1, 4),
            "row count contributes even for uniform pictures");
    return 1;
}

static int test_distinct(const char *path)
{
    Nv2aFrameStats s;
    uint32_t flips, distinct, unknown;

    REQUIRE(nv2a_frame_stats_open(&s, path), "open the statistics file");
    REQUIRE(nv2a_frame_stats_flip(&s, 1000, 3, 30, 0x11, 7) == 1, "the first frame is distinct");
    REQUIRE(nv2a_frame_stats_flip(&s, 2000, 3, 30, 0x11, 7) == 0, "a repeat is not distinct");
    REQUIRE(nv2a_frame_stats_flip(&s, 3000, 0, 0, 0x22, 7) == 1, "a change is distinct");
    REQUIRE(nv2a_frame_stats_flip(&s, 4000, 1, 0, 0, 7) == -1, "no picture is unknown");
    REQUIRE(nv2a_frame_stats_flip(&s, 5000, 1, 0, 0x22, 7) == 1,
            "a frame after an unknown one is not called a repeat");
    nv2a_frame_stats_take_report(&s, &flips, &distinct, &unknown);
    REQUIRE(flips == 5 && distinct == 3 && unknown == 1, "report counts");
    nv2a_frame_stats_take_report(&s, &flips, &distinct, &unknown);
    REQUIRE(flips == 0 && distinct == 0 && unknown == 0, "report counts reset");
    nv2a_frame_stats_close(&s);
    return 1;
}

static void method(uint32_t address, uint32_t value)
{
    nv2a_pb_exec_method(0, address, value);
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

static void flip(void)
{
    method(0x012C, 0);                               /* FLIP_INCREMENT_WRITE */
    method(0x0130, 0);                               /* FLIP_STALL */
}

static void clear_color(uint32_t color)
{
    method(0x1D90, color);
    method(0x1D94, 0xF0);
}

/* Upload c[96] = (value, 0, 0, 1) as a title would between draws. */
static void constants(float value)
{
    static const float rest[3] = {0, 0, 1};
    uint32_t bits;
    int i;

    method(0x1EA4, 96 + 96);                         /* load slot, biased by 96 */
    memcpy(&bits, &value, sizeof bits);
    method(0x0B80, bits);
    for (i = 0; i < 3; ++i) {
        memcpy(&bits, &rest[i], sizeof bits);
        method(0x0B84 + i * 4, bits);
    }
}

static void fake_draw(const Nv2aSurface *surface, const Nv2aBatch *batch)
{
    (void)surface;
    (void)batch;
}

static const Nv2aBackend fake_backend = {NULL, fake_draw, NULL};

static unsigned s_fake_clears, s_fake_clear_bad;

static void fake_clear(const Nv2aSurface *surface, const Nv2aRenderState *state,
                       uint32_t flags, uint32_t color, uint32_t zstencil)
{
    (void)color;
    s_fake_clears++;
    if (!surface || !state || (flags != 1 && flags != 3) || zstencil != 0xFFFFFF5Au)
        s_fake_clear_bad++;
}

static const Nv2aBackend fake_complete_backend = {fake_clear, fake_draw, NULL};
static const Nv2aBackend fake_clear_only_backend = {fake_clear, NULL, NULL};

static int test_depth_clear_owner(const char *xbe, const char *mode)
{
    uint32_t surface = XBOX_CONTIG_BASE + 0x00200000u;
    unsigned expected_callbacks = 0;

    REQUIRE(initialize(xbe), "initialize depth clear owner fixture");
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x128);
    method(0x020C, 64u | 64u << 16);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    method(0x0214, surface + 0x1000 - XBOX_CONTIG_BASE);
    if (strcmp(mode, "depth-owner-backend") == 0) {
        nv2a_backend_register(&fake_complete_backend);
        expected_callbacks = 2;
    } else if (strcmp(mode, "depth-owner-draw-only") == 0) {
        nv2a_backend_register(&fake_backend);
    } else if (strcmp(mode, "depth-owner-clear-only") == 0) {
        nv2a_backend_register(&fake_clear_only_backend);
        expected_callbacks = 2;
    } else {
        REQUIRE(strcmp(mode, "depth-owner-cpu") == 0, "valid depth clear owner mode");
    }
    method(0x1D8C, 0xFFFFFF5Au);
    method(0x1D94, 1);
    method(0x1D94, 3);
    nv2a_census_write();
    REQUIRE(s_fake_clears == expected_callbacks && !s_fake_clear_bad,
            "depth clear callback receives the original arguments");
    printf("depth clear callbacks %u\n", s_fake_clears);
    return 1;
}

/* Clear, flip, flip again unchanged, clear another colour, flip; then the
 * same with a back end registered, which owns the pixels. */
static int test_executor(const char *xbe)
{
    uint32_t surface = XBOX_CONTIG_BASE + 0x00200000u;

    REQUIRE(initialize(xbe), "initialize");
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x108);                           /* linear A8R8G8B8 */
    method(0x020C, 64);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    clear_color(0xFF102030u);
    constants(1.0f);
    flip();
    constants(1.0f);
    flip();
    clear_color(0xFF405060u);
    constants(2.0f);
    flip();
    nv2a_backend_register(&fake_backend);
    flip();
    return 1;
}

static void method_float(uint32_t address, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    method(address, bits);
}

static void screen_triangle(void)
{
    static const float points[3][2] = {{0, 0}, {16, 0}, {0, 16}};
    unsigned i;

    method(0x17FC, 5);                               /* TRIANGLES */
    method(0x194C, 0xFFFFFFFFu);
    for (i = 0; i < 3; ++i) {
        method_float(0x1500, points[i][0]);
        method_float(0x1504, points[i][1]);
        method_float(0x1508, 1);
    }
    method(0x17FC, 0);
}

static int test_flip_dumps(const char *xbe, const char *gate)
{
    uint32_t surface = XBOX_CONTIG_BASE + 0x00200000u;
    FILE *f;

    REQUIRE(initialize(xbe), "initialize flip capture fixture");
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x108);
    method(0x020C, 64);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    screen_triangle();
    nv2a_pb_exec_report();
    flip();
    nv2a_pb_exec_report();
    f = fopen(gate, "wb");
    REQUIRE(f && fclose(f) == 0, "open the flip capture gate");
    clear_color(0xFF345678u);
    flip();
    REQUIRE(remove(gate) == 0, "close the flip capture gate");
    clear_color(0xFFAABBCCu);
    flip();
    nv2a_pb_exec_report();
    return 1;
}

/* A draw before the gate opens is not counted; after it opens, a
 * screen-space draw, a texture that is the colour surface, a clear and an
 * unhandled method are. */
static int test_census(const char *xbe, const char *gate)
{
    uint32_t surface = XBOX_CONTIG_BASE + 0x00200000u;
    FILE *f;

    REQUIRE(initialize(xbe), "initialize");
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x108);
    method(0x020C, 64);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    screen_triangle();
    flip();
    f = fopen(gate, "wb");
    REQUIRE(f && fclose(f) == 0, "open the census gate");
    flip();
    clear_color(0xFF000000u);
    method(0x1B00, surface - XBOX_CONTIG_BASE);      /* stage 0 = the surface */
    method(0x1B04, 2u << 4 | 0x12u << 8 | 1u << 16);  /* 2D linear A8R8G8B8 */
    method(0x1B1C, 16u << 16 | 16u);
    method(0x1B10, 64u << 16);                       /* pitch */
    screen_triangle();
    method(0x1D7C, 0x12345678u);                     /* not handled */
    flip();
    REQUIRE(remove(gate) == 0, "close the census gate");
    flip();
    screen_triangle();
    nv2a_census_write();
    return 1;
}

/* A fixed-function array triangle draws, unless the probe drops transformed
 * batches; a screen-space immediate triangle draws either way. */
static int test_probe(const char *xbe, int dropping)
{
    uint32_t surface, vertex_buffer, *pixels, i;
    float *vertices;

    REQUIRE(initialize(xbe), "initialize");
    surface = xbox_ContiguousAlloc(4096, 4096);
    vertex_buffer = xbox_ContiguousAlloc(4096, 4096);
    REQUIRE(surface && vertex_buffer, "allocate synthetic surfaces");
    pixels = (uint32_t *)((uintptr_t)surface + xbox_GetMemoryOffset());
    vertices = (float *)((uintptr_t)vertex_buffer + xbox_GetMemoryOffset());
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x105);
    method(0x020C, 64);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    for (i = 0; i < 16; ++i)
        method_float(0x0680 + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
    method(0x1720, vertex_buffer - XBOX_CONTIG_BASE);
    method(0x1760, 0x1042);
    clear_color(0);
    {
        static const float points[3][4] = {{0, 0, 0.5f, 1}, {16, 0, 0.5f, 1}, {0, 16, 0.5f, 1}};
        memcpy(vertices, points, sizeof points);
    }
    method(0x17FC, 5);
    method(0x1810, 2u << 24);
    method(0x17FC, 0);
    REQUIRE((pixels[2 * 16 + 2] != 0) == !dropping,
            "the probe drops exactly the transformed batch");
    method(0x1760, 0x02);                            /* no position array */
    clear_color(0);
    screen_triangle();
    REQUIRE(pixels[2 * 16 + 2] != 0, "screen-space batches still draw under the probe");
    return 1;
}

/* The extended back-end interface: what a transformed batch carries, and the
 * readback hook at a flip. */
static uint32_t s_ext_draws, s_ext_vertices, s_readbacks, s_readback_va;
static Nv2aXVertex s_ext_first;
static int s_ext_has_rc, s_ext_near_clip;
static float s_ext_near_z;
static uint32_t s_readback_fill;

static void ext_draw(const Nv2aSurface *s, const Nv2aXBatch *b)
{
    (void)s;
    if (!s_ext_draws)
        s_ext_first = b->vertices[0];
    s_ext_draws++;
    s_ext_vertices += b->count;
    s_ext_has_rc = b->rc != NULL;
    s_ext_near_clip = b->near_clip;
    s_ext_near_z = b->near_z;
}

static int ext_readback(uint32_t color_va)
{
    uint32_t *p = (uint32_t *)((uintptr_t)color_va + xbox_GetMemoryOffset());
    unsigned i;
    s_readbacks++;
    s_readback_va = color_va;
    for (i = 0; i < 16 * 16; ++i)
        p[i] = s_readback_fill;
    return 1;
}

static const Nv2aBackendExt fake_ext = {ext_draw, NULL, NULL, NULL, ext_readback};
static int s_base_draws;

static void base_draw(const Nv2aSurface *s, const Nv2aBatch *b)
{
    (void)s;
    (void)b;
    s_base_draws++;
}

static const Nv2aBackend base_backend = {NULL, base_draw, NULL};

static int test_extended(const char *xbe)
{
    uint32_t surface, vertex_buffer, colors;
    float *vertices;
    uint32_t i;

    REQUIRE(initialize(xbe), "initialize");
    nv2a_backend_ext_register(&fake_ext);
    nv2a_backend_register(&base_backend);
    surface = xbox_ContiguousAlloc(4096, 4096);
    vertex_buffer = xbox_ContiguousAlloc(4096, 4096);
    colors = xbox_ContiguousAlloc(4096, 4096);
    vertices = (float *)((uintptr_t)vertex_buffer + xbox_GetMemoryOffset());
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x108);
    method(0x020C, 64);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    for (i = 0; i < 16; ++i)
        method_float(0x0680 + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
    {
        static const float points[3][4] = {{0, 0, 0.5f, 1}, {16, 0, 0.5f, 1}, {0, 16, 0.5f, 1}};
        uint32_t *c = (uint32_t *)((uintptr_t)colors + xbox_GetMemoryOffset());
        memcpy(vertices, points, sizeof points);
        c[0] = c[1] = c[2] = 0x80FF4000u;
    }
    method(0x1720, vertex_buffer - XBOX_CONTIG_BASE);
    method(0x1760, 0x1042);
    method(0x172C, colors - XBOX_CONTIG_BASE);
    method(0x176C, 0x0440);
    method(0x1E60, 1);                               /* combiners programmed */
    method(0x17FC, 5);
    method(0x1810, 2u << 24);
    method(0x17FC, 0);
    REQUIRE(s_ext_draws == 1 && s_ext_vertices == 3 && s_base_draws == 0,
            "a transformed batch goes to the extended draw, not the base one");
    REQUIRE(s_ext_first.x == 0.0f && s_ext_first.rhw == 1.0f, "surface position and 1/w");
    REQUIRE(s_ext_first.d0[0] == 1.0f && s_ext_first.d0[3] > 0.49f && s_ext_first.d0[3] < 0.51f,
            "diffuse colour as floats");
    REQUIRE(s_ext_first.fog == 1.0f, "fog factor 1 with fog disabled");
    REQUIRE(s_ext_has_rc && s_ext_near_clip, "combiners and near clipping are passed");
    s_readback_fill = 0xFF336699u;
    flip();
    REQUIRE(s_readbacks == 1 && s_readback_va == surface, "the presented surface is read back");
    return 1;
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if (argc != 4)
        return 2;
    if (strcmp(argv[2], "unit") == 0) {
        if (!test_hash() || !test_distinct(argv[3]))
            return 1;
        puts("frame statistics unit tests passed");
        return 0;
    }
    if (strcmp(argv[2], "executor") == 0) {
        if (_putenv_s("RECOMP_FRAME_STATS", argv[3]) != 0 ||
            _putenv_s("RECOMP_SWIZZLED_RT", "1") != 0 || !test_executor(argv[1]))
            return 1;
        puts("frame statistics executor run finished");
        return 0;
    }
    if (strcmp(argv[2], "probe-on") == 0 || strcmp(argv[2], "probe-off") == 0) {
        int dropping = strcmp(argv[2], "probe-on") == 0;
        if (_putenv_s("RECOMP_PROBE_DROP_TRANSFORMED", dropping ? "1" : "0") != 0 ||
            _putenv_s("RECOMP_RASTER_THREADS", "4") != 0 || !test_probe(argv[1], dropping))
            return 1;
        puts("probe tests passed");
        return 0;
    }
    if (strcmp(argv[2], "extended") == 0 || strcmp(argv[2], "extended-experiments") == 0) {
        int experiments = strcmp(argv[2], "extended-experiments") == 0;
        if (_putenv_s("RECOMP_FRAME_STATS", argv[3]) != 0 ||
            _putenv_s("RECOMP_CULL_FACE", experiments ? "1" : "0") != 0 ||
            _putenv_s("RECOMP_PROGRAM_NEAR_ZERO", experiments ? "1" : "0") != 0 ||
            !test_extended(argv[1]))
            return 1;
        puts("extended back-end tests passed");
        return 0;
    }
    if (strcmp(argv[2], "census") == 0) {
        char gate[1024];
        snprintf(gate, sizeof gate, "%s.gate", argv[3]);
        remove(gate);
        if (_putenv_s("RECOMP_GPU_CENSUS", argv[3]) != 0 ||
            _putenv_s("RECOMP_GPU_CENSUS_GATE", gate) != 0 ||
            _putenv_s("RECOMP_SWIZZLED_RT", "1") != 0 || !test_census(argv[1], gate))
            return 1;
        puts("census run finished");
        return 0;
    }
    if (strcmp(argv[2], "dump-default") == 0 || strcmp(argv[2], "dump-flip-only") == 0) {
        char gate[1024];
        int flip_only = strcmp(argv[2], "dump-flip-only") == 0;
        snprintf(gate, sizeof gate, "%s.gate", argv[3]);
        remove(gate);
        if (_putenv_s("RECOMP_FB_DUMP", argv[3]) != 0 ||
            _putenv_s("RECOMP_FB_DUMP_FLIPS", gate) != 0 ||
            _putenv_s("RECOMP_FB_DUMP_FLIPS_ONLY", flip_only ? "1" : "0") != 0 ||
            !test_flip_dumps(argv[1], gate))
            return 1;
        puts("flip capture tests passed");
        return 0;
    }
    if (strncmp(argv[2], "depth-owner-", 12) == 0) {
        if (_putenv_s("RECOMP_GPU_CENSUS", argv[3]) != 0 ||
            _putenv_s("RECOMP_GPU_CENSUS_GATE", "") != 0 ||
            !test_depth_clear_owner(argv[1], argv[2]))
            return 1;
        puts("depth clear owner tests passed");
        return 0;
    }
    return 2;
}
