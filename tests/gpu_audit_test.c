#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "nv2a_backend_ext.h"
#include "nv2a_gpu_audit.h"
#include "nv2a_vsh_interp.h"
#include "xbox_memory_layout.h"

typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 0; } \
} while (0)

static char s_events[128];
static unsigned s_event_count;
static int s_accept_vsh, s_bad_callback;

static void event(char value)
{
    if (s_event_count < sizeof s_events - 1)
        s_events[s_event_count++] = value;
    else
        s_bad_callback = 1;
}

static void fake_clear(const Nv2aSurface *surface, const Nv2aRenderState *state,
                       uint32_t flags, uint32_t color, uint32_t zstencil)
{
    (void)color;
    (void)zstencil;
    if (!surface || !state || flags != 0xF0)
        s_bad_callback = 1;
    event('C');
}

static void fake_base(const Nv2aSurface *surface, const Nv2aBatch *batch)
{
    if (!surface || !batch || !batch->vertices || batch->count != 3)
        s_bad_callback = 1;
    event('B');
}

static void fake_extended(const Nv2aSurface *surface, const Nv2aXBatch *batch)
{
    if (!surface || !batch || !batch->vertices || batch->count != 3)
        s_bad_callback = 1;
    event('E');
}

static int fake_vsh(const Nv2aSurface *surface, const Nv2aXVsBatch *batch)
{
    if (!surface || !batch || !batch->program || batch->nindices != 3)
        s_bad_callback = 1;
    event(s_accept_vsh ? 'A' : 'R');
    return s_accept_vsh;
}

static void fake_flip(void) { event('F'); }
static const Nv2aBackend s_backend = {fake_clear, fake_base, fake_flip};
static const Nv2aBackendExt s_extended = {fake_extended, fake_vsh};

static uint32_t *guest(uint32_t va)
{
    return (uint32_t *)((uintptr_t)va + xbox_GetMemoryOffset());
}

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

static int initialize(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *image;
    long length;
    int ok;
    REQUIRE(f != NULL, "open synthetic XBE");
    REQUIRE(fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) > 0 &&
            fseek(f, 0, SEEK_SET) == 0, "size synthetic XBE");
    image = (unsigned char *)malloc((size_t)length);
    REQUIRE(image != NULL, "allocate synthetic XBE");
    REQUIRE(fread(image, 1, (size_t)length, f) == (size_t)length &&
            fclose(f) == 0, "read synthetic XBE");
    ok = xbox_MemoryLayoutInit(image, (size_t)length);
    free(image);
    REQUIRE(ok, "initialize synthetic guest memory");
    return 1;
}

static void immediate(uint32_t primitive, unsigned vertices)
{
    static const float points[3][2] = {{0, 0}, {16, 0}, {0, 16}};
    unsigned i;
    method(0x17FC, primitive);
    method(0x194C, 0xFFFFFFFFu);
    for (i = 0; i < vertices; i++) {
        method_float(0x1500, points[i % 3][0]);
        method_float(0x1504, points[i % 3][1]);
        method_float(0x1508, 1);
    }
    method(0x17FC, 0);
}

static void array_draw(uint32_t primitive, unsigned count)
{
    method(0x17FC, primitive);
    method(0x1810, (count - 1) << 24);
    method(0x17FC, 0);
}

static void inline_short(void)
{
    unsigned i;
    method(0x1760, 0x1042);                       /* float4, four dwords */
    method(0x17FC, 5);
    for (i = 0; i < 8; i++)
        method(0x1818, i);
    method(0x17FC, 0);
}

