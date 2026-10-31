/*
 * Direct3D 11 render back end (RECOMP_GPU=d3d11).
 *
 * Generic parts adapted from Blood Wake's bw_d3d11.c (bloodwake-recomp at
 * e631edddfe56d22344e9f696923aeb6d78a9e7ba, MIT licence; see
 * THIRD_PARTY_NOTICES.md): surfaces, depth buffers, the texture cache, state
 * objects, the vertex ring and the combiner pixel shaders. The generic
 * physical-resolution sizing and resolve passes are adapted separately in
 * render_scale.c. Its edition, widescreen, other visual effects,
 * texture-restoration and title-specific code are not used.
 *
 * The executor (nv2a_pb_exec.c) decodes the pushbuffer and hands this back
 * end clears, screen-space batches (nv2a_backend.h) and transformed batches
 * with the full pixel-pipeline state (nv2a_backend_ext.h). Rendering is
 * GPU-authoritative:
 *   - one render target per guest colour surface and one D24S8 buffer per
 *     zeta surface; colour and eligible experimental depth buffers are
 *     seeded once from guest memory;
 *   - a texture whose address is a colour surface drawn here samples that
 *     render target directly (render to texture without readback);
 *   - other textures are decoded into a cache whose saved input is compared
 *     on every use; an explicitly enabled approximate mode keeps the older
 *     per-frame and sparse checks for performance experiments;
 *   - blend, depth-stencil, rasteriser and sampler states are cached;
 *   - vertices go through one dynamic ring buffer mapped NO_OVERWRITE.
 *
 * Correctness reference: the CPU rasteriser. The defaults reproduce its
 * rules where they are a choice: texture level 0 only, colours and fog
 * interpolated affinely, the surface clip as the only scissor, depth as the
 * title's screen z over the zeta format's range (SET_CLIP_MIN/MAX are not
 * applied). Known differences: depth is D24 rather than the CPU path's float
 * buffer, and stencil state is honoured here while the CPU path ignores it.
 *
 * Vertex programs run on the GPU (draw_vsh): each program becomes a vertex
 * shader (vsh_hlsl.c) compiled on a worker thread; until it is ready the
 * executor transforms the batch on the CPU. Vertex streams arrive as the
 * title's bytes and are converted by the input layout and the shader.
 *
 * At each flip the presented surface is resolved to guest size and read back
 * into guest memory (the executor's readback hook), so the framework's CPU
 * consumers, frame dumps and frame statistics keep working unchanged.
 *
 * RECOMP_D3D11_WARP=1 uses the WARP software device (tests, no GPU needed).
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d/d3d8_swizzle.h"
#include "kernel.h"
#include "nv2a_backend.h"
#include "nv2a_backend_ext.h"
#include "xbox_memory_layout.h"
#include "video/gpu_present.h"
#include "d3d11_backend.h"
#include "d3d11_device_status.h"
#include "render_scale.h"
#include "rc_hlsl.h"
#include "shader_cache.h"
#include "texture_decode.h"
#include "vsh_hlsl.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define GUEST(va) ((uint8_t *)((uintptr_t)(va) + xbox_GetMemoryOffset()))

static ID3D11Device *s_dev;
static ID3D11DeviceContext *s_ctx;
static int s_failed;
static int s_warp;
static int s_depth_texture;
static int s_stencil_alias;
static int s_direct_present;
static volatile LONG s_device_thread;

void gpu_d3d11_check_device(ID3D11Device *device, HRESULT result, const char *operation)
{
    HRESULT reason = device ? ID3D11Device_GetDeviceRemovedReason(device) : S_OK;
    if (FAILED(reason) || result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET
        || result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR) {
        fprintf(stderr, "[GPU-D3D11] device lost during %s: result 0x%08lX,"
                        " removed reason 0x%08lX; recovery is not implemented,"
                        " stopping rather than completing outstanding GPU work\n",
                operation, (unsigned long)result, (unsigned long)reason);
        fflush(stderr);
        abort();
    }
}

HRESULT gpu_d3d11_get_data(ID3D11Device *device, ID3D11DeviceContext *context,
                           ID3D11Asynchronous *query, void *data, UINT size, UINT flags,
                           const char *operation)
{
    HRESULT hr = ID3D11DeviceContext_GetData(context, query, data, size, flags);
    gpu_d3d11_check_device(device, hr, operation);
    return hr;
}

static int gpu_call_failed(HRESULT result, const char *operation)
{
    if (FAILED(result)) {
        gpu_d3d11_check_device(s_dev, result, operation);
        fprintf(stderr, "[GPU-D3D11] %s failed 0x%08lX\n",
                operation, (unsigned long)result);
        return 1;
    }
    return 0;
}

/* ---- statistics ---------------------------------------------------------- */

static struct {
    unsigned draws, xdraws, tris, uploads, rtt, clears, readbacks, ps_compiles, flips;
    unsigned overlap_rtt;       /* a texture inside a surface but not at its start */
    unsigned vdraws, vraw, vfall, kup, kskip;  /* draw_vsh: drawn, raw streams, CPU, constants */
    unsigned tex_gpu, tex_cpu, tex_bc, tex_direct, tex_pitch;
    unsigned tex_unsupported, tex_unsupported_hits;
    unsigned depth_seeds, depth_samples, depth_snapshots, depth_rejected;
    unsigned stencil_samples, stencil_conversions;
    unsigned guest_uploads, guest_conflicts;
    unsigned present_shared, present_busy;
    uint64_t tex_hashed_bytes;
    double readback_ms;
} s_st;

static unsigned s_q_reads;
static double s_q_wait_ms;
static unsigned s_rep_written, s_rep_flushes;

static double qpc_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart)
        QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static void stats_flip(void)
{
    static double last;
    double now = qpc_ms();
    s_st.flips++;
    if (!last)
        last = now;
    if (now - last >= 10000.0) {
        fprintf(stderr, "[GPU-D3D11] %u flips: %u draws (%u extended, %u vertex programs"
                        " [%u raw], %u back to the CPU), %u triangles, %u uploads,"
                        " %u render-to-texture, %u overlapping, %u clears, %u pixel shaders"
                        " compiled, constants %u uploaded %u skipped, readback %.2f ms/flip,"
                        " %u visibility reads waiting %.1f ms, %u reports written"
                        " asynchronously (%u polls found one pending),"
                        " textures GPU %u CPU %u BC %u direct %u, %u stale pitches,"
                        " unsupported %u (%u cached),"
                        " depth seeds %u samples %u snapshots %u rejected %u,"
                        " stencil aliases %u conversions %u,"
                        " guest uploads %u conflicts %u,"
                        " shared presents %u busy %u,"
                        " %llu texture bytes checked or copied (upper bound)\n",
                s_st.flips, s_st.draws, s_st.xdraws, s_st.vdraws, s_st.vraw, s_st.vfall,
                s_st.tris, s_st.uploads, s_st.rtt, s_st.overlap_rtt, s_st.clears,
                s_st.ps_compiles, s_st.kup, s_st.kskip,
                s_st.readbacks ? s_st.readback_ms / s_st.readbacks : 0.0,
                s_q_reads, s_q_wait_ms, s_rep_written, s_rep_flushes,
                s_st.tex_gpu, s_st.tex_cpu, s_st.tex_bc, s_st.tex_direct, s_st.tex_pitch,
                s_st.tex_unsupported, s_st.tex_unsupported_hits,
                s_st.depth_seeds, s_st.depth_samples, s_st.depth_snapshots, s_st.depth_rejected,
                s_st.stencil_samples, s_st.stencil_conversions,
                s_st.guest_uploads, s_st.guest_conflicts,
                s_st.present_shared, s_st.present_busy,
                (unsigned long long)s_st.tex_hashed_bytes);
        s_q_reads = 0;
        s_q_wait_ms = 0.0;
        s_rep_written = s_rep_flushes = 0;
        memset(&s_st, 0, sizeof s_st);
        last = now;
    }
}

/* ---- shaders ------------------------------------------------------------- */

#define SHADER_COMPILE_FLAGS D3DCOMPILE_OPTIMIZATION_LEVEL3

static int compile(const char *src, size_t len, const char *name, const char *entry,
                   const char *target, ID3DBlob **out)
{
    ID3DBlob *err = NULL;
    HRESULT hr = D3DCompile(src, len, name, NULL, NULL, entry, target,
                            SHADER_COMPILE_FLAGS, 0, out, &err);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] shader %s/%s failed: %s\n", name, entry,
                err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        if (err)
            ID3D10Blob_Release(err);
        return 0;
    }
    if (err)
        ID3D10Blob_Release(err);
    return 1;
}

/* Screen-space batches (base interface): stage 0 times the diffuse colour,
 * as the CPU path's raster_indexed. */
static const char s_base_hlsl[] =
GPU_WINDOW_CLIP_HLSL
GPU_SHADOW_HLSL
"cbuffer B : register(b0) { float4 surf; float4 texinfo; float4 alpha; float4 depth; };\n"
"struct VSIn { float4 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD; };\n"
"struct PSIn { float4 p : SV_Position; float4 c : COLOR; float2 uv : TEXCOORD; };\n"
"PSIn vs_main(VSIn i) {\n"
"  PSIn o;\n"
"  float w = (i.p.w > 0.0) ? 1.0 / i.p.w : 1.0;\n"
"  float x = i.p.x * surf.z * 2.0 - 1.0;\n"
"  float y = 1.0 - i.p.y * surf.w * 2.0;\n"
"  float z = (i.p.z - depth.x) * depth.y;\n"
"  o.p = float4(x * w, y * w, z * w, w);\n"
"  o.c = i.c;\n"
"  o.uv = i.uv * texinfo.xy;\n"
"  return o;\n"
"}\n"
"Texture2D t0 : register(t0);\n"
"SamplerState s0 : register(s0);\n"
"float4 ps_main(PSIn i) : SV_Target {\n"
"  if (!window_allows(i.p.xy)) discard;\n"
"  float4 c = i.c;\n"
"  if (texinfo.z > 0.5) {\n"
"    float4 t = t0.Sample(s0, i.uv);\n"
"    if (texinfo.w > 0.5) t = shadow_compare(t.r * 16777215.0, 0.0, (uint)texinfo.w - 1).xxxx;\n"
"    c *= t;\n"
"  }\n"
"  if (alpha.z > 0.5) { int v = (int)(c.a * 255.0 + 0.5), r = (int)alpha.y; int f = (int)alpha.x;\n"
"    bool ok = (f == 7) || (f == 1 && v < r) || (f == 2 && v == r) || (f == 3 && v <= r)\n"
"      || (f == 4 && v > r) || (f == 5 && v != r) || (f == 6 && v >= r);\n"
"    if (!ok) discard; }\n"
"  return c;\n"
"}\n";

typedef struct {
    float surf[4], tex[4], alpha[4], depth[4];
} BaseConsts;

/* Transformed batches (extended interface): the executor's vertices, in
 * surface pixels with 1/w, mapped to an affine image of clip space so the
 * texture coordinates interpolate perspective-correctly. */
static const char s_xvs_hlsl[] =
GPU_RC_CBUFFER
"struct VSIn { float4 p : POSITION; float4 d0 : COLOR0; float4 d1 : COLOR1;\n"
"  float4 fog : TEXCOORD4; float4 tc0 : TEXCOORD0; float4 tc1 : TEXCOORD1;\n"
"  float4 tc2 : TEXCOORD2; float4 tc3 : TEXCOORD3; };\n"
"struct VSOut { float4 p : SV_Position; noperspective float4 d0 : COLOR0;\n"
"  noperspective float4 d1 : COLOR1; noperspective float fog : FOG;\n"
"  float4 tc0 : TEXCOORD0; float4 tc1 : TEXCOORD1;\n"
"  float4 tc2 : TEXCOORD2; float4 tc3 : TEXCOORD3; float fogp : FOG1; };\n"
"VSOut vs_main(VSIn i) {\n"
"  VSOut o;\n"
"  float w = (i.p.w > 0.0) ? 1.0 / i.p.w : 1.0;\n"
"  float x = i.p.x * surf.z * 2.0 - 1.0;\n"
"  float y = 1.0 - i.p.y * surf.w * 2.0;\n"
"  float zr = (i.p.z - depth.x) * depth.y;\n"
"  float z = depth.z > 0.5 ? saturate(zr) : zr;\n"
"  o.p = float4(x * w, y * w, z * w, w);\n"
"  o.d0 = i.d0; o.d1 = i.d1; o.fog = i.fog.x; o.fogp = i.fog.x;\n"
"  o.tc0 = i.tc0; o.tc1 = i.tc1; o.tc2 = i.tc2; o.tc3 = i.tc3;\n"
"  return o;\n"
"}\n";

static ID3D11VertexShader *s_base_vs, *s_xvs;
static ID3D11PixelShader *s_base_ps;
static ID3D11InputLayout *s_base_layout, *s_xlayout;
static ID3D11Buffer *s_base_cb, *s_xcb, *s_window_cb;
static ID3D11RasterizerState *s_rs;
static ID3D11RasterizerState *s_rs_cull[2][2];  /* [cull back][front counter-clockwise] */

#define MAX_PS 1024
typedef struct { GpuRcKey key; ID3D11PixelShader *ps; } PsEntry;
static PsEntry *s_ps;
static int s_nps;

static ID3D11PixelShader *get_rc_shader(const GpuRcKey *k)
{
    static char src[64 * 1024];
    static const char name[] = "rc", entry[] = "ps_main", target[] = "ps_5_0";
    size_t len, cached_size = 0;
    void *cached = NULL;
    ID3DBlob *blob = NULL;
    ID3D11PixelShader *ps = NULL;
    HRESULT hr;
    double started;
    int i;

    for (i = 0; i < s_nps; i++)
        if (!memcmp(&s_ps[i].key, k, sizeof *k))
            return s_ps[i].ps;
    if (s_nps == MAX_PS)
        return NULL;
    len = gpu_rc_hlsl(k, src, sizeof src);
    if (!len)
        return NULL;
    started = qpc_ms();
    if (gpu_pscache_load(src, len, name, entry, target, SHADER_COMPILE_FLAGS,
                         &cached, &cached_size)) {
        hr = ID3D11Device_CreatePixelShader(s_dev, cached, cached_size, NULL, &ps);
        free(cached);
        if (FAILED(hr) || !ps) {
            gpu_d3d11_check_device(s_dev, hr, "creating cached pixel shader");
            fprintf(stderr, "[GPU-PSCACHE] cached shader rejected 0x%08lX; recompiling\n",
                    (unsigned long)hr);
            if (ps)
                ID3D11PixelShader_Release(ps);
            ps = NULL;
        } else {
            fprintf(stderr, "[GPU-PSCACHE] hit in %.3f ms\n", qpc_ms() - started);
        }
    }
    if (!ps) {
        started = qpc_ms();
        if (!compile(src, len, name, entry, target, &blob))
            return NULL;
        hr = ID3D11Device_CreatePixelShader(s_dev, ID3D10Blob_GetBufferPointer(blob),
                                            ID3D10Blob_GetBufferSize(blob), NULL, &ps);
        if (FAILED(hr) || !ps) {
            gpu_d3d11_check_device(s_dev, hr, "creating pixel shader");
            fprintf(stderr, "[GPU-D3D11] CreatePixelShader(rc) failed 0x%08lX\n",
                    (unsigned long)hr);
            ID3D10Blob_Release(blob);
            return NULL;
        }
        s_st.ps_compiles++;
        if (gpu_pscache_enabled()) {
            fprintf(stderr, "[GPU-PSCACHE] compiled in %.3f ms\n", qpc_ms() - started);
            gpu_pscache_store(src, len, name, entry, target, SHADER_COMPILE_FLAGS,
                              ID3D10Blob_GetBufferPointer(blob),
                              ID3D10Blob_GetBufferSize(blob));
        }
        ID3D10Blob_Release(blob);
    }
    s_ps[s_nps].key = *k;
    s_ps[s_nps].ps = ps;
    s_nps++;
    return ps;
}

/* ---- device -------------------------------------------------------------- */

static int ensure_device(void)
{
    static const D3D11_INPUT_ELEMENT_DESC base_il[3] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_B8G8R8A8_UNORM,     0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    /* Nv2aXVertex: x y z rhw | d0 | d1 | fog pad[3] | tc[4]. */
    static const D3D11_INPUT_ELEMENT_DESC x_il[8] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
    ID3DBlob *vsb = NULL, *psb = NULL, *xvsb = NULL;
    D3D11_BUFFER_DESC bd;
    D3D11_RASTERIZER_DESC rd;
    HRESULT hr;

    if (s_dev)
        gpu_d3d11_check_device(s_dev, S_OK, "starting GPU work");
    if (s_failed)
        return 0;
    if (s_dev)
        return 1;
    s_failed = 1;               /* until everything below has worked */
    hr = D3D11CreateDevice(NULL, s_warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                           NULL, D3D11_CREATE_DEVICE_SINGLETHREADED, &fl, 1,
                           D3D11_SDK_VERSION, &s_dev, &got, &s_ctx);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(NULL, hr, "creating D3D11 device");
        fprintf(stderr, "[GPU-D3D11] D3D11CreateDevice failed 0x%08lX\n", (unsigned long)hr);
        return 0;
    }
    InterlockedExchange(&s_device_thread, (LONG)GetCurrentThreadId());
    if (!compile(s_base_hlsl, sizeof s_base_hlsl - 1, "base", "vs_main", "vs_5_0", &vsb)
        || !compile(s_base_hlsl, sizeof s_base_hlsl - 1, "base", "ps_main", "ps_5_0", &psb)
        || !compile(s_xvs_hlsl, sizeof s_xvs_hlsl - 1, "xvs", "vs_main", "vs_5_0", &xvsb)) {
        if (vsb) ID3D10Blob_Release(vsb);
        if (psb) ID3D10Blob_Release(psb);
        return 0;
    }
    gpu_call_failed(ID3D11Device_CreateVertexShader(s_dev, ID3D10Blob_GetBufferPointer(vsb),
                                                    ID3D10Blob_GetBufferSize(vsb), NULL, &s_base_vs),
                    "creating base vertex shader");
    gpu_call_failed(ID3D11Device_CreatePixelShader(s_dev, ID3D10Blob_GetBufferPointer(psb),
                                                   ID3D10Blob_GetBufferSize(psb), NULL, &s_base_ps),
                    "creating base pixel shader");
    gpu_call_failed(ID3D11Device_CreateInputLayout(s_dev, base_il, 3, ID3D10Blob_GetBufferPointer(vsb),
                                                   ID3D10Blob_GetBufferSize(vsb), &s_base_layout),
                    "creating base input layout");
    gpu_call_failed(ID3D11Device_CreateVertexShader(s_dev, ID3D10Blob_GetBufferPointer(xvsb),
                                                    ID3D10Blob_GetBufferSize(xvsb), NULL, &s_xvs),
                    "creating extended vertex shader");
    gpu_call_failed(ID3D11Device_CreateInputLayout(s_dev, x_il, 8, ID3D10Blob_GetBufferPointer(xvsb),
                                                   ID3D10Blob_GetBufferSize(xvsb), &s_xlayout),
                    "creating extended input layout");
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);
    ID3D10Blob_Release(xvsb);

    memset(&bd, 0, sizeof bd);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = sizeof(BaseConsts);
    gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_base_cb),
                    "creating base constants");
    bd.ByteWidth = (sizeof(GpuRcConsts) + 15) & ~15u;
    gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_xcb),
                    "creating extended constants");
    bd.ByteWidth = 9 * 16;
    gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_window_cb),
                    "creating window clip constants");

    memset(&rd, 0, sizeof rd);
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;      /* the executor culls, as the CPU path */
    rd.DepthClipEnable = FALSE;         /* the executor clips at the near plane */
    rd.ScissorEnable = TRUE;
    gpu_call_failed(ID3D11Device_CreateRasterizerState(s_dev, &rd, &s_rs),
                    "creating rasterizer state");
    {
        int back, ccw;
        for (back = 0; back < 2; back++)
            for (ccw = 0; ccw < 2; ccw++) {
                rd.CullMode = back ? D3D11_CULL_BACK : D3D11_CULL_FRONT;
                rd.FrontCounterClockwise = ccw ? TRUE : FALSE;
                gpu_call_failed(ID3D11Device_CreateRasterizerState(s_dev, &rd,
                                                                   &s_rs_cull[back][ccw]),
                                "creating cull state");
            }
        rd.CullMode = D3D11_CULL_NONE;
        rd.FrontCounterClockwise = FALSE;
    }

    s_ps = (PsEntry *)calloc(MAX_PS, sizeof *s_ps);
    if (!s_base_vs || !s_base_ps || !s_base_layout || !s_xvs || !s_xlayout || !s_base_cb
        || !s_xcb || !s_window_cb || !s_rs || !s_ps)
        return 0;
    if (gpu_scale_requested() && !gpu_scale_gpu_init(s_dev, s_ctx))
        return 0;
    s_failed = 0;
    fprintf(stderr, "[GPU-D3D11] %s device ready (feature level 0x%x)\n",
            s_warp ? "WARP" : "hardware", (unsigned)got);
    return 1;
}

