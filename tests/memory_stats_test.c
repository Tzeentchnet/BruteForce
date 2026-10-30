#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guest_vmem.h"
#include "kernel.h"
#include "xbox_memory_layout.h"

typedef void (*recomp_func_t)(void);

extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);
extern RECOMP_TLS uint32_t g_eax, g_esp;

recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

static uint32_t scratch;
static recomp_func_t query_bridge;

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static uint32_t *guest(uint32_t va)
{
    return (uint32_t *)((uintptr_t)va + xbox_GetMemoryOffset());
}

static uint32_t query(uint32_t output)
{
    uint32_t *stack = guest(scratch + 0x100);
    stack[0] = 0x00112233u;
    stack[1] = output;
    g_esp = scratch + 0x100;
    g_eax = 0xDEADBEEFu;
    query_bridge();
    if (g_esp != scratch + 0x108)
        return 0xFFFFFFFFu;
    return g_eax;
}

static int get_stats(XBOX_MM_STATISTICS *out)
{
    uint32_t va = scratch + 0x200;
    if (query(va) != 0)
        return 0;
    memcpy(out, guest(va), sizeof(*out));
    return 1;
}

int main(int argc, char **argv)
{
    FILE *file;
    unsigned char *xbe;
    long length;
    uint32_t a, b, contiguous, baseline, expected, base, size, status, stack_commit;
    XBOX_MM_STATISTICS stats;
    int devkit, extended, retained;

    if (argc != 3)
        return 2;
    devkit = strcmp(argv[2], "devkit") == 0;
    extended = strcmp(argv[2], "extended") == 0;
    retained = strcmp(argv[2], "retained") == 0;
    if (!devkit && !extended && !retained && strcmp(argv[2], "retail") != 0)
        return 2;
    if (_putenv_s("RECOMP_HEAP_RECLAIM", retained ? "" : "1") != 0 ||
        _putenv_s("RECOMP_EXT_VMA", extended ? "1" : "") != 0)
        return 2;
    if (devkit)
        xbox_SetTotalRam(XBOX_DEVKIT_RAM);

    file = fopen(argv[1], "rb");
    REQUIRE(file != NULL, "open synthetic XBE");
    REQUIRE(fseek(file, 0, SEEK_END) == 0, "seek synthetic XBE");
    length = ftell(file);
    REQUIRE(length > 0 && fseek(file, 0, SEEK_SET) == 0, "size synthetic XBE");
    xbe = (unsigned char *)malloc((size_t)length);
    REQUIRE(xbe != NULL, "allocate synthetic XBE");
    REQUIRE(fread(xbe, 1, (size_t)length, file) == (size_t)length, "read synthetic XBE");
    REQUIRE(fclose(file) == 0, "close synthetic XBE");
    REQUIRE(length >= 0x134, "synthetic XBE contains stack commitment");
    stack_commit = devkit ? 0x20000u : extended ? 0xFFFFFFFFu : 0;
    memcpy(xbe + 0x130, &stack_commit, sizeof(stack_commit));
    REQUIRE(xbox_MemoryLayoutInit(xbe, (size_t)length), "initialize guest memory");
    free(xbe);

    scratch = xbox_HeapAlloc(4096, 4096);
    REQUIRE(scratch != 0, "allocate bridge scratch page");
    *guest(scratch) = 0x80000000u | 181u;
    xbox_kernel_set_thunk_address(scratch, 1);
    xbox_kernel_bridge_init();
    query_bridge = recomp_lookup_kernel(*guest(scratch));
    REQUIRE(query_bridge != NULL, "resolve MmQueryStatistics bridge");

    REQUIRE(query(0) == 0xC000000Du, "reject a null guest output pointer");
    REQUIRE(get_stats(&stats), "query initial statistics");
    REQUIRE(stats.Length == sizeof(stats), "statistics structure size");
    REQUIRE(stats.TotalPhysicalPages == (devkit ? 32768u : 16384u), "guest RAM size");
    expected = stats.TotalPhysicalPages
             - (1u + XBOX_GPU_INSTANCE_DEFAULT / 4096u)
             - ((g_xbox_image_hi + 4095u) / 4096u - XBOX_BASE_ADDRESS / 4096u)
             - ((g_xbox_stack_base - g_xbox_region_base) / 4096u)
             - (devkit ? 32u : extended ? XBOX_STACK_SIZE / 4096u : 1u)
             - 1u; /* bridge scratch page */
    REQUIRE(stats.AvailablePages == expected, "initial guest page accounting");
    baseline = stats.AvailablePages;

    a = xbox_HeapAlloc(1, 16);
    REQUIRE(a != 0 && get_stats(&stats), "allocate first small heap block");
    REQUIRE(stats.AvailablePages == baseline - 1, "first small block uses one page");
    b = xbox_HeapAlloc(1, 16);
    REQUIRE(b != 0 && get_stats(&stats), "allocate second small heap block");
    REQUIRE(stats.AvailablePages == baseline - 1, "shared heap page is not double counted");
    xbox_HeapFree(a);
    REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline - 1,
            "a shared page remains in use until its last block is freed");
    xbox_HeapFree(b);
    REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline,
            "freeing the last block returns the heap page");

    contiguous = xbox_ContiguousAlloc(8193, 4096);
    REQUIRE(contiguous != 0 && get_stats(&stats), "allocate contiguous memory");
    REQUIRE(stats.AvailablePages == baseline - 3, "contiguous allocations use rounded pages");
    REQUIRE(xbox_ContiguousFree(contiguous), "free contiguous memory");
    if (retained)
        baseline -= 3;
    REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline,
            "contiguous frees reflect whether pages are reusable");
    if (retained) {
        a = xbox_ContiguousAlloc(8193, 4096);
        REQUIRE(a != 0 && a != contiguous, "retained contiguous block is not reused");
        baseline -= 3;
        REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline,
                "a replacement contiguous block uses additional pages");
    }

    contiguous = XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE - XBOX_GPU_INSTANCE_DEFAULT;
    xbox_ContiguousClaim(contiguous, 4096);
    REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline,
            "claimed GPU instance pages are not counted twice");
    REQUIRE(xbox_ContiguousFree(contiguous), "release claimed GPU instance page");

    if (extended) {
        REQUIRE(guest_vmem_active(), "extended VMA is active");
        base = 0x76000000u;
        size = 0x10000u;
        REQUIRE(guest_vmem_allocate(&base, &size, 0x2000u, 4, &status) && status == 0,
                "reserve extended guest memory");
        REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline,
                "an extended reservation consumes no pages");
        base = 0x76000000u;
        size = 0x2000u;
        REQUIRE(guest_vmem_allocate(&base, &size, 0x1000u, 4, &status) && status == 0,
                "commit extended guest memory");
        REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline - 2,
                "committed extended pages are counted");
        base = 0x76000000u;
        size = 0x1000u;
        REQUIRE(guest_vmem_free(&base, &size, 0x4000u, &status) && status == 0,
                "decommit an extended guest page");
        REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline - 1,
                "decommitted extended pages are restored");
        base = 0x76000000u;
        size = 0;
        REQUIRE(guest_vmem_free(&base, &size, 0x8000u, &status) && status == 0,
                "release extended guest memory");
        REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline,
                "released extended pages are restored");
    }

    if (!devkit && !extended && !retained) {
        contiguous = xbox_ContiguousAlloc(40u * 1024u * 1024u, 4096);
        a = xbox_HeapAlloc(24u * 1024u * 1024u, 4096);
        REQUIRE(contiguous != 0 && a != 0, "allocate beyond the combined guest RAM budget");
        REQUIRE(get_stats(&stats) && stats.AvailablePages == 0,
                "free pages saturate at zero rather than wrapping");
        xbox_HeapFree(a);
        REQUIRE(xbox_ContiguousFree(contiguous), "free large contiguous block");
        REQUIRE(get_stats(&stats) && stats.AvailablePages == baseline,
                "free pages recover after exhaustion");
    }

    xbox_MemoryLayoutShutdown();
    puts("guest memory statistics passed");
    return 0;
}
