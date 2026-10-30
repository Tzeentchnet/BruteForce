/*
 * HLSL pixel shaders from NV2A register-combiner state.
 *
 * Adapted from Blood Wake's bw_rc_hlsl.c (bloodwake-recomp at
 * e631edddfe56d22344e9f696923aeb6d78a9e7ba, MIT licence; see
 * THIRD_PARTY_NOTICES.md). Generic code only.
 *
 * Generates, for one combiner configuration, the shader that does on the GPU
 * what the framework's CPU path does per pixel: the texture stages' fetch by
 * SHADER_STAGE_PROGRAM mode (rc_stage_fetch in nv2a_pb_exec.c), up to eight
 * general combiner stages over the 16-register file, and the final combiner
 * (nv2a_combiner.c, itself after xemu's psh.c). Rule for rule the same, so
 * the GPU back end draws what the CPU rasteriser draws.
 *
 * Known gaps, as in the CPU path: bump-environment and dot-product stage
 * modes read zero, and only texture level 0 is sampled. Colours and fog are
 * interpolated affinely in screen space and fog is clamped per vertex, as
 * the CPU rasteriser does; texture coordinates are perspective-correct.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_hlsl.h"

typedef struct {
    char *p;
    size_t n, cap;
    int overflow;
} Buf;

static void put(Buf *b, const char *fmt, ...)
{
    va_list ap;
    int w;
    if (b->overflow)
        return;
    va_start(ap, fmt);
    w = vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= b->cap - b->n) {
        b->overflow = 1;
        return;
    }
    b->n += (size_t)w;
}

/* read_rgb / read_alpha of nv2a_combiner.c, for one input byte. */
static void put_rgb(Buf *b, const char *var, uint32_t byte)
{
    put(b, "  float3 %s = rd3(R[%u], %d, %u);\n", var, byte & 0xF,
        (byte & 0x10) ? 1 : 0, (byte >> 5) & 7);
}

static void put_alpha(Buf *b, const char *var, uint32_t byte)
{
    put(b, "  float %s = rda(R[%u], %d, %u);\n", var, byte & 0xF,
        (byte & 0x10) ? 1 : 0, (byte >> 5) & 7);
}