/* ---- surfaces ------------------------------------------------------------ */

#define MAX_SURF 32
typedef struct {
    uint32_t va, pitch, bpp, w, h, pw, ph, aa_sx, aa_sy;
    unsigned scale_epoch;
    ID3D11Texture2D *tex;
    ID3D11RenderTargetView *rtv;
    ID3D11ShaderResourceView *srv;
    ID3D11Texture2D *guest_tex;
    ID3D11ShaderResourceView *guest_srv;
    uint8_t *guest_shadow;
    size_t guest_shadow_bytes;
    unsigned guest_check_gen;
    int gpu_dirty;
} Surf;

typedef struct {
    uint32_t va, w, h, pitch, format, surface_type, initialized, recreated;
    unsigned scale_epoch;
    uint64_t revision, packed_revision;
    int packed_valid;
    ID3D11Texture2D *tex, *snapshot;
    ID3D11DepthStencilView *dsv;
    ID3D11ShaderResourceView *srv, *snapshot_srv;
    ID3D11ShaderResourceView *stencil_srv, *snapshot_stencil_srv, *packed_srv;
    ID3D11Texture2D *packed;
    ID3D11UnorderedAccessView *packed_uav;
} Depth;

static Surf s_surf[MAX_SURF];
static int s_nsurf;
static Depth s_depth[MAX_SURF];
static int s_ndepth;
static Surf *s_cur;
static Depth *s_cur_depth;
static uint32_t s_gen = 1;          /* frame number */

static int depth_views_enabled(void)
{
    return s_depth_texture || s_stencil_alias;
}

static void release_depth(Depth *d)
{
    if (d->packed_uav) ID3D11UnorderedAccessView_Release(d->packed_uav);
    if (d->packed_srv) ID3D11ShaderResourceView_Release(d->packed_srv);
    if (d->packed) ID3D11Texture2D_Release(d->packed);
    if (d->snapshot_stencil_srv) ID3D11ShaderResourceView_Release(d->snapshot_stencil_srv);
    if (d->stencil_srv) ID3D11ShaderResourceView_Release(d->stencil_srv);
    if (d->snapshot_srv) ID3D11ShaderResourceView_Release(d->snapshot_srv);
    if (d->snapshot) ID3D11Texture2D_Release(d->snapshot);
    if (d->srv) ID3D11ShaderResourceView_Release(d->srv);
    if (d->dsv) ID3D11DepthStencilView_Release(d->dsv);
    if (d->tex) ID3D11Texture2D_Release(d->tex);
}

static DXGI_FORMAT surf_format(uint32_t bpp)
{
    return bpp == 2 ? DXGI_FORMAT_B5G6R5_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
}

static void release_surf(Surf *s)
{
    if (s->srv) ID3D11ShaderResourceView_Release(s->srv);
    if (s->rtv) ID3D11RenderTargetView_Release(s->rtv);
    if (s->tex) ID3D11Texture2D_Release(s->tex);
    s->srv = NULL; s->rtv = NULL; s->tex = NULL;
}

static void release_surf_guest(Surf *s)
{
    if (s->guest_srv) ID3D11ShaderResourceView_Release(s->guest_srv);
    if (s->guest_tex) ID3D11Texture2D_Release(s->guest_tex);
    free(s->guest_shadow);
    s->guest_srv = NULL;
    s->guest_tex = NULL;
    s->guest_shadow = NULL;
    s->guest_shadow_bytes = 0;
    s->guest_check_gen = 0;
    s->gpu_dirty = 0;
}

static size_t surf_guest_row(const Surf *s)
{
    size_t row = (size_t)s->w * s->bpp;
    return row <= s->pitch ? row : s->pitch;
}

static int ensure_guest_shadow(Surf *s)
{
    size_t row = surf_guest_row(s);
    size_t bytes;
    uint8_t *shadow;

    if (!row || !s->h || s->h > SIZE_MAX / row)
        return 0;
    bytes = row * s->h;
    if (s->guest_shadow && s->guest_shadow_bytes == bytes)
        return 1;
    shadow = (uint8_t *)realloc(s->guest_shadow, bytes);
    if (!shadow) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate %zu-byte guest color shadow for %08X\n",
                bytes, s->va);
        return 0;
    }
    s->guest_shadow = shadow;
    s->guest_shadow_bytes = bytes;
    return 1;
}

static int snapshot_guest_surface(Surf *s)
{
    const uint8_t *guest = GUEST(s->va);
    size_t row = surf_guest_row(s);
    uint32_t y;

    if (!ensure_guest_shadow(s))
        return 0;
    for (y = 0; y < s->h; ++y)
        memcpy(s->guest_shadow + (size_t)y * row,
               guest + (size_t)y * s->pitch, row);
    return 1;
}

static int guest_surface_changed(const Surf *s)
{
    const uint8_t *guest = GUEST(s->va);
    size_t row = surf_guest_row(s);
    uint32_t y;

    if (!s->guest_shadow || s->guest_shadow_bytes != row * s->h)
        return 1;
    for (y = 0; y < s->h; ++y)
        if (memcmp(s->guest_shadow + (size_t)y * row,
                   guest + (size_t)y * s->pitch, row))
            return 1;
    return 0;
}

/* Seeded from guest memory: whatever the title (or the boot) left there.
 * After this the GPU copy is authoritative. */
static int create_surf(Surf *s, ID3D11ShaderResourceView *gpu_seed,
                       uint32_t gpu_seed_width, uint32_t gpu_seed_height)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA init, *pinit = NULL;
    ID3D11Texture2D *seed = NULL;
    ID3D11ShaderResourceView *seed_srv = NULL;
    HRESULT hr;

    memset(&td, 0, sizeof td);
    td.Width = s->pw;
    td.Height = s->ph;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = surf_format(s->bpp);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (s->pw == s->w && s->ph == s->h && s->pitch >= s->w * s->bpp) {
        init.pSysMem = GUEST(s->va);
        init.SysMemPitch = s->pitch;
        init.SysMemSlicePitch = 0;
        pinit = &init;
    }
    if (gpu_call_failed(ID3D11Device_CreateTexture2D(s_dev, &td, pinit, &s->tex),
                        "creating color surface"))
        return 0;
    if (gpu_call_failed(ID3D11Device_CreateRenderTargetView(s_dev, (ID3D11Resource *)s->tex,
                                                            NULL, &s->rtv),
                        "creating color render-target view")) {
        release_surf(s);
        return 0;
    }
    if (gpu_call_failed(ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)s->tex,
                                                              NULL, &s->srv),
                        "creating color shader-resource view")) {
        release_surf(s);
        return 0;
    }
    if (gpu_seed) {
        enum GpuScaleBlitMode mode =
            gpu_seed_width > s->pw || gpu_seed_height > s->ph ? GPU_SCALE_BLIT_BOX
                                                               : GPU_SCALE_BLIT_BILINEAR;
        if (!gpu_scale_blit(gpu_seed, gpu_seed_width, gpu_seed_height,
                            s->rtv, s->pw, s->ph, mode)) {
            fprintf(stderr, "[GPU-SCALE] migrating scaled color surface %08X failed\n", s->va);
            release_surf(s);
            return 0;
        }
    } else if ((s->pw != s->w || s->ph != s->h) && s->pitch >= s->w * s->bpp) {
        td.Width = s->w;
        td.Height = s->h;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        init.pSysMem = GUEST(s->va);
        init.SysMemPitch = s->pitch;
        init.SysMemSlicePitch = 0;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, &init, &seed);
        if (SUCCEEDED(hr))
            hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)seed,
                                                       NULL, &seed_srv);
        if (FAILED(hr) || !gpu_scale_blit(seed_srv, s->w, s->h, s->rtv, s->pw, s->ph,
                                          GPU_SCALE_BLIT_BILINEAR)) {
            gpu_d3d11_check_device(s_dev, hr, "seeding scaled color surface");
            fprintf(stderr, "[GPU-SCALE] scaled color seed for %08X failed 0x%08lX\n",
                    s->va, (unsigned long)hr);
            if (seed_srv) ID3D11ShaderResourceView_Release(seed_srv);
            if (seed) ID3D11Texture2D_Release(seed);
            release_surf(s);
            return 0;
        }
        ID3D11ShaderResourceView_Release(seed_srv);
        ID3D11Texture2D_Release(seed);
    }
    return s->rtv && s->srv;
}

static Surf *find_surf(uint32_t va)
{
    int i;
    for (i = 0; i < s_nsurf; i++)
        if (s_surf[i].va == va && s_surf[i].tex)
            return &s_surf[i];
    return NULL;
}

/* A surface's width is its pitch in pixels where that is at least the clip
 * width, so a texture reading the same memory with that pitch sees the same
 * layout. */
static Surf *get_surf(const Nv2aSurface *in)
{
    uint32_t bpp = in->bytes_per_pixel ? in->bytes_per_pixel : 4;
    uint32_t w = in->pitch && in->pitch / bpp >= in->width ? in->pitch / bpp : in->width;
    uint32_t h = in->height;
    uint32_t aa_sx = in->aa_sx > 1 ? in->aa_sx : 1;
    uint32_t aa_sy = in->aa_sy > 1 ? in->aa_sy : 1;
    uint32_t pw, ph;
    Surf *s = NULL;
    int i;

    for (i = 0; i < s_nsurf; i++)
        if (s_surf[i].va == in->color_va) { s = &s_surf[i]; break; }
    if (s) {
        w = w > s->w ? w : s->w;
        h = h > s->h ? h : s->h;
    }
    if (!gpu_scale_surface_size(w, h, aa_sx, aa_sy, &pw, &ph))
        return NULL;
    if (s && (s->bpp != bpp || s->w != w || s->h != h || s->pw != pw || s->ph != ph ||
              s->pitch != in->pitch || !s->tex)) {
        Surf replacement = *s;
        int guest_layout_changed =
            s->bpp != bpp || s->w != w || s->h != h || s->pitch != in->pitch;
        replacement.bpp = bpp;
        replacement.w = w;
        replacement.h = h;
        replacement.pw = pw;
        replacement.ph = ph;
        replacement.aa_sx = aa_sx;
        replacement.aa_sy = aa_sy;
        replacement.scale_epoch = gpu_scale_epoch();
        replacement.pitch = in->pitch;
        replacement.tex = NULL;
        replacement.rtv = NULL;
        replacement.srv = NULL;
        if (guest_layout_changed) {
            replacement.guest_tex = NULL;
            replacement.guest_srv = NULL;
            replacement.guest_shadow = NULL;
            replacement.guest_shadow_bytes = 0;
            replacement.guest_check_gen = 0;
            replacement.gpu_dirty = 0;
        }
        if (!create_surf(&replacement, gpu_scale_requested() ? s->srv : NULL,
                         s->pw, s->ph) ||
            (guest_layout_changed && !snapshot_guest_surface(&replacement))) {
            release_surf(&replacement);
            if (guest_layout_changed)
                release_surf_guest(&replacement);
            return NULL;
        }
        if (s_cur == s)
            s_cur = NULL;
        release_surf(s);
        if (guest_layout_changed)
            release_surf_guest(s);
        *s = replacement;
    }
    if (s) {
        s->aa_sx = aa_sx;
        s->aa_sy = aa_sy;
        s->scale_epoch = gpu_scale_epoch();
    }
    if (!s) {
        if (s_nsurf == MAX_SURF)
            return NULL;
        s = &s_surf[s_nsurf++];
        memset(s, 0, sizeof *s);
        s->va = in->color_va;
        s->pitch = in->pitch;
        s->bpp = bpp;
        s->w = w;
        s->h = h;
        s->pw = pw;
        s->ph = ph;
        s->aa_sx = aa_sx;
        s->aa_sy = aa_sy;
        s->scale_epoch = gpu_scale_epoch();
        if (!create_surf(s, NULL, 0, 0) || !snapshot_guest_surface(s)) {
            release_surf(s);
            release_surf_guest(s);
            s_nsurf--;
            return NULL;
        }
    }
    return s;
}

static int readable_guest_range(uint32_t va, uint64_t bytes)
{
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    uint64_t guest_end = (uint64_t)va + bytes;
    uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset(), p, end;

    if (!va || !bytes || bytes > XBOX_CONTIG_SIZE ||
        !(guest_end <= XBOX_CONTIG_SIZE ||
          (va >= XBOX_CONTIG_BASE && guest_end <= (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE)) ||
        offset > UINTPTR_MAX - va)
        return 0;
    p = offset + va;
    if (bytes > UINTPTR_MAX - p)
        return 0;
    end = p + (uintptr_t)bytes;
    while (p < end) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t base, available;
        if (VirtualQuery((const void *)p, &info, sizeof info) != sizeof info ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & readable))
            return 0;
        base = (uintptr_t)info.BaseAddress;
        if (p < base || p - base >= info.RegionSize)
            return 0;
        available = info.RegionSize - (p - base);
        p += available < end - p ? available : end - p;
    }
    return 1;
}

/* Xbox Z24S8 stores depth in the high 24 bits; D3D11 R24G8 stores it in
 * the low 24. Seed before any GPU depth writes, not from stale guest bytes
 * when an existing GPU surface must be recreated. */
static uint32_t *depth_seed(uint32_t va, uint32_t w, uint32_t h, uint32_t pitch,
                            uint32_t format, uint32_t surface_type)
{
    uint64_t row = (uint64_t)w * 4, span;
    uint32_t *seed, x, y;
    if (!w || !h || format != 2 || surface_type != 1 || pitch < row)
        return NULL;
    span = (uint64_t)(h - 1) * pitch + row;
    if (!readable_guest_range(va, span))
        return NULL;
    seed = (uint32_t *)malloc((size_t)row * h);
    if (!seed) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate depth seed for surface %08X\n", va);
        return NULL;
    }
    for (y = 0; y < h; y++) {
        const uint8_t *src = GUEST(va) + (size_t)y * pitch;
        for (x = 0; x < w; x++) {
            uint32_t value;
            memcpy(&value, src + (size_t)x * 4, sizeof value);
            seed[(size_t)y * w + x] = (value >> 8) | (value << 24);
        }
    }
    return seed;
}

static Depth *get_depth(uint32_t va, uint32_t w, uint32_t h)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA init;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    Depth *d = NULL;
    uint32_t *seed = NULL;
    uint32_t pitch = nv2a_backend_reg(0x020C) >> 16;
    uint32_t surface_format = nv2a_backend_reg(0x0208);
    uint32_t format = (surface_format >> 4) & 0xF;
    uint32_t surface_type = (surface_format >> 8) & 0xF;
    int existing;
    HRESULT hr;
    int i;

    if (!va)
        return NULL;
    for (i = 0; i < s_ndepth; i++)
        if (s_depth[i].va == va) { d = &s_depth[i]; break; }
    existing = d != NULL;
    if (d && d->scale_epoch == gpu_scale_epoch() && d->w >= w && d->h >= h &&
        (!depth_views_enabled() || (d->pitch == pitch && d->format == format &&
                                   d->surface_type == surface_type)))
        return d;
    if (d) {
        if (s_cur_depth == d)
            s_cur_depth = NULL;
        if (d->scale_epoch == gpu_scale_epoch() &&
            (!depth_views_enabled() || (d->pitch == pitch && d->format == format &&
                                       d->surface_type == surface_type))) {
            w = w > d->w ? w : d->w;
            h = h > d->h ? h : d->h;
        }
        release_depth(d);
    } else {
        if (s_ndepth == MAX_SURF)
            return NULL;
        d = &s_depth[s_ndepth++];
    }
    memset(d, 0, sizeof *d);
    d->va = va; d->w = w; d->h = h; d->pitch = pitch; d->format = format;
    d->surface_type = surface_type; d->recreated = existing;
    d->scale_epoch = gpu_scale_epoch();
    memset(&td, 0, sizeof td);
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = depth_views_enabled() ? DXGI_FORMAT_R24G8_TYPELESS
                                      : DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL |
                   (depth_views_enabled() ? D3D11_BIND_SHADER_RESOURCE : 0);
    if (depth_views_enabled() && !existing)
        seed = depth_seed(va, w, h, pitch, format, surface_type);
    memset(&init, 0, sizeof init);
    init.pSysMem = seed;
    init.SysMemPitch = w * 4;
    hr = ID3D11Device_CreateTexture2D(s_dev, &td, seed ? &init : NULL, &d->tex);
    if (FAILED(hr) && seed) {
        gpu_d3d11_check_device(s_dev, hr, "uploading depth seed");
        fprintf(stderr, "[GPU-D3D11] depth seed upload for %08X failed 0x%08lX;"
                        " sampling will require a GPU depth clear\n", va, (unsigned long)hr);
        free(seed);
        seed = NULL;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &d->tex);
    }
    if (FAILED(hr))
        goto failed;
    if (seed) {
        d->initialized = 3;
        s_st.depth_seeds++;
        free(seed);
        seed = NULL;
    }
    memset(&dv, 0, sizeof dv);
    dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = ID3D11Device_CreateDepthStencilView(s_dev, (ID3D11Resource *)d->tex, &dv, &d->dsv);
    if (FAILED(hr))
        goto failed;
    if (depth_views_enabled()) {
        memset(&sv, 0, sizeof sv);
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)d->tex, &sv, &d->srv);
        if (FAILED(hr))
            goto failed;
    }
    return d;

failed:
    gpu_d3d11_check_device(s_dev, hr, "creating depth resources");
    free(seed);
    fprintf(stderr, "[GPU-D3D11] depth surface creation failed 0x%08lX\n", (unsigned long)hr);
    release_depth(d);
    memset(d, 0, sizeof *d);
    return NULL;
}

static Depth *check_depth_texture(const Nv2aTexture *t, int cube, unsigned levels,
                                  unsigned initialized)
{
    static int warned;
    Depth *d = NULL;
    int i;

    for (i = 0; i < s_ndepth; i++)
        if (s_depth[i].va == t->offset) { d = &s_depth[i]; break; }
    if (!d || !d->srv || cube || levels > 1 || d->format != 2 || d->surface_type != 1 ||
        t->width != d->w || t->height != d->h || t->pitch != d->pitch ||
        t->pitch < t->width * 4) {
        s_st.depth_rejected++;
        if (!warned++) {
            fprintf(stderr, "[GPU-D3D11] depth texture alias rejected: va %08X %ux%u"
                            " pitch %u levels %u cube %d; surface %s %ux%u pitch %u"
                            " format %u type %u\n",
                    t->offset, t->width, t->height, t->pitch, levels, cube,
                    d ? "tracked" : "missing", d ? d->w : 0, d ? d->h : 0,
                    d ? d->pitch : 0, d ? d->format : 0, d ? d->surface_type : 0);
        }
        return NULL;
    }
    if ((d->initialized & initialized) != initialized) {
        static int warned_uninitialized;
        s_st.depth_rejected++;
        if (!warned_uninitialized++)
            fprintf(stderr, "[GPU-D3D11] depth texture alias rejected: surface %08X"
                            " has not been initialized from guest memory or a GPU depth clear"
                            " (required planes %u, initialized %u; %ux%u pitch %u,"
                            " type %u, recreated %u)\n",
                    d->va, initialized, d->initialized, d->w, d->h, d->pitch,
                    d->surface_type, d->recreated);
        return NULL;
    }
    return d;
}

