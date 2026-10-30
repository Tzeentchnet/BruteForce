/* HLSL pixel shaders from NV2A register-combiner state; see rc_hlsl.c.
 * Adapted from Blood Wake's bw_rc_hlsl.h (MIT; THIRD_PARTY_NOTICES.md). */
#ifndef GPU_RC_HLSL_H
#define GPU_RC_HLSL_H

#include <stddef.h>
#include <stdint.h>

/* Everything that changes the shader's code. Constants (c0/c1, the final
 * combiner's constants, fog colour, texture sizes, alpha test) live in the
 * constant buffer instead, so a material colour change does not compile. */
typedef struct {
    uint32_t color_icw[8], alpha_icw[8], color_ocw[8], alpha_ocw[8];
    uint32_t final0, final1, control, stage_program, clip_plane_mode;
    uint8_t tex_valid[4], tex_normalized[4], tex_cube[4], pad;
} GpuRcKey;

/* The constant buffer both shaders read (register b0):
 *   surf     surface width, height, 1/width, 1/height (real pixels)
 *   depth    z offset, z scale to 0..1, clamp (1/0), -
 *   alpha    alpha function (1 LESS .. 7 ALWAYS), reference 0..255, enabled
 *   fogc     fog colour
 *   C0, C1   per-stage combiner constants; FC0, FC1 the final combiner's
 *   texinfo  per stage: 1/width, 1/height for texel-space coordinates */
#define GPU_RC_CBUFFER \
    "cbuffer C : register(b0) {\n" \
    "  float4 surf; float4 depth; float4 alpha; float4 fogc;\n" \
    "  float4 C0[8]; float4 C1[8]; float4 FC0; float4 FC1;\n" \
    "  float4 texinfo[4];\n" \
    "};\n"

typedef struct {
    float surf[4], depth[4], alpha[4], fogc[4];
    float c0[8][4], c1[8][4], fc0[4], fc1[4];
    float texinfo[4][4];
} GpuRcConsts;

/* Write the pixel shader (entry point ps_main) for `k` into out. Returns the
 * length, or 0 if it did not fit. */
size_t gpu_rc_hlsl(const GpuRcKey *k, char *out, size_t cap);

#endif
