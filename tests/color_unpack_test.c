#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define TEST_SSE2 1
#endif

#include "nv2a_combiner.h"

static void reference_unpack(uint32_t c, float out[4])
{
    volatile float divisor = 255.0f;
    out[0] = (float)((c >> 16) & 0xFFu) / divisor;
    out[1] = (float)((c >> 8) & 0xFFu) / divisor;
    out[2] = (float)(c & 0xFFu) / divisor;
    out[3] = (float)(c >> 24) / divisor;
}

static int check(uint32_t color)
{
    float storage[8], expected[4];
    float *out = storage;

    if (((uintptr_t)out & 15u) == 0)
        ++out;
    reference_unpack(color, expected);
    nv2a_rc_unpack(color, out);
    if (memcmp(expected, out, sizeof(expected)) != 0) {
        fprintf(stderr, "color unpack mismatch for 0x%08X\n", color);
        return 0;
    }
    return 1;
}

static int check_colors(void)
{
    static const unsigned shifts[] = {0, 8, 16, 24};
    uint32_t state = 0x915A3E17u;
    unsigned i, channel;

    for (channel = 0; channel < 4; ++channel)
        for (i = 0; i < 256; ++i)
            if (!check((uint32_t)i << shifts[channel]))
                return 0;
    for (i = 0; i < 250000; ++i) {
        state = state * 1664525u + 1013904223u;
        if (!check(state))
            return 0;
    }
    return 1;
}

int main(void)
{
#if defined(TEST_SSE2)
    static const unsigned rounding[] = {
        _MM_ROUND_NEAREST, _MM_ROUND_DOWN, _MM_ROUND_UP, _MM_ROUND_TOWARD_ZERO
    };
    unsigned initial = _mm_getcsr(), i;

    for (i = 0; i < sizeof(rounding) / sizeof(rounding[0]); ++i) {
        _mm_setcsr((initial & ~_MM_ROUND_MASK) | rounding[i]);
        if (!check_colors()) {
            _mm_setcsr(initial);
            return 1;
        }
    }
    _mm_setcsr(initial);
#else
    if (!check_colors())
        return 1;
#endif
    puts("color unpack tests passed");
    return 0;
}