static ID3D11ShaderResourceView *depth_view(Depth *d)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    HRESULT hr;

    s_st.depth_samples++;
    if (d != s_cur_depth)
        return d->srv;
    if (!d->snapshot) {
        ID3D11Texture2D_GetDesc(d->tex, &td);
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &d->snapshot);
        if (FAILED(hr))
            goto failed;
        memset(&sv, 0, sizeof sv);
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)d->snapshot,
                                                   &sv, &d->snapshot_srv);
        if (FAILED(hr))
            goto failed;
    }
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &s_cur->rtv, NULL);
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)d->snapshot,
                                     (ID3D11Resource *)d->tex);
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &s_cur->rtv, d->dsv);
    s_st.depth_snapshots++;
    return d->snapshot_srv;

failed:
    gpu_d3d11_check_device(s_dev, hr, "creating depth snapshot");
    fprintf(stderr, "[GPU-D3D11] depth snapshot creation failed 0x%08lX\n", (unsigned long)hr);
    if (d->snapshot_srv) ID3D11ShaderResourceView_Release(d->snapshot_srv);
    if (d->snapshot) ID3D11Texture2D_Release(d->snapshot);
    d->snapshot = NULL;
    d->snapshot_srv = NULL;
    return NULL;
}

static ID3D11ShaderResourceView *get_depth_texture(const Nv2aTexture *t, int cube,
                                                    unsigned levels)
{
    Depth *d = check_depth_texture(t, cube, levels, 1);
    return d ? depth_view(d) : NULL;
}

static Depth *overlapping_depth(const Nv2aTexture *t, int cube)
{
    uint64_t start = t->offset;
    uint64_t bytes = gpu_texture_span(t, cube ? 6 : 1);
    uint64_t end = start + (bytes ? bytes : 1);
    int i;

    for (i = 0; i < s_ndepth; i++) {
        Depth *d = &s_depth[i];
        uint64_t depth_end = (uint64_t)d->va + (uint64_t)d->pitch * d->h;
        if (d->tex && d->pitch && start < depth_end && d->va < end)
            return d;
    }
    return NULL;
}

static int required_depth_alias(const Nv2aTexture *t, int cube)
{
    return (s_depth_texture && t->color == 0x2E) ||
           (s_stencil_alias && t->color == 0x41 && overlapping_depth(t, cube));
}

static const char s_depth_packed_hlsl[] =
    "Texture2D<float> depth_tex : register(t0);\n"
    "Texture2D<uint2> stencil_tex : register(t1);\n"
    "RWTexture2D<float4> packed_tex : register(u0);\n"
    "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
    "  uint w, h; packed_tex.GetDimensions(w, h);\n"
    "  if (id.x >= w || id.y >= h) return;\n"
    "  int3 p = int3(id.xy, 0);\n"
    "  uint z = (uint)round(saturate(depth_tex.Load(p)) * 16777215.0f);\n"
    "  uint s = stencil_tex.Load(p).g;\n"
    "  packed_tex[id.xy] = float4((z >> 16) & 255u, (z >> 8) & 255u,"
    " z & 255u, s) / 255.0f;\n"
    "}\n";

static ID3D11ComputeShader *s_depth_packed_cs;
static int s_depth_packed_failed;

static int prepare_packed_depth(Depth *d)
{
    D3D11_TEXTURE2D_DESC td;
    ID3DBlob *code = NULL;
    HRESULT hr;

    if (!s_depth_packed_cs && !s_depth_packed_failed) {
        if (!compile(s_depth_packed_hlsl, sizeof s_depth_packed_hlsl - 1,
                     "depth_packed", "main", "cs_5_0", &code)) {
            s_depth_packed_failed = 1;
            return 0;
        }
        hr = ID3D11Device_CreateComputeShader(s_dev, ID3D10Blob_GetBufferPointer(code),
                                               ID3D10Blob_GetBufferSize(code), NULL,
                                               &s_depth_packed_cs);
        ID3D10Blob_Release(code);
        if (FAILED(hr)) {
            gpu_d3d11_check_device(s_dev, hr, "creating packed-depth shader");
            fprintf(stderr, "[GPU-D3D11] packed depth shader creation failed 0x%08lX\n",
                    (unsigned long)hr);
            s_depth_packed_failed = 1;
            return 0;
        }
    }
    if (!s_depth_packed_cs)
        return 0;
    if (d->packed)
        return d->packed_srv && d->packed_uav;
    memset(&td, 0, sizeof td);
    td.Width = d->w; td.Height = d->h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &d->packed);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)d->packed,
                                                   NULL, &d->packed_srv);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateUnorderedAccessView(s_dev, (ID3D11Resource *)d->packed,
                                                    NULL, &d->packed_uav);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(s_dev, hr, "creating packed-depth texture");
        fprintf(stderr, "[GPU-D3D11] packed depth texture creation failed 0x%08lX\n",
                (unsigned long)hr);
        if (d->packed_uav) ID3D11UnorderedAccessView_Release(d->packed_uav);
        if (d->packed_srv) ID3D11ShaderResourceView_Release(d->packed_srv);
        if (d->packed) ID3D11Texture2D_Release(d->packed);
        d->packed_uav = NULL; d->packed_srv = NULL; d->packed = NULL;
        return 0;
    }
    return 1;
}

static ID3D11ShaderResourceView *get_packed_depth_texture(const Nv2aTexture *t, int cube,
                                                           unsigned levels)
{
    Depth *d = check_depth_texture(t, cube, levels, 3);
    ID3D11ShaderResourceView *views[2], *none[4] = { NULL, NULL, NULL, NULL };
    ID3D11ShaderResourceView **stencil;
    ID3D11UnorderedAccessView *no_uav = NULL;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    ID3D11Texture2D *source;
    HRESULT hr;

    if (!d || !prepare_packed_depth(d))
        return NULL;
    s_st.stencil_samples++;
    if (d->packed_valid && d->packed_revision == d->revision)
        return d->packed_srv;
    views[0] = depth_view(d);
    if (!views[0])
        return NULL;
    source = d == s_cur_depth ? d->snapshot : d->tex;
    stencil = d == s_cur_depth ? &d->snapshot_stencil_srv : &d->stencil_srv;
    if (!*stencil) {
        memset(&sv, 0, sizeof sv);
        sv.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)source,
                                                   &sv, stencil);
        if (FAILED(hr)) {
            gpu_d3d11_check_device(s_dev, hr, "creating stencil view");
            fprintf(stderr, "[GPU-D3D11] stencil view creation failed 0x%08lX\n",
                    (unsigned long)hr);
            return NULL;
        }
    }
    views[1] = *stencil;
    ID3D11DeviceContext_PSSetShaderResources(s_ctx, 0, 4, none);
    ID3D11DeviceContext_CSSetShader(s_ctx, s_depth_packed_cs, NULL, 0);
    ID3D11DeviceContext_CSSetShaderResources(s_ctx, 0, 2, views);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(s_ctx, 0, 1, &d->packed_uav, NULL);
    ID3D11DeviceContext_Dispatch(s_ctx, (d->w + 7) / 8, (d->h + 7) / 8, 1);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(s_ctx, 0, 1, &no_uav, NULL);
    ID3D11DeviceContext_CSSetShaderResources(s_ctx, 0, 2, none);
    ID3D11DeviceContext_CSSetShader(s_ctx, NULL, NULL, 0);
    d->packed_revision = d->revision;
    d->packed_valid = 1;
    s_st.stencil_conversions++;
    return d->packed_srv;
}

static void reject_depth_mode(uint32_t mode, int cube, int base)
{
    static uint32_t warned_base, warned_extended;
    static int warned_cube;
    uint32_t *warned = base ? &warned_base : &warned_extended;
    s_st.depth_rejected++;
    if (cube) {
        if (!warned_cube++)
            fprintf(stderr, "[GPU-D3D11] depth cube textures are unsupported\n");
    } else if (!(*warned & (1u << mode))) {
        *warned |= 1u << mode;
        fprintf(stderr, "[GPU-D3D11] depth texture stage mode %u is unsupported on %s draws\n",
                mode, base ? "base" : "extended");
    }
}

static void bind_targets(Surf *s, Depth *d)
{
    D3D11_VIEWPORT vp;
    ID3D11ShaderResourceView *none[4] = { NULL, NULL, NULL, NULL };

    /* A surface about to be drawn into must not stay bound as a texture. */
    ID3D11DeviceContext_PSSetShaderResources(s_ctx, 0, 4, none);
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &s->rtv, d ? d->dsv : NULL);
    vp.TopLeftX = 0; vp.TopLeftY = 0;
    vp.Width = (float)s->pw; vp.Height = (float)s->ph;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(s_ctx, 1, &vp);
    s_cur = s;
    s_cur_depth = d;
}

static LONG scale_edge(uint32_t value, uint32_t logical, uint32_t physical)
{
    return logical ? (LONG)(((uint64_t)value * physical + logical / 2) / logical) : 0;
}

/* The surface clip, as the CPU rasteriser bounds its triangles. */
static void apply_scissor(const Nv2aSurface *s)
{
    D3D11_RECT r;
    uint32_t right = s->width < s_cur->w ? s->width : s_cur->w;
    uint32_t bottom = s->height < s_cur->h ? s->height : s_cur->h;
    r.left = 0; r.top = 0;
    r.right = scale_edge(right, s_cur->w, s_cur->pw);
    r.bottom = scale_edge(bottom, s_cur->h, s_cur->ph);
    ID3D11DeviceContext_RSSetScissorRects(s_ctx, 1, &r);
}

static int begin(const Nv2aSurface *s, const Nv2aRenderState *rs)
{
    Surf *surf;
    Depth *d;

    if (!ensure_device() || !s || !s->color_va)
        return 0;
    surf = get_surf(s);
    if (!surf)
        return 0;
    d = rs ? get_depth(rs->zeta_va, surf->pw, surf->ph) : NULL;
    if (surf != s_cur || d != s_cur_depth)
        bind_targets(surf, d);
    apply_scissor(s);
    return 1;
}

static int ensure_guest_texture(Surf *s)
{
    D3D11_TEXTURE2D_DESC td;
    HRESULT hr;

    if (s->guest_tex && s->guest_srv)
        return 1;
    memset(&td, 0, sizeof td);
    td.Width = s->w;
    td.Height = s->h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = surf_format(s->bpp);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &s->guest_tex);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)s->guest_tex,
                                                   NULL, &s->guest_srv);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(s_dev, hr, "creating guest color upload texture");
        fprintf(stderr, "[GPU-D3D11] guest color upload texture for %08X failed 0x%08lX\n",
                s->va, (unsigned long)hr);
        if (s->guest_srv) ID3D11ShaderResourceView_Release(s->guest_srv);
        if (s->guest_tex) ID3D11Texture2D_Release(s->guest_tex);
        s->guest_srv = NULL;
        s->guest_tex = NULL;
        return 0;
    }
    return 1;
}

static int upload_guest_surface(Surf *s)
{
    Surf *restore = s_cur;
    Depth *restore_depth = s_cur_depth;

    if (s->pw == s->w && s->ph == s->h) {
        ID3D11DeviceContext_UpdateSubresource(s_ctx, (ID3D11Resource *)s->tex, 0, NULL,
                                             GUEST(s->va), s->pitch, 0);
        return 1;
    }
    if (!ensure_guest_texture(s))
        return 0;
    ID3D11DeviceContext_UpdateSubresource(s_ctx, (ID3D11Resource *)s->guest_tex, 0, NULL,
                                         GUEST(s->va), s->pitch, 0);
    if (!gpu_scale_blit(s->guest_srv, s->w, s->h, s->rtv, s->pw, s->ph,
                        GPU_SCALE_BLIT_BILINEAR)) {
        fprintf(stderr, "[GPU-D3D11] guest color upload for %08X failed\n", s->va);
        if (restore)
            bind_targets(restore, restore_depth);
        return 0;
    }
    if (restore)
        bind_targets(restore, restore_depth);
    return 1;
}

static int sync_guest_surface(Surf *s, int force)
{
    static int warned_conflict;

    if (!force && s->guest_check_gen == s_gen)
        return 1;
    s->guest_check_gen = s_gen;
    if (!guest_surface_changed(s))
        return 1;
    if (s->gpu_dirty) {
        s_st.guest_conflicts++;
        if (!warned_conflict++)
            fprintf(stderr, "[GPU-D3D11] guest writes raced GPU color work on surface %08X;"
                            " preserving the GPU frame\n", s->va);
        return 1;
    }
    if (!upload_guest_surface(s) || !snapshot_guest_surface(s))
        return 0;
    s_st.guest_uploads++;
    return 1;
}

static int mirror_full_color_clear(Surf *s, uint32_t argb)
{
    uint8_t *guest = GUEST(s->va);
    uint32_t x, y;

    if (s->bpp == 4) {
        for (y = 0; y < s->h; ++y) {
            uint32_t *row = (uint32_t *)(guest + (size_t)y * s->pitch);
            for (x = 0; x < s->w; ++x)
                row[x] = argb;
        }
    } else if (s->bpp == 2) {
        uint16_t value = (uint16_t)(((argb >> 8) & 0xF800u) |
                                    ((argb >> 5) & 0x07E0u) |
                                    ((argb >> 3) & 0x001Fu));
        for (y = 0; y < s->h; ++y) {
            uint16_t *row = (uint16_t *)(guest + (size_t)y * s->pitch);
            for (x = 0; x < s->w; ++x)
                row[x] = value;
        }
    } else {
        return 0;
    }
    if (!snapshot_guest_surface(s))
        return 0;
    s->guest_check_gen = 0;
    s->gpu_dirty = 0;
    return 1;
}

/* ---- textures ------------------------------------------------------------ */

#define MAX_TEX 2048
#define MAX_TEX_SNAPSHOT_BYTES (128u << 20)
typedef struct {
    uint32_t offset, w, h, pitch, color, levels, palette, palette_entries;
    uint64_t hash, sparse, palette_hash;
    uint32_t gen, stable, last_use;
    uint8_t cube, unsupported;
    uint8_t *snapshot;
    size_t snapshot_bytes;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
} Tex;

static Tex *s_tex;
static int s_ntex;
static uint32_t *s_decode;
static size_t s_decode_cap;
static size_t s_tex_snapshot_bytes;
static GpuTextureDecoder s_texture_decoder;
static int s_tex_gpu = 1, s_tex_log, s_tex_approx;