static void set_matrix(void)
{
    unsigned i;
    for (i = 0; i < 16; i++)
        method_float(0x0680 + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
}

static void set_program(void)
{
    static const uint32_t pos[4] = {
        0, (1u << 21) | 0x1Bu, 2u << 26, (0xFu << 12) | (1u << 11) | 1u
    };
    nv2a_vsh_set_instruction(0, pos);
    method(0x1E94, 2);
}

static int exercise(const char *xbe, const char *path, int backend)
{
    static const float vertices[3][4] = {{0, 0, 0.5f, 1}, {16, 0, 0.5f, 1},
                                          {0, 16, 0.5f, 1}};
    uint32_t surface, vb, semaphore, marker;
    volatile uint32_t *reg;
    FILE *out;
    unsigned i;

    REQUIRE(initialize(xbe), "initialize fixture");
    surface = xbox_ContiguousAlloc(4096, 4096);
    vb = xbox_ContiguousAlloc(4096, 4096);
    semaphore = xbox_ContiguousAlloc(4096, 4096);
    REQUIRE(surface && vb && semaphore, "allocate synthetic guest objects");
    nv2a_pb_set_semaphore_target(semaphore);
    method(0x1D70, 0xA1B2C3D4u);                /* returns before executor initialization */
    REQUIRE(*guest(semaphore) == 0xA1B2C3D4u, "direct semaphore release");
    nv2a_pb_set_semaphore_target(0);
    method(0x1D70, 0x1234u);                    /* no target: executor-unhandled */
    nv2a_pb_set_semaphore_target(semaphore);
    nv2a_pb_exec_method(4, 0x1D70, 0x7777u);
    REQUIRE(*guest(semaphore) == 0xA1B2C3D4u, "other subchannels do not release semaphore");

    nv2a_pb_exec_method(5, 0x0310, 0x12345678u);
    reg = xbox_Nv2aRegPtr(0x400B10);
    REQUIRE(reg && *reg == 0x12345678u, "DMA kickoff marker is still written");
    marker = *reg;
    nv2a_pb_exec_method(6, 0x0310, 0x12345678u);
    method(0x1D7C, 1);
    nv2a_pb_exec_method(5, 0x1D7C, 2);
    method(0x1B0C, 0x80000000u);                /* mirrored, not decoded as texture state */

    if (backend) {
        nv2a_backend_ext_register(&s_extended);
        nv2a_backend_register(&s_backend);
    }
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x108);
    method(0x020C, 64);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    method(0x1D98, 1u | 14u << 16);
    method(0x1D9C, 2u | 13u << 16);
    method(0x1D90, 0xFF102030u);
    method(0x1D94, 0xF0);

    method(0x17FC, 5);                           /* empty request */
    method(0x17FC, 0);
    array_draw(5, 1);
    method(0x17FC, 5);
    method(0x1810, 0);
    method(0x17FC, 5);                           /* supersedes the unfinished request */
    method(0x17FC, 0);
    inline_short();
    method(0x1760, 0x02);
    method(0x17FC, 5);
    method(0x1818, 0x1234);
    method(0x17FC, 0);                           /* no inline vertex layout */
    immediate(5, 2);
    immediate(1, 1);                             /* unsupported point */
    immediate(2, 3);                             /* unsupported line */
    immediate(5, 3);                             /* screen-space triangle */

    memcpy(guest(vb), vertices, sizeof vertices);
    method(0x1720, vb - XBOX_CONTIG_BASE);
    method(0x1760, 0x1042);
    {
        float *p = (float *)guest(vb);
        p[4] = 1;
        p[9] = 1;
        array_draw(5, 3);                        /* untransformed / tiny */
        memcpy(p, vertices, sizeof vertices);
    }
    set_matrix();
    array_draw(5, 3);                            /* fixed function */
    method(0x0308, 1);
    method(0x039C, 0x408);                       /* cull both faces */
    method(0x03A0, 0x900);
    array_draw(5, 3);
    method(0x0308, 0);

    set_program();
    s_accept_vsh = 0;
    array_draw(5, 3);                            /* rejected VSH falls back */
    if (backend) {
        s_accept_vsh = 1;
        array_draw(5, 3);                        /* accepted VSH */
    }
    method(0x012C, 0);
    method(0x0130, 0);
    REQUIRE(!s_bad_callback, "backend callback arguments and bounds");
    if (backend)
        REQUIRE(strcmp(s_events, "CBEREAF") == 0, "backend callbacks and flip order");
    else {
        REQUIRE(s_event_count == 0, "CPU route uses no registered backend");
        REQUIRE(guest(surface)[2 * 16 + 2] != 0xFF102030u,
                "CPU path really rasterized the screen-space triangle");
    }
    out = fopen(path, "wb");
    REQUIRE(out != NULL, "open synthetic snapshot");
    REQUIRE(fprintf(out, "sem=%08X marker=%08X events=%s\n",
                    *guest(semaphore), marker, s_events) > 0, "write ordering snapshot");
    for (i = 0; i < 16 * 16; i++)
        REQUIRE(fwrite(&guest(surface)[i], sizeof(uint32_t), 1, out) == 1, "write pixels");
    REQUIRE(fclose(out) == 0, "close snapshot");
    puts("GPU audit executor fixture passed");
    return 1;
}

