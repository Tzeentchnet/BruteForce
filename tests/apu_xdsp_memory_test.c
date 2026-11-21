#include "apu_state.h"
#include "kernel.h"
#include "xbox_memory_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t address) { (void)address; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t address) { (void)address; return NULL; }

void recomp_xdsp_test_sg_rw(MCPXAPUState *d, uint32_t addr, uint8_t *ptr,
                            size_t len, int write);
void mcpx_apu_write(void *opaque, uint64_t addr, uint64_t value,
                    unsigned int size);

static int failures;

static void check(int ok, const char *what)
{
    printf("%-66s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

static unsigned char *load_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *bytes;
    long n;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    bytes = malloc((size_t)n);
    if (!bytes || fread(bytes, 1, (size_t)n, f) != (size_t)n) {
        free(bytes);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return bytes;
}

static uint32_t phys(uint32_t va)
{
    return (uint32_t)xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)va);
}

static uint8_t *guest(uint32_t va)
{
    return (uint8_t *)((uintptr_t)va + xbox_GetMemoryOffset());
}

static void put32(uint8_t *p, uint32_t value)
{
    memcpy(p, &value, sizeof value);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "alias";
    const char *path = argc > 2 ? argv[2] : "tools/conformance/test.xbe";
    int fallback = !strcmp(mode, "fallback") || !strcmp(mode, "fallback-128");
    int devkit = !strcmp(mode, "alias-128") || !strcmp(mode, "fallback-128") ||
                 !strcmp(mode, "pinned-128");
    int pinned = !strcmp(mode, "pinned") || !strcmp(mode, "pinned-128");
    unsigned char *xbe;
    size_t xbe_len = 0;
    uint32_t a, alias, table, ram_name, window_name;
    uint8_t *ram, *window, byte, expected[20], actual[20];
    MCPXAPUState *d;
    int i;

    if (strcmp(mode, "alias") && !fallback && !devkit && !pinned) {
        fprintf(stderr, "unknown mode %s\n", mode);
        return 2;
    }
    _putenv_s("RECOMP_PHYS_RAM_ALIAS", fallback ? "0" : "");
    _putenv_s("RECOMP_APU_XDSP_PAGES", pinned ? "window" : "");
    if (devkit)
        xbox_SetTotalRam(XBOX_DEVKIT_RAM);
    xbe = load_file(path, &xbe_len);
    if (!xbe || !xbox_MemoryLayoutInit(xbe, xbe_len)) {
        fprintf(stderr, "cannot initialize synthetic XBE %s\n", path);
        free(xbe);
        return 2;
    }
    g_apu_ram_ptr = (uint8_t *)(uintptr_t)xbox_GetMemoryOffset();
    d = calloc(1, sizeof *d);
    a = xbox_HeapAlloc(0x7000, 4096);
    if (!d || !a || a + 0x7000 > XBOX_CONTIG_SIZE) {
        fprintf(stderr, "cannot allocate the synthetic low heap block\n");
        free(d);
        free(xbe);
        xbox_MemoryLayoutShutdown();
        return 2;
    }
    xbox_ContiguousClaim(XBOX_CONTIG_BASE + a, 0x7000);
    ram = guest(a);
    window = guest(XBOX_CONTIG_BASE + a);
    alias = xbox_PhysRamAlias();
    check(fallback ? alias == 0 : alias == (devkit ? 0x08000000u : 0x04000000u),
          "the requested physical naming mode is active");

    table = phys(XBOX_CONTIG_BASE + a);
    ram_name = phys(a + 0x1080);
    put32(window, ram_name);
    ram[0x1080] = 0xA1;
    window[0x1080] = 0xB1;
    d->regs[NV_PAPU_GPSADDR] = table;
    d->regs[NV_PAPU_GPSMAXSGE] = 0;
    byte = 0;
    recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
    check(byte == (pinned ? 0xB1 : 0xA1),
          pinned ? "window override redirects a mirrored heap entry" :
                   "window table resolves an unaligned RAM entry");

    if (pinned && devkit) {
        uint32_t high = 0x05000000u;
        put32(window, high);
        *guest(high) = 0x7E;
        byte = 0;
        recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
        check(byte == 0x7E, "window override leaves high RAM directly addressable");
    } else if (!pinned) {
        uint32_t image = g_xbox_image_lo & ~0xFFFu;
        uint32_t stack = XBOX_STACK_BASE;
        uint8_t *image_ram = guest(image);
        uint8_t *image_window = guest(XBOX_CONTIG_BASE + image);
        uint8_t *stack_ram = guest(stack);
        uint8_t *stack_window = guest(XBOX_CONTIG_BASE + stack);

        window_name = phys(XBOX_CONTIG_BASE + a + 0x1080);
        byte = 0;
        recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
        check(byte == 0xA1, "later window translation does not move a cached RAM entry");
        put32(window + 8, window_name);
        d->regs[NV_PAPU_GPSMAXSGE] = 1;
        byte = 0;
        recomp_xdsp_test_sg_rw(d, 4096, &byte, 1, 0);
        check(byte == 0xB1, "a second entry resolves the overlapping window copy");
        byte = 0x5C;
        recomp_xdsp_test_sg_rw(d, 4, &byte, 1, 1);
        check(ram[0x1084] == 0x5C && window[0x1084] == 0,
              "writes to a cached RAM entry do not reach the window");
        byte = 0x6D;
        recomp_xdsp_test_sg_rw(d, 4100, &byte, 1, 1);
        check(window[0x1084] == 0x6D && ram[0x1084] == 0x5C,
              "writes to a window entry do not reach RAM");
        for (i = 0; i < 6; ++i)
            expected[i] = ram[0x1080 + 4090 + i] = (uint8_t)(0x10 + i);
        for (i = 0; i < 14; ++i)
            expected[6 + i] = window[0x1080 + i] = (uint8_t)(0x30 + i);
        memset(actual, 0, sizeof actual);
        recomp_xdsp_test_sg_rw(d, 4090, actual, sizeof actual, 0);
        check(!memcmp(actual, expected, sizeof actual),
              "a scatter-gather transfer crosses an unaligned entry boundary");

        xbox_ContiguousClaim(XBOX_CONTIG_BASE + image, 4096);
        xbox_ContiguousClaim(XBOX_CONTIG_BASE + stack, 4096);
        table = phys(XBOX_CONTIG_BASE + a + 0x6000);
        put32(window + 0x6000, phys(image));
        image_ram[0x80] = 0xE1;
        image_window[0x80] = 0xF1;
        d->regs[NV_PAPU_GPSADDR] = table;
        byte = 0;
        recomp_xdsp_test_sg_rw(d, 0x80, &byte, 1, 0);
        check(byte == 0xE1, "a low image SGE keeps its RAM translation");
        phys(XBOX_CONTIG_BASE + image);
        byte = 0xE2;
        recomp_xdsp_test_sg_rw(d, 0x81, &byte, 1, 1);
        check(image_ram[0x81] == 0xE2 && image_window[0x81] == 0,
              "a cached image SGE write does not reach the window");

        put32(window + 0x6000, phys(stack));
        stack_ram[0x80] = 0xD1;
        stack_window[0x80] = 0xC1;
        byte = 0;
        recomp_xdsp_test_sg_rw(d, 0x80, &byte, 1, 0);
        check(byte == 0xD1, "a low stack SGE keeps its RAM translation");
        phys(XBOX_CONTIG_BASE + stack);
        byte = 0xD2;
        recomp_xdsp_test_sg_rw(d, 0x81, &byte, 1, 1);
        check(stack_ram[0x81] == 0xD2 && stack_window[0x81] == 0,
              "a cached stack SGE write does not reach the window");

        table = phys(a + 0x4000);
        ram_name = phys(a + 0x1080);
        put32(ram + 0x4000, ram_name);
        put32(window + 0x4000, 0);
        d->regs[NV_PAPU_GPSADDR] = table;
        d->regs[NV_PAPU_GPSMAXSGE] = 0;
        byte = 0;
        recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
        check(byte == ram[0x1080], "a RAM-named SGE table is read from RAM");

        _putenv_s("RECOMP_APU_DSP", "xemu");
        table = phys(XBOX_CONTIG_BASE + a + 0x5000);
        ram_name = phys(a + 0x2080);
        put32(window + 0x5000, ram_name);
        ram[0x2080] = 0xC1;
        window[0x2080] = 0xD1;
        mcpx_apu_write(d, NV_PAPU_GPSADDR, table, 4);
        phys(XBOX_CONTIG_BASE + a + 0x2080);
        byte = 0;
        recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
        check(byte == 0xC1,
              "an SGE register write captures its RAM choice before later translation");

        table = phys(a + 0x4000);
        d->regs[NV_PAPU_GPSADDR] = table;

        put32(ram + 0x4000, 0x10000000u);
        byte = 0xFF;
        recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
        check(byte == 0, "an out-of-range entry is rejected instead of wrapping");
        put32(ram + 0x4000, (alias ? alias : 0) + XBOX_CONTIG_SIZE - 4095);
        byte = 0xFF;
        recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
        check(byte == 0, "an entry crossing the end of its physical region is rejected");
        d->regs[NV_PAPU_GPSADDR] = 0x0FFFFFFDu;
        byte = 0xFF;
        recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
        check(byte == 0, "an invalid SGE table is rejected before reading it");

        if (devkit) {
            uint32_t high = 0x05000000u;
            table = phys(XBOX_CONTIG_BASE + a);
            put32(window, high);
            *guest(high) = 0x7E;
            d->regs[NV_PAPU_GPSADDR] = table;
            byte = 0;
            recomp_xdsp_test_sg_rw(d, 0, &byte, 1, 0);
            check(byte == 0x7E, "128 MB guest RAM above the window remains directly addressable");
        }
    }

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    free(d);
    free(xbe);
    xbox_MemoryLayoutShutdown();
    return failures ? 1 : 0;
}