static uint64_t hash_bytes(const uint8_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        memcpy(&v, p + i, 8);
        h = (h ^ v) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    for (; i < n; i++)
        h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

/* The bytes level 0 (all faces, for a cube) occupies in guest memory. */
static size_t tex_bytes(const Nv2aTexture *t, int faces)
{
    return gpu_texture_span(t, faces);
}

static uint64_t tex_hash(const Nv2aTexture *t, int faces)
{
    size_t n = tex_bytes(t, faces);
    if (!n)
        return 0;
    s_st.tex_hashed_bytes += n;
    return hash_bytes(GUEST(t->offset), n) ^ ((uint64_t)t->color << 56);
}

/* 128 eight-byte samples spread over the texture. */
static uint64_t tex_sparse(const Nv2aTexture *t, int faces)
{
    size_t n = tex_bytes(t, faces), i;
    const uint8_t *p = GUEST(t->offset);
    uint64_t h = 1469598103934665603ull;
    if (n < 8)
        return 0;
    for (i = 0; i < 128; i++) {
        uint64_t v;
        size_t off = ((n - 8) * i / 127) & ~(size_t)7;
        memcpy(&v, p + off, 8);
        h = (h ^ v) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    return h;
}

static void release_tex_snapshot(Tex *e)
{
    if (e->snapshot) {
        free(e->snapshot);
        s_tex_snapshot_bytes -= e->snapshot_bytes;
        e->snapshot = NULL;
        e->snapshot_bytes = 0;
    }
}

static void release_tex(Tex *e)
{
    if (e->srv) ID3D11ShaderResourceView_Release(e->srv);
    if (e->tex) ID3D11Texture2D_Release(e->tex);
    release_tex_snapshot(e);
    e->srv = NULL;
    e->tex = NULL;
}

static void retain_tex_snapshot(Tex *e, uint8_t *snapshot, size_t bytes)
{
    int i, lru;
    if (bytes > MAX_TEX_SNAPSHOT_BYTES) {
        free(snapshot);
        return;
    }
    while (s_tex_snapshot_bytes > MAX_TEX_SNAPSHOT_BYTES - bytes) {
        lru = -1;
        for (i = 0; i < s_ntex; ++i)
            if (&s_tex[i] != e && s_tex[i].snapshot
                && (lru < 0 || s_tex[i].last_use < s_tex[lru].last_use))
                lru = i;
        if (lru < 0) {
            free(snapshot);
            return;
        }
        release_tex_snapshot(&s_tex[lru]);
    }
    e->snapshot = snapshot;
    e->snapshot_bytes = bytes;
    s_tex_snapshot_bytes += bytes;
}

/* A slot for a texture: its old entry, a free one, or the least recently
 * used. */
static Tex *tex_slot(Tex *e)
{
    int i, lru = 0;
    if (e) {
        release_tex(e);
        return e;
    }
    if (s_ntex < MAX_TEX)
        return &s_tex[s_ntex++];
    for (i = 1; i < s_ntex; i++)
        if (s_tex[i].last_use < s_tex[lru].last_use)
            lru = i;
    release_tex(&s_tex[lru]);
    return &s_tex[lru];
}

static void discard_tex(Tex *e)
{
    int index = (int)(e - s_tex);
    release_tex(e);
    --s_ntex;
    if (index != s_ntex)
        s_tex[index] = s_tex[s_ntex];
    memset(&s_tex[s_ntex], 0, sizeof s_tex[s_ntex]);
}

static void set_tex_key(Tex *e, const Nv2aTexture *t, int cube, unsigned palette_entries)
{
    e->offset = t->offset; e->w = t->width; e->h = t->height;
    e->color = t->color; e->pitch = t->pitch; e->cube = (uint8_t)cube;
    e->levels = t->levels; e->palette = t->palette; e->palette_entries = palette_entries;
    e->last_use = s_gen;
}

static int ensure_decode(size_t texels)
{
    if (texels <= s_decode_cap)
        return 1;
    free(s_decode);
    s_decode = (uint32_t *)malloc(texels * 4);
    s_decode_cap = s_decode ? texels : 0;
    return s_decode != NULL;
}

static void check_p8_span(uint32_t va, size_t bytes, int palette)
{
    uint32_t remaining = va >= XBOX_CONTIG_BASE
                       && (uint64_t)va < (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE
                       ? xbox_ContiguousBlockSize(va) : xbox_HeapBlockSize(va);
    if (remaining && bytes > remaining) {
        fprintf(stderr, "[GPU-D3D11] P8 %s allocation is too short:"
                        " %zu bytes requested, %u tracked bytes remain at 0x%08X\n",
                palette ? "palette" : "texture", bytes, remaining, va);
        fflush(stderr);
        abort();
    }
    if (!readable_guest_range(va, bytes)) {
        fprintf(stderr, "[GPU-D3D11] P8 %s span at 0x%08X (%zu bytes) is not readable\n",
                palette ? "palette" : "texture", va, bytes);
        fflush(stderr);
        abort();
    }
}

typedef enum {
    TEX_UPLOAD_FAILED,
    TEX_UPLOAD_OK,
    TEX_UPLOAD_UNSUPPORTED
} TextureUploadResult;

/* Level 0 of a 2D texture (or all six faces of a cube) to the GPU. DXT
 * blocks go as they are (BC1-3 are the same S3TC blocks); everything else
 * through the framework's decoder, the same one the CPU path samples with. */
static TextureUploadResult upload(Tex *e, const Nv2aTexture *t, int cube,
                                  const uint8_t *source, const uint8_t *palette,
                                  unsigned palette_entries, const char **path)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA init[6];
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    int dxt = (int)d3d8_format_dxt_block_bytes(t->color), faces = cube ? 6 : 1, f;
    size_t face_bytes = cube ? gpu_texture_face_stride(t) : tex_bytes(t, 1);
    HRESULT hr;
    int decoded_gpu = 0, attempted_gpu = 0;

    memset(&td, 0, sizeof td);
    td.Width = t->width;
    td.Height = t->height;
    td.MipLevels = 1;
    td.ArraySize = (UINT)faces;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
    memset(init, 0, sizeof init);
    if (dxt && !t->pitch && (t->width & 3) == 0 && (t->height & 3) == 0) {
        td.Format = t->color == 0x0C ? DXGI_FORMAT_BC1_UNORM
                  : t->color == 0x0E ? DXGI_FORMAT_BC2_UNORM : DXGI_FORMAT_BC3_UNORM;
        for (f = 0; f < faces; f++) {
            init[f].pSysMem = source + f * face_bytes;
            init[f].SysMemPitch = (t->width / 4) * (UINT)dxt;
        }
        *path = "BC";
        s_st.tex_bc++;
    } else if (t->color == 0x12 && t->pitch >= t->width * 4) {
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        for (f = 0; f < faces; f++) {
            init[f].pSysMem = source + f * face_bytes;
            init[f].SysMemPitch = t->pitch;
        }
        *path = "direct";
        s_st.tex_direct++;
    } else {
        if (s_tex_gpu && gpu_texture_decode_supported(t)) {
            attempted_gpu = 1;
            decoded_gpu = gpu_texture_decode(&s_texture_decoder, s_dev, s_ctx, t, faces,
                                              source, palette, palette_entries, &e->tex);
            if (!decoded_gpu) {
                gpu_d3d11_check_device(s_dev, S_OK, "texture compute decoding");
                fprintf(stderr, "[GPU-D3D11] falling back to CPU texture decoding\n");
            }
        }
        if (decoded_gpu) {
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            *path = "GPU";
            s_st.tex_gpu++;
        } else {
            size_t texels = (size_t)t->width * t->height;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            if (!ensure_decode(texels * faces)) {
                fprintf(stderr, "[GPU-D3D11] cannot allocate CPU texture decode buffer\n");
                return TEX_UPLOAD_FAILED;
            }
            if (t->color == 0x0B
                && !gpu_texture_decode_p8_cpu(t, faces, source, palette, palette_entries, s_decode))
                return TEX_UPLOAD_FAILED;
            for (f = 0; f < faces; f++) {
                if (t->color != 0x0B) {
                    Nv2aTexture face = *t;
                    face.offset = t->offset + (uint32_t)(f * face_bytes);
                    if (!nv2a_backend_decode_texture(&face, s_decode + texels * f)) {
                        static unsigned failures;
                        if (failures++ < 10)
                            fprintf(stderr, "[GPU-D3D11] cannot decode texture format 0x%02X"
                                            " at 0x%08X%s\n", t->color, t->offset,
                                    failures == 10 ? " (further failures suppressed)" : "");
                        /* The pinned CPU decoder only rejects unsupported non-P8 formats;
                         * an attempted GPU decode may instead have failed transiently. */
                        return attempted_gpu ? TEX_UPLOAD_FAILED : TEX_UPLOAD_UNSUPPORTED;
                    }
                }
                init[f].pSysMem = s_decode + texels * f;
                init[f].SysMemPitch = t->width * 4;
            }
            if (t->color != 0x0B && memcmp(source, GUEST(t->offset), tex_bytes(t, faces))) {
                fprintf(stderr, "[GPU-D3D11] texture changed during CPU decoding;"
                                " cannot safely cache the result\n");
                fflush(stderr);
                abort();
            }
            *path = "CPU";
            s_st.tex_cpu++;
        }
    }
    if (!decoded_gpu) {
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, init, &e->tex);
        if (FAILED(hr)) {
            gpu_d3d11_check_device(s_dev, hr, "texture upload");
            fprintf(stderr, "[GPU-D3D11] texture upload failed 0x%08lX\n", (unsigned long)hr);
            return TEX_UPLOAD_FAILED;
        }
    }
    memset(&sv, 0, sizeof sv);
    sv.Format = td.Format;
    if (cube) {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = 1;
    } else {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
    }
    hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)e->tex, &sv, &e->srv);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(s_dev, hr, "texture view creation");
        fprintf(stderr, "[GPU-D3D11] texture view creation failed 0x%08lX\n", (unsigned long)hr);
        release_tex(e);
        return TEX_UPLOAD_FAILED;
    }
    s_st.uploads++;
    return TEX_UPLOAD_OK;
}

/* The view a stage samples, and the size its texel-space coordinates are
 * divided by (*tw, *th). */
static ID3D11ShaderResourceView *get_texture(const Nv2aTexture *t, int cube, unsigned stage,
                                             float *tw, float *th)
{
    Tex *e = NULL;
    Surf *rt;
    Nv2aTexture normalized;
    uint64_t h, palette_hash = 0;
    size_t source_bytes, palette_bytes, snapshot_bytes;
    uint8_t *snapshot;
    unsigned palette_entries = 0, invalid_index;
    const char *path = NULL;
    TextureUploadResult upload_result;
    int rewritten;
    int i, faces = cube ? 6 : 1;

    if (!t || !t->width || !t->height || t->width > 4096 || t->height > 4096)
        return NULL;
    if (t->pitch && (d3d8_format_is_swizzled(t->color)
                     || d3d8_format_dxt_block_bytes(t->color))) {
        normalized = *t;
        normalized.pitch = 0;
        t = &normalized;
        s_st.tex_pitch++;
    }
    *tw = (float)t->width;
    *th = (float)t->height;
    if (s_depth_texture && t->color == 0x2E)
        return get_depth_texture(t, cube, (nv2a_backend_reg(0x1B04 + stage * 0x40) >> 16) & 0xF);
    if (s_stencil_alias && t->color == 0x41 && overlapping_depth(t, cube)) {
        uint32_t format = nv2a_backend_reg(0x1B04 + stage * 0x40);
        return get_packed_depth_texture(t, (format & 4) != 0, (format >> 16) & 0xF);
    }
    if (!cube) {
        /* Render to texture: a surface this back end drew. */
        rt = find_surf(t->offset);
        if (rt && rt != s_cur) {
            s_st.rtt++;
            *tw = (float)rt->w;
            *th = (float)rt->h;
            return rt->srv;
        }
        if (!rt) {
            for (i = 0; i < s_nsurf; i++)
                if (t->offset > s_surf[i].va
                    && t->offset - s_surf[i].va < s_surf[i].pitch * s_surf[i].h) {
                    s_st.overlap_rtt++;
                    break;
                }
        }
    }
    source_bytes = tex_bytes(t, faces);
    if (!source_bytes) {
        fprintf(stderr, "[GPU-D3D11] invalid texture span for format 0x%02X, %ux%u\n",
                t->color, t->width, t->height);
        return NULL;
    }
    if (t->color == 0x0B) {
        palette_entries = 256u >> ((nv2a_backend_reg(0x1B20 + stage * 0x40) >> 2) & 3u);
        check_p8_span(t->offset, source_bytes, 0);
        check_p8_span(t->palette, (size_t)palette_entries * 4, 1);
    }
    palette_bytes = (size_t)palette_entries * 4;
    snapshot_bytes = source_bytes + palette_bytes;
    for (i = 0; i < s_ntex; i++)
        if (s_tex[i].offset == t->offset && s_tex[i].w == t->width && s_tex[i].h == t->height
                && s_tex[i].color == t->color && s_tex[i].pitch == t->pitch
                && s_tex[i].cube == (uint8_t)cube && s_tex[i].palette == t->palette
                && s_tex[i].palette_entries == palette_entries
                && (!cube || s_tex[i].levels == t->levels)) {
            e = &s_tex[i];
            break;
        }
    if (e && e->unsupported) {
        e->last_use = s_gen;
        s_st.tex_unsupported_hits++;
        return NULL;
    }
    if (s_tex_approx && palette_bytes)
        palette_hash = hash_bytes(GUEST(t->palette), palette_bytes);
    if (!s_tex_approx) {
        s_st.tex_hashed_bytes += source_bytes;
        if (e && e->snapshot && e->snapshot_bytes == snapshot_bytes
            && !memcmp(e->snapshot, GUEST(t->offset), source_bytes)
            && (!palette_bytes
                || !memcmp(e->snapshot + source_bytes, GUEST(t->palette), palette_bytes))) {
            e->last_use = s_gen;
            return e->srv;
        }
    } else {
        if (e && e->gen == s_gen && e->palette_hash == palette_hash) {
            e->last_use = s_gen;
            return e->srv;
        }
        if (e && e->palette_hash == palette_hash && e->stable >= 30
                && ((s_gen + (e->offset >> 12)) & 31) != 0
                && tex_sparse(t, faces) == e->sparse) {
            e->gen = s_gen;
            e->last_use = s_gen;
            return e->srv;
        }
        h = tex_hash(t, faces);
        if (e && e->hash == h && h && e->palette_hash == palette_hash) {
            if (e->stable < 0xFFFFFFFFu)
                e->stable++;
            if (e->stable == 30)
                e->sparse = tex_sparse(t, faces);
            e->gen = s_gen;
            e->last_use = s_gen;
            return e->srv;
        }
    }
    snapshot = (uint8_t *)malloc(snapshot_bytes);
    if (!snapshot) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate %zu-byte texture snapshot\n", snapshot_bytes);
        fflush(stderr);
        abort();
    }
    memcpy(snapshot, GUEST(t->offset), source_bytes);
    if (palette_bytes)
        memcpy(snapshot + source_bytes, GUEST(t->palette), palette_bytes);
    if (palette_bytes && !gpu_texture_p8_indices_valid(t, faces, snapshot,
                                                        palette_entries, &invalid_index)) {
        if (invalid_index < 256)
            fprintf(stderr, "[GPU-D3D11] P8 index %u exceeds %u-entry palette at stage %u;"
                            " hardware behavior is not established\n",
                    invalid_index, palette_entries, stage);
        else
            fprintf(stderr, "[GPU-D3D11] invalid P8 texture dimensions or layout at stage %u\n",
                    stage);
        free(snapshot);
        fflush(stderr);
        abort();
    }
    h = s_tex_approx ? hash_bytes(snapshot, source_bytes) ^ ((uint64_t)t->color << 56) : 0;
    if (s_tex_approx && palette_bytes)
        palette_hash = hash_bytes(snapshot + source_bytes, palette_bytes);
    rewritten = e != NULL;
    e = tex_slot(e);
    memset(e, 0, sizeof *e);
    set_tex_key(e, t, cube, palette_entries);
    upload_result = upload(e, t, cube, snapshot, palette_bytes ? snapshot + source_bytes : NULL,
                           palette_entries, &path);
    if (upload_result != TEX_UPLOAD_OK) {
        free(snapshot);
        if (upload_result == TEX_UPLOAD_UNSUPPORTED) {
            e->unsupported = 1;
            s_st.tex_unsupported++;
        } else {
            discard_tex(e);
            if (s_tex_log)
                fprintf(stderr, "[GPU-TEX] discarded failed upload (%d cache entries)\n", s_ntex);
        }
        return NULL;
    }
    e->palette_hash = palette_hash;
    e->hash = h; e->gen = s_gen;
    if (s_tex_approx)
        free(snapshot);
    else
        retain_tex_snapshot(e, snapshot, snapshot_bytes);
    if (s_tex_log)
        fprintf(stderr, "[GPU-TEX] %s %s va=0x%08X format=0x%02X %ux%u pitch=%u"
                        " faces=%d levels=%u palette=0x%08X\n",
                rewritten ? "changed" : "new", path, t->offset, t->color, t->width,
                t->height, t->pitch, faces, t->levels, t->palette);
    return e->srv;
}

/* ---- state objects --------------------------------------------------------- */

static D3D11_BLEND map_blend(uint32_t f)
{
    switch (f) {
    case 0: return D3D11_BLEND_ZERO;
    case 1: return D3D11_BLEND_ONE;
    case 0x300: return D3D11_BLEND_SRC_COLOR;
    case 0x301: return D3D11_BLEND_INV_SRC_COLOR;
    case 0x302: return D3D11_BLEND_SRC_ALPHA;
    case 0x303: return D3D11_BLEND_INV_SRC_ALPHA;
    case 0x304: return D3D11_BLEND_DEST_ALPHA;
    case 0x305: return D3D11_BLEND_INV_DEST_ALPHA;
    case 0x306: return D3D11_BLEND_DEST_COLOR;
    case 0x307: return D3D11_BLEND_INV_DEST_COLOR;
    case 0x308: return D3D11_BLEND_SRC_ALPHA_SAT;
    case 0x8001: case 0x8003: return D3D11_BLEND_BLEND_FACTOR;
    case 0x8002: case 0x8004: return D3D11_BLEND_INV_BLEND_FACTOR;
    default: return D3D11_BLEND_ONE;
    }
}

static D3D11_BLEND alpha_only(D3D11_BLEND b)
{
    switch (b) {
    case D3D11_BLEND_SRC_COLOR: return D3D11_BLEND_SRC_ALPHA;
    case D3D11_BLEND_INV_SRC_COLOR: return D3D11_BLEND_INV_SRC_ALPHA;
    case D3D11_BLEND_DEST_COLOR: return D3D11_BLEND_DEST_ALPHA;
    case D3D11_BLEND_INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_ALPHA;
    default: return b;
    }
}

static D3D11_BLEND_OP map_blend_op(uint32_t e)
{
    switch (e) {
    case 0x8007: return D3D11_BLEND_OP_MIN;
    case 0x8008: return D3D11_BLEND_OP_MAX;
    case 0x800A: return D3D11_BLEND_OP_SUBTRACT;
    case 0x800B: return D3D11_BLEND_OP_REV_SUBTRACT;
    default: return D3D11_BLEND_OP_ADD;
    }
}

static D3D11_COMPARISON_FUNC map_cmp(uint32_t f)
{
    switch (f) {
    case 0x200: return D3D11_COMPARISON_NEVER;
    case 0x201: return D3D11_COMPARISON_LESS;
    case 0x202: return D3D11_COMPARISON_EQUAL;
    case 0x203: return D3D11_COMPARISON_LESS_EQUAL;
    case 0x204: return D3D11_COMPARISON_GREATER;
    case 0x205: return D3D11_COMPARISON_NOT_EQUAL;
    case 0x206: return D3D11_COMPARISON_GREATER_EQUAL;
    default: return D3D11_COMPARISON_ALWAYS;
    }
}

static D3D11_STENCIL_OP map_sop(uint32_t o)
{
    switch (o) {
    case 0: return D3D11_STENCIL_OP_ZERO;
    case 0x1E01: return D3D11_STENCIL_OP_REPLACE;
    case 0x1E02: return D3D11_STENCIL_OP_INCR_SAT;
    case 0x1E03: return D3D11_STENCIL_OP_DECR_SAT;
    case 0x150A: return D3D11_STENCIL_OP_INVERT;
    case 0x8507: return D3D11_STENCIL_OP_INCR;
    case 0x8508: return D3D11_STENCIL_OP_DECR;
    default: return D3D11_STENCIL_OP_KEEP;
    }
}

#define MAX_STATE 256
typedef struct { uint32_t key[5]; ID3D11BlendState *bs; } BlendEntry;
typedef struct { uint32_t key[10]; ID3D11DepthStencilState *ds; } DsEntry;
typedef struct { uint32_t key[3]; ID3D11SamplerState *ss; } SampEntry;
static BlendEntry s_blend[MAX_STATE];
static int s_nblend;
static DsEntry s_ds[MAX_STATE];
static int s_nds;
static SampEntry s_samp[MAX_STATE];
static int s_nsamp;

static ID3D11BlendState *get_blend(const Nv2aRenderState *rs)
{
    uint32_t key[5] = { rs->blend_enable, rs->blend_src, rs->blend_dst, rs->blend_eq,
                        rs->color_mask };
    D3D11_BLEND_DESC bd;
    D3D11_RENDER_TARGET_BLEND_DESC *t = &bd.RenderTarget[0];
    UINT8 mask = 0;
    int i;

    for (i = 0; i < s_nblend; i++)
        if (!memcmp(s_blend[i].key, key, sizeof key))
            return s_blend[i].bs;
    if (s_nblend == MAX_STATE)
        return NULL;
    memset(&bd, 0, sizeof bd);
    t->BlendEnable = rs->blend_enable ? TRUE : FALSE;
    t->SrcBlend = map_blend(rs->blend_src);
    t->DestBlend = map_blend(rs->blend_dst);
    t->BlendOp = map_blend_op(rs->blend_eq);
    t->SrcBlendAlpha = alpha_only(t->SrcBlend);
    t->DestBlendAlpha = alpha_only(t->DestBlend);
    t->BlendOpAlpha = t->BlendOp;
    if (rs->color_mask & 0x00010000u) mask |= D3D11_COLOR_WRITE_ENABLE_RED;
    if (rs->color_mask & 0x00000100u) mask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
    if (rs->color_mask & 0x00000001u) mask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
    if (rs->color_mask & 0x01000000u) mask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
    t->RenderTargetWriteMask = mask;
    memcpy(s_blend[s_nblend].key, key, sizeof key);
    if (gpu_call_failed(ID3D11Device_CreateBlendState(s_dev, &bd, &s_blend[s_nblend].bs),
                        "creating blend state"))
        return NULL;
    return s_blend[s_nblend++].bs;
}

static ID3D11DepthStencilState *get_ds(const Nv2aRenderState *rs, int has_depth)
{
    uint32_t key[10] = { has_depth && rs->depth_test_enable, rs->depth_func,
                         has_depth && rs->depth_write, has_depth && rs->stencil_enable,
                         rs->stencil_func, rs->stencil_func_mask, rs->stencil_write_mask,
                         rs->stencil_fail, rs->stencil_zfail, rs->stencil_zpass };
    D3D11_DEPTH_STENCIL_DESC dd;
    int i;

    for (i = 0; i < s_nds; i++)
        if (!memcmp(s_ds[i].key, key, sizeof key))
            return s_ds[i].ds;
    if (s_nds == MAX_STATE)
        return NULL;
    memset(&dd, 0, sizeof dd);
    dd.DepthEnable = key[0] || key[2] ? TRUE : FALSE;
    dd.DepthFunc = key[0] ? map_cmp(rs->depth_func) : D3D11_COMPARISON_ALWAYS;
    dd.DepthWriteMask = key[2] ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.StencilEnable = key[3] ? TRUE : FALSE;
    dd.StencilReadMask = (UINT8)rs->stencil_func_mask;
    dd.StencilWriteMask = (UINT8)rs->stencil_write_mask;
    dd.FrontFace.StencilFunc = map_cmp(rs->stencil_func);
    dd.FrontFace.StencilFailOp = map_sop(rs->stencil_fail);
    dd.FrontFace.StencilDepthFailOp = map_sop(rs->stencil_zfail);
    dd.FrontFace.StencilPassOp = map_sop(rs->stencil_zpass);
    dd.BackFace = dd.FrontFace;
    memcpy(s_ds[s_nds].key, key, sizeof key);
    if (gpu_call_failed(ID3D11Device_CreateDepthStencilState(s_dev, &dd, &s_ds[s_nds].ds),
                        "creating depth-stencil state"))
        return NULL;
    return s_ds[s_nds++].ds;
}

