#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *memory;
static struct { uint32_t method, value; } seen[64];
static unsigned seen_count;
static volatile uint32_t dma_get = 0x12345678u;
static volatile uint32_t dma_subroutine;

ptrdiff_t xbox_GetMemoryOffset(void)
{
    return (ptrdiff_t)memory - (ptrdiff_t)0x80000000u;
}

void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t value)
{
    (void)subch;
    if (seen_count < sizeof(seen) / sizeof(seen[0])) {
        seen[seen_count].method = method;
        seen[seen_count].value = value;
    }
    ++seen_count;
    if (method == 0x0100 && value == 0x63u) {
        if (dma_subroutine != 0x2405u)
            abort();
        dma_subroutine = 0x2404u;
    } else if (method == 0x0100 && value == 0x67u) {
        if (dma_subroutine != 0x2505u)
            abort();
        dma_subroutine = 0x2511u;
    } else if (method == 0x0100 && value == 0x6Bu) {
        dma_subroutine = 0x04000001u;
    }
}

void nv2a_pb_exec_report(void) {}
void nv2a_pb_exec_walk_begin(void) {}
void xbox_Nv2aAckBusyBits(void) {}
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t offset)
{
    if (offset == 0x800044u) return &dma_get;
    if (offset == 0x00324Cu) return &dma_subroutine;
    return NULL;
}
uint32_t nv2a_pb_scan_segment(uint32_t start, uint32_t end);
void nv2a_pb_scan_wrapped(uint32_t start, uint32_t end,
                          uint32_t put, uint32_t fallback);
void nv2a_pb_resync(uint32_t get);
void nv2a_pb_scan_staged(uint32_t put);

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static void put(uint32_t offset, uint32_t word)
{
    memcpy(memory + offset, &word, sizeof(word));
}

static void scan(uint32_t start, uint32_t end)
{
    nv2a_pb_scan_segment(0x80000000u + start, 0x80000000u + end);
}

static int observed(unsigned index, uint32_t method, uint32_t value)
{
    return index < seen_count && seen[index].method == method && seen[index].value == value;
}