size_t gpu_rc_hlsl(const GpuRcKey *k, char *out, size_t cap)
{
    Buf b = { out, 0, cap, 0 };
    uint32_t n = k->control & 0xFF, flags = k->control >> 8;
    int st;
    uint32_t s;

    if (n > 8)
        n = 8;
    put(&b, GPU_RC_CBUFFER);
    /* A stage bound to a six-face cube map is a TextureCube. */
    for (st = 0; st < 4; st++)
        put(&b, "%s t%d : register(t%d);\n", k->tex_cube[st] ? "TextureCube" : "Texture2D", st, st);
    put(&b,
        "SamplerState s0 : register(s0); SamplerState s1 : register(s1);\n"
        "SamplerState s2 : register(s2); SamplerState s3 : register(s3);\n");
    put(&b, "struct PSIn { float4 p : SV_Position; noperspective float4 d0 : COLOR0;\n"
            "  noperspective float4 d1 : COLOR1; noperspective float fog : FOG;\n"
            "  float4 tc0 : TEXCOORD0; float4 tc1 : TEXCOORD1;\n"
            "  float4 tc2 : TEXCOORD2; float4 tc3 : TEXCOORD3; float fogp : FOG1; };\n");
    put(&b,
        /* map_in */
        "float mi(float x, int m) { float p = max(x, 0.0);\n"
        "  if (m == 0) return p; if (m == 1) return 1.0 - saturate(x);\n"
        "  if (m == 2) return 2.0 * p - 1.0; if (m == 3) return -2.0 * p + 1.0;\n"
        "  if (m == 4) return p - 0.5; if (m == 5) return -p + 0.5;\n"
        "  if (m == 6) return x; return -x; }\n"
        "float3 rd3(float4 r, int arep, int m) { float3 v = arep ? r.aaa : r.rgb;\n"
        "  return float3(mi(v.x, m), mi(v.y, m), mi(v.z, m)); }\n"
        "float rda(float4 r, int arep, int m) { return mi(arep ? r.a : r.b, m); }\n"
        /* map_out */
        "float3 mo(float3 x, int m) {\n"
        "  if (m == 0x08) return x - 0.5; if (m == 0x10) return x * 2.0;\n"
        "  if (m == 0x18) return (x - 0.5) * 2.0; if (m == 0x20) return x * 4.0;\n"
        "  if (m == 0x30) return x * 0.5; return x; }\n");
    put(&b,
        "float4 ps_main(PSIn i) : SV_Target {\n"
        "  float4 R[16];\n"
        "  float4 tc[4] = { i.tc0, i.tc1, i.tc2, i.tc3 };\n"
        "  float4 T[4];\n");

    /* rc_stage_fetch, per stage. */
    for (st = 0; st < 4; st++) {
        uint32_t mode = (k->stage_program >> (st * 5)) & 0x1F;
        switch (mode) {
        case 0:
            put(&b, "  T[%d] = float4(0, 0, 0, 1);\n", st);
            break;
        case 1: case 2: case 3:
            if (!k->tex_valid[st]) {
                put(&b, "  T[%d] = float4(1, 1, 1, 1);\n", st);
                break;
            }
            if (mode == 3 && k->tex_cube[st]) {
                /* CUBEMAP: the coordinate is a direction; D3D picks the face
                 * the same way the CPU path's rc_stage_fetch does. */
                put(&b, "  T[%d] = t%d.Sample(s%d, tc[%d].xyz);\n", st, st, st, st);
                break;
            }
            if (mode == 3)          /* CUBEMAP on a 2D texture: face 0, as the CPU path */
                put(&b, "  { float2 uv = tc[%d].xy;\n", st);
            else
                put(&b, "  { float q = tc[%d].w != 0.0 ? tc[%d].w : 1.0;"
                        " float2 uv = tc[%d].xy / q;\n", st, st, st);
            if (!k->tex_normalized[st])
                put(&b, "    uv *= texinfo[%d].xy;\n", st);
            put(&b, "    T[%d] = t%d.Sample(s%d, uv); }\n", st, st, st);
            break;
        case 4:
            put(&b, "  T[%d] = saturate(tc[%d]);\n", st, st);
            break;
        case 5: {
            int j;
            put(&b, "  T[%d] = float4(0, 0, 0, 0);\n", st);
            for (j = 0; j < 4; j++) {
                int ge = (k->clip_plane_mode >> (st * 4 + j)) & 1;
                put(&b, "  if (tc[%d][%d] %s 0.0) discard;\n", st, j, ge ? ">=" : "<");
            }
            break;
        }
        default:                    /* bump-env and dot-product modes: zero */
            put(&b, "  T[%d] = float4(0, 0, 0, 0);\n", st);
            break;
        }
    }

    /* The register file (nv2a_rc_eval's start). */
    put(&b,
        "  R[0] = 0; R[1] = 0; R[2] = 0; R[6] = 0; R[7] = 0;\n"
        "  R[12] = 0; R[13] = 0; R[14] = 0; R[15] = 0;\n"
        "  R[3] = float4(fogc.rgb, i.fog); R[4] = i.d0; R[5] = i.d1;\n"
        "  R[8] = T[0]; R[9] = T[1]; R[10] = T[2]; R[11] = T[3];\n"
        "  R[12].a = %s;\n", (k->stage_program & 0x1F) ? "T[0].a" : "1.0");

    for (s = 0; s < n; s++) {
        uint32_t icw = k->color_icw[s], ocw = k->color_ocw[s];
        uint32_t aicw = k->alpha_icw[s], aocw = k->alpha_ocw[s];
        uint32_t fl = ocw >> 12, afl = aocw >> 12;
        put(&b, "  { /* stage %u */\n", s);
        put(&b, "  R[1] = C0[%u]; R[2] = C1[%u];\n",
            (flags & 0x010) ? s : 0, (flags & 0x100) ? s : 0);
        if (flags & 1)
            put(&b, "  bool mux = R[12].a >= 0.5;\n");
        else
            put(&b, "  bool mux = (((int)(R[12].a * 255.0)) & 1) != 0;\n");
        put_rgb(&b, "A", icw >> 24); put_rgb(&b, "B", icw >> 16);
        put_rgb(&b, "C", icw >> 8);  put_rgb(&b, "D", icw);
        put_alpha(&b, "aA", aicw >> 24); put_alpha(&b, "aB", aicw >> 16);
        put_alpha(&b, "aC", aicw >> 8);  put_alpha(&b, "aD", aicw);
        put(&b, "  float3 pab = %s; float3 pcd = %s;\n",
            (fl & 2) ? "dot(A, B).xxx" : "A * B", (fl & 1) ? "dot(C, D).xxx" : "C * D");
        put(&b, "  float3 sum = %s;\n", (fl & 4) ? "mux ? pcd : pab" : "pab + pcd");
        put(&b, "  float3 ab = clamp(mo(pab, %u), -1.0, 1.0);"
                " float3 cd = clamp(mo(pcd, %u), -1.0, 1.0);"
                " float3 ms = clamp(mo(sum, %u), -1.0, 1.0);\n",
            fl & 0x38, fl & 0x38, fl & 0x38);
        put(&b, "  float apab = aA * aB, apcd = aC * aD;\n");
        put(&b, "  float asum = %s;\n", (afl & 4) ? "mux ? apcd : apab" : "apab + apcd");
        put(&b, "  float aab = clamp(mo(apab.xxx, %u).x, -1.0, 1.0);"
                " float acd = clamp(mo(apcd.xxx, %u).x, -1.0, 1.0);"
                " float ams = clamp(mo(asum.xxx, %u).x, -1.0, 1.0);\n",
            afl & 0x38, afl & 0x38, afl & 0x38);
        if ((ocw >> 4) & 0xF) {
            put(&b, "  R[%u].rgb = ab;\n", (ocw >> 4) & 0xF);
            if (fl & 0x80)
                put(&b, "  R[%u].a = ab.b;\n", (ocw >> 4) & 0xF);
        }
        if (ocw & 0xF) {
            put(&b, "  R[%u].rgb = cd;\n", ocw & 0xF);
            if (fl & 0x40)
                put(&b, "  R[%u].a = cd.b;\n", ocw & 0xF);
        }
        if ((ocw >> 8) & 0xF)
            put(&b, "  R[%u].rgb = ms;\n", (ocw >> 8) & 0xF);
        if ((aocw >> 4) & 0xF)
            put(&b, "  R[%u].a = aab;\n", (aocw >> 4) & 0xF);
        if (aocw & 0xF)
            put(&b, "  R[%u].a = acd;\n", aocw & 0xF);
        if ((aocw >> 8) & 0xF)
            put(&b, "  R[%u].a = ams;\n", (aocw >> 8) & 0xF);
        put(&b, "  R[0] = 0; }\n");
    }

    if (k->final0 || k->final1) {
        uint32_t f0 = k->final0, f1 = k->final1, ff = f1 & 0xFF;
        put(&b, "  { /* final combiner */\n  R[1] = FC0; R[2] = FC1;\n");
        put(&b, "  float3 va = %s; float3 vb = %s;\n",
            (ff & 0x40) ? "1.0 - R[5].rgb" : "R[5].rgb",
            (ff & 0x20) ? "1.0 - R[12].rgb" : "R[12].rgb");
        put(&b, "  R[14] = float4(%s, 0.0);\n", (ff & 0x80) ? "saturate(va + vb)" : "va + vb");
        put_rgb(&b, "E", f1 >> 24);
        put_rgb(&b, "F", f1 >> 16);
        put(&b, "  R[15] = float4(E * F, 0.0);\n");
        put_rgb(&b, "A", f0 >> 24); put_rgb(&b, "B", f0 >> 16);
        put_rgb(&b, "C", f0 >> 8);  put_rgb(&b, "D", f0);
        put_alpha(&b, "G", f1 >> 8);
        put(&b, "  float4 o = float4(D + C * (1.0 - A) + B * A, G); o = saturate(o);\n");
    } else {
        put(&b, "  { float4 o = saturate(R[12]);\n");
    }
    /* alpha_test_pass, on the final alpha as 0..255 */
    put(&b,
        "  if (alpha.z > 0.5) { int v = (int)(o.a * 255.0 + 0.5), r = (int)alpha.y; int f = (int)alpha.x;\n"
        "    bool ok = (f == 7) || (f == 1 && v < r) || (f == 2 && v == r) || (f == 3 && v <= r)\n"
        "      || (f == 4 && v > r) || (f == 5 && v != r) || (f == 6 && v >= r);\n"
        "    if (!ok) discard; }\n"
        "  return o; }\n"
        "}\n");
    if (b.overflow)
        return 0;
    return b.n;
}