/* Address modes and the filter as the CPU path's rc_texel samples: bilinear
 * when MAG is 2 or MIN is 2, 4 or 6, else the nearest texel; level 0 only. */
static ID3D11SamplerState *get_sampler(uint32_t addr_u, uint32_t addr_v, uint32_t filter)
{
    uint32_t mag = (filter >> 24) & 0xF, min = (filter >> 16) & 0xFF;
    int linear = mag == 2 || min == 2 || min == 4 || min == 6;
    uint32_t key[3] = { addr_u, addr_v, (uint32_t)linear };
    D3D11_SAMPLER_DESC sd;
    int i;

    for (i = 0; i < s_nsamp; i++)
        if (!memcmp(s_samp[i].key, key, sizeof key))
            return s_samp[i].ss;
    if (s_nsamp == MAX_STATE)
        return NULL;
    memset(&sd, 0, sizeof sd);
    sd.Filter = linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = addr_u == 1 ? D3D11_TEXTURE_ADDRESS_WRAP : addr_u == 2 ? D3D11_TEXTURE_ADDRESS_MIRROR
                : D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = addr_v == 1 ? D3D11_TEXTURE_ADDRESS_WRAP : addr_v == 2 ? D3D11_TEXTURE_ADDRESS_MIRROR
                : D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = 0.0f;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    memcpy(s_samp[s_nsamp].key, key, sizeof key);
    if (gpu_call_failed(ID3D11Device_CreateSamplerState(s_dev, &sd, &s_samp[s_nsamp].ss),
                        "creating sampler state"))
        return NULL;
    return s_samp[s_nsamp++].ss;
}

/* The zeta format's largest value (SURFACE_FORMAT bits 4-7: 1 Z16, 2
 * Z24S8). The CPU path compares the title's screen z with the raw clear
 * value in this range and ignores SET_CLIP_MIN/MAX, so depth is z / zmax. */
static float zeta_max(void)
{
    return ((nv2a_backend_reg(0x0208) >> 4) & 0xF) == 1 ? 65535.0f : 16777215.0f;
}

static void depth_consts(const Nv2aRenderState *rs, float out[4])
{
    (void)rs;
    out[0] = 0.0f;
    out[1] = 1.0f / zeta_max();
    out[2] = 0.0f;              /* no vertex clamp: D3D clamps per pixel to 0..1 */
    out[3] = 0.0f;
}

static void blend_factor(const Nv2aRenderState *rs, float bf[4])
{
    bf[0] = ((rs->blend_color >> 16) & 0xFF) / 255.0f;
    bf[1] = ((rs->blend_color >> 8) & 0xFF) / 255.0f;
    bf[2] = (rs->blend_color & 0xFF) / 255.0f;
    bf[3] = ((rs->blend_color >> 24) & 0xFF) / 255.0f;
    if (rs->blend_src == 0x8003 || rs->blend_src == 0x8004 ||
        rs->blend_dst == 0x8003 || rs->blend_dst == 0x8004)
        bf[0] = bf[1] = bf[2] = bf[3];
}

static void set_alpha_test(const Nv2aRenderState *rs, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (rs->alpha_test_enable) {
        out[0] = (float)(rs->alpha_func >= 0x200 && rs->alpha_func <= 0x207
                         ? rs->alpha_func - 0x200 : 7);
        out[1] = (float)(rs->alpha_ref & 0xFF);
        out[2] = 1.0f;
    }
}

/* ---- vertex ring ----------------------------------------------------------- */

static ID3D11Buffer *s_vring;
static UINT s_vring_size, s_vring_pos;

/* The vertex ring, at least `bytes` large. */
static int ring_ensure(UINT bytes)
{
    D3D11_BUFFER_DESC bd;
    UINT want;

    if (s_vring && bytes <= s_vring_size)
        return 1;
    want = bytes * 4 > (32u << 20) ? bytes * 4 : (32u << 20);
    if (s_vring) ID3D11Buffer_Release(s_vring);
    s_vring = NULL;
    s_vring_size = 0;
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = want;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_vring),
                        "creating vertex ring"))
        return 0;
    s_vring_size = want;
    s_vring_pos = want;                 /* the first map discards */
    return 1;
}

static UINT ring_push(const void *data, UINT bytes, UINT align)
{
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT off;

    if (!bytes || !ring_ensure(bytes))
        return ~0u;
    off = (s_vring_pos + align - 1) / align * align;
    if (off > s_vring_size || s_vring_size - off < bytes) {
        mode = D3D11_MAP_WRITE_DISCARD;
        off = 0;
    }
    if (gpu_call_failed(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_vring,
                                                0, mode, 0, &m), "mapping vertex ring"))
        return ~0u;
    memcpy((uint8_t *)m.pData + off, data, bytes);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_vring, 0);
    s_vring_pos = off + bytes;
    return off;
}

static void unpack_argb(uint32_t c, float o[4])
{
    o[0] = ((c >> 16) & 0xFF) / 255.0f;
    o[1] = ((c >> 8) & 0xFF) / 255.0f;
    o[2] = (c & 0xFF) / 255.0f;
    o[3] = ((c >> 24) & 0xFF) / 255.0f;
}

static const char s_clear_hlsl[] =
"cbuffer C : register(b2) { float4 color; float4 depth; };\n"
"float4 vs_main(uint id : SV_VertexID) : SV_Position {\n"
"  float2 p = float2((id << 1) & 2, id & 2);\n"
"  return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, depth.x, 1.0);\n"
"}\n"
"float4 ps_main() : SV_Target { return color; }\n";

typedef struct {
    float color[4], depth[4];
} ClearConsts;

static ID3D11VertexShader *s_clear_vs;
static ID3D11PixelShader *s_clear_ps;
static ID3D11Buffer *s_clear_cb;
static ID3D11BlendState *s_clear_blend[16];
static ID3D11DepthStencilState *s_clear_ds[4];

static int query_suspend_clear(void);
static void query_resume_clear(int counting);

static void clear_error(const char *operation, HRESULT hr)
{
    gpu_d3d11_check_device(s_dev, hr, operation);
    fprintf(stderr, "[GPU-D3D11] %s failed 0x%08lX (device removed reason 0x%08lX);"
                    " cannot safely clear the surface\n",
            operation, (unsigned long)hr,
            (unsigned long)ID3D11Device_GetDeviceRemovedReason(s_dev));
    fflush(stderr);
    abort();
}

static void clear_resources(void)
{
    D3D11_BUFFER_DESC bd;
    ID3DBlob *vs = NULL, *ps = NULL;
    HRESULT hr;

    if (s_clear_vs)
        return;
    if (!compile(s_clear_hlsl, sizeof s_clear_hlsl - 1, "clear", "vs_main", "vs_5_0", &vs) ||
        !compile(s_clear_hlsl, sizeof s_clear_hlsl - 1, "clear", "ps_main", "ps_5_0", &ps))
        clear_error("compile rectangular clear shaders", E_FAIL);
    hr = ID3D11Device_CreateVertexShader(s_dev, ID3D10Blob_GetBufferPointer(vs),
                                         ID3D10Blob_GetBufferSize(vs), NULL, &s_clear_vs);
    if (FAILED(hr))
        clear_error("CreateVertexShader(clear)", hr);
    hr = ID3D11Device_CreatePixelShader(s_dev, ID3D10Blob_GetBufferPointer(ps),
                                        ID3D10Blob_GetBufferSize(ps), NULL, &s_clear_ps);
    if (FAILED(hr))
        clear_error("CreatePixelShader(clear)", hr);
    ID3D10Blob_Release(vs);
    ID3D10Blob_Release(ps);

    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = sizeof(ClearConsts);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_clear_cb);
    if (FAILED(hr))
        clear_error("CreateBuffer(clear constants)", hr);
}

static ID3D11BlendState *clear_blend(unsigned mask)
{
    D3D11_BLEND_DESC bd;
    D3D11_RENDER_TARGET_BLEND_DESC *target;
    HRESULT hr;

    if (s_clear_blend[mask])
        return s_clear_blend[mask];
    memset(&bd, 0, sizeof bd);
    target = &bd.RenderTarget[0];
    target->SrcBlend = D3D11_BLEND_ONE;
    target->DestBlend = D3D11_BLEND_ZERO;
    target->BlendOp = D3D11_BLEND_OP_ADD;
    target->SrcBlendAlpha = D3D11_BLEND_ONE;
    target->DestBlendAlpha = D3D11_BLEND_ZERO;
    target->BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target->RenderTargetWriteMask = (UINT8)mask;
    hr = ID3D11Device_CreateBlendState(s_dev, &bd, &s_clear_blend[mask]);
    if (FAILED(hr))
        clear_error("CreateBlendState(clear)", hr);
    return s_clear_blend[mask];
}

static ID3D11DepthStencilState *clear_depth_stencil(unsigned flags)
{
    D3D11_DEPTH_STENCIL_DESC dd;
    HRESULT hr;

    if (s_clear_ds[flags])
        return s_clear_ds[flags];
    memset(&dd, 0, sizeof dd);
    dd.DepthEnable = (flags & 1) != 0;
    dd.DepthWriteMask = (flags & 1) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    dd.StencilEnable = (flags & 2) != 0;
    dd.StencilReadMask = dd.StencilWriteMask = 0xFF;
    dd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dd.BackFace = dd.FrontFace;
    hr = ID3D11Device_CreateDepthStencilState(s_dev, &dd, &s_clear_ds[flags]);
    if (FAILED(hr))
        clear_error("CreateDepthStencilState(clear)", hr);
    return s_clear_ds[flags];
}

static void clear_draw(const D3D11_RECT *rect, uint32_t flags, uint32_t argb,
                       uint32_t zstencil, float z)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    ClearConsts c;
    ID3D11BlendState *blend;
    ID3D11DepthStencilState *ds;
    HRESULT hr;
    int counting;

    clear_resources();
    blend = clear_blend((flags >> 4) & 0xF);
    ds = clear_depth_stencil(flags & 3);
    memset(&c, 0, sizeof c);
    unpack_argb(argb, c.color);
    c.depth[0] = z;
    hr = ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_clear_cb, 0,
                                D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
        clear_error("Map(clear constants)", hr);
    memcpy(mapped.pData, &c, sizeof c);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_clear_cb, 0);

    ID3D11DeviceContext_IASetInputLayout(s_ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(s_ctx, s_clear_vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx, 2, 1, &s_clear_cb);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(s_ctx, s_clear_ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx, 2, 1, &s_clear_cb);
    ID3D11DeviceContext_RSSetState(s_ctx, s_rs);
    ID3D11DeviceContext_RSSetScissorRects(s_ctx, 1, rect);
    ID3D11DeviceContext_OMSetBlendState(s_ctx, blend, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, ds, (UINT8)zstencil);
    counting = query_suspend_clear();
    ID3D11DeviceContext_Draw(s_ctx, 3, 0);
    query_resume_clear(counting);
}

/* ---- the back end ---------------------------------------------------------- */

static void d3d_clear(const Nv2aSurface *s, const Nv2aRenderState *rs, uint32_t flags,
                      uint32_t argb, uint32_t zstencil)
{
    uint32_t bounds[4];
    uint32_t depth_flags = flags & 3;
    uint32_t color_flags = flags & 0xF0;
    D3D11_RECT rect;
    float c[4], z;
    int full_color, full_depth;

    if (!begin(s, rs))
        return;
    if (nv2a_backend_clear_rect(bounds)) {
        uint32_t sx = s->aa_sx > 1 ? 2 : 1, sy = s->aa_sy > 1 ? 2 : 1;
        uint32_t x0 = bounds[0] * sx, y0 = bounds[1] * sy;
        uint32_t x1 = bounds[2] * sx, y1 = bounds[3] * sy;
        x0 = x0 < s_cur->w ? x0 : s_cur->w;
        y0 = y0 < s_cur->h ? y0 : s_cur->h;
        x1 = x1 < s_cur->w ? x1 : s_cur->w;
        y1 = y1 < s_cur->h ? y1 : s_cur->h;
        rect.left = scale_edge(x0, s_cur->w, s_cur->pw);
        rect.top = scale_edge(y0, s_cur->h, s_cur->ph);
        rect.right = scale_edge(x1, s_cur->w, s_cur->pw);
        rect.bottom = scale_edge(y1, s_cur->h, s_cur->ph);
    } else {
        rect.left = rect.top = 0;
        rect.right = (LONG)s_cur->pw;
        rect.bottom = (LONG)s_cur->ph;
    }
    s_st.clears++;
    if (rect.left >= rect.right || rect.top >= rect.bottom)
        return;
    if (s_cur_depth && depth_flags)
        s_cur_depth->revision++;
    full_color = rect.left == 0 && rect.top == 0 &&
                 (uint32_t)rect.right == s_cur->pw && (uint32_t)rect.bottom == s_cur->ph;
    full_depth = s_cur_depth && rect.left == 0 && rect.top == 0 &&
                 (uint32_t)rect.right == s_cur_depth->w &&
                 (uint32_t)rect.bottom == s_cur_depth->h;
    z = zeta_max() == 65535.0f ? (zstencil & 0xFFFFu) / 65535.0f
                               : (zstencil >> 8) / 16777215.0f;
    if ((flags & 0xF0) == 0xF0 && full_color) {
        unpack_argb(argb, c);
        ID3D11DeviceContext_ClearRenderTargetView(s_ctx, s_cur->rtv, c);
        flags &= ~0xF0u;
    }
    if ((flags & 3) && full_depth) {
        UINT f = ((flags & 1) ? D3D11_CLEAR_DEPTH : 0) | ((flags & 2) ? D3D11_CLEAR_STENCIL : 0);
        ID3D11DeviceContext_ClearDepthStencilView(s_ctx, s_cur_depth->dsv, f, z,
                                                  (UINT8)(zstencil & 0xFF));
        s_cur_depth->initialized |= flags & 3;
        flags &= ~3u;
    }
    if (!s_cur_depth) {
        if ((flags & 3) && rs && rs->zeta_va)
            fprintf(stderr, "[GPU-D3D11] cannot clear unavailable depth surface %08X\n",
                    rs->zeta_va);
        flags &= ~3u;
    }
    if (flags & 0xF3)
        clear_draw(&rect, flags, argb, zstencil, z);
    if (color_flags) {
        if (color_flags == 0xF0 && full_color) {
            if (!mirror_full_color_clear(s_cur, argb))
                s_cur->gpu_dirty = 1;
        } else {
            s_cur->gpu_dirty = 1;
        }
    }
}

static int set_window_clip(const Nv2aSurface *s)
{
    static struct {
        uint32_t rect[NV2A_WINDOW_CLIP_COUNT][4];
        uint32_t info[4];
    } last;
    static int have_last;
    struct {
        uint32_t rect[NV2A_WINDOW_CLIP_COUNT][4];
        uint32_t info[4];
    } c;
    D3D11_MAPPED_SUBRESOURCE m;
    unsigned i;

    memset(&c, 0, sizeof c);
    if (s->window_clip_enabled && s->window_clip.axes == 3) {
        uint32_t sx = s->aa_sx > 1 ? 2 : 1, sy = s->aa_sy > 1 ? 2 : 1;
        c.info[0] = s->window_clip.uniform ? 1 : NV2A_WINDOW_CLIP_COUNT;
        c.info[1] = s->window_clip.exclusive;
        for (i = 0; i < c.info[0]; i++) {
            uint32_t h = s->window_clip.horizontal[i];
            uint32_t v = s->window_clip.vertical[i];
            uint32_t x0 = (h & 0xFFFu) * sx;
            uint32_t x1 = (((h >> 16) & 0xFFFu) + 1) * sx;
            uint32_t y0 = (v & 0xFFFu) * sy;
            uint32_t y1 = (((v >> 16) & 0xFFFu) + 1) * sy;
            c.rect[i][0] = (uint32_t)scale_edge(x0, s_cur->w, s_cur->pw);
            c.rect[i][1] = (uint32_t)scale_edge(x1, s_cur->w, s_cur->pw) - 1;
            c.rect[i][2] = (uint32_t)scale_edge(y0, s_cur->h, s_cur->ph);
            c.rect[i][3] = (uint32_t)scale_edge(y1, s_cur->h, s_cur->ph) - 1;
        }
    }
    if (!have_last || memcmp(&last, &c, sizeof c)) {
        HRESULT hr = ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_window_cb, 0,
                                              D3D11_MAP_WRITE_DISCARD, 0, &m);
        if (FAILED(hr)) {
            gpu_d3d11_check_device(s_dev, hr, "mapping window clip constants");
            fprintf(stderr, "[GPU-D3D11] window clip constants map failed 0x%08lX\n",
                    (unsigned long)hr);
            return 0;
        }
        memcpy(m.pData, &c, sizeof c);
        ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_window_cb, 0);
        memcpy(&last, &c, sizeof c);
        have_last = 1;
    }
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx, 1, 1, &s_window_cb);
    return 1;
}

static void d3d_draw(const Nv2aSurface *s, const Nv2aBatch *b)
{
    const Nv2aRenderState *rs;
    D3D11_MAPPED_SUBRESOURCE m;
    BaseConsts c;
    UINT stride = sizeof(Nv2aVertex), offs;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11SamplerState *ss = NULL;
    float bf[4], tw = 1.0f, th = 1.0f;

    if (!b || !b->count || !b->state || !begin(s, b->state) ||
        !sync_guest_surface(s_cur, 0) || !set_window_clip(s))
        return;
    rs = b->state;
    if (b->texture) {
        if (s_depth_texture && b->texture->color == 0x2E) {
            uint32_t mode = nv2a_backend_reg(0x1E70) & 0x1F;
            int cube = (nv2a_backend_reg(0x1B04) & 4) != 0;
            if (cube || mode != 1) {
                reject_depth_mode(mode, cube, 1);
                return;
            }
        }
        srv = get_texture(b->texture, 0, 0, &tw, &th);
        if (!srv && required_depth_alias(b->texture, 0))
            return;
        ss = get_sampler(b->texture->addr_u, b->texture->addr_v, b->texture->filter);
    }
    memset(&c, 0, sizeof c);
    c.surf[0] = (float)s_cur->w; c.surf[1] = (float)s_cur->h;
    c.surf[2] = 1.0f / s_cur->w; c.surf[3] = 1.0f / s_cur->h;
    if (srv) {
        c.tex[0] = 1.0f / tw;
        c.tex[1] = 1.0f / th;
        c.tex[2] = 1.0f;
        if (b->texture->color == 0x2E && s_depth_texture)
            c.tex[3] = (float)((nv2a_backend_reg(0x1E6C) & 7) + 1);
    }
    set_alpha_test(rs, c.alpha);
    depth_consts(rs, c.depth);
    if (gpu_call_failed(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_base_cb, 0,
                                                D3D11_MAP_WRITE_DISCARD, 0, &m),
                        "mapping base constants"))
        return;
    memcpy(m.pData, &c, sizeof c);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_base_cb, 0);
    offs = ring_push(b->vertices, b->count * stride, stride);
    if (offs == ~0u)
        return;
    blend_factor(rs, bf);
    ID3D11DeviceContext_IASetInputLayout(s_ctx, s_base_layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, 1, &s_vring, &stride, &offs);
    ID3D11DeviceContext_VSSetShader(s_ctx, s_base_vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx, 0, 1, &s_base_cb);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(s_ctx, s_base_ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx, 0, 1, &s_base_cb);
    ID3D11DeviceContext_PSSetShaderResources(s_ctx, 0, 1, &srv);
    ID3D11DeviceContext_PSSetSamplers(s_ctx, 0, 1, &ss);
    ID3D11DeviceContext_RSSetState(s_ctx, s_rs);
    ID3D11DeviceContext_OMSetBlendState(s_ctx, get_blend(rs), bf, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, get_ds(rs, s_cur_depth != NULL),
                                               rs->stencil_ref & 0xFF);
    ID3D11DeviceContext_Draw(s_ctx, b->count, 0);
    s_cur->gpu_dirty = 1;
    if (s_cur_depth)
        s_cur_depth->revision++;
    s_st.draws++;
    s_st.tris += b->count / 3;
}