static int capacity(const char *xbe)
{
    unsigned i;
    REQUIRE(initialize(xbe), "initialize capacity fixture");
    method(0x17FC, 1);
    for (i = 0; i < 257; i++)
        method(0x1810, 255u << 24);               /* 65,792 indices, cap 65,536 */
    method(0x17FC, 0);
    method(0x17FC, 5);
    for (i = 0; i < 65537; i++)
        method(0x1818, i);                        /* one dword past inline cap */
    method(0x17FC, 0);
    method(0x17FC, 1);
    for (i = 0; i < 1025; i++) {
        method_float(0x1500, 1);
        method_float(0x1504, 1);
        method_float(0x1508, 1);                  /* one vertex past immediate cap */
    }
    method(0x17FC, 0);
    puts("GPU audit capacity fixture passed");
    return 1;
}

static int overflow(void)
{
    uint32_t i;
    for (i = 0; i < 10; i++)
        nv2a_gpu_audit_method(2, 0x0400, i);
    nv2a_gpu_audit_method(8, 0x0400, 1);
    nv2a_gpu_audit_method(1, 0x0401, 1);
    nv2a_gpu_audit_unhandled(9, 0x0400);
    nv2a_gpu_audit_mirror_only(0, 0x2000);
    nv2a_gpu_audit_begin(5);
    nv2a_gpu_audit_input(NV2A_AUDIT_ARRAY, UINT64_MAX, UINT64_MAX);
    nv2a_gpu_audit_input(NV2A_AUDIT_ARRAY, 1, 1);
    nv2a_gpu_audit_route(NV2A_AUDIT_SCREEN_SPACE);
    nv2a_gpu_audit_end(0, 0, 0, 0, 0);
    for (i = 0; i < 3200; i++) {
        nv2a_gpu_audit_begin(0x10000u + i);
        nv2a_gpu_audit_input(NV2A_AUDIT_ARRAY, 3, 3);
        nv2a_gpu_audit_drop(NV2A_AUDIT_UNSUPPORTED_PRIMITIVE);
        nv2a_gpu_audit_end(0, 0, 0, 0, 0);
    }
    puts("GPU audit overflow fixture passed");
    return 1;
}

