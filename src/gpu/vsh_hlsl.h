/* HLSL vertex shaders from NV2A vertex programs; see vsh_hlsl.c.
 * Adapted from Blood Wake's bw_vsh_hlsl.h (MIT; THIRD_PARTY_NOTICES.md). */
#ifndef GPU_VSH_HLSL_H
#define GPU_VSH_HLSL_H

#include <stddef.h>
#include <stdint.h>

#include "nv2a_backend_ext.h"   /* NV2A_XVS_*, nv2a_vsh_field */

/* The second constant buffer the vertex shader reads (register b1).
 * k is the program's c[0..191]. */
typedef struct {
    float k[192][4];
    float vsinfo[4];    /* aa scale x, aa scale y, near-plane clip on, near z */
    float foginfo[4];   /* fog enabled, fog mode (0x800..), param 0, param 1 */
} GpuVshConsts;

/* gpu_vsh_hlsl flags. */
#define GPU_VSH_MULZERO 1u   /* mul/mad: anything times zero is zero (xemu) */

/* The geometry shader (entry point gs_main) that drops triangles with a
 * non-finite vertex, as the CPU path's clipper does. */
extern const char gpu_vsh_gs_hlsl[];

/* Write the vertex shader (entry point vs_main) for the `n` instructions at
 * `prog` (the last one FINAL). The inputs in `input_mask` arrive in
 * ascending order as attributes ATTR0, ATTR1, ...; conv (NULL: all floats)
 * gives each one's Nv2aXVsInput.conv, and the integer formats then arrive
 * raw and are converted as fetch_attr does. Returns the length, or 0 if it
 * did not fit. */
size_t gpu_vsh_hlsl(const uint32_t (*prog)[4], uint32_t n, uint32_t input_mask,
                    const uint8_t *conv, uint32_t flags, char *out, size_t cap);

#endif