/* Without programmed combiners the CPU path draws stage 0 times diffuse; a
 * one-stage combiner computing T0 * D0 (or D0 alone) is the same thing. */
static void simple_combiner(const Nv2aXBatch *b, Nv2aCombiner *rc)
{
    memset(rc, 0, sizeof *rc);
    rc->control = 1;
    rc->stage_program = b->tex_valid[0] ? 1 : 0;
    /* A = T0 (reg 8) or 1 (reg 0 inverted), B = D0 (reg 4); out to R12
     * (spare0), which the final combiner absent reads as the result. */
    rc->color_icw[0] = (b->tex_valid[0] ? 0x08u : 0x20u) << 24 | 0x04u << 16;
    rc->alpha_icw[0] = (b->tex_valid[0] ? 0x18u : 0x30u) << 24 | 0x14u << 16;
    rc->color_ocw[0] = 0x0Cu << 4;
    rc->alpha_ocw[0] = 0x0Cu << 4;
}

/* The rasteriser state for a batch: no culling, unless the batch asks the
 * back end to cull as the CPU path would (draw_vsh with RECOMP_CULL_FACE).
 * NULL when both faces are culled. The executor's surface space has y
 * down, where D3D judges winding too; a positive area is clockwise. */
static ID3D11RasterizerState *batch_rs(const Nv2aXBatch *b)
{
    const Nv2aRenderState *rs = b->state;
    if (!b->cull || !rs->cull_enable)
        return s_rs;
    if (rs->cull_face == 0x408u)
        return NULL;
    return s_rs_cull[rs->cull_face != 0x404u][rs->front_face == 0x901u];
}

static GpuRcConsts s_xlast;         /* the last batch's constants, for vsh_readback */

/* Everything a transformed batch needs but its vertices: targets, combiner
 * shader, textures, samplers and output-merger state. 1 ready, 0 failed, -1
 * nothing to draw (both faces culled). */
static int xdraw_prepare(const Nv2aSurface *s, const Nv2aXBatch *b)
{
    const Nv2aRenderState *rs;
    const Nv2aCombiner *rc;
    Nv2aCombiner simple;
    GpuRcKey key;
    GpuRcConsts c;
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11PixelShader *ps;
    ID3D11RasterizerState *rst;
    ID3D11ShaderResourceView *srv[4] = { NULL, NULL, NULL, NULL };
    ID3D11SamplerState *ss[4] = { NULL, NULL, NULL, NULL };
    float bf[4];
    int st;

    if (!b || !b->state || !begin(s, b->state) ||
        !sync_guest_surface(s_cur, 0) || !set_window_clip(s))
        return 0;
    rs = b->state;
    rst = batch_rs(b);
    if (!rst)
        return -1;
    rc = b->rc;
    if (!rc) {
        simple_combiner(b, &simple);
        rc = &simple;
    }
    memset(&key, 0, sizeof key);
    memcpy(key.color_icw, rc->color_icw, sizeof key.color_icw);
    memcpy(key.alpha_icw, rc->alpha_icw, sizeof key.alpha_icw);
    memcpy(key.color_ocw, rc->color_ocw, sizeof key.color_ocw);
    memcpy(key.alpha_ocw, rc->alpha_ocw, sizeof key.alpha_ocw);
    key.final0 = rc->final0;
    key.final1 = rc->final1;
    key.control = rc->control;
    key.stage_program = rc->stage_program;
    key.clip_plane_mode = b->clip_plane_mode;

    memset(&c, 0, sizeof c);
    for (st = 0; st < 4; st++) {
        uint32_t mode = (rc->stage_program >> (st * 5)) & 0x1F;
        int cube = b->tex_cube[st] && mode == 3;
        float tw = 1.0f, th = 1.0f;
        if (s_depth_texture && b->tex_valid[st] && b->tex[st].color == 0x2E &&
            mode != 0 && (b->tex_cube[st] || (mode != 1 && mode != 2))) {
            reject_depth_mode(mode, b->tex_cube[st], 0);
            return 0;
        }
        if (!b->tex_valid[st] || mode < 1 || mode > 3)
            continue;
        srv[st] = get_texture(&b->tex[st], cube, st, &tw, &th);
        if (!srv[st]) {
            if (required_depth_alias(&b->tex[st], cube))
                return 0;
            continue;
        }
        key.tex_valid[st] = 1;
        key.tex_normalized[st] = b->tex_normalized[st];
        key.tex_cube[st] = (uint8_t)cube;
        if (s_depth_texture && b->tex[st].color == 0x2E) {
            key.tex_depth[st] = 1;
            key.shadow_func = nv2a_backend_reg(0x1E6C) & 7;
        }
        ss[st] = get_sampler(b->tex[st].addr_u, b->tex[st].addr_v, b->tex[st].filter);
        c.texinfo[st][0] = 1.0f / tw;
        c.texinfo[st][1] = 1.0f / th;
    }
    ps = get_rc_shader(&key);
    if (!ps)
        return 0;
    c.surf[0] = (float)s_cur->w; c.surf[1] = (float)s_cur->h;
    c.surf[2] = 1.0f / s_cur->w; c.surf[3] = 1.0f / s_cur->h;
    depth_consts(rs, c.depth);
    set_alpha_test(rs, c.alpha);
    c.fogc[0] = b->fogc[0]; c.fogc[1] = b->fogc[1]; c.fogc[2] = b->fogc[2];
    for (st = 0; st < 8; st++) {
        unpack_argb(rc->factor0[st], c.c0[st]);
        unpack_argb(rc->factor1[st], c.c1[st]);
    }
    unpack_argb(rc->final_c0, c.fc0);
    unpack_argb(rc->final_c1, c.fc1);
    if (gpu_call_failed(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_xcb, 0,
                                                D3D11_MAP_WRITE_DISCARD, 0, &m),
                        "mapping extended constants"))
        return 0;
    memcpy(m.pData, &c, sizeof c);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_xcb, 0);
    s_xlast = c;
    blend_factor(rs, bf);
    ID3D11DeviceContext_PSSetShader(s_ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(s_ctx, 0, 1, &s_xcb);
    ID3D11DeviceContext_PSSetShaderResources(s_ctx, 0, 4, srv);
    ID3D11DeviceContext_PSSetSamplers(s_ctx, 0, 4, ss);
    ID3D11DeviceContext_RSSetState(s_ctx, rst);
    ID3D11DeviceContext_OMSetBlendState(s_ctx, get_blend(rs), bf, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, get_ds(rs, s_cur_depth != NULL),
                                               rs->stencil_ref & 0xFF);
    return 1;
}

static void d3d_xdraw(const Nv2aSurface *s, const Nv2aXBatch *b)
{
    UINT stride = sizeof(Nv2aXVertex), offs;

    if (!b || !b->count || xdraw_prepare(s, b) <= 0)
        return;
    offs = ring_push(b->vertices, b->count * stride, stride);
    if (offs == ~0u)
        return;
    ID3D11DeviceContext_IASetInputLayout(s_ctx, s_xlayout);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, 1, &s_vring, &stride, &offs);
    ID3D11DeviceContext_VSSetShader(s_ctx, s_xvs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx, 0, 1, &s_xcb);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    ID3D11DeviceContext_Draw(s_ctx, b->count, 0);
    s_cur->gpu_dirty = 1;
    if (s_cur_depth)
        s_cur_depth->revision++;
    s_st.draws++;
    s_st.xdraws++;
    s_st.tris += b->count / 3;
}

/* ---- vertex programs on the GPU (draw_vsh) ----------------------------------- */

#define MAX_VSH 1024
#define VSH_MAX_INSTR 136
#define VSH_RAW_IL 8
typedef struct {
    uint64_t key;
    D3D11_INPUT_ELEMENT_DESC el[16];
    ID3D11InputLayout *il;
} RawIl;
typedef struct {
    uint64_t hash;
    uint32_t n, mask, nin;
    uint8_t conv[16];                   /* the shader's conversions; 0 = floats */
    RawIl raw[VSH_RAW_IL];              /* layouts for raw streams */
    uint32_t raw_next;
    uint32_t words[VSH_MAX_INSTR][4];
    char *src;                          /* worker input */
    size_t len;
    ID3DBlob *blob;                     /* worker output */
    volatile LONG state;                /* 0 queued, 1 compiled, 2 ready, -1 failed */
    ID3D11VertexShader *vs;
    ID3D11InputLayout *il;              /* ATTR0..nin-1 as float4s in one stream */
    void *code;                         /* bytecode, for layouts and readback */
    SIZE_T code_size;
    ID3D11GeometryShader *so;           /* stream-output twin, for readback */
} Vsh;

static Vsh *s_vsh;
static volatile LONG s_nvsh, s_vsh_compiles, s_vsh_failures;
static HANDLE s_vsh_wake, s_vsh_thread;
static ID3D11Buffer *s_kcb, *s_iring;
static UINT s_iring_size, s_iring_pos;
static GpuVshConsts s_kshadow;
static int s_kvalid;
static Vsh *s_vlast;                    /* the last draw_vsh, for vsh_readback */
static int s_vlast_culled;
static UINT s_vlast_voff, s_vlast_stride, s_vlast_nverts, s_vlast_ns;
static UINT s_vlast_strides[NV2A_XVS_MAX_STREAMS], s_vlast_offs[NV2A_XVS_MAX_STREAMS];
static int s_vlast_raw;
static ID3D11InputLayout *s_vlast_il;
static uint32_t s_vsh_flags;            /* GPU_VSH_* for every shader */
static int s_vsh_sync;                  /* RECOMP_GPU_VSH_SYNC */

static DWORD WINAPI vsh_worker(LPVOID arg)
{
    (void)arg;
    for (;;) {
        LONG i, n = s_nvsh;
        int did = 0;
        for (i = 0; i < n; i++) {
            Vsh *v = &s_vsh[i];
            ID3DBlob *blob = NULL, *err = NULL;
            if (v->state != 0)
                continue;
            if (FAILED(D3DCompile(v->src, v->len, "vsh", NULL, NULL, "vs_main", "vs_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS,
                                  0, &blob, &err))) {
                if (InterlockedIncrement(&s_vsh_failures) <= 3)
                    fprintf(stderr, "[GPU-D3D11] vertex shader %ld failed: %s\n", (long)i,
                            err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
                if (err)
                    ID3D10Blob_Release(err);
                InterlockedExchange(&v->state, -1);
            } else {
                if (err)
                    ID3D10Blob_Release(err);
                v->blob = blob;
                InterlockedIncrement(&s_vsh_compiles);
                InterlockedExchange(&v->state, 1);
            }
            did = 1;
        }
        if (!did)
            WaitForSingleObject(s_vsh_wake, INFINITE);
    }
}

static uint64_t prog_hash(const uint32_t (*p)[4], uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 4; j++)
            h = (h ^ p[i][j]) * 1099511628211ull;
    return h ^ n;
}

/* On the NV2A thread: the compiled shader to a vertex shader and its float
 * input layout. */
static void vsh_finish(Vsh *v)
{
    static const uint8_t no_conv[16];
    D3D11_INPUT_ELEMENT_DESC il[16];
    const void *code = ID3D10Blob_GetBufferPointer(v->blob);
    SIZE_T size = ID3D10Blob_GetBufferSize(v->blob);
    uint32_t k;
    int ok;

    ok = !gpu_call_failed(ID3D11Device_CreateVertexShader(s_dev, code, size, NULL, &v->vs),
                          "creating vertex-program shader");
    if (ok && v->nin && !memcmp(v->conv, no_conv, sizeof no_conv)) {
        for (k = 0; k < v->nin; k++) {
            il[k].SemanticName = "ATTR";
            il[k].SemanticIndex = k;
            il[k].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            il[k].InputSlot = 0;
            il[k].AlignedByteOffset = 16 * k;
            il[k].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            il[k].InstanceDataStepRate = 0;
        }
        ok = !gpu_call_failed(ID3D11Device_CreateInputLayout(s_dev, il, v->nin, code, size, &v->il),
                              "creating vertex-program input layout");
    }
    if (ok && (v->code = malloc(size)) != NULL) {
        memcpy(v->code, code, size);
        v->code_size = size;
    }
    ID3D10Blob_Release(v->blob);
    v->blob = NULL;
    free(v->src);
    v->src = NULL;
    v->state = ok ? 2 : -1;
}

static void vsh_conv_key(const Nv2aXVsBatch *b, uint8_t conv[16])
{
    uint32_t k;
    memset(conv, 0, 16);
    if (!b->nstreams)
        return;
    for (k = 0; k < b->ninputs && k < 16; k++) {
        uint8_t c = b->in[k].conv;
        switch (c >> 4) {
        case NV2A_XVS_COLOR: case NV2A_XVS_UBYTE: case NV2A_XVS_SHORT: case NV2A_XVS_CMP:
            conv[k] = c;
            break;
        default:
            break;
        }
    }
}

/* The ready shader for a batch's program and input formats, or NULL while it
 * compiles (the executor then transforms the batch on the CPU). */
static Vsh *get_vsh(const Nv2aXVsBatch *b)
{
    static char src[96 * 1024];
    static LONG last = -1;
    uint8_t conv[16];
    uint64_t h = prog_hash(b->program, b->ninstr);
    LONG i, n = s_nvsh;
    Vsh *v;

    vsh_conv_key(b, conv);
    for (i = -1; i < n; i++) {
        LONG j = i < 0 ? last : i;
        if (j < 0 || (i >= 0 && i == last))
            continue;
        v = &s_vsh[j];
        if (v->hash != h || v->n != b->ninstr || memcmp(v->conv, conv, sizeof conv)
            || memcmp(v->words, b->program, b->ninstr * sizeof v->words[0]))
            continue;
        last = j;
        if (v->state == 1)
            vsh_finish(v);
        return v->state == 2 ? v : NULL;
    }
    if (n == MAX_VSH || b->ninstr > VSH_MAX_INSTR)
        return NULL;
    if (!s_vsh && !(s_vsh = (Vsh *)calloc(MAX_VSH, sizeof *s_vsh)))
        return NULL;
    if (!s_vsh_thread) {
        s_vsh_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
        s_vsh_thread = CreateThread(NULL, 0, vsh_worker, NULL, 0, NULL);
        if (!s_vsh_thread)
            return NULL;
    }
    v = &s_vsh[n];
    memset(v, 0, sizeof *v);
    v->hash = h;
    v->n = b->ninstr;
    v->mask = b->input_mask;
    v->nin = b->ninputs;
    memcpy(v->conv, conv, sizeof conv);
    memcpy(v->words, b->program, b->ninstr * sizeof v->words[0]);
    v->len = gpu_vsh_hlsl(b->program, b->ninstr, b->input_mask, conv, s_vsh_flags,
                          src, sizeof src);
    if (!v->len || !(v->src = (char *)malloc(v->len + 1))) {
        v->state = -1;
    } else {
        memcpy(v->src, src, v->len + 1);
    }
    InterlockedExchange(&s_nvsh, n + 1);
    SetEvent(s_vsh_wake);
    if (s_vsh_sync) {
        /* RECOMP_GPU_VSH_SYNC=1 (tests): wait for the compile rather than
         * handing this batch to the CPU. */
        while (v->state == 0)
            Sleep(1);
        if (v->state == 1)
            vsh_finish(v);
        return v->state == 2 ? v : NULL;
    }
    return NULL;
}

static ID3D11GeometryShader *vsh_filter_gs(void)
{
    static ID3D11GeometryShader *gs;
    static int tried;
    ID3DBlob *blob = NULL;

    if (tried)
        return gs;
    tried = 1;
    if (!compile(gpu_vsh_gs_hlsl, strlen(gpu_vsh_gs_hlsl), "vsh_gs", "gs_main", "gs_5_0", &blob))
        return NULL;
    if (gpu_call_failed(ID3D11Device_CreateGeometryShader(s_dev, ID3D10Blob_GetBufferPointer(blob),
                                                          ID3D10Blob_GetBufferSize(blob), NULL, &gs),
                        "creating vertex filter shader"))
        gs = NULL;
    ID3D10Blob_Release(blob);
    return gs;
}

/* fetch_attr's integer conversions as a table: x/1023 at x + 1024 for x in
 * -1024..1023, z/511 at z + 2560 for z in -512..511, c/255 at c + 3072. */
static ID3D11ShaderResourceView *vfetch_table(void)
{
    static ID3D11ShaderResourceView *srv;
    static int tried;
    static float t[3328];
    D3D11_BUFFER_DESC bd;
    D3D11_SUBRESOURCE_DATA sd;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd;
    ID3D11Buffer *buf = NULL;
    int i;

    if (tried)
        return srv;
    tried = 1;
    for (i = -1024; i < 1024; i++)
        t[i + 1024] = (float)i / 1023.0f;
    for (i = -512; i < 512; i++)
        t[i + 2560] = (float)i / 511.0f;
    for (i = 0; i < 256; i++)
        t[i + 3072] = (float)i / 255.0f;
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = sizeof t;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    memset(&sd, 0, sizeof sd);
    sd.pSysMem = t;
    if (gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, &sd, &buf),
                        "creating vertex conversion table"))
        return NULL;
    memset(&vd, 0, sizeof vd);
    vd.Format = DXGI_FORMAT_R32_FLOAT;
    vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    vd.Buffer.FirstElement = 0;
    vd.Buffer.NumElements = 3328;
    if (gpu_call_failed(ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)buf, &vd, &srv),
                        "creating vertex conversion view"))
        srv = NULL;
    ID3D11Buffer_Release(buf);
    return srv;
}

static DXGI_FORMAT vfetch_format(uint8_t conv)
{
    uint32_t s = conv & 15u;
    switch (conv >> 4) {
    case NV2A_XVS_FLOAT:
        return s == 1 ? DXGI_FORMAT_R32_FLOAT : s == 2 ? DXGI_FORMAT_R32G32_FLOAT
             : s == 3 ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
    case NV2A_XVS_CONST: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case NV2A_XVS_COLOR: return DXGI_FORMAT_R8G8B8A8_UINT;
    case NV2A_XVS_UBYTE:
        return s == 1 ? DXGI_FORMAT_R8_UINT : s == 2 ? DXGI_FORMAT_R8G8_UINT : DXGI_FORMAT_R8G8B8A8_UINT;
    case NV2A_XVS_SHORT:
        return s == 1 ? DXGI_FORMAT_R16_SINT : s == 2 ? DXGI_FORMAT_R16G16_SINT
                                                      : DXGI_FORMAT_R16G16B16A16_SINT;
    case NV2A_XVS_CMP:   return DXGI_FORMAT_R32_UINT;
    default:             return DXGI_FORMAT_UNKNOWN;
    }
}

