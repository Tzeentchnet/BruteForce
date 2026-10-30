#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "nv2a_texture_sample.h"

static uint32_t reference_wrap(uint32_t c, uint32_t size, uint32_t mode)
{
    int32_t sc = (int32_t)c, n = (int32_t)size;
    if (!size)
        return 0;
    if (mode == 1)
        return (uint32_t)(((sc % n) + n) % n);
    if (mode == 2) {
        int32_t m = ((sc % (2 * n)) + 2 * n) % (2 * n);
        return (uint32_t)(m < n ? m : 2 * n - 1 - m);
    }
    return sc < 0 ? 0u : (sc >= n ? size - 1 : c);
}

static int check_wrap(uint32_t coord, uint32_t size, uint32_t mode)
{
    uint32_t expected = reference_wrap(coord, size, mode);
    uint32_t actual = nv2a_texture_wrap_coord(coord, size, mode);
    if (actual != expected) {
        fprintf(stderr, "wrap mismatch: coord=%d size=%u mode=%u expected=%u actual=%u\n",
                (int32_t)coord, size, mode, expected, actual);
        return 0;
    }
    return 1;
}

int main(void)
{
    static const uint32_t sizes[] = {0, 1, 2, 3, 7, 16, 63, 128, 255, 256,
                                     512, 4096, 32768, 65535};
    static const int32_t coords[] = {INT32_MIN, -65537, -4097, -513, -257,
                                     -16, -2, -1, 0, 1, 2, 15, 16, 255,
                                     256, 511, 512, 4096, 65535, INT32_MAX};
    static const uint32_t unusual_sizes[] = {0x3FFFFFFFu, 0x40000000u,
                                              0x7FFFFFFFu, 0x80000000u,
                                              0xFFFFFFFFu};
    static const uint32_t blocks[] = {1, 3, 7, 8, 16, 24, 32, 255};
    uint64_t state = UINT64_C(0x915A3E17B5B54D29);
    unsigned s, mode, i;

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        int32_t n = (int32_t)sizes[s];
        int32_t edges[] = {-2 * n - 1, -n - 1, -n, -n + 1, n - 1, n,
                           n + 1, 2 * n - 1, 2 * n, 2 * n + 1};
        for (mode = 0; mode < 16; ++mode) {
            for (i = 0; i < sizeof(coords) / sizeof(coords[0]); ++i)
                if (!check_wrap((uint32_t)coords[i], sizes[s], mode))
                    return 1;
            for (i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i)
                if (!check_wrap((uint32_t)edges[i], sizes[s], mode))
                    return 1;
            for (i = 0; i < 2048; ++i) {
                state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                if (!check_wrap((uint32_t)(state >> 32), sizes[s], mode))
                    return 1;
            }
        }
    }
    for (s = 0; s < sizeof(unusual_sizes) / sizeof(unusual_sizes[0]); ++s)
        for (mode = 0; mode < 16; ++mode) {
            if (mode == 1 || mode == 2)
                continue;
            for (i = 0; i < sizeof(coords) / sizeof(coords[0]); ++i)
                if (!check_wrap((uint32_t)coords[i], unusual_sizes[s], mode))
                    return 1;
        }
    for (i = 0; i < 1000000; ++i) {
        uint32_t block = blocks[i % (sizeof(blocks) / sizeof(blocks[0]))];
        uintptr_t key;
        size_t expected, actual;
        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        key = (uintptr_t)state;
        expected = ((key / block) ^ (key >> 16)) & 4095u;
        actual = nv2a_texture_dxt_slot(key, block);
        if (actual != expected) {
            fprintf(stderr, "DXT cache slot mismatch: block=%u\n", block);
            return 1;
        }
    }
    puts("texture math tests passed");
    return 0;
}