static int rejection_and_offscreen(const char *xbe)
{
    static const float inside[3][4] = {{0, 0, 0.5f, 1}, {16, 0, 0.5f, 1},
                                        {0, 16, 0.5f, 1}};
    static const float outside[3][4] = {{32, 32, 0.5f, 1}, {48, 32, 0.5f, 1},
                                         {32, 48, 0.5f, 1}};
    uint32_t surface, vb;
    unsigned i;
    REQUIRE(initialize(xbe), "initialize rejection fixture");
    surface = xbox_ContiguousAlloc(4096, 4096);
    vb = xbox_ContiguousAlloc(4096, 4096);
    REQUIRE(surface && vb, "allocate rejection fixture buffers");
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0);                            /* unsupported surface format */
    method(0x020C, 1);                            /* fallback pitch/width yields zero bpp */
    method(0x0210, surface - XBOX_CONTIG_BASE);
    method(0x1720, vb - XBOX_CONTIG_BASE);
    method(0x1760, 0x1042);
    set_matrix();
    memcpy(guest(vb), inside, sizeof inside);
    array_draw(5, 3);
    method(0x0208, 0x108);
    method(0x020C, 64);
    memcpy(guest(vb), outside, sizeof outside);
    array_draw(5, 3);                             /* counted once, not once per counter */
    method(0x17FC, 5);
    for (i = 0; i < 3; i++) {
        method_float(0x1500, (float)(i * 8));
        method_float(0x1504, (float)(i * 8));
        method_float(0x1508, 1);
    }
    method(0x17FC, 0);                            /* collinear screen triangle */
    puts("GPU audit rejection fixture passed");
    return 1;
}

static int missing_program(const char *xbe)
{
    uint32_t surface, vb;
    REQUIRE(initialize(xbe), "initialize missing-program fixture");
    surface = xbox_ContiguousAlloc(4096, 4096);
    vb = xbox_ContiguousAlloc(4096, 4096);
    REQUIRE(surface && vb, "allocate missing-program buffers");
    nv2a_backend_ext_register(&s_extended);
    nv2a_backend_register(&s_backend);
    method(0x0200, 16u << 16);
    method(0x0204, 16u << 16);
    method(0x0208, 0x108);
    method(0x020C, 64);
    method(0x0210, surface - XBOX_CONTIG_BASE);
    method(0x1720, vb - XBOX_CONTIG_BASE);
    method(0x1760, 0x1042);
    method(0x1E94, 2);
    array_draw(5, 3);
    REQUIRE(s_event_count == 0, "missing program reaches no backend draw");
    puts("GPU audit missing-program fixture passed");
    return 1;
}

static DWORD WINAPI report_thread(void *unused)
{
    unsigned i;
    (void)unused;
    for (i = 0; i < 32; i++)
        nv2a_gpu_audit_write();
    return 0;
}

static int report_snapshot(void)
{
    HANDLE writer;
    unsigned i;
    writer = CreateThread(NULL, 0, report_thread, NULL, 0, NULL);
    REQUIRE(writer != NULL, "start report thread");
    for (i = 0; i < 10000; i++)
        nv2a_gpu_audit_method(3, 0x0770, 0x1234);
    REQUIRE(WaitForSingleObject(writer, INFINITE) == WAIT_OBJECT_0, "join report thread");
    CloseHandle(writer);
    nv2a_gpu_audit_clear(1, 70000, 90000, 0, 0, 0);
    nv2a_gpu_audit_begin(8);
    nv2a_gpu_audit_input(NV2A_AUDIT_INLINE, 5, 4);
    puts("GPU audit report snapshot fixture passed");
    return 1;
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if (argc != 4)
        return 2;
    if (_putenv_s("RECOMP_PB_WALK", "dma") != 0 ||
        _putenv_s("RECOMP_CULL_FACE", "1") != 0 ||
        _putenv_s("RECOMP_RASTER_THREADS", "1") != 0)
        return 1;
    if (!strcmp(argv[2], "cpu"))
        return exercise(argv[1], argv[3], 0) ? 0 : 1;
    if (!strcmp(argv[2], "backend"))
        return exercise(argv[1], argv[3], 1) ? 0 : 1;
    if (!strcmp(argv[2], "capacity"))
        return capacity(argv[1]) ? 0 : 1;
    if (!strcmp(argv[2], "overflow"))
        return overflow() ? 0 : 1;
    if (!strcmp(argv[2], "rejections"))
        return rejection_and_offscreen(argv[1]) ? 0 : 1;
    if (!strcmp(argv[2], "missing-program"))
        return missing_program(argv[1]) ? 0 : 1;
    if (!strcmp(argv[2], "snapshot"))
        return report_snapshot() ? 0 : 1;
    return 2;
}