static ID3D11InputLayout *vfetch_layout(Vsh *v, const Nv2aXVsBatch *b)
{
    D3D11_INPUT_ELEMENT_DESC el[16];
    uint64_t key = 1469598103934665603ull;
    uint32_t k, i;
    RawIl *e;

    if (!v->code || !v->nin || v->nin > 16)
        return NULL;
    memset(el, 0, sizeof el);
    for (k = 0; k < v->nin; k++) {
        const Nv2aXVsInput *in = &b->in[k];
        el[k].SemanticName = "ATTR";
        el[k].SemanticIndex = k;
        el[k].Format = vfetch_format(in->conv);
        el[k].InputSlot = in->stream;
        el[k].AlignedByteOffset = in->offset;
        el[k].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        if (el[k].Format == DXGI_FORMAT_UNKNOWN)
            return NULL;
        key = (key ^ ((uint64_t)el[k].Format | (uint64_t)in->stream << 8
                      | (uint64_t)in->offset << 16)) * 1099511628211ull;
    }
    for (i = 0; i < VSH_RAW_IL; i++) {
        e = &v->raw[i];
        if (e->il && e->key == key && !memcmp(e->el, el, v->nin * sizeof el[0]))
            return e->il;
    }
    e = &v->raw[v->raw_next++ % VSH_RAW_IL];
    if (e->il)
        ID3D11InputLayout_Release(e->il);
    e->il = NULL;
    if (gpu_call_failed(ID3D11Device_CreateInputLayout(s_dev, el, v->nin, v->code,
                                                       v->code_size, &e->il),
                        "creating raw vertex input layout"))
        return NULL;
    e->key = key;
    memcpy(e->el, el, sizeof el);
    return e->il;
}

static int d3d_vsh_raw_ok(void)
{
    return 1;
}

/* Indices: a second dynamic ring, mapped as the vertex ring is. */
static UINT index_push(const uint32_t *data, UINT count)
{
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT bytes = count * 4u, off;

    if (!bytes)
        return ~0u;
    if (!s_iring || bytes > s_iring_size) {
        D3D11_BUFFER_DESC bd;
        UINT want = bytes * 4 > (16u << 20) ? bytes * 4 : (16u << 20);
        if (s_iring) ID3D11Buffer_Release(s_iring);
        s_iring = NULL;
        s_iring_size = 0;
        memset(&bd, 0, sizeof bd);
        bd.ByteWidth = want;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_iring),
                            "creating index ring"))
            return ~0u;
        s_iring_size = want;
        s_iring_pos = want;
    }
    off = (s_iring_pos + 3u) & ~3u;
    if (off > s_iring_size || s_iring_size - off < bytes) {
        mode = D3D11_MAP_WRITE_DISCARD;
        off = 0;
    }
    if (gpu_call_failed(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_iring, 0, mode, 0, &m),
                        "mapping index ring"))
        return ~0u;
    memcpy((uint8_t *)m.pData + off, data, bytes);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_iring, 0);
    s_iring_pos = off + bytes;
    return off;
}

/* Several streams into the vertex ring at once, 16-byte aligned each. */
static int vertex_streams_push(const Nv2aXVsBatch *b)
{
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT pos[NV2A_XVS_MAX_STREAMS], total = 0, base, k;

    for (k = 0; k < b->nstreams; k++) {
        total = (total + 15u) & ~15u;
        pos[k] = total;
        total += b->streams[k].bytes;
    }
    if (!ring_ensure(total))
        return 0;
    base = (s_vring_pos + 15u) & ~15u;
    if (base > s_vring_size || s_vring_size - base < total) {
        mode = D3D11_MAP_WRITE_DISCARD;
        base = 0;
    }
    if (gpu_call_failed(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_vring, 0, mode, 0, &m),
                        "mapping vertex streams"))
        return 0;
    for (k = 0; k < b->nstreams; k++) {
        memcpy((uint8_t *)m.pData + base + pos[k], b->streams[k].data, b->streams[k].bytes);
        s_vlast_strides[k] = b->streams[k].stride;
        s_vlast_offs[k] = base + pos[k];
    }
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_vring, 0);
    s_vring_pos = base + total;
    return 1;
}

static int d3d_xdraw_vsh(const Nv2aSurface *s, const Nv2aXVsBatch *b)
{
    Vsh *v;
    ID3D11Buffer *cbs[2];
    UINT voff = 0, ioff, stride;
    ID3D11InputLayout *il;
    int r;

    s_vlast = NULL;
    s_vlast_culled = 0;
    if (!b || !b->nindices || !ensure_device())
        return 0;
    if (!(v = get_vsh(b))) {
        s_st.vfall++;
        return 0;
    }
    r = xdraw_prepare(s, &b->xb);
    if (r < 0) {
        s_vlast_culled = 1;
        return 1;               /* both faces culled: nothing to draw */
    }
    if (r == 0)
        return 0;
    if (!s_kcb) {
        D3D11_BUFFER_DESC bd;
        memset(&bd, 0, sizeof bd);
        bd.ByteWidth = (sizeof(GpuVshConsts) + 15) & ~15u;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &s_kcb),
                            "creating vertex-program constants"))
            return 0;
    }
    {
        /* The constants: skipped when neither the constant file (by its
         * generation) nor the per-batch values changed. */
        static GpuVshConsts k;
        static uint32_t kgen;
        static int kgen_ok;
        float vsinfo[4], foginfo[4];
        vsinfo[0] = b->aa[0]; vsinfo[1] = b->aa[1];
        vsinfo[2] = b->xb.near_clip ? 1.0f : 0.0f; vsinfo[3] = b->xb.near_z;
        foginfo[0] = b->fog_enable ? 1.0f : 0.0f;
        foginfo[1] = (float)b->fog_mode;
        foginfo[2] = b->fog_param[0]; foginfo[3] = b->fog_param[1];
        if (s_kvalid && kgen_ok && b->consts_gen == kgen
                && !memcmp(vsinfo, s_kshadow.vsinfo, sizeof vsinfo)
                && !memcmp(foginfo, s_kshadow.foginfo, sizeof foginfo)) {
            s_st.kskip++;
        } else {
            memcpy(k.k, b->consts, sizeof k.k);
            memcpy(k.vsinfo, vsinfo, sizeof vsinfo);
            memcpy(k.foginfo, foginfo, sizeof foginfo);
            if (!s_kvalid || memcmp(&k, &s_kshadow, sizeof k)) {
                D3D11_MAPPED_SUBRESOURCE m;
                if (gpu_call_failed(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_kcb, 0,
                                                            D3D11_MAP_WRITE_DISCARD, 0, &m),
                                    "mapping vertex-program constants"))
                    return 0;
                memcpy(m.pData, &k, sizeof k);
                ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_kcb, 0);
                s_kshadow = k;
                s_kvalid = 1;
                s_st.kup++;
            }
            kgen = b->consts_gen;
            kgen_ok = 1;
        }
    }
    stride = 16u * v->nin;
    il = v->il;
    if (b->nstreams && v->nin) {
        ID3D11Buffer *bufs[NV2A_XVS_MAX_STREAMS];
        static int conv_bound;
        UINT k;
        if (b->nstreams > NV2A_XVS_MAX_STREAMS || !(il = vfetch_layout(v, b)))
            return 0;
        if (!vertex_streams_push(b))
            return 0;
        for (k = 0; k < b->nstreams; k++)
            bufs[k] = s_vring;
        if (!conv_bound) {
            ID3D11ShaderResourceView *t = vfetch_table();
            if (!t)
                return 0;
            ID3D11DeviceContext_VSSetShaderResources(s_ctx, 0, 1, &t);
            conv_bound = 1;
        }
        ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, b->nstreams, bufs, s_vlast_strides,
                                               s_vlast_offs);
        s_vlast_ns = b->nstreams;
        s_vlast_il = il;
        s_vlast_raw = 1;
        s_st.vraw++;
    } else {
        if (v->nin) {
            voff = ring_push(b->inputs, b->nverts * stride, stride);
            if (voff == ~0u)
                return 0;
            ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, 1, &s_vring, &stride, &voff);
        }
        s_vlast_raw = 0;
    }
    ioff = index_push(b->indices, b->nindices);
    if (ioff == ~0u)
        return 0;
    cbs[0] = s_xcb;
    cbs[1] = s_kcb;
    ID3D11DeviceContext_IASetInputLayout(s_ctx, il);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetIndexBuffer(s_ctx, s_iring, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetShader(s_ctx, v->vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_ctx, 0, 2, cbs);
    ID3D11DeviceContext_GSSetShader(s_ctx, vsh_filter_gs(), NULL, 0);
    ID3D11DeviceContext_DrawIndexed(s_ctx, b->nindices, ioff / 4u, 0);
    s_cur->gpu_dirty = 1;
    if (s_cur_depth)
        s_cur_depth->revision++;
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    s_vlast = v;
    s_vlast_voff = voff;
    s_vlast_stride = stride;
    s_vlast_nverts = b->nverts;
    s_st.draws++;
    s_st.vdraws++;
    s_st.tris += b->nindices / 3;
    return 1;
}

/* RECOMP_GPU_VSH_VERIFY: the last draw_vsh batch's vertices through its
 * shader again, captured with stream output, mapped back to the executor's
 * terms (surface pixels and the title's z). */
static int d3d_vsh_readback(Nv2aXVsOut *out, uint32_t n)
{
    static ID3D11Buffer *sob, *stg;
    static UINT so_cap;
    static const D3D11_SO_DECLARATION_ENTRY decl[] = {
        { 0, "SV_POSITION", 0, 0, 4, 0 },
        { 0, "COLOR", 0, 0, 4, 0 },
        { 0, "COLOR", 1, 0, 4, 0 },
        { 0, "FOG", 0, 0, 1, 0 },
        { 0, "SV_ClipDistance", 0, 0, 2, 0 },
        { 0, NULL, 0, 0, 1, 0 },                  /* pad */
        { 0, "TEXCOORD", 0, 0, 4, 0 },
        { 0, "TEXCOORD", 1, 0, 4, 0 },
        { 0, "TEXCOORD", 2, 0, 4, 0 },
        { 0, "TEXCOORD", 3, 0, 4, 0 },
    };
    UINT stride = sizeof(Nv2aXVsOut), zero = 0, need = n * (UINT)sizeof(Nv2aXVsOut), i;
    Vsh *v = s_vlast;
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11Buffer *nullb = NULL;

    if (s_vlast_culled) {
        fprintf(stderr, "[VSH-VERIFY] skipped fully culled batch: no GPU vertex output\n");
        return 0;
    }
    if (!v || !v->code || n != s_vlast_nverts || !n)
        return 0;
    gpu_d3d11_check_device(s_dev, S_OK, "starting vertex verification readback");
    if (!v->so && gpu_call_failed(ID3D11Device_CreateGeometryShaderWithStreamOutput(
                    s_dev, v->code, v->code_size, decl, 10, &stride, 1,
                    D3D11_SO_NO_RASTERIZED_STREAM, NULL, &v->so),
                    "creating vertex verification shader")) {
        v->so = NULL;
        return 0;
    }
    if (need > so_cap) {
        D3D11_BUFFER_DESC bd;
        if (sob) ID3D11Buffer_Release(sob);
        if (stg) ID3D11Buffer_Release(stg);
        sob = stg = NULL;
        so_cap = 0;
        memset(&bd, 0, sizeof bd);
        bd.ByteWidth = need;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_STREAM_OUTPUT;
        if (gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &sob),
                            "creating vertex verification output"))
            return 0;
        bd.Usage = D3D11_USAGE_STAGING;
        bd.BindFlags = 0;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (gpu_call_failed(ID3D11Device_CreateBuffer(s_dev, &bd, NULL, &stg),
                            "creating vertex verification staging buffer"))
            return 0;
        so_cap = need;
    }
    if (s_vlast_raw) {
        ID3D11Buffer *bufs[NV2A_XVS_MAX_STREAMS];
        for (i = 0; i < s_vlast_ns; i++)
            bufs[i] = s_vring;
        ID3D11DeviceContext_IASetInputLayout(s_ctx, s_vlast_il);
        ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, s_vlast_ns, bufs, s_vlast_strides,
                                               s_vlast_offs);
    } else {
        ID3D11DeviceContext_IASetInputLayout(s_ctx, v->il);
        if (v->nin)
            ID3D11DeviceContext_IASetVertexBuffers(s_ctx, 0, 1, &s_vring, &s_vlast_stride,
                                                   &s_vlast_voff);
    }
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11DeviceContext_VSSetShader(s_ctx, v->vs, NULL, 0);
    ID3D11DeviceContext_GSSetShader(s_ctx, v->so, NULL, 0);
    ID3D11DeviceContext_SOSetTargets(s_ctx, 1, &sob, &zero);
    ID3D11DeviceContext_Draw(s_ctx, n, 0);
    ID3D11DeviceContext_SOSetTargets(s_ctx, 1, &nullb, &zero);
    ID3D11DeviceContext_GSSetShader(s_ctx, NULL, NULL, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(s_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)stg, (ID3D11Resource *)sob);
    if (gpu_call_failed(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)stg, 0,
                                                D3D11_MAP_READ, 0, &m),
                        "mapping vertex verification readback"))
        return 0;
    memcpy(out, m.pData, need);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)stg, 0);
    for (i = 0; i < n; i++) {
        float *p = out[i].pos, w = p[3];
        if (w != 0.0f) {
            float zd = p[2] / w;
            p[0] = (p[0] / w + 1.0f) * 0.5f * s_xlast.surf[0];
            p[1] = (1.0f - p[1] / w) * 0.5f * s_xlast.surf[1];
            p[2] = s_xlast.depth[1] != 0.0f ? zd / s_xlast.depth[1] + s_xlast.depth[0] : zd;
        }
    }
    return 1;
}

/* ---- visibility tests -------------------------------------------------------- */

#define MAX_QUERIES 64
#define MAX_REPORTS 1024
static ID3D11Query *s_q_pool[MAX_QUERIES];
static int s_q_free;                    /* pool entries not in use */
static ID3D11Query *s_q_pending[MAX_QUERIES];
static double s_q_pending_divisor[MAX_QUERIES];
static int s_q_npending;
static ID3D11Query *s_q_active;
static uint64_t s_q_total;
static ID3D11Query *s_event_pool[MAX_REPORTS];
static int s_event_free;

static void query_error(const char *operation, HRESULT hr)
{
    gpu_d3d11_check_device(s_dev, hr, operation);
    fprintf(stderr, "[GPU-D3D11] %s failed 0x%08lX (device removed reason 0x%08lX);"
                    " cannot safely complete visibility reports\n",
            operation, (unsigned long)hr,
            (unsigned long)ID3D11Device_GetDeviceRemovedReason(s_dev));
    fflush(stderr);
    abort();
}

static ID3D11Query *query_create(D3D11_QUERY type)
{
    HRESULT hr;
    D3D11_QUERY_DESC qd;
    ID3D11Query *q = NULL;
    qd.Query = type;
    qd.MiscFlags = 0;
    hr = ID3D11Device_CreateQuery(s_dev, &qd, &q);
    if (FAILED(hr))
        query_error("CreateQuery", hr);
    return q;
}

static ID3D11Query *query_get(void)
{
    return s_q_free ? s_q_pool[--s_q_free] : query_create(D3D11_QUERY_OCCLUSION);
}

static void query_put(ID3D11Query *q)
{
    if (s_q_free < MAX_QUERIES)
        s_q_pool[s_q_free++] = q;
    else
        ID3D11Query_Release(q);
}

static ID3D11Query *event_get(void)
{
    return s_event_free ? s_event_pool[--s_event_free] : query_create(D3D11_QUERY_EVENT);
}

static void event_put(ID3D11Query *q)
{
    if (s_event_free < MAX_REPORTS)
        s_event_pool[s_event_free++] = q;
    else
        ID3D11Query_Release(q);
}

static double query_divisor(void)
{
    double logical, physical;

    if (!s_cur || !s_cur->w || !s_cur->h)
        return 1.0;
    logical = (double)s_cur->w * s_cur->h;
    physical = (double)s_cur->pw * s_cur->ph;
    return physical > 0.0 ? physical / logical : 1.0;
}

static uint64_t normalize_query_count(UINT64 count, double divisor)
{
    return divisor < 0.999999 || divisor > 1.000001
               ? (uint64_t)((double)count / divisor + 0.5)
               : count;
}

/* Wait for every ended query and add its count. */
static void query_drain(void)
{
    int i;
    double t0 = qpc_ms();
    for (i = 0; i < s_q_npending; i++) {
        UINT64 n = 0;
        HRESULT hr;
        do {
            hr = gpu_d3d11_get_data(s_dev, s_ctx, (ID3D11Asynchronous *)s_q_pending[i],
                                    &n, sizeof n, 0, "waiting for visibility query");
            if (hr == S_FALSE)
                SwitchToThread();
        } while (hr == S_FALSE);
        if (hr != S_OK)
            query_error("GetData(occlusion)", hr);
        s_q_total += normalize_query_count(n, s_q_pending_divisor[i]);
        query_put(s_q_pending[i]);
    }
    s_q_npending = 0;
    s_q_wait_ms += qpc_ms() - t0;
}

static void query_end(void)
{
    if (!s_q_active)
        return;
    ID3D11DeviceContext_End(s_ctx, (ID3D11Asynchronous *)s_q_active);
    if (s_q_npending == MAX_QUERIES)
        query_drain();
    s_q_pending[s_q_npending] = s_q_active;
    s_q_pending_divisor[s_q_npending++] = query_divisor();
    s_q_active = NULL;
}

static int query_suspend_clear(void)
{
    int counting = s_q_active != NULL;
    if (counting)
        query_end();
    return counting;
}

static void query_resume_clear(int counting)
{
    if (counting) {
        s_q_active = query_get();
        ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
    }
}

/* RECOMP_GPU_ZPASS=async: a report is written into guest memory when its
 * queries complete, polled from the NV2A thread's loop, instead of waiting
 * for the GPU at GET_REPORT. The title sees the result later in time, as it
 * would from the console's GPU, and D3D's own polling waits for it. Off by
 * default while it is measured. */
typedef struct {
    uint32_t va;
    uint64_t stamp;
    uint64_t prefix;
    int reset;                          /* a clear came before these queries */
    int nq;
    ID3D11Query *event;
    ID3D11Query *q[MAX_QUERIES];
    double divisor[MAX_QUERIES];
} Report;
static Report *s_rep;                   /* ring of MAX_REPORTS */
static uint32_t s_rep_head, s_rep_tail;
static uint64_t s_rep_running;          /* the count since the last clear, resolved */
static int s_rep_reset_next;
static int s_zpass_async;
static double s_rep_last_flush;

static void report_write(const Report *r, uint64_t count)
{
    uint8_t *at = GUEST(r->va);
    uint32_t c = count > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)count, done = 0;
    memcpy(at, &r->stamp, 8);
    memcpy(at + 8, &c, 4);
    MemoryBarrier();
    memcpy(at + 12, &done, 4);
    s_rep_written++;
}

static void report_pending(void)
{
    double now = qpc_ms();
    if (now - s_rep_last_flush > 0.25) {
        ID3D11DeviceContext_Flush(s_ctx);
        s_rep_last_flush = now;
    }
    s_rep_flushes++;
}

/* Complete reports in command order, only after all preceding GPU work. */
static void d3d_poll(void)
{
    LONG owner = InterlockedCompareExchange(&s_device_thread, 0, 0);
    if (!owner || (DWORD)owner != GetCurrentThreadId() || !s_rep)
        return;
    while (s_rep_head != s_rep_tail) {
        Report *r = &s_rep[s_rep_head % MAX_REPORTS];
        uint64_t sum = r->prefix;
        BOOL done = FALSE;
        HRESULT hr;
        int i;
        hr = gpu_d3d11_get_data(s_dev, s_ctx, (ID3D11Asynchronous *)r->event, &done,
                                sizeof done, D3D11_ASYNC_GETDATA_DONOTFLUSH,
                                "polling visibility report event");
        if (hr == S_FALSE || (hr == S_OK && !done)) {
            report_pending();
            return;
        }
        if (hr != S_OK)
            query_error("GetData(report event)", hr);
        for (i = 0; i < r->nq; i++) {
            UINT64 n = 0;
            hr = gpu_d3d11_get_data(s_dev, s_ctx, (ID3D11Asynchronous *)r->q[i], &n,
                                    sizeof n, D3D11_ASYNC_GETDATA_DONOTFLUSH,
                                    "polling visibility query");
            if (hr == S_FALSE) {
                report_pending();
                return;
            }
            if (hr != S_OK)
                query_error("GetData(occlusion)", hr);
            sum += normalize_query_count(n, r->divisor[i]);
        }
        if (r->reset)
            s_rep_running = 0;
        s_rep_running += sum;
        report_write(r, s_rep_running);
        for (i = 0; i < r->nq; i++)
            query_put(r->q[i]);
        event_put(r->event);
        s_rep_head++;
    }
}