int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "";
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (_putenv_s("RECOMP_PB_EXEC", "1") != 0)
        return 2;
    memory = VirtualAlloc(NULL, 64u * 1024u * 1024u,
                          MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    REQUIRE(memory != NULL, "allocate synthetic physical memory");

    if (*mode) {
        nv2a_pb_resync(0x100);
        if (strcmp(mode, "nested") == 0) {
            put(0x100, 0x202u);
            put(0x200, 0x302u);
        } else if (strcmp(mode, "orphan-return") == 0) {
            put(0x100, 0x00020000u);
        } else if (strcmp(mode, "bad-jump") == 0) {
            put(0x100, 0x04000001u);
        } else if (strcmp(mode, "bad-return") == 0) {
            put(0x100, 0x202u);
            put(0x200, (1u << 18) | 0x0100u);
            put(0x204, 0x6Bu);
            put(0x208, 0x00020000u);
        } else if (strcmp(mode, "bad-put") != 0) {
            fprintf(stderr, "Unknown mode: %s\n", mode);
            return 2;
        }
        nv2a_pb_scan_staged(strcmp(mode, "bad-put") == 0 ? 0x04000000u : 0x108u);
        fprintf(stderr, "Expected invalid pushbuffer to stop\n");
        return 2;
    }

    put(0x100, (3u << 18) | 0x0100u);
    put(0x104, 11);
    put(0x108, 0x0D386032u);
    put(0x10C, 0x0F800001u);
    put(0x110, (1u << 18) | 0x0300u);
    put(0x114, 22);
    scan(0x100, 0x108);
    scan(0x108, 0x10C);
    scan(0x10C, 0x118);
    REQUIRE(seen_count == 4, "split increasing packet and following packet");
    REQUIRE(observed(0, 0x0100, 11) && observed(1, 0x0104, 0x0D386032u) &&
            observed(2, 0x0108, 0x0F800001u) && observed(3, 0x0300, 22),
            "split parameters must not be interpreted as CALL or JUMP");

    put(0x200, 0x40000000u | (2u << 18) | 0x0200u);
    put(0x204, 33);
    put(0x208, 44);
    scan(0x200, 0x204);
    scan(0x204, 0x208);
    scan(0x208, 0x20C);
    REQUIRE(seen_count == 6 && observed(4, 0x0200, 33) && observed(5, 0x0200, 44),
            "non-increasing packet resumes across multiple segments");

    put(0x300, 0x402u);
    put(0x304, (1u << 18) | 0x0304u);
    put(0x308, 66);
    put(0x400, (1u << 18) | 0x0308u);
    put(0x404, 55);
    put(0x408, 0x00020000u);
    scan(0x300, 0x30C);
    REQUIRE(seen_count == 8 && observed(6, 0x0308, 55) && observed(7, 0x0304, 66),
            "valid CALL returns to its parent packet stream");

    put(0x500, 0x04000002u);
    scan(0x500, 0x504);
    REQUIRE(seen_count == 8, "out-of-range CALL must not read guest memory");

    put(0x700, (3u << 18) | 0x0100u);
    put(0x704, 77);
    put(0x800, (1u << 18) | 0x030Cu);
    put(0x804, 88);
    scan(0x700, 0x708);
    scan(0x800, 0x808);
    REQUIRE(seen_count == 10 && observed(8, 0x0100, 77) && observed(9, 0x030C, 88),
            "discontinuous input reports and resets the incomplete packet");

    put(0x900, 0x03FFFFFAu);
    put(0x03FFFFF8u, (1u << 18) | 0x0310u);
    put(0x03FFFFFCu, 99);
    scan(0x900, 0x904);
    REQUIRE(seen_count == 11 && observed(10, 0x0310, 99),
            "a CALL at the end of RAM must stop at the physical boundary");

    nv2a_pb_scan_segment(0x83FFFFFCu, 0x84000004u);
    REQUIRE(seen_count == 11, "out-of-range segment must not be read");

    put(0x1800, (3u << 18) | 0x0100u);
    put(0x1804, 101);
    put(0x1808, 102);
    put(0x180C, 103);
    put(0x1810, (1u << 18) | 0x0300u);
    put(0x1814, 104);
    put(0x1818, 0x1001u);
    put(0x1000, (1u << 18) | 0x0304u);
    put(0x1004, 105);
    nv2a_pb_scan_wrapped(0x80001800u, 0x80002000u,
                         0x80001008u, 0x80001000u);
    REQUIRE(seen_count == 16 && observed(11, 0x0100, 101) &&
            observed(12, 0x0104, 102) && observed(13, 0x0108, 103) &&
            observed(14, 0x0300, 104) && observed(15, 0x0304, 105),
            "ring wrap follows the actual jump after the last observed PUT");

    put(0x1B00, (1u << 18) | 0x0308u);
    put(0x1B04, 106);
    put(0x1008, (1u << 18) | 0x030Cu);
    put(0x100C, 107);
    nv2a_pb_scan_wrapped(0x80001B00u, 0x80001B08u,
                         0x80001010u, 0x80001008u);
    REQUIRE(seen_count == 18 && observed(16, 0x0308, 106) &&
            observed(17, 0x030C, 107), "a missing ring jump is reported before fallback");

    put(0x2000, (1u << 18) | 0x0400u);
    put(0x2004, 201);
    put(0x2008, 0x3001u);
    put(0x200C, 0x04000002u);
    put(0x2010, (1u << 18) | 0x0408u);
    put(0x2014, 203);
    put(0x3000, (1u << 18) | 0x0404u);
    put(0x3004, 202);
    put(0x3008, 0x2011u);
    nv2a_pb_resync(0x2000);
    nv2a_pb_scan_staged(0x2018);
    REQUIRE(seen_count == 21 && observed(18, 0x0400, 201) &&
            observed(19, 0x0404, 202) && observed(20, 0x0408, 203),
            "jump to an external pushbuffer and back without executing skipped data");
    REQUIRE(dma_get == 0x12345678u, "staged walk does not publish GET mid-scan");

    put(0x2100, 0x3102u);
    put(0x2104, (1u << 18) | 0x0410u);
    put(0x2108, 205);
    put(0x3100, (1u << 18) | 0x040Cu);
    put(0x3104, 204);
    put(0x3108, 0x00020000u);
    nv2a_pb_resync(0x2100);
    nv2a_pb_scan_staged(0x210C);
    REQUIRE(seen_count == 23 && observed(21, 0x040C, 204) &&
            observed(22, 0x0410, 205), "staged CALL returns to the parent stream");

    put(0x2200, (2u << 18) | 0x0414u);
    put(0x2204, 206);
    put(0x2208, 0x04000002u);
    put(0x220C, (1u << 18) | 0x041Cu);
    put(0x2210, 208);
    nv2a_pb_resync(0x2200);
    nv2a_pb_scan_staged(0x2208);
    nv2a_pb_scan_staged(0x2214);
    REQUIRE(seen_count == 26 && observed(23, 0x0414, 206) &&
            observed(24, 0x0418, 0x04000002u) && observed(25, 0x041C, 208),
            "staged split packet preserves parameters across PUT updates");

    put(0x23F0, (1u << 18) | 0x0420u);
    put(0x23F4, 209);
    put(0x23F8, 0x2301u);
    put(0x2300, (1u << 18) | 0x0424u);
    put(0x2304, 210);
    nv2a_pb_resync(0x23F0);
    nv2a_pb_scan_staged(0x2308);
    REQUIRE(seen_count == 28 && observed(26, 0x0420, 209) &&
            observed(27, 0x0424, 210), "staged walk follows a ring-wrap jump");

    put(0x2400, 0x3402u);
    put(0x2404, 0x3502u);
    put(0x2408, (1u << 18) | 0x042Cu);
    put(0x240C, 213);
    put(0x3400, (1u << 18) | 0x0100u);
    put(0x3404, 0x63u);
    put(0x3408, 0x2405u);
    put(0x3500, (1u << 18) | 0x0428u);
    put(0x3504, 212);
    put(0x3508, 0x00020000u);
    nv2a_pb_resync(0x2400);
    nv2a_pb_scan_staged(0x2410);
    REQUIRE(seen_count == 31 && observed(28, 0x0100, 0x63u) &&
            observed(29, 0x0428, 212) && observed(30, 0x042C, 213),
            "guest clearing subroutine state permits a subsequent CALL");
    REQUIRE(dma_subroutine == 0x2408u,
            "CALL and RETURN publish the hardware subroutine register");

    put(0x2500, 0x3602u);
    put(0x2510, (1u << 18) | 0x0430u);
    put(0x2514, 214);
    put(0x3600, (1u << 18) | 0x0100u);
    put(0x3604, 0x67u);
    put(0x3608, 0x00020000u);
    nv2a_pb_resync(0x2500);
    nv2a_pb_scan_staged(0x2518);
    REQUIRE(seen_count == 33 && observed(31, 0x0100, 0x67u) &&
            observed(32, 0x0430, 214) && dma_subroutine == 0x2510u,
            "RETURN uses a guest-updated subroutine address");
    REQUIRE(VirtualFree(memory, 0, MEM_RELEASE), "free synthetic physical memory");
    puts("pushbuffer segment decoding passed");
    return 0;
}