static int d3d_zpass_report(uint32_t report_va, uint64_t stamp)
{
    Report *r;
    int counting = s_q_active != NULL, i;

    if (!s_zpass_async || !s_dev)
        return 0;
    gpu_d3d11_check_device(s_dev, S_OK, "starting visibility report");
    if (!s_rep && !(s_rep = (Report *)calloc(MAX_REPORTS, sizeof *s_rep))) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate asynchronous reports; using synchronous queries\n");
        s_zpass_async = 0;
        return 0;
    }
    if (s_rep_tail - s_rep_head == MAX_REPORTS)
        while (s_rep_tail - s_rep_head == MAX_REPORTS) {
            d3d_poll();
            SwitchToThread();
        }
    query_end();
    r = &s_rep[s_rep_tail % MAX_REPORTS];
    r->va = report_va;
    r->stamp = stamp;
    r->prefix = s_q_total;
    r->reset = s_rep_reset_next;
    r->nq = s_q_npending;
    r->event = event_get();
    ID3D11DeviceContext_End(s_ctx, (ID3D11Asynchronous *)r->event);
    for (i = 0; i < s_q_npending; i++) {
        r->q[i] = s_q_pending[i];
        r->divisor[i] = s_q_pending_divisor[i];
    }
    s_q_npending = 0;
    s_q_total = 0;
    s_rep_reset_next = 0;
    s_rep_tail++;
    if (counting) {
        s_q_active = query_get();
        ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
    }
    d3d_poll();
    return 1;
}

static void d3d_zpass(int op)
{
    if (!ensure_device())
        return;
    if (s_zpass_async && op == NV2A_ZPASS_CLEAR) {
        /* Discarded queries may still be in flight, so never reuse them. */
        int counting = s_q_active != NULL, i;
        query_end();
        for (i = 0; i < s_q_npending; i++)
            ID3D11Query_Release(s_q_pending[i]);
        s_q_npending = 0;
        s_q_total = 0;
        s_rep_reset_next = 1;
        if (counting) {
            s_q_active = query_get();
            ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
        }
        return;
    }
    if (op == NV2A_ZPASS_BEGIN) {
        query_end();
        s_q_active = query_get();
        ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
    } else if (op == NV2A_ZPASS_END) {
        query_end();
    } else {                                /* CLEAR_REPORT_VALUE */
        int counting = s_q_active != NULL;
        query_end();
        query_drain();
        s_q_total = 0;
        if (counting) {
            s_q_active = query_get();
            ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
        }
    }
}

static uint32_t d3d_zpass_count(void)
{
    int counting = s_q_active != NULL;
    if (!s_dev)
        return 0;
    gpu_d3d11_check_device(s_dev, S_OK, "reading visibility count");
    query_end();
    query_drain();
    if (counting) {
        s_q_active = query_get();
        ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
    }
    s_q_reads++;
    return s_q_total > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)s_q_total;
}

/* ---- readback and flip ----------------------------------------------------- */

static ID3D11Texture2D *s_staging;
static uint32_t s_staging_w, s_staging_h, s_staging_bpp;
static ID3D11Texture2D *s_readback_resolve;
static ID3D11RenderTargetView *s_readback_resolve_rtv;
static uint32_t s_readback_resolve_w, s_readback_resolve_h, s_readback_resolve_bpp;
static ID3D11Texture2D *s_present_shared;
static ID3D11RenderTargetView *s_present_rtv;
static IDXGIKeyedMutex *s_present_mutex;
static HANDLE s_present_handle;
static uint32_t s_present_w, s_present_h;
static int s_present_unsubmitted, s_present_failed;

static void release_shared_present(void)
{
    if (s_present_mutex) IDXGIKeyedMutex_Release(s_present_mutex);
    if (s_present_rtv) ID3D11RenderTargetView_Release(s_present_rtv);
    if (s_present_shared) ID3D11Texture2D_Release(s_present_shared);
    s_present_mutex = NULL;
    s_present_rtv = NULL;
    s_present_shared = NULL;
    s_present_handle = NULL;
    s_present_w = s_present_h = 0;
}

static int create_shared_present(uint32_t width, uint32_t height)
{
    D3D11_TEXTURE2D_DESC td;
    IDXGIResource *resource = NULL;
    HRESULT hr;

    memset(&td, 0, sizeof td);
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &s_present_shared);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateRenderTargetView(s_dev, (ID3D11Resource *)s_present_shared,
                                                 NULL, &s_present_rtv);
    if (SUCCEEDED(hr))
        hr = ID3D11Texture2D_QueryInterface(s_present_shared, &IID_IDXGIResource,
                                            (void **)&resource);
    if (SUCCEEDED(hr))
        hr = IDXGIResource_GetSharedHandle(resource, &s_present_handle);
    if (SUCCEEDED(hr))
        hr = ID3D11Texture2D_QueryInterface(s_present_shared, &IID_IDXGIKeyedMutex,
                                            (void **)&s_present_mutex);
    if (resource)
        IDXGIResource_Release(resource);
    if (FAILED(hr) || !s_present_handle) {
        gpu_d3d11_check_device(s_dev, hr, "creating shared presentation texture");
        fprintf(stderr, "[GPU-D3D11] shared presentation texture failed 0x%08lX;"
                        " retaining CPU presentation\n", (unsigned long)hr);
        release_shared_present();
        s_present_failed = 1;
        return 0;
    }
    s_present_w = width;
    s_present_h = height;
    return 1;
}

static void publish_shared_present(Surf *sf)
{
    uint32_t width = sf->pw, height = sf->ph;
    HRESULT hr;

    if (!s_direct_present || s_present_failed)
        return;
    if (s_present_unsubmitted) {
        hr = IDXGIKeyedMutex_AcquireSync(s_present_mutex, 1, 0);
        if (hr == WAIT_TIMEOUT) {
            gpu_d3d11_check_device(s_dev, hr, "reclaiming shared frame");
            return;
        }
        if (hr == S_OK)
            hr = IDXGIKeyedMutex_ReleaseSync(s_present_mutex, 0);
        if (hr != S_OK) {
            gpu_d3d11_check_device(s_dev, hr, "reclaiming shared frame");
            fprintf(stderr, "[GPU-D3D11] cannot reclaim an unsubmitted shared frame 0x%08lX\n",
                    (unsigned long)hr);
            s_present_failed = 1;
            return;
        }
        s_present_unsubmitted = 0;
    }
    if (sf->bpp != 4 || sf->pitch != sf->w * 4)
        return;
    if (gpu_scale_requested())
        gpu_present_output_size(&width, &height);
    if (!gpu_present_shared_ready(width, height))
        return;
    if (s_present_shared && (s_present_w != width || s_present_h != height)) {
        hr = IDXGIKeyedMutex_AcquireSync(s_present_mutex, 0, 0);
        if (hr == WAIT_TIMEOUT) {
            s_st.present_busy++;
            return;
        }
        if (hr != S_OK) {
            gpu_d3d11_check_device(s_dev, hr, "resizing shared frame");
            fprintf(stderr, "[GPU-D3D11] shared frame resize acquire failed 0x%08lX;"
                            " retaining CPU presentation\n", (unsigned long)hr);
            s_present_failed = 1;
            return;
        }
        IDXGIKeyedMutex_ReleaseSync(s_present_mutex, 0);
        release_shared_present();
    }
    if (!s_present_shared && !create_shared_present(width, height))
        return;
    hr = IDXGIKeyedMutex_AcquireSync(s_present_mutex, 0, 0);
    if (hr == WAIT_TIMEOUT) {
        gpu_d3d11_check_device(s_dev, hr, "acquiring shared frame");
        s_st.present_busy++;
        return;
    }
    if (hr != S_OK) {
        gpu_d3d11_check_device(s_dev, hr, "acquiring shared frame");
        fprintf(stderr, "[GPU-D3D11] shared frame acquire failed 0x%08lX;"
                        " retaining CPU presentation\n", (unsigned long)hr);
        s_present_failed = 1;
        return;
    }
    if (sf->pw == width && sf->ph == height) {
        ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)s_present_shared,
                                         (ID3D11Resource *)sf->tex);
    } else {
        enum GpuScaleBlitMode mode =
            sf->pw > width || sf->ph > height ? GPU_SCALE_BLIT_BOX
                                              : GPU_SCALE_BLIT_CATMULL_ROM;
        if (!gpu_scale_blit(sf->srv, sf->pw, sf->ph, s_present_rtv, width, height, mode)) {
            fprintf(stderr, "[GPU-D3D11] final scaled presentation resolve failed;"
                            " retaining CPU presentation\n");
            IDXGIKeyedMutex_ReleaseSync(s_present_mutex, 0);
            s_present_failed = 1;
            return;
        }
    }
    ID3D11DeviceContext_Flush(s_ctx);
    hr = IDXGIKeyedMutex_ReleaseSync(s_present_mutex, 1);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(s_dev, hr, "releasing shared frame");
        fprintf(stderr, "[GPU-D3D11] shared frame release failed 0x%08lX;"
                        " retaining CPU presentation\n", (unsigned long)hr);
        s_present_failed = 1;
        return;
    }
    if (gpu_present_submit_shared(s_present_handle, width, height,
                                  xbox_FramebufferWindowNextFrameId()))
        s_st.present_shared++;
    else
        s_present_unsubmitted = 1;
}

static ID3D11Texture2D *readback_source(Surf *sf)
{
    D3D11_TEXTURE2D_DESC td;
    HRESULT hr;

    if (sf->pw == sf->w && sf->ph == sf->h)
        return sf->tex;
    if (!s_readback_resolve || s_readback_resolve_w != sf->w ||
        s_readback_resolve_h != sf->h || s_readback_resolve_bpp != sf->bpp) {
        if (s_readback_resolve_rtv)
            ID3D11RenderTargetView_Release(s_readback_resolve_rtv);
        if (s_readback_resolve)
            ID3D11Texture2D_Release(s_readback_resolve);
        s_readback_resolve_rtv = NULL;
        s_readback_resolve = NULL;
        memset(&td, 0, sizeof td);
        td.Width = sf->w;
        td.Height = sf->h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = surf_format(sf->bpp);
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &s_readback_resolve);
        if (SUCCEEDED(hr))
            hr = ID3D11Device_CreateRenderTargetView(
                s_dev, (ID3D11Resource *)s_readback_resolve, NULL, &s_readback_resolve_rtv);
        if (FAILED(hr)) {
            gpu_d3d11_check_device(s_dev, hr, "creating scaled readback resolve");
            fprintf(stderr, "[GPU-SCALE] readback resolve creation failed 0x%08lX\n",
                    (unsigned long)hr);
            if (s_readback_resolve_rtv)
                ID3D11RenderTargetView_Release(s_readback_resolve_rtv);
            if (s_readback_resolve)
                ID3D11Texture2D_Release(s_readback_resolve);
            s_readback_resolve_rtv = NULL;
            s_readback_resolve = NULL;
            return NULL;
        }
        s_readback_resolve_w = sf->w;
        s_readback_resolve_h = sf->h;
        s_readback_resolve_bpp = sf->bpp;
    }
    if (!gpu_scale_blit(sf->srv, sf->pw, sf->ph, s_readback_resolve_rtv,
                        sf->w, sf->h,
                        sf->pw > sf->w || sf->ph > sf->h ? GPU_SCALE_BLIT_BOX
                                                         : GPU_SCALE_BLIT_BILINEAR)) {
        fprintf(stderr, "[GPU-SCALE] guest-sized readback resolve failed\n");
        return NULL;
    }
    return s_readback_resolve;
}

static int d3d_readback(uint32_t color_va)
{
    Surf *sf = find_surf(color_va);
    ID3D11Texture2D *source;
    D3D11_MAPPED_SUBRESOURCE m;
    uint32_t y, row;
    double t0 = qpc_ms();
    HRESULT hr;
    int ok = 0;

    if (s_dev)
        gpu_d3d11_check_device(s_dev, S_OK, "starting surface readback");
    if (!sf || !s_dev)
        return 0;
    if (!sync_guest_surface(sf, 1))
        goto done;
    publish_shared_present(sf);
    source = readback_source(sf);
    if (!source)
        goto done;
    if (!s_staging || s_staging_w != sf->w || s_staging_h != sf->h || s_staging_bpp != sf->bpp) {
        D3D11_TEXTURE2D_DESC td;
        if (s_staging)
            ID3D11Texture2D_Release(s_staging);
        s_staging = NULL;
        memset(&td, 0, sizeof td);
        td.Width = sf->w; td.Height = sf->h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = surf_format(sf->bpp);
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &s_staging);
        if (FAILED(hr)) {
            gpu_d3d11_check_device(s_dev, hr, "creating readback texture");
            fprintf(stderr, "[GPU-D3D11] readback texture creation failed 0x%08lX\n",
                    (unsigned long)hr);
            goto done;
        }
        s_staging_w = sf->w; s_staging_h = sf->h; s_staging_bpp = sf->bpp;
    }
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)s_staging,
                                     (ID3D11Resource *)source);
    hr = ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_staging, 0,
                                D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(s_dev, hr, "mapping surface readback");
        fprintf(stderr, "[GPU-D3D11] readback map failed 0x%08lX\n", (unsigned long)hr);
        goto done;
    }
    row = sf->w * sf->bpp;
    if (row > sf->pitch)
        row = sf->pitch;
    for (y = 0; y < sf->h; y++)
        memcpy(GUEST(sf->va) + (size_t)y * sf->pitch, (const uint8_t *)m.pData + (size_t)y * m.RowPitch,
               row);
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_staging, 0);
    if (!snapshot_guest_surface(sf))
        goto done;
    sf->guest_check_gen = s_gen;
    sf->gpu_dirty = 0;
    s_st.readbacks++;
    s_st.readback_ms += qpc_ms() - t0;
    ok = 1;
done:
    if (s_cur)
        bind_targets(s_cur, s_cur_depth);
    return ok;
}

static void d3d_flip(void)
{
    uint32_t width, height;
    if (s_dev)
        gpu_d3d11_check_device(s_dev, S_OK, "completing flip");
    if (gpu_scale_native() && gpu_present_output_size(&width, &height))
        gpu_scale_set_output(width, height);
    s_gen++;
    stats_flip();
}

static const Nv2aBackend s_backend = { d3d_clear, d3d_draw, d3d_flip };
static const Nv2aBackendExt s_backend_ext = { d3d_xdraw, d3d_xdraw_vsh, d3d_vsh_readback,
                                              d3d_vsh_raw_ok, d3d_readback, d3d_zpass,
                                              d3d_zpass_count, d3d_zpass_report, d3d_poll,
                                              NV2A_BACKEND_CAP_WINDOW_CLIP };

int bruteforce_d3d11_install(void)
{
    const char *warp = getenv("RECOMP_D3D11_WARP");
    const char *mulzero = getenv("RECOMP_GPU_VSH_MULZERO");
    const char *tex_gpu = getenv("RECOMP_GPU_TEX_DECODE");
    const char *tex_log = getenv("RECOMP_GPU_TEXLOG");
    const char *tex_approx = getenv("RECOMP_GPU_TEX_CACHE_APPROX");
    const char *depth_texture = getenv("RECOMP_GPU_DEPTH_TEXTURE");
    const char *stencil_alias = getenv("RECOMP_GPU_STENCIL_ALIAS");
    const char *direct_present = getenv("RECOMP_GPU_DIRECT_PRESENT");

    if (depth_texture && *depth_texture && strcmp(depth_texture, "0") != 0 &&
        strcmp(depth_texture, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_DEPTH_TEXTURE must be 0 or 1\n");
        return 0;
    }
    if (stencil_alias && *stencil_alias && strcmp(stencil_alias, "0") != 0 &&
        strcmp(stencil_alias, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_STENCIL_ALIAS must be 0 or 1\n");
        return 0;
    }
    if (direct_present && *direct_present && strcmp(direct_present, "0") != 0 &&
        strcmp(direct_present, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_DIRECT_PRESENT must be 0 or 1\n");
        return 0;
    }
    if (tex_approx && *tex_approx && strcmp(tex_approx, "0") != 0 &&
        strcmp(tex_approx, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_TEX_CACHE_APPROX must be 0 or 1\n");
        return 0;
    }
    if (!gpu_scale_configure())
        return 0;
    if (!gpu_pscache_init())
        return 0;
    s_warp = warp && strcmp(warp, "1") == 0;
    s_depth_texture = depth_texture && strcmp(depth_texture, "1") == 0;
    s_stencil_alias = stencil_alias && strcmp(stencil_alias, "1") == 0;
    s_direct_present = (direct_present && strcmp(direct_present, "1") == 0) ||
                       (gpu_scale_requested() && (!direct_present || !*direct_present));
    if (gpu_scale_requested() && direct_present && strcmp(direct_present, "0") == 0) {
        fprintf(stderr, "[GPU-SCALE] scaled rendering requires shared GPU presentation;"
                        " RECOMP_GPU_DIRECT_PRESENT cannot be 0\n");
        return 0;
    }
    if (gpu_scale_requested() && (s_depth_texture || s_stencil_alias)) {
        fprintf(stderr, "[GPU-SCALE] scaled depth/stencil texture aliases are not supported;"
                        " disable RECOMP_GPU_DEPTH_TEXTURE and RECOMP_GPU_STENCIL_ALIAS\n");
        return 0;
    }
    s_tex_gpu = !tex_gpu || strcmp(tex_gpu, "0") != 0;
    s_tex_log = tex_log && strcmp(tex_log, "1") == 0;
    s_tex_approx = tex_approx && strcmp(tex_approx, "1") == 0;
    if (s_tex_approx)
        fprintf(stderr, "[GPU-D3D11] experimental approximate texture cache enabled;"
                        " texture rewrites can be missed\n");
    s_vsh_flags = mulzero && strcmp(mulzero, "1") == 0 ? GPU_VSH_MULZERO : 0;
    s_vsh_sync = getenv("RECOMP_GPU_VSH_SYNC") && strcmp(getenv("RECOMP_GPU_VSH_SYNC"), "1") == 0;
    s_zpass_async = getenv("RECOMP_GPU_ZPASS") && strcmp(getenv("RECOMP_GPU_ZPASS"), "async") == 0;
    s_tex = (Tex *)calloc(MAX_TEX, sizeof *s_tex);
    if (!s_tex) {
        fprintf(stderr, "Cannot allocate the D3D11 texture cache\n");
        return 0;
    }
    nv2a_backend_ext_register(&s_backend_ext);
    nv2a_backend_register(&s_backend);
    fprintf(stderr, "[GPU] Direct3D 11 back end registered (RECOMP_GPU=d3d11%s)\n",
            s_warp ? ", WARP" : "");
    if (s_depth_texture)
        fprintf(stderr, "[GPU-D3D11] experimental D24 depth texture aliases enabled\n");
    if (s_stencil_alias)
        fprintf(stderr, "[GPU-D3D11] experimental packed D24S8 texture aliases enabled\n");
    if (s_direct_present)
        fprintf(stderr, "[GPU-D3D11] shared GPU presentation enabled;"
                        " guest readback retained\n");
    return 1;
}
