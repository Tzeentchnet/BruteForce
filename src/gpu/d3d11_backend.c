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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d/d3d8_swizzle.h"
#include "kernel.h"
#include "nv2a_backend.h"
#include "nv2a_backend_ext.h"
#include "platform/host_time.h"
#include "xbox_memory_layout.h"
#include "video/gpu_present.h"
#include "d3d11_backend.h"
#include "d3d11_device_status.h"
#include "render_scale.h"
#include "rc_hlsl.h"
#include "shader_cache.h"
#include "texture_decode.h"
#include "texture_sampler.h"
#include "vsh_hlsl.h"
#include "../perf_counters.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define GUEST(va) ((uint8_t *)((uintptr_t)(va) + xbox_GetMemoryOffset()))

static ID3D11Device *s_dev;
static ID3D11DeviceContext *s_ctx;
static int s_failed;
static int s_warp;
static int s_depth_texture;
static int s_stencil_alias;
static int s_direct_present;
static int s_skip_clean_readback;
static int s_tex_mips;
static volatile LONG s_device_thread;
static GpuScaleConfig s_pending_scale;
static int s_scale_pending;

#ifdef BRUTEFORCE_D3D11_TESTING
static const char *s_test_fail_stage;
static unsigned s_test_fail_occurrence;

void bruteforce_d3d11_test_fail(const char *stage, unsigned occurrence)
{
    s_test_fail_stage = stage;
    s_test_fail_occurrence = occurrence;
}

static int scale_test_failure(const char *stage)
{
    if (!s_test_fail_stage || strcmp(stage, s_test_fail_stage) != 0 ||
        !s_test_fail_occurrence || --s_test_fail_occurrence)
        return 0;
    s_test_fail_stage = NULL;
    return 1;
}
#else
static int scale_test_failure(const char *stage) { (void)stage; return 0; }
#endif

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

static void report_device_identity(D3D_FEATURE_LEVEL feature_level)
{
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    DXGI_ADAPTER_DESC desc;
    LARGE_INTEGER driver_version;
    char adapter_name[512];
    char driver[64];
    HRESULT hr;

    hr = ID3D11Device_QueryInterface(s_dev, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] cannot query DXGI device identity 0x%08lX\n",
                (unsigned long)hr);
        return;
    }
    hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    IDXGIDevice_Release(dxgi_device);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] cannot query adapter identity 0x%08lX\n",
                (unsigned long)hr);
        return;
    }
    hr = IDXGIAdapter_GetDesc(adapter, &desc);
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] cannot read adapter description 0x%08lX\n",
                (unsigned long)hr);
        IDXGIAdapter_Release(adapter);
        return;
    }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, desc.Description, -1,
                             adapter_name, sizeof adapter_name, NULL, NULL)) {
        fprintf(stderr, "[GPU-D3D11] cannot encode adapter description (Windows error %lu)\n",
                (unsigned long)GetLastError());
        snprintf(adapter_name, sizeof adapter_name, "<encoding unavailable>");
    }
    hr = IDXGIAdapter_CheckInterfaceSupport(adapter, &IID_ID3D11Device, &driver_version);
    if (SUCCEEDED(hr)) {
        snprintf(driver, sizeof driver, "%u.%u.%u.%u",
                 HIWORD(driver_version.HighPart), LOWORD(driver_version.HighPart),
                 HIWORD(driver_version.LowPart), LOWORD(driver_version.LowPart));
    } else {
        snprintf(driver, sizeof driver, "unavailable:0x%08lX", (unsigned long)hr);
    }
    fprintf(stderr, "[GPU-D3D11] device adapter=\"%s\" vendor=0x%04X device=0x%04X"
                    " subsystem=0x%08X revision=%u dedicated_vram=%llu"
                    " luid=%08lX:%08lX driver=%s feature_level=0x%04X\n",
            adapter_name, desc.VendorId, desc.DeviceId, desc.SubSysId, desc.Revision,
            (unsigned long long)desc.DedicatedVideoMemory,
            (unsigned long)desc.AdapterLuid.HighPart,
            (unsigned long)desc.AdapterLuid.LowPart,
            driver, (unsigned)feature_level);
    IDXGIAdapter_Release(adapter);
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
    unsigned readback_coherent, work_batches, work_waits;
    uint64_t tex_approx_hash_bytes;
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

static void report_validation_perf(const char *name,
                                   const BruteForceValidationPerfCounters *counters)
{
    fprintf(stderr, "[PERF] %s: %llu calls, %llu accepted, %llu rejected,"
                    " %.3f ms total; VirtualQuery %llu calls, %llu failures,"
                    " %llu regions, %.3f ms\n",
            name,
            (unsigned long long)counters->calls,
            (unsigned long long)counters->accepted,
            (unsigned long long)counters->rejected,
            bruteforce_perf_ticks_ms(counters->total_ticks),
            (unsigned long long)counters->virtual_queries,
            (unsigned long long)counters->query_failures,
            (unsigned long long)counters->regions,
            bruteforce_perf_ticks_ms(counters->query_ticks));
}

static void report_detailed_perf(void)
{
    static uint64_t previous_us;
    BruteForcePerfCounters counters;
    const BruteForceTexturePerfCounters *texture;
    uint64_t report_us;
    unsigned i;

    bruteforce_perf_counters_take(&counters);
    report_us = xbox_HostNowNs() / 1000u;
    report_validation_perf("guest_word", &counters.guest_word);
    report_validation_perf("readable_guest_range", &counters.guest_range);
    texture = &counters.texture;
    fprintf(stderr, "[PERF] texture cache: %llu lookups, %llu probes,"
                    " %llu key hits, %llu misses, %.3f ms scanning;"
                    " exact %llu checks, %llu hits, %llu source bytes,"
                    " %llu palette bytes, %.3f ms;"
                    " snapshots %llu copies, %llu bytes, %.3f ms;"
                    " rewrites %llu, entry evictions %llu, snapshot evictions %llu,"
                    " failed-upload discards %llu, unsupported hits %llu\n",
            (unsigned long long)texture->lookups,
            (unsigned long long)texture->probes,
            (unsigned long long)texture->key_hits,
            (unsigned long long)texture->key_misses,
            bruteforce_perf_ticks_ms(texture->lookup_ticks),
            (unsigned long long)texture->exact_checks,
            (unsigned long long)texture->exact_hits,
            (unsigned long long)texture->source_compare_bytes,
            (unsigned long long)texture->palette_compare_bytes,
            bruteforce_perf_ticks_ms(texture->exact_ticks),
            (unsigned long long)texture->snapshot_copies,
            (unsigned long long)texture->snapshot_copy_bytes,
            bruteforce_perf_ticks_ms(texture->snapshot_copy_ticks),
            (unsigned long long)texture->rewrites,
            (unsigned long long)texture->evictions,
            (unsigned long long)texture->snapshot_evictions,
            (unsigned long long)texture->discards,
            (unsigned long long)texture->unsupported_hits);
    fprintf(stderr, "[PERF-SHADER] kind=pixel-compile calls=%llu failures=%llu"
                    " elapsed_ms=%.3f window_start_us=%llu window_end_us=%llu\n",
            (unsigned long long)counters.pixel_shader_compile.calls,
            (unsigned long long)counters.pixel_shader_compile.failures,
            bruteforce_perf_ticks_ms(counters.pixel_shader_compile.ticks),
            (unsigned long long)previous_us, (unsigned long long)report_us);
    for (i = 0; i < BRUTEFORCE_PERF_MAX_CALLERS; ++i) {
        const BruteForceSubmitPerfCounter *pfb = &counters.pfb[i];
        const BruteForceSubmitPerfCounter *put = &counters.put[i];
        if (pfb->calls)
            fprintf(stderr, "[PERF-SUBMIT] kind=pfb site=%08X caller=%08X"
                            " calls=%llu elapsed_ms=%.3f window_start_us=%llu window_end_us=%llu\n",
                    pfb->site, pfb->caller, (unsigned long long)pfb->calls,
                    bruteforce_perf_ticks_ms(pfb->ticks),
                    (unsigned long long)previous_us, (unsigned long long)report_us);
        if (put->calls)
            fprintf(stderr, "[PERF-SUBMIT] kind=put site=%08X caller=%08X"
                            " calls=%llu elapsed_ms=%.3f window_start_us=%llu window_end_us=%llu\n",
                    put->site, put->caller, (unsigned long long)put->calls,
                    bruteforce_perf_ticks_ms(put->ticks),
                    (unsigned long long)previous_us, (unsigned long long)report_us);
    }
    if (counters.pfb_overflow || counters.put_overflow)
        fprintf(stderr, "[PERF-SUBMIT] caller table overflow: pfb=%llu put=%llu\n",
                (unsigned long long)counters.pfb_overflow,
                (unsigned long long)counters.put_overflow);
    previous_us = report_us;
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
                        " readback copies %u coherent %u, GPU batches %u waits %u,"
                        " %llu approximate texture-hash bytes\n",
                s_st.flips, s_st.draws, s_st.xdraws, s_st.vdraws, s_st.vraw, s_st.vfall,
                s_st.tris, s_st.uploads, s_st.rtt, s_st.overlap_rtt, s_st.clears,
                s_st.ps_compiles, s_st.kup, s_st.kskip,
                s_st.flips ? s_st.readback_ms / s_st.flips : 0.0,
                s_q_reads, s_q_wait_ms, s_rep_written, s_rep_flushes,
                s_st.tex_gpu, s_st.tex_cpu, s_st.tex_bc, s_st.tex_direct, s_st.tex_pitch,
                s_st.tex_unsupported, s_st.tex_unsupported_hits,
                s_st.depth_seeds, s_st.depth_samples, s_st.depth_snapshots, s_st.depth_rejected,
                s_st.stencil_samples, s_st.stencil_conversions,
                s_st.guest_uploads, s_st.guest_conflicts,
                s_st.present_shared, s_st.present_busy,
                s_st.readbacks, s_st.readback_coherent, s_st.work_batches, s_st.work_waits,
                (unsigned long long)s_st.tex_approx_hash_bytes);
        if (g_bruteforce_perf_counters_enabled)
            report_detailed_perf();
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
    uint64_t perf_started;
    int perf = g_bruteforce_perf_counters_enabled;
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
        perf_started = perf ? bruteforce_perf_clock() : 0;
        if (!compile(src, len, name, entry, target, &blob)) {
            if (perf)
                bruteforce_perf_record_pixel_shader_compile(
                    0, bruteforce_perf_clock() - perf_started);
            return NULL;
        }
        hr = ID3D11Device_CreatePixelShader(s_dev, ID3D10Blob_GetBufferPointer(blob),
                                            ID3D10Blob_GetBufferSize(blob), NULL, &ps);
        if (FAILED(hr) || !ps) {
            if (perf)
                bruteforce_perf_record_pixel_shader_compile(
                    0, bruteforce_perf_clock() - perf_started);
            gpu_d3d11_check_device(s_dev, hr, "creating pixel shader");
            fprintf(stderr, "[GPU-D3D11] CreatePixelShader(rc) failed 0x%08lX\n",
                    (unsigned long)hr);
            ID3D10Blob_Release(blob);
            return NULL;
        }
        if (perf)
            bruteforce_perf_record_pixel_shader_compile(
                1, bruteforce_perf_clock() - perf_started);
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

static int ensure_device_for_scale(int scale_requested)
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
        return !scale_requested || gpu_scale_gpu_init(s_dev, s_ctx);
    s_failed = 1;               /* until everything below has worked */
    hr = D3D11CreateDevice(NULL, s_warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                           NULL, D3D11_CREATE_DEVICE_SINGLETHREADED, &fl, 1,
                           D3D11_SDK_VERSION, &s_dev, &got, &s_ctx);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(NULL, hr, "creating D3D11 device");
        fprintf(stderr, "[GPU-D3D11] D3D11CreateDevice failed 0x%08lX\n", (unsigned long)hr);
        goto failed;
    }
    InterlockedExchange(&s_device_thread, (LONG)GetCurrentThreadId());
    report_device_identity(got);
    if (!compile(s_base_hlsl, sizeof s_base_hlsl - 1, "base", "vs_main", "vs_5_0", &vsb)
        || !compile(s_base_hlsl, sizeof s_base_hlsl - 1, "base", "ps_main", "ps_5_0", &psb)
        || !compile(s_xvs_hlsl, sizeof s_xvs_hlsl - 1, "xvs", "vs_main", "vs_5_0", &xvsb)) {
        goto failed;
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
    vsb = psb = xvsb = NULL;

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
        || !s_xcb || !s_window_cb || !s_rs || !s_rs_cull[0][0] || !s_rs_cull[0][1]
        || !s_rs_cull[1][0] || !s_rs_cull[1][1] || !s_ps)
        goto failed;
    if (scale_requested &&
        (scale_test_failure("resolve-init") || !gpu_scale_gpu_init(s_dev, s_ctx))) {
        gpu_d3d11_check_device(s_dev, S_OK, "initializing scaled resolve");
        fprintf(stderr, "[GPU-SCALE] resolve initialization failed; configuration not committed\n");
        goto failed;
    }
    s_failed = 0;
    fprintf(stderr, "[GPU-D3D11] %s device ready (feature level 0x%x)\n",
            s_warp ? "WARP" : "hardware", (unsigned)got);
    return 1;

failed:
    if (vsb) ID3D10Blob_Release(vsb);
    if (psb) ID3D10Blob_Release(psb);
    if (xvsb) ID3D10Blob_Release(xvsb);
    free(s_ps); s_ps = NULL;
    {
        int back, ccw;
        for (back = 0; back < 2; ++back)
            for (ccw = 0; ccw < 2; ++ccw) {
                if (s_rs_cull[back][ccw]) ID3D11RasterizerState_Release(s_rs_cull[back][ccw]);
                s_rs_cull[back][ccw] = NULL;
            }
    }
    if (s_rs) ID3D11RasterizerState_Release(s_rs);
    if (s_window_cb) ID3D11Buffer_Release(s_window_cb);
    if (s_xcb) ID3D11Buffer_Release(s_xcb);
    if (s_base_cb) ID3D11Buffer_Release(s_base_cb);
    if (s_xlayout) ID3D11InputLayout_Release(s_xlayout);
    if (s_base_layout) ID3D11InputLayout_Release(s_base_layout);
    if (s_xvs) ID3D11VertexShader_Release(s_xvs);
    if (s_base_ps) ID3D11PixelShader_Release(s_base_ps);
    if (s_base_vs) ID3D11VertexShader_Release(s_base_vs);
    s_rs = NULL;
    s_window_cb = s_xcb = s_base_cb = NULL;
    s_xlayout = s_base_layout = NULL;
    s_xvs = s_base_vs = NULL;
    s_base_ps = NULL;
    if (s_ctx) ID3D11DeviceContext_Release(s_ctx);
    if (s_dev) ID3D11Device_Release(s_dev);
    s_ctx = NULL;
    s_dev = NULL;
    s_failed = 0;
    InterlockedExchange(&s_device_thread, 0);
    return 0;
}

static int ensure_device(void)
{
    if (s_scale_pending) {
        if (!ensure_device_for_scale(s_pending_scale.requested)) {
            fprintf(stderr, "[GPU-SCALE] D3D11 initialization failed;"
                            " retaining prior configuration\n");
            return 0;
        }
        if (!gpu_scale_commit_config(&s_pending_scale)) {
            fprintf(stderr, "[GPU-SCALE] cannot commit D3D11 configuration\n");
            return 0;
        }
        s_scale_pending = 0;
        return 1;
    }
    return ensure_device_for_scale(gpu_scale_requested());
}

#ifdef BRUTEFORCE_D3D11_TESTING
int bruteforce_d3d11_test_initialize_device(void) { return ensure_device(); }
uint32_t bruteforce_d3d11_test_device_thread(void)
{
    return (uint32_t)InterlockedCompareExchange(&s_device_thread, 0, 0);
}
#endif

/* ---- surfaces ------------------------------------------------------------ */

#define MAX_SURF 32
typedef struct {
    uint64_t layout, allocation, bounds;
} GuestSpanIdentity;

typedef struct {
    uint32_t va, pitch, bpp, w, h, pw, ph, aa_sx, aa_sy;
    unsigned scale_epoch;
    ID3D11Texture2D *tex;
    ID3D11RenderTargetView *rtv;
    ID3D11ShaderResourceView *srv;
    ID3D11Texture2D *guest_tex;
    ID3D11ShaderResourceView *guest_srv;
    ID3D11Texture2D *sample_copy;
    ID3D11ShaderResourceView *sample_copy_srv;
    uint8_t *guest_shadow;
    size_t guest_shadow_bytes;
    GuestSpanIdentity identity;
    unsigned guest_check_gen;
    int gpu_dirty;
    int guest_exact;
    int alias_uncertain;
} Surf;

typedef struct {
    uint32_t va, w, h, pitch, format, surface_type, initialized, recreated;
    uint32_t logical_w[2], logical_h[2];
    unsigned scale_epoch;
    uint64_t revision, packed_revision;
    int packed_valid, gpu_dirty, guest_conflict;
    uint8_t *guest_shadow;
    size_t guest_shadow_bytes;
    GuestSpanIdentity identity;
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
static Surf *s_last_presented; /* Last readback surface for flip-only resize preflight. */
static Depth *s_cur_depth;
static uint32_t s_gen = 1;          /* frame number */
static int s_tex_strict_bounds;

typedef enum {
    GUEST_COPY_OK,
    GUEST_COPY_INVALID,
    GUEST_COPY_SHORT,
    GUEST_COPY_UNREGISTERED,
    GUEST_COPY_CHANGED,
    GUEST_COPY_UNREADABLE
} GuestCopyResult;

enum GuestBoundsPolicy {
    GUEST_BOUNDS_IGNORE,
    GUEST_BOUNDS_REQUIRED,
    GUEST_BOUNDS_TRACK_IF_REGISTERED
};

/* The RAM views and the separate contiguous/tiled views have distinct host
 * backing. Keep allocation identity in the appropriate backing domain. */
static int guest_data_span(uint32_t va, size_t bytes, uint64_t *physical)
{
    uint64_t mapped = xbox_GetMappedSize(), end, relative;
    uint64_t tiled_size = mapped < XBOX_CONTIG_SIZE ? mapped : XBOX_CONTIG_SIZE;
    if (!mapped || !va || !bytes || bytes > XBOX_CONTIG_SIZE)
        return 0;
    end = (uint64_t)va + bytes;
    if (va < XBOX_CONTIG_BASE && (uint64_t)va < mapped * (1ull + XBOX_NUM_MIRRORS)) {
        relative = va % mapped;
        if (end > XBOX_CONTIG_BASE || bytes > mapped - relative)
            return 0;
        *physical = relative;
        return 1;
    }
    if (va >= XBOX_CONTIG_BASE &&
        (uint64_t)va < (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE) {
        relative = (uint64_t)va - XBOX_CONTIG_BASE;
        if (bytes > XBOX_CONTIG_SIZE - relative)
            return 0;
        *physical = relative;
        return 2;
    }
    if (va >= XBOX_TILED_BASE &&
        (uint64_t)va < (uint64_t)XBOX_TILED_BASE + tiled_size) {
        relative = (uint64_t)va - XBOX_TILED_BASE;
        if (bytes > tiled_size - relative)
            return 0;
        *physical = relative;
        return 2;
    }
    return 0;
}

static uint64_t guest_allocation_identity(uint32_t va, uint32_t *remaining)
{
    uint64_t physical;
    int domain = guest_data_span(va, 1, &physical);
    if (domain == 1)
        return xbox_HeapBlockIdentity((uint32_t)physical, remaining);
    if (domain == 2)
        return xbox_ContiguousBlockIdentity(XBOX_CONTIG_BASE + (uint32_t)physical, remaining);
    if (remaining)
        *remaining = 0;
    return 0;
}

static GuestCopyResult checked_guest_copy(uint32_t va, size_t bytes, void *dest,
                                          int bounds_policy, GuestSpanIdentity *identity)
{
    GuestSpanIdentity before = {0}, after = {0};
    uint32_t remaining, after_remaining;
    uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset(), host;
    uint64_t physical, start = 0;
    SIZE_T copied = 0;
    GuestCopyResult result = GUEST_COPY_INVALID;
    int perf = g_bruteforce_perf_counters_enabled;

    if (perf) start = bruteforce_perf_clock();
    if (!guest_data_span(va, bytes, &physical) || offset > UINTPTR_MAX - va)
        goto done;
    host = offset + va;
    if (bytes > UINTPTR_MAX - host)
        goto done;
    before.layout = xbox_MemoryLayoutGeneration();
    if (!before.layout)
        goto done;
    before.allocation = guest_allocation_identity(va, &remaining);
    if (remaining && (!before.allocation || bytes > remaining)) {
        result = GUEST_COPY_SHORT;
        goto done;
    }
    if (bounds_policy != GUEST_BOUNDS_IGNORE) {
        before.bounds = xbox_GuestBoundsIdentity(va, &remaining);
        if (!before.bounds && bounds_policy == GUEST_BOUNDS_REQUIRED) {
            result = GUEST_COPY_UNREGISTERED;
            goto done;
        }
        if (before.bounds && bytes > remaining) {
            result = GUEST_COPY_SHORT;
            goto done;
        }
    }
    if (!ReadProcessMemory(GetCurrentProcess(), (const void *)host, dest, bytes, &copied) ||
        copied != bytes) {
        result = GUEST_COPY_UNREADABLE;
        goto done;
    }
    after.layout = xbox_MemoryLayoutGeneration();
    after.allocation = guest_allocation_identity(va, &after_remaining);
    if (bounds_policy != GUEST_BOUNDS_IGNORE)
        after.bounds = xbox_GuestBoundsIdentity(va, &remaining);
    if (before.layout != after.layout || before.allocation != after.allocation ||
        before.bounds != after.bounds || (after_remaining && bytes > after_remaining) ||
        (after.bounds && bytes > remaining)) {
        result = GUEST_COPY_CHANGED;
        goto done;
    }
    if (identity) *identity = before;
    result = GUEST_COPY_OK;
done:
    if (perf)
        bruteforce_perf_record_guest_range(result == GUEST_COPY_OK, 0, 0, 0,
                                           bruteforce_perf_clock() - start, 0);
    return result;
}

static int guest_identity_still_valid(uint32_t va, size_t bytes, int bounds_policy,
                                      const GuestSpanIdentity *expected)
{
    uint32_t remaining;
    uint64_t allocation, bounds = 0;
    if (!expected->layout || xbox_MemoryLayoutGeneration() != expected->layout)
        return 0;
    allocation = guest_allocation_identity(va, &remaining);
    if (allocation != expected->allocation || (remaining && bytes > remaining))
        return 0;
    if (bounds_policy != GUEST_BOUNDS_IGNORE) {
        bounds = xbox_GuestBoundsIdentity(va, &remaining);
        if ((!bounds && bounds_policy == GUEST_BOUNDS_REQUIRED) ||
            (bounds && bytes > remaining))
            return 0;
    }
    return bounds == expected->bounds &&
           xbox_MemoryLayoutGeneration() == expected->layout;
}

static int query_suspend_clear(void);
static void query_resume_clear(int counting);
static void bind_targets(Surf *s, Depth *d);
static void gpu_work_begin(void);
static void gpu_work_checkpoint(void);
static void gpu_work_finish(void);

static void restore_targets(void)
{
    if (s_cur)
        bind_targets(s_cur, s_cur_depth);
    else
        ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);
}

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
    free(d->guest_shadow);
    d->guest_shadow = NULL;
}

static DXGI_FORMAT surf_format(uint32_t bpp)
{
    return bpp == 2 ? DXGI_FORMAT_B5G6R5_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
}

static void release_surf(Surf *s)
{
    if (s->sample_copy_srv) ID3D11ShaderResourceView_Release(s->sample_copy_srv);
    if (s->sample_copy) ID3D11Texture2D_Release(s->sample_copy);
    if (s->srv) ID3D11ShaderResourceView_Release(s->srv);
    if (s->rtv) ID3D11RenderTargetView_Release(s->rtv);
    if (s->tex) ID3D11Texture2D_Release(s->tex);
    s->sample_copy_srv = NULL; s->sample_copy = NULL;
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
    s->guest_exact = 0;
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

static uint8_t *s_surface_probe;
static size_t s_surface_probe_cap;

static int surface_guest_span(const Surf *s, size_t *span)
{
    size_t row = surf_guest_row(s);
    if (!row || !s->h ||
        (s->pitch && (size_t)(s->h - 1) > (SIZE_MAX - row) / s->pitch))
        return 0;
    *span = (size_t)(s->h - 1) * s->pitch + row;
    return 1;
}

static int copy_guest_surface(const Surf *s, GuestSpanIdentity *identity)
{
    size_t span;
    uint8_t *probe;
    if (!surface_guest_span(s, &span))
        return 0;
    if (span > s_surface_probe_cap) {
        probe = (uint8_t *)realloc(s_surface_probe, span);
        if (!probe) return 0;
        s_surface_probe = probe;
        s_surface_probe_cap = span;
    }
    return checked_guest_copy(s->va, span, s_surface_probe,
                              s_tex_strict_bounds ? GUEST_BOUNDS_TRACK_IF_REGISTERED
                                                  : GUEST_BOUNDS_IGNORE,
                              identity) == GUEST_COPY_OK;
}

static int same_guest_identity(const GuestSpanIdentity *a, const GuestSpanIdentity *b)
{
    return a->layout && a->layout == b->layout && a->allocation == b->allocation &&
           a->bounds == b->bounds;
}

static int surf_identity_current(const Surf *s)
{
    size_t span;
    return surface_guest_span(s, &span) &&
           guest_identity_still_valid(s->va, span,
               s_tex_strict_bounds ? GUEST_BOUNDS_TRACK_IF_REGISTERED : GUEST_BOUNDS_IGNORE,
               &s->identity);
}

static void shadow_from_probe(Surf *s)
{
    size_t row = surf_guest_row(s);
    uint32_t y;
    for (y = 0; y < s->h; ++y)
        memcpy(s->guest_shadow + (size_t)y * row,
               s_surface_probe + (size_t)y * s->pitch, row);
}

static int snapshot_guest_surface(Surf *s)
{
    GuestSpanIdentity identity;

    if (!ensure_guest_shadow(s) || !copy_guest_surface(s, &identity) ||
        (s->identity.layout && !same_guest_identity(&s->identity, &identity)))
        return 0;
    s->identity = identity;
    shadow_from_probe(s);
    return 1;
}

static int guest_surface_changed(const Surf *s)
{
    size_t row = surf_guest_row(s);
    uint32_t y;
    GuestSpanIdentity identity;

    if (!copy_guest_surface(s, &identity) || !same_guest_identity(&s->identity, &identity))
        return -1;
    if (!s->guest_shadow || s->guest_shadow_bytes != row * s->h)
        return 1;
    for (y = 0; y < s->h; ++y)
        if (memcmp(s->guest_shadow + (size_t)y * row,
                   s_surface_probe + (size_t)y * s->pitch, row))
            return 1;
    return 0;
}

/* The preceding checked read rejects existing guards. SEH writes respect
 * read-only protection, unlike debugger-oriented WriteProcessMemory on some
 * executable pages. */
static int guarded_guest_write(void *destination, const void *source, size_t bytes)
{
    __try {
        memcpy(destination, source, bytes);
        return 1;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
                GetExceptionCode() == EXCEPTION_GUARD_PAGE
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return 0;
    }
}

#ifdef BRUTEFORCE_D3D11_TESTING
int bruteforce_d3d11_test_guarded_write(void *destination, const void *source, size_t bytes)
{
    return guarded_guest_write(destination, source, bytes);
}
#endif

static int checked_write_guest_surface(Surf *s, const void *source, size_t source_pitch)
{
    GuestSpanIdentity identity;
    size_t row = surf_guest_row(s), span, bytes;
    uint32_t y, rows;
    static unsigned rejected;

    if (!source || !surface_guest_span(s, &span) || source_pitch < row ||
        (s->h - 1) > (SIZE_MAX - row) / source_pitch ||
        !copy_guest_surface(s, &identity) || !same_guest_identity(&s->identity, &identity))
        goto failed;
    rows = s->pitch == row && source_pitch == row ? s->h : 1;
    bytes = row * rows;
    for (y = 0; y < s->h; y += rows) {
        if (!guarded_guest_write(GUEST(s->va) + (size_t)y * s->pitch,
                                 (const uint8_t *)source + (size_t)y * source_pitch, bytes))
            goto failed;
    }
    if (!guest_identity_still_valid(s->va, span,
            s_tex_strict_bounds ? GUEST_BOUNDS_TRACK_IF_REGISTERED : GUEST_BOUNDS_IGNORE,
            &identity))
        goto failed;
    return 1;
failed:
    if (rejected < 32) {
        ++rejected;
        fprintf(stderr, "[GPU-D3D11] checked guest color write for %08X failed or changed%s\n",
                s->va, rejected == 32 ? " (further reports suppressed)" : "");
    }
    return 0;
}

static int validate_seed_shadow(Surf *s)
{
    int changed = guest_surface_changed(s);
    if (changed < 0)
        return 0;
    if (changed) {
        s->gpu_dirty = 1;
        s->guest_check_gen = 0;
    }
    return 1;
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

    s->sample_copy = NULL;
    s->sample_copy_srv = NULL;
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
        init.pSysMem = s->guest_shadow;
        init.SysMemPitch = (UINT)surf_guest_row(s);
        init.SysMemSlicePitch = 0;
        pinit = &init;
    }
    hr = scale_test_failure("color-texture") ? E_OUTOFMEMORY :
         ID3D11Device_CreateTexture2D(s_dev, &td, pinit, &s->tex);
    if (gpu_call_failed(hr, "creating color surface"))
        return 0;
#ifdef BRUTEFORCE_D3D11_TESTING
    if (scale_test_failure("guest-change-seed"))
        *(uint32_t *)GUEST(s->va) ^= 1u;
#endif
    hr = scale_test_failure("color-rtv") ? E_OUTOFMEMORY :
         ID3D11Device_CreateRenderTargetView(s_dev, (ID3D11Resource *)s->tex, NULL, &s->rtv);
    if (gpu_call_failed(hr, "creating color render-target view")) {
        release_surf(s);
        return 0;
    }
    hr = scale_test_failure("color-srv") ? E_OUTOFMEMORY :
         ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)s->tex, NULL, &s->srv);
    if (gpu_call_failed(hr, "creating color shader-resource view")) {
        release_surf(s);
        return 0;
    }
    if (gpu_seed) {
        int counting, ok;
        enum GpuScaleBlitMode mode =
            gpu_seed_width > s->pw || gpu_seed_height > s->ph ? GPU_SCALE_BLIT_BOX
                                                               : GPU_SCALE_BLIT_BILINEAR;
        counting = query_suspend_clear();
        ok = !scale_test_failure("color-migrate") &&
             gpu_scale_blit(gpu_seed, gpu_seed_width, gpu_seed_height,
                            s->rtv, s->pw, s->ph, mode);
        restore_targets();
        query_resume_clear(counting);
        if (!ok) {
            gpu_d3d11_check_device(s_dev, S_OK, "migrating scaled color surface");
            fprintf(stderr, "[GPU-SCALE] migrating scaled color surface %08X failed\n", s->va);
            release_surf(s);
            return 0;
        }
    } else if ((s->pw != s->w || s->ph != s->h) && s->pitch >= s->w * s->bpp) {
        td.Width = s->w;
        td.Height = s->h;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        init.pSysMem = s->guest_shadow;
        init.SysMemPitch = (UINT)surf_guest_row(s);
        init.SysMemSlicePitch = 0;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, &init, &seed);
        if (SUCCEEDED(hr))
            hr = ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)seed,
                                                       NULL, &seed_srv);
        if (SUCCEEDED(hr)) {
            int counting = query_suspend_clear();
            int ok = gpu_scale_blit(seed_srv, s->w, s->h, s->rtv, s->pw, s->ph,
                                    GPU_SCALE_BLIT_BILINEAR);
            restore_targets();
            query_resume_clear(counting);
            if (!ok)
                hr = E_FAIL;
        }
        if (FAILED(hr)) {
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
    s->guest_exact = 0;
    return s->rtv && s->srv;
}

static Surf *find_surf(uint32_t va)
{
    int i;
    for (i = 0; i < s_nsurf; i++)
        if (s_surf[i].va == va && s_surf[i].tex && surf_identity_current(&s_surf[i]))
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
    int i, stale;

    if (aa_sx > 2 || aa_sy > 2) {
        fprintf(stderr, "[GPU-SCALE] unsupported guest anti-aliasing factors %ux%u\n",
                aa_sx, aa_sy);
        return NULL;
    }
    for (i = 0; i < s_nsurf; i++)
        if (s_surf[i].va == in->color_va) { s = &s_surf[i]; break; }
    stale = s && !surf_identity_current(s);
    if (s && !stale) {
        w = w > s->w ? w : s->w;
        h = h > s->h ? h : s->h;
    }
    if (!gpu_scale_surface_size(w, h, aa_sx, aa_sy, &pw, &ph))
        return NULL;
    if (s && (stale || s->bpp != bpp || s->w != w || s->h != h || s->pw != pw || s->ph != ph ||
              s->pitch != in->pitch || !s->tex)) {
        Surf replacement = *s;
        int guest_layout_changed =
            stale || s->bpp != bpp || s->w != w || s->h != h || s->pitch != in->pitch;
        int migrate = !stale && gpu_scale_requested() && s->srv;
        int uncertain = 0;
        if (guest_layout_changed && !stale && s->tex) {
            int changed = guest_surface_changed(s);
            if (changed < 0 || (changed && s->gpu_dirty) || (s->gpu_dirty && !migrate))
                uncertain = 1;
            if (changed > 0 && !s->gpu_dirty)
                migrate = 0;
        }
        if (stale)
            memset(&replacement, 0, sizeof replacement);
        replacement.va = in->color_va;
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
        replacement.sample_copy = NULL;
        replacement.sample_copy_srv = NULL;
        if (guest_layout_changed) {
            replacement.guest_tex = NULL;
            replacement.guest_srv = NULL;
            replacement.guest_shadow = NULL;
            replacement.guest_shadow_bytes = 0;
            replacement.guest_check_gen = 0;
            replacement.gpu_dirty = migrate != 0;
            replacement.guest_exact = 0;
            replacement.alias_uncertain = !stale && (s->alias_uncertain || uncertain);
        }
        if ((guest_layout_changed && !snapshot_guest_surface(&replacement)) ||
            !create_surf(&replacement, migrate ? s->srv : NULL,
                         s->pw, s->ph) ||
            (guest_layout_changed && !validate_seed_shadow(&replacement))) {
            release_surf(&replacement);
            if (guest_layout_changed)
                release_surf_guest(&replacement);
            fprintf(stderr, "[GPU-D3D11] retaining prior color resources for %08X\n", s->va);
            return NULL;
        }
        if (guest_layout_changed && migrate && guest_surface_changed(s) != 0)
            replacement.alias_uncertain = 1;
        if (s_cur == s)
            s_cur = NULL;
        if (stale && s_last_presented == s)
            s_last_presented = NULL;
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
        if (s_nsurf == MAX_SURF) {
            fprintf(stderr, "[GPU-D3D11] color surface limit reached for %08X\n", in->color_va);
            return NULL;
        }
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
        if (!snapshot_guest_surface(s) || !create_surf(s, NULL, 0, 0) ||
            !validate_seed_shadow(s)) {
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
    uint64_t total_start = 0, query_start = 0, query_ticks = 0;
    unsigned queries = 0, query_failures = 0, regions = 0;
    int accepted = 0;
    int perf = g_bruteforce_perf_counters_enabled;

    if (perf)
        total_start = bruteforce_perf_clock();
    if (!va || !bytes || bytes > XBOX_CONTIG_SIZE ||
        !(guest_end <= XBOX_CONTIG_SIZE ||
          (va >= XBOX_CONTIG_BASE && guest_end <= (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE)) ||
        offset > UINTPTR_MAX - va)
        goto done;
    p = offset + va;
    if (bytes > UINTPTR_MAX - p)
        goto done;
    end = p + (uintptr_t)bytes;
    while (p < end) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t base, available;
        SIZE_T query_result;
        if (perf)
            query_start = bruteforce_perf_clock();
        queries++;
        query_result = VirtualQuery((const void *)p, &info, sizeof info);
        if (perf)
            query_ticks += bruteforce_perf_clock() - query_start;
        if (query_result != sizeof info) {
            query_failures++;
            goto done;
        }
        regions++;
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & readable))
            goto done;
        base = (uintptr_t)info.BaseAddress;
        if (p < base || p - base >= info.RegionSize)
            goto done;
        available = info.RegionSize - (p - base);
        p += available < end - p ? available : end - p;
    }
    accepted = 1;

done:
    if (perf)
        bruteforce_perf_record_guest_range(accepted, queries, query_failures, regions,
                                           bruteforce_perf_clock() - total_start, query_ticks);
    return accepted;
}

static int depth_guest_span(const Depth *d, size_t *span)
{
    uint64_t row = (uint64_t)d->w * 4;
    uint64_t bytes;
    if (!d->w || !d->h || d->format != 2 || d->surface_type != 1 ||
        d->pitch < row)
        return 0;
    bytes = (uint64_t)(d->h - 1) * d->pitch + row;
    if (bytes > SIZE_MAX || bytes > XBOX_CONTIG_SIZE)
        return 0;
    *span = (size_t)bytes;
    return 1;
}

static int capture_depth_guest(Depth *d)
{
    GuestSpanIdentity identity;
    uint8_t *shadow;
    size_t span;
    if (!depth_guest_span(d, &span) || !readable_guest_range(d->va, span))
        return 0;
    shadow = (uint8_t *)malloc(span);
    if (!shadow)
        return 0;
    if (checked_guest_copy(d->va, span, shadow, GUEST_BOUNDS_TRACK_IF_REGISTERED,
                           &identity) != GUEST_COPY_OK) {
        free(shadow);
        return 0;
    }
    if (d->identity.layout == identity.layout && d->identity.allocation &&
        !identity.allocation) {
        free(shadow);
        return 0;
    }
    free(d->guest_shadow);
    d->guest_shadow = shadow;
    d->guest_shadow_bytes = span;
    d->identity = identity;
    return 1;
}

/* A different nonzero allocation identity, or a new memory layout, proves
 * the previous resource belonged to an ended guest lifetime. */
static int depth_new_lifetime(const Depth *d, const GuestSpanIdentity *current)
{
    return d->identity.layout && current->layout &&
           (d->identity.layout != current->layout ||
            (d->identity.allocation && current->allocation &&
             d->identity.allocation != current->allocation));
}

/* -1: unreadable or unverifiable, 0: unchanged, 1: changed, 2: new lifetime. */
static int probe_depth_guest(const Depth *d)
{
    GuestSpanIdentity identity;
    size_t span;
    uint8_t *probe;
    if (!d->guest_shadow || !depth_guest_span(d, &span) ||
        span != d->guest_shadow_bytes || !readable_guest_range(d->va, span))
        return -1;
    if (span > s_surface_probe_cap) {
        probe = (uint8_t *)realloc(s_surface_probe, span);
        if (!probe) return -1;
        s_surface_probe = probe;
        s_surface_probe_cap = span;
    }
    if (checked_guest_copy(d->va, span, s_surface_probe,
                           GUEST_BOUNDS_TRACK_IF_REGISTERED, &identity) != GUEST_COPY_OK)
        return -1;
    if (d->identity.layout == identity.layout && d->identity.allocation &&
        !identity.allocation)
        return -1;
    if (depth_new_lifetime(d, &identity))
        return 2;
    if (!same_guest_identity(&d->identity, &identity) ||
        memcmp(d->guest_shadow, s_surface_probe, span))
        return 1;
    return 0;
}

/* Xbox Z24S8 stores depth in the high 24 bits; D3D11 R24G8 stores it in
 * the low 24. Only a checked snapshot may seed a GPU depth resource. */
static uint32_t *depth_seed(const Depth *d)
{
    uint32_t *seed, x, y;
    size_t row = (size_t)d->w * 4;
    if (!d->guest_shadow || d->h > SIZE_MAX / row)
        return NULL;
    seed = (uint32_t *)malloc(row * d->h);
    if (!seed) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate depth seed for surface %08X\n", d->va);
        return NULL;
    }
    for (y = 0; y < d->h; y++) {
        const uint8_t *src = d->guest_shadow + (size_t)y * d->pitch;
        for (x = 0; x < d->w; x++) {
            uint32_t value;
            memcpy(&value, src + (size_t)x * 4, sizeof value);
            seed[(size_t)y * d->w + x] = (value >> 8) | (value << 24);
        }
    }
    return seed;
}

static void depth_record_use(Depth *d, const Surf *surf)
{
    unsigned x = surf->aa_sx > 1, y = surf->aa_sy > 1;
    if (d->logical_w[x] < surf->w) d->logical_w[x] = surf->w;
    if (d->logical_h[y] < surf->h) d->logical_h[y] = surf->h;
}

static int depth_size_for(const Depth *d, float scale, uint32_t *width, uint32_t *height)
{
    uint32_t w = 0, h = 0;
    unsigned x, y;
    for (x = 0; x < 2; ++x)
        for (y = 0; y < 2; ++y) {
            uint32_t pw, ph;
            if (!d->logical_w[x] || !d->logical_h[y])
                continue;
            if (!gpu_scale_surface_size_for(scale, d->logical_w[x], d->logical_h[y],
                                            x + 1, y + 1, &pw, &ph))
                return 0;
            if (w < pw) w = pw;
            if (h < ph) h = ph;
        }
    if (!w || !h)
        return 0;
    *width = w;
    *height = h;
    return 1;
}

static int create_depth_resource(Depth *d, int existing)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA init;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    uint32_t *seed = NULL;
    int seeded = 0;
    HRESULT hr;

    memset(&td, 0, sizeof td);
    td.Width = d->w; td.Height = d->h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = depth_views_enabled() ? DXGI_FORMAT_R24G8_TYPELESS
                                      : DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL |
                   (depth_views_enabled() ? D3D11_BIND_SHADER_RESOURCE : 0);
    if (depth_views_enabled() && !gpu_scale_requested() &&
        d->format == 2 && d->surface_type == 1) {
        if (capture_depth_guest(d) && !existing)
            seed = depth_seed(d);
        if (!d->guest_shadow || (!existing && !seed))
            d->guest_conflict = 1;
    }
    memset(&init, 0, sizeof init);
    init.pSysMem = seed;
    init.SysMemPitch = d->w * 4;
    hr = scale_test_failure("depth-texture") ? E_OUTOFMEMORY :
         ID3D11Device_CreateTexture2D(s_dev, &td, seed ? &init : NULL, &d->tex);
    if (FAILED(hr) && seed) {
        gpu_d3d11_check_device(s_dev, hr, "uploading depth seed");
        fprintf(stderr, "[GPU-D3D11] depth seed upload for %08X failed 0x%08lX;"
                        " sampling will require a GPU depth clear\n", d->va, (unsigned long)hr);
        free(seed);
        seed = NULL;
        d->guest_conflict = 1;
        hr = ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &d->tex);
    }
    if (FAILED(hr))
        goto failed;
    if (seed) {
        d->initialized = 3;
        seeded = 1;
        free(seed);
        seed = NULL;
    }
    memset(&dv, 0, sizeof dv);
    dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = scale_test_failure("depth-view") ? E_OUTOFMEMORY :
         ID3D11Device_CreateDepthStencilView(s_dev, (ID3D11Resource *)d->tex, &dv, &d->dsv);
    if (FAILED(hr))
        goto failed;
    if (depth_views_enabled()) {
        memset(&sv, 0, sizeof sv);
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = scale_test_failure("depth-srv") ? E_OUTOFMEMORY :
             ID3D11Device_CreateShaderResourceView(s_dev, (ID3D11Resource *)d->tex, &sv, &d->srv);
        if (FAILED(hr))
            goto failed;
    }
#ifdef BRUTEFORCE_D3D11_TESTING
    if (seeded && scale_test_failure("depth-seed-race"))
        *(uint32_t *)GUEST(d->va) ^= 1u;
#endif
    if (seeded && probe_depth_guest(d) != 0) {
        hr = E_FAIL;
        goto failed;
    }
    if (seeded)
        s_st.depth_seeds++;
    return 1;

failed:
    gpu_d3d11_check_device(s_dev, hr, "creating depth resources");
    free(seed);
    fprintf(stderr, "[GPU-D3D11] depth surface creation failed 0x%08lX\n", (unsigned long)hr);
    release_depth(d);
    memset(d, 0, sizeof *d);
    return 0;
}

static void depth_reject(const char *reason, uint32_t va, uint32_t other)
{
    static unsigned reports;
    s_st.depth_rejected++;
    if (reports < 32) {
        ++reports;
        fprintf(stderr, "[GPU-D3D11] depth ownership rejected: %s (%08X, %08X)%s\n",
                reason, va, other, reports == 32 ? " (further reports suppressed)" : "");
    }
}

static void retire_depth(Depth *d)
{
    if (s_cur_depth == d) {
        if (s_cur)
            bind_targets(s_cur, NULL);
        else
            s_cur_depth = NULL;
    }
    release_depth(d);
    memset(d, 0, sizeof *d);
}

static int depth_reseed(Depth *d)
{
    Depth replacement;
    int bound = s_cur && s_cur_depth == d;
    memset(&replacement, 0, sizeof replacement);
    replacement.va = d->va;
    replacement.w = d->w;
    replacement.h = d->h;
    replacement.pitch = d->pitch;
    replacement.format = d->format;
    replacement.surface_type = d->surface_type;
    replacement.scale_epoch = d->scale_epoch;
    replacement.recreated = 1;
    replacement.revision = d->revision + 1;
    replacement.identity = d->identity;
    memcpy(replacement.logical_w, d->logical_w, sizeof replacement.logical_w);
    memcpy(replacement.logical_h, d->logical_h, sizeof replacement.logical_h);
    if (!create_depth_resource(&replacement, 0))
        return 0;
    if (replacement.initialized != 3 || replacement.guest_conflict) {
        release_depth(&replacement);
        return 0;
    }
    release_depth(d);
    *d = replacement;
    if (bound)
        bind_targets(s_cur, d);
    return 1;
}

static int sync_depth_guest(Depth *d)
{
    int change;
    if (!depth_views_enabled() || gpu_scale_requested() ||
        d->format != 2 || d->surface_type != 1)
        return 1;
    change = probe_depth_guest(d);
    if (change == 2 || (!d->gpu_dirty && change >= 0 &&
                        (change != 0 || d->guest_conflict))) {
        if (depth_reseed(d))
            return 1;
    } else if (change == 0 && !d->guest_conflict) {
        return 1;
    }
    d->guest_conflict = 1;
    depth_reject(change < 0 ? "guest depth range unreadable or unverifiable" :
                 "guest bytes or identity conflict with GPU depth", d->va, 0);
    return 0;
}

/* A full depth+stencil clear can discard the old GPU planes, but only after
 * establishing a fresh, checked baseline for subsequent guest-write checks. */
static int rebaseline_depth(Depth *d)
{
    Depth snapshot = *d;
    snapshot.guest_shadow = NULL;
    snapshot.guest_shadow_bytes = 0;
    if (!capture_depth_guest(&snapshot))
        goto failed;
#ifdef BRUTEFORCE_D3D11_TESTING
    if (scale_test_failure("depth-baseline-race"))
        *(uint32_t *)GUEST(snapshot.va) ^= 1u;
#endif
    if (probe_depth_guest(&snapshot) != 0)
        goto failed;
    free(d->guest_shadow);
    d->guest_shadow = snapshot.guest_shadow;
    d->guest_shadow_bytes = snapshot.guest_shadow_bytes;
    d->identity = snapshot.identity;
    d->guest_conflict = 0;
    return 1;

failed:
    free(snapshot.guest_shadow);
    depth_reject("cannot establish guest baseline for full clear", d->va, 0);
    return 0;
}

static int physical_range(uint64_t start, uint64_t span, uint64_t *physical);

static int guest_ranges_overlap(uint32_t a, uint64_t aspan, uint32_t b, uint64_t bspan)
{
    uint64_t ap = 0, bp = 0;
    int av, bv;
    if (!aspan || !bspan || (uint64_t)a + aspan > 0x100000000ull ||
        (uint64_t)b + bspan > 0x100000000ull)
        return 1;
    av = physical_range(a, aspan, &ap);
    bv = physical_range(b, bspan, &bp);
    return av < 0 || bv < 0 ||
           ((uint64_t)a < (uint64_t)b + bspan && (uint64_t)b < (uint64_t)a + aspan) ||
           (av > 0 && bv > 0 && ap < bp + bspan && bp < ap + aspan);
}

static int depth_known_stale(const Depth *d)
{
    uint32_t remaining;
    uint64_t allocation, layout;
    if (!d->identity.layout)
        return 0;
    layout = xbox_MemoryLayoutGeneration();
    allocation = guest_allocation_identity(d->va, &remaining);
    return (layout && layout != d->identity.layout) ||
           (d->identity.allocation && allocation && allocation != d->identity.allocation);
}

static int tracked_allocation_lost(uint32_t va, const GuestSpanIdentity *identity)
{
    uint32_t remaining;
    return identity->allocation && identity->layout == xbox_MemoryLayoutGeneration() &&
           !guest_allocation_identity(va, &remaining);
}

static uint64_t depth_owner_span(const Depth *d)
{
    uint32_t height = d->logical_h[0] > d->logical_h[1]
                    ? d->logical_h[0] : d->logical_h[1];
    return (uint64_t)d->pitch * height;
}

static int depth_owners_compatible(uint32_t va, uint64_t span, Depth *same,
                                   const Surf *target)
{
    int i;
    for (i = 0; i < s_ndepth; ++i) {
        Depth *other = &s_depth[i];
        if (!other->tex || other == same)
            continue;
        if (depth_known_stale(other)) {
            retire_depth(other);
            continue;
        }
        if (!guest_ranges_overlap(va, span, other->va, depth_owner_span(other)))
            continue;
        if (other->gpu_dirty || other->guest_conflict ||
            tracked_allocation_lost(other->va, &other->identity)) {
            depth_reject("overlapping GPU depth owners", va, other->va);
            return 0;
        }
        retire_depth(other);
    }
    for (i = 0; i < s_nsurf; ++i) {
        Surf *other = &s_surf[i];
        size_t other_span;
        int current;
        if (!other->tex)
            continue;
        current = surf_identity_current(other);
        if (!current && !tracked_allocation_lost(other->va, &other->identity))
            continue;
        if (!surface_guest_span(other, &other_span) ||
            guest_ranges_overlap(va, span, other->va, other_span)) {
            if (!current || other == target || other->gpu_dirty || other->alias_uncertain) {
                depth_reject("overlapping color and depth owners", va, other->va);
                return 0;
            }
            if (s_cur == other) {
                ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);
                s_cur = NULL;
                s_cur_depth = NULL;
            }
            if (s_last_presented == other)
                s_last_presented = NULL;
            release_surf(other);
            release_surf_guest(other);
        }
    }
    return 1;
}

static int color_depth_compatible(const Nv2aSurface *in)
{
    uint32_t bpp = in->bytes_per_pixel ? in->bytes_per_pixel : 4;
    uint32_t width = in->pitch && in->pitch / bpp >= in->width
                   ? in->pitch / bpp : in->width;
    uint32_t height = in->height;
    uint64_t row, span;
    int i;
    for (i = 0; i < s_nsurf; ++i) {
        Surf *existing = &s_surf[i];
        if (existing->tex && existing->va == in->color_va && surf_identity_current(existing)) {
            if (width < existing->w) width = existing->w;
            if (height < existing->h) height = existing->h;
            break;
        }
    }
    row = (uint64_t)width * bpp;
    span = height && in->pitch
         ? (uint64_t)(height - 1) * in->pitch +
           (row < in->pitch ? row : in->pitch) : 0;
    for (i = 0; i < s_ndepth; ++i) {
        Depth *other = &s_depth[i];
        if (!other->tex)
            continue;
        if (depth_known_stale(other)) {
            retire_depth(other);
            continue;
        }
        if (!guest_ranges_overlap(in->color_va, span, other->va, depth_owner_span(other)))
            continue;
        if (other->gpu_dirty || other->guest_conflict ||
            tracked_allocation_lost(other->va, &other->identity)) {
            depth_reject("color overlaps GPU depth owner", in->color_va, other->va);
            return 0;
        }
        retire_depth(other);
    }
    return 1;
}

static Depth *get_depth(uint32_t va, const Surf *surf)
{
    uint32_t w = surf->pw, h = surf->ph;
    uint32_t pitch = nv2a_backend_reg(0x020C) >> 16;
    uint32_t surface_format = nv2a_backend_reg(0x0208);
    uint32_t format = (surface_format >> 4) & 0xF;
    uint32_t surface_type = (surface_format >> 8) & 0xF;
    Depth *d = NULL, *empty = NULL, replacement;
    uint32_t guest_height = surf->h;
    int existing, same_layout, unsafe_recreation, i;

    if (!va)
        return NULL;
    for (i = 0; i < s_ndepth; i++) {
        if (s_depth[i].tex && s_depth[i].va == va) { d = &s_depth[i]; break; }
        if (!s_depth[i].tex && !empty) empty = &s_depth[i];
    }
    if (d && depth_views_enabled() && depth_known_stale(d)) {
        retire_depth(d);
        empty = d;
        d = NULL;
    }
    existing = d != NULL;
    same_layout = d && (!depth_views_enabled() ||
                        (d->pitch == pitch && d->format == format &&
                         d->surface_type == surface_type));
    if (d) {
        if (guest_height < d->logical_h[0]) guest_height = d->logical_h[0];
        if (guest_height < d->logical_h[1]) guest_height = d->logical_h[1];
    }
    if (depth_views_enabled() &&
        !depth_owners_compatible(va, (uint64_t)pitch * guest_height, d, surf))
        return NULL;
    if (d && d->scale_epoch == gpu_scale_epoch() && d->w >= w && d->h >= h && same_layout) {
        depth_record_use(d, surf);
        return d;
    }
    if (!d && !empty && s_ndepth == MAX_SURF) {
        fprintf(stderr, "[GPU-D3D11] depth surface limit reached for %08X\n", va);
        return NULL;
    }
    memset(&replacement, 0, sizeof replacement);
    if (same_layout) {
        memcpy(replacement.logical_w, d->logical_w, sizeof replacement.logical_w);
        memcpy(replacement.logical_h, d->logical_h, sizeof replacement.logical_h);
        if (d->scale_epoch == gpu_scale_epoch()) {
            if (w < d->w) w = d->w;
            if (h < d->h) h = d->h;
        }
    }
    replacement.va = va; replacement.w = w; replacement.h = h; replacement.pitch = pitch;
    replacement.format = format; replacement.surface_type = surface_type;
    replacement.recreated = existing;
    replacement.scale_epoch = gpu_scale_epoch();
    unsafe_recreation = depth_views_enabled() && d && (d->gpu_dirty || d->guest_conflict);
    if (unsafe_recreation &&
        (gpu_scale_requested() || format != 2 || surface_type != 1)) {
        depth_reject("dirty depth transition to unsupported layout", va, va);
        return NULL;
    }
    if (d) {
        replacement.revision = d->revision + 1;
        replacement.identity = d->identity;
    }
    depth_record_use(&replacement, surf);
    if (!create_depth_resource(&replacement, depth_views_enabled() ? unsafe_recreation : existing)) {
        if (d)
            fprintf(stderr, "[GPU-D3D11] retaining prior depth resources for %08X\n", va);
        return NULL;
    }
    if (unsafe_recreation) {
        replacement.gpu_dirty = 1;
        replacement.guest_conflict = 1;
    }
    if (d) {
        if (s_cur_depth == d) {
            if (s_cur)
                bind_targets(s_cur, NULL);
            else
                s_cur_depth = NULL;
        }
        release_depth(d);
    } else {
        d = empty ? empty : &s_depth[s_ndepth++];
    }
    *d = replacement;
    return d;
}

/* The RAM mirrors, contiguous window and tiled aperture may name the same
 * bytes. A span crossing one of their backing boundaries is indeterminate. */
static int physical_range(uint64_t start, uint64_t span, uint64_t *physical)
{
    uint64_t mapped = xbox_GetMappedSize(), end = start + span;
    uint64_t tiled_size = mapped < 0x0D000000u ? mapped : 0x0D000000u;
    uint64_t relative;

    if (!mapped || end > 0x100000000ull)
        return -1;
    if (start >= XBOX_CONTIG_BASE && start < (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE) {
        relative = start - XBOX_CONTIG_BASE;
        if (span > XBOX_CONTIG_SIZE - relative || relative + span > mapped)
            return -1;
    } else if (start >= XBOX_TILED_BASE && start < (uint64_t)XBOX_TILED_BASE + tiled_size) {
        relative = start - XBOX_TILED_BASE;
        if (span > tiled_size - relative)
            return -1;
    } else if (start < mapped * (1ull + XBOX_NUM_MIRRORS) && start < XBOX_CONTIG_BASE) {
        relative = start % mapped;
        if (span > mapped - relative)
            return -1;
    } else {
        if ((start < XBOX_CONTIG_BASE && end > XBOX_CONTIG_BASE) ||
            (start < XBOX_TILED_BASE && end > XBOX_TILED_BASE))
            return -1;
        return 0;
    }
    *physical = relative;
    return 1;
}

static Depth *overlapping_depth(const Nv2aTexture *t, int cube, int *ambiguous)
{
    GpuTextureLayout layout;
    uint64_t start = t->offset, end = start;
    uint64_t physical_start = 0;
    Depth *match = NULL;
    int i, physical_valid = 0;
    int valid = gpu_texture_layout(t, cube ? 6 : 1, s_tex_mips, &layout);

    *ambiguous = !valid;
    if (valid) {
        end += layout.span;
        physical_valid = physical_range(start, layout.span, &physical_start);
        if (physical_valid < 0)
            *ambiguous = 1;
    }
    for (i = 0; i < s_ndepth; i++) {
        Depth *d = &s_depth[i];
        uint32_t guest_height = d->logical_h[0] > d->logical_h[1]
                              ? d->logical_h[0] : d->logical_h[1];
        uint64_t depth_span = (uint64_t)d->pitch * guest_height;
        uint64_t depth_end = (uint64_t)d->va + depth_span, depth_physical = 0;
        int depth_physical_valid, overlaps;
        if (!d->tex)
            continue;
        if (depth_views_enabled() && depth_known_stale(d)) {
            retire_depth(d);
            continue;
        }
        depth_physical_valid = physical_range(d->va, depth_span, &depth_physical);
        overlaps = !valid || !depth_span || physical_valid < 0 || depth_physical_valid < 0 ||
                   (start < depth_end && d->va < end) ||
                   (physical_valid > 0 && depth_physical_valid > 0 &&
                    physical_start < depth_physical + depth_span &&
                    depth_physical < physical_start + layout.span);
        if (overlaps) {
            if (!depth_span || physical_valid < 0 || depth_physical_valid < 0)
                *ambiguous = 1;
            if (match)
                *ambiguous = 1;
            else
                match = d;
        }
    }
    return match;
}

#ifdef BRUTEFORCE_D3D11_TESTING
int bruteforce_d3d11_test_depth_overlap(const Nv2aTexture *texture, int cube, int *ambiguous)
{
    return overlapping_depth(texture, cube, ambiguous) != NULL;
}
#endif

static Depth *check_depth_texture(const Nv2aTexture *t, int cube, unsigned levels,
                                  unsigned initialized)
{
    static int warned;
    Depth *d = NULL, *overlap;
    int ambiguous;
    int i;

    for (i = 0; i < s_ndepth; i++)
        if (s_depth[i].tex && s_depth[i].va == t->offset) { d = &s_depth[i]; break; }
    overlap = overlapping_depth(t, cube, &ambiguous);
    if (!d || !d->srv || cube || levels > 1 || ambiguous || overlap != d ||
        gpu_scale_requested() || d->format != 2 || d->surface_type != 1 ||
        t->width != d->w || t->height != d->h || t->pitch != d->pitch ||
        t->pitch < t->width * 4) {
        s_st.depth_rejected++;
        if (!warned++) {
            fprintf(stderr, "[GPU-D3D11] depth texture alias rejected: va %08X %ux%u"
                            " pitch %u levels %u cube %d; surface %s %ux%u pitch %u"
                            " format %u type %u; ambiguous overlap %d, scaled %d\n",
                    t->offset, t->width, t->height, t->pitch, levels, cube,
                    d ? "tracked" : "missing", d ? d->w : 0, d ? d->h : 0,
                    d ? d->pitch : 0, d ? d->format : 0, d ? d->surface_type : 0,
                    ambiguous, gpu_scale_requested());
        }
        return NULL;
    }
    if (!sync_depth_guest(d))
        return NULL;
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

static int required_depth_alias(const Nv2aTexture *t, int cube)
{
    int ambiguous;
    return (s_depth_texture && t->color == 0x2E) ||
           (depth_views_enabled() && overlapping_depth(t, cube, &ambiguous));
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

static void surface_clear_rect(const Nv2aSurface *s, const Surf *surf, D3D11_RECT *rect)
{
    uint32_t bounds[4];
    if (nv2a_backend_clear_rect(bounds)) {
        uint32_t sx = s->aa_sx > 1 ? 2 : 1, sy = s->aa_sy > 1 ? 2 : 1;
        uint32_t x0 = bounds[0] * sx, y0 = bounds[1] * sy;
        uint32_t x1 = bounds[2] * sx, y1 = bounds[3] * sy;
        x0 = x0 < surf->w ? x0 : surf->w;
        y0 = y0 < surf->h ? y0 : surf->h;
        x1 = x1 < surf->w ? x1 : surf->w;
        y1 = y1 < surf->h ? y1 : surf->h;
        rect->left = scale_edge(x0, surf->w, surf->pw);
        rect->top = scale_edge(y0, surf->h, surf->ph);
        rect->right = scale_edge(x1, surf->w, surf->pw);
        rect->bottom = scale_edge(y1, surf->h, surf->ph);
    } else {
        rect->left = rect->top = 0;
        rect->right = (LONG)surf->pw;
        rect->bottom = (LONG)surf->ph;
    }
}

static int begin(const Nv2aSurface *s, const Nv2aRenderState *rs, uint32_t clear_flags)
{
    Surf *surf;
    Depth *d;

    if (!ensure_device() || !s || !s->color_va)
        return 0;
    gpu_work_begin();
    if (depth_views_enabled() && !color_depth_compatible(s))
        return 0;
    surf = get_surf(s);
    if (!surf)
        return 0;
    d = rs && (!depth_views_enabled() || !clear_flags || (clear_flags & 3))
      ? get_depth(rs->zeta_va, surf) : NULL;
    if (rs && rs->zeta_va && !d &&
        (!depth_views_enabled() || !clear_flags || (clear_flags & 3)))
        return 0;
    if (d && !sync_depth_guest(d)) {
        D3D11_RECT rect;
        surface_clear_rect(s, surf, &rect);
        if ((clear_flags & 3) != 3 || rect.left != 0 || rect.top != 0 ||
            (uint32_t)rect.right != d->w || (uint32_t)rect.bottom != d->h ||
            !rebaseline_depth(d))
            return 0;
    }
    if (surf != s_cur || d != s_cur_depth) {
        int counting = surf != s_cur ? query_suspend_clear() : 0;
        bind_targets(surf, d);
        query_resume_clear(counting);
    }
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
    int counting, ok;

    if ((uint64_t)s->w * s->bpp > s->pitch)
        return 0;
    if (s->pw == s->w && s->ph == s->h) {
        ID3D11DeviceContext_UpdateSubresource(s_ctx, (ID3D11Resource *)s->tex, 0, NULL,
                                             s_surface_probe, s->pitch, 0);
        return 1;
    }
    if (!ensure_guest_texture(s))
        return 0;
    ID3D11DeviceContext_UpdateSubresource(s_ctx, (ID3D11Resource *)s->guest_tex, 0, NULL,
                                         s_surface_probe, s->pitch, 0);
    counting = query_suspend_clear();
    ok = gpu_scale_blit(s->guest_srv, s->w, s->h, s->rtv, s->pw, s->ph,
                        GPU_SCALE_BLIT_BILINEAR);
    if (!ok) {
        fprintf(stderr, "[GPU-D3D11] guest color upload for %08X failed\n", s->va);
        if (restore)
            bind_targets(restore, restore_depth);
        else
            ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);
        query_resume_clear(counting);
        return 0;
    }
    if (restore)
        bind_targets(restore, restore_depth);
    else
        ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);
    query_resume_clear(counting);
    return 1;
}

static int sync_guest_surface(Surf *s, int force)
{
    static int warned_conflict;
    int changed;

    if (!force && s->guest_check_gen == s_gen)
        return 1;
    changed = guest_surface_changed(s);
    if (changed < 0)
        return 0;
    s->guest_check_gen = s_gen;
    if (!changed)
        return 1;
    if (s->gpu_dirty) {
        s_st.guest_conflicts++;
        if (!warned_conflict++)
            fprintf(stderr, "[GPU-D3D11] guest writes raced GPU color work on surface %08X;"
                            " preserving the GPU frame\n", s->va);
        return 2;
    }
    if (!upload_guest_surface(s))
        return 0;
    shadow_from_probe(s);
#ifdef BRUTEFORCE_D3D11_TESTING
    if (scale_test_failure("guest-change-upload"))
        *(uint32_t *)GUEST(s->va) ^= 1u;
#endif
    s->guest_exact = 0;
    s_st.guest_uploads++;
    changed = guest_surface_changed(s);
    if (changed) {
        s->gpu_dirty = 1;
        s->guest_check_gen = 0;
        return changed < 0 ? 0 : 2;
    }
    return 1;
}

static uint32_t guest_clear_value(const Surf *s, uint32_t argb)
{
    if (s->bpp == 2)
        return ((argb >> 8) & 0xF800u) | ((argb >> 5) & 0x07E0u) |
               ((argb >> 3) & 0x001Fu);
    return argb;
}

static void shadow_from_clear(Surf *s, uint32_t argb)
{
    uint32_t value = guest_clear_value(s, argb);
    size_t i, count = (size_t)s->w * s->h;
    for (i = 0; i < count; ++i)
        memcpy(s->guest_shadow + i * s->bpp, &value, s->bpp);
}

static int mirror_full_color_clear(Surf *s, uint32_t argb)
{
    int changed;

    if ((s->bpp != 2 && s->bpp != 4) ||
        (uint64_t)s->w * s->bpp > s->pitch || !ensure_guest_shadow(s))
        return 0;
    shadow_from_clear(s, argb);
    if (!checked_write_guest_surface(s, s->guest_shadow, surf_guest_row(s)))
        return 0;
#ifdef BRUTEFORCE_D3D11_TESTING
    if (scale_test_failure("guest-change-mirror"))
        *(uint32_t *)GUEST(s->va) ^= 1u;
#endif
    changed = guest_surface_changed(s);
    if (changed < 0)
        return 0;
    s->guest_check_gen = 0;
    s->gpu_dirty = changed != 0;
    s->guest_exact = !changed && s_skip_clean_readback && s->bpp == 4 && s->pitch >= s->w * 4 &&
                     s->pw == s->w && s->ph == s->h;
    return 1;
}

/* ---- textures ------------------------------------------------------------ */

#define MAX_TEX 2048
#define MAX_TEX_SNAPSHOT_BYTES (128u << 20)
typedef struct {
    uint32_t offset, w, h, pitch, color, levels, uploaded_levels, palette, palette_entries;
    uint64_t hash, sparse, palette_hash;
    GuestSpanIdentity source_identity, palette_identity;
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
static uint8_t *s_tex_probe;
static size_t s_tex_probe_cap;
static size_t s_tex_snapshot_bytes;
static GpuTextureDecoder s_texture_decoder;
static int s_tex_gpu = 1, s_tex_log, s_tex_approx, s_tex_aniso;

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

static uint64_t tex_hash(const uint8_t *p, uint32_t color, size_t n)
{
    if (!n)
        return 0;
    s_st.tex_approx_hash_bytes += n;
    return hash_bytes(p, n) ^ ((uint64_t)color << 56);
}

/* 128 eight-byte samples spread over the texture. */
static uint64_t tex_sparse(const uint8_t *p, size_t n)
{
    size_t i;
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
        if (g_bruteforce_perf_counters_enabled)
            bruteforce_perf_record_texture_snapshot_eviction();
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
    if (g_bruteforce_perf_counters_enabled)
        bruteforce_perf_record_texture_eviction();
    release_tex(&s_tex[lru]);
    return &s_tex[lru];
}

static void discard_tex(Tex *e)
{
    int index = (int)(e - s_tex);
    if (g_bruteforce_perf_counters_enabled)
        bruteforce_perf_record_texture_discard();
    release_tex(e);
    --s_ntex;
    if (index != s_ntex)
        s_tex[index] = s_tex[s_ntex];
    memset(&s_tex[s_ntex], 0, sizeof s_tex[s_ntex]);
}

static void set_tex_key(Tex *e, const Nv2aTexture *t, int cube, unsigned palette_entries,
                        unsigned uploaded_levels)
{
    e->offset = t->offset; e->w = t->width; e->h = t->height;
    e->color = t->color; e->pitch = t->pitch; e->cube = (uint8_t)cube;
    e->levels = t->levels; e->uploaded_levels = uploaded_levels;
    e->palette = t->palette; e->palette_entries = palette_entries;
    e->last_use = s_gen;
}

static int ensure_decode(size_t texels)
{
    if (texels <= s_decode_cap)
        return 1;
    if (texels > SIZE_MAX / sizeof *s_decode)
        return 0;
    free(s_decode);
    s_decode = (uint32_t *)malloc(texels * 4);
    s_decode_cap = s_decode ? texels : 0;
    return s_decode != NULL;
}

static int ensure_tex_probe(size_t bytes)
{
    uint8_t *probe;
    if (bytes <= s_tex_probe_cap)
        return 1;
    probe = (uint8_t *)realloc(s_tex_probe, bytes);
    if (!probe)
        return 0;
    s_tex_probe = probe;
    s_tex_probe_cap = bytes;
    return 1;
}

static void report_texture_copy_failure(GuestCopyResult result, uint32_t va,
                                        size_t bytes, int palette, int p8)
{
    static unsigned reports;
    const char *part = palette ? "palette" : "texture";
    if (!p8) {
        if (reports >= 32)
            return;
        ++reports;
    }
    if (result == GUEST_COPY_SHORT)
        fprintf(stderr, "[GPU-D3D11] %s%s allocation is too short for %zu bytes at 0x%08X\n",
                p8 ? "P8 " : "", part, bytes, va);
    else if (result == GUEST_COPY_UNREGISTERED)
        fprintf(stderr, "[GPU-D3D11] %s%s span at 0x%08X (%zu bytes) lacks registered bounds\n",
                p8 ? "P8 " : "", part, va, bytes);
    else if (result == GUEST_COPY_CHANGED)
        fprintf(stderr, "[GPU-D3D11] %s%s span at 0x%08X (%zu bytes) changed allocation"
                        " or mapping during the checked read\n",
                p8 ? "P8 " : "", part, va, bytes);
    else
        fprintf(stderr, "[GPU-D3D11] %s%s span at 0x%08X (%zu bytes) is not readable\n",
                p8 ? "P8 " : "", part, va, bytes);
    if (p8) {
        fflush(stderr);
        abort();
    }
}

static void mip_fallback(const char *reason, const Nv2aTexture *t, unsigned stage)
{
    static unsigned reports;
    if (reports++ < 32)
        fprintf(stderr, "[GPU-MIPS] stage %u texture 0x%08X format 0x%02X: %s%s\n",
                stage, t->offset, t->color, reason,
                reports == 32 ? " (further mip fallback reports suppressed)" : "");
}

static int mip_filter_requested(uint32_t filter)
{
    uint32_t min_filter = (filter >> 16) & 0xFFu;
    return min_filter >= 3 && min_filter <= 6;
}

typedef enum {
    TEX_UPLOAD_FAILED,
    TEX_UPLOAD_OK,
    TEX_UPLOAD_UNSUPPORTED
} TextureUploadResult;

/* DXT blocks go as BC1-3; the other supported formats use the compute or
 * reference CPU decoder. D3D subresources are ordered by face, then level. */
static TextureUploadResult upload(Tex *e, const Nv2aTexture *t, int cube,
                                  const GpuTextureLayout *layout,
                                  const uint8_t *source, const uint8_t *palette,
                                  unsigned palette_entries, const char **path)
{
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA init[GPU_TEXTURE_MAX_FACES * GPU_TEXTURE_MAX_LEVELS];
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    int dxt = (int)d3d8_format_dxt_block_bytes(t->color);
    unsigned face, level;
    HRESULT hr;
    int decoded_gpu = 0, attempted_gpu = 0;

    memset(&td, 0, sizeof td);
    td.Width = t->width;
    td.Height = t->height;
    td.MipLevels = layout->levels;
    td.ArraySize = layout->faces;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
    memset(init, 0, sizeof init);
    if (dxt && !t->pitch && (t->width & 3) == 0 && (t->height & 3) == 0) {
        td.Format = t->color == 0x0C ? DXGI_FORMAT_BC1_UNORM
                  : t->color == 0x0E ? DXGI_FORMAT_BC2_UNORM : DXGI_FORMAT_BC3_UNORM;
        for (face = 0; face < layout->faces; ++face)
            for (level = 0; level < layout->levels; ++level) {
                const GpuTextureSubresource *sub = &layout->subresource[face][level];
                unsigned index = level + face * layout->levels;
                init[index].pSysMem = source + sub->offset;
                init[index].SysMemPitch = (UINT)sub->row_pitch;
            }
        *path = "BC";
        s_st.tex_bc++;
    } else if (t->color == 0x12 && t->pitch >= t->width * 4) {
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        for (face = 0; face < layout->faces; ++face) {
            const GpuTextureSubresource *sub = &layout->subresource[face][0];
            unsigned index = face * layout->levels;
            init[index].pSysMem = source + sub->offset;
            init[index].SysMemPitch = (UINT)sub->row_pitch;
        }
        *path = "direct";
        s_st.tex_direct++;
    } else {
        if (s_tex_gpu && gpu_texture_decode_supported(t)) {
            attempted_gpu = 1;
            decoded_gpu = gpu_texture_decode(&s_texture_decoder, s_dev, s_ctx, t, layout,
                                              source, layout->span, palette, palette_entries,
                                              &e->tex);
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
            size_t total_texels = 0, used_texels = 0;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            for (face = 0; face < layout->faces; ++face)
                for (level = 0; level < layout->levels; ++level) {
                    const GpuTextureSubresource *sub = &layout->subresource[face][level];
                    size_t texels = (size_t)sub->width * sub->height;
                    if (texels > SIZE_MAX - total_texels)
                        return TEX_UPLOAD_FAILED;
                    total_texels += texels;
                }
            if (!ensure_decode(total_texels)) {
                fprintf(stderr, "[GPU-D3D11] cannot allocate CPU texture decode buffer\n");
                return TEX_UPLOAD_FAILED;
            }
            for (face = 0; face < layout->faces; ++face)
                for (level = 0; level < layout->levels; ++level) {
                    const GpuTextureSubresource *sub = &layout->subresource[face][level];
                    size_t texels = (size_t)sub->width * sub->height;
                    unsigned index = level + face * layout->levels;
                    Nv2aTexture image = *t;
                    if (t->color == 0x0B) {
                        if (!gpu_texture_decode_p8_cpu(layout, face, level, source, layout->span,
                                                       palette, palette_entries,
                                                       s_decode + used_texels, texels))
                            return TEX_UPLOAD_FAILED;
                    } else {
                        image.width = sub->width;
                        image.height = sub->height;
                        image.pitch = layout->storage == GPU_TEXTURE_LINEAR
                                    ? (uint32_t)sub->row_pitch : 0;
                        image.levels = 1;
                        if (!nv2a_backend_decode_texture_data(&image, source + sub->offset,
                                                               sub->size, s_decode + used_texels,
                                                               texels)) {
                            static unsigned failures;
                            if (failures++ < 10)
                                fprintf(stderr, "[GPU-D3D11] cannot decode texture format 0x%02X"
                                                " at 0x%08X%s\n", t->color, t->offset,
                                        failures == 10 ? " (further failures suppressed)" : "");
                            /* An attempted GPU decode may have failed transiently. */
                            return attempted_gpu ? TEX_UPLOAD_FAILED : TEX_UPLOAD_UNSUPPORTED;
                        }
                    }
                    init[index].pSysMem = s_decode + used_texels;
                    init[index].SysMemPitch = sub->width * 4;
                    used_texels += texels;
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
        sv.TextureCube.MipLevels = layout->levels;
    } else {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = layout->levels;
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

static int s_texture_hard_reject;

static ID3D11ShaderResourceView *snapshot_bound_surface(Surf *s)
{
    D3D11_TEXTURE2D_DESC desc;
    HRESULT hr;
    if (!s->sample_copy) {
        ID3D11Texture2D_GetDesc(s->tex, &desc);
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        hr = ID3D11Device_CreateTexture2D(s_dev, &desc, NULL, &s->sample_copy);
        if (SUCCEEDED(hr))
            hr = ID3D11Device_CreateShaderResourceView(s_dev,
                    (ID3D11Resource *)s->sample_copy, NULL, &s->sample_copy_srv);
        if (FAILED(hr)) {
            gpu_d3d11_check_device(s_dev, hr, "creating color alias snapshot");
            fprintf(stderr, "[GPU-D3D11] color alias snapshot creation failed 0x%08lX\n",
                    (unsigned long)hr);
            if (s->sample_copy) ID3D11Texture2D_Release(s->sample_copy);
            s->sample_copy = NULL;
            return NULL;
        }
    }
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)s->sample_copy,
                                    (ID3D11Resource *)s->tex);
    ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 1, &s->rtv,
                                          s_cur_depth ? s_cur_depth->dsv : NULL);
    return s->sample_copy_srv;
}

/* A GPU-owned overlap may not be read from stale guest bytes. An exact,
 * layout-compatible surface can be sampled after reconciling its guest
 * shadow; the currently bound target needs a separate ordered GPU copy. */
static int color_texture_alias(const Nv2aTexture *t, int cube,
                               const GpuTextureLayout *layout,
                               ID3D11ShaderResourceView **view)
{
    uint64_t start = t->offset, end = start + layout->span, phys = 0;
    int physical = physical_range(start, layout->span, &phys);
    int i, overlaps = 0, dirty_overlap = 0;
    Surf *exact = NULL;
    static unsigned rejected;

    *view = NULL;
    for (i = 0; i < s_nsurf; ++i) {
        Surf *s = &s_surf[i];
        uint64_t span, surface_end, surface_phys = 0;
        int sp, overlap, sync;
        if (!s->tex)
            continue;
        span = (uint64_t)(s->h - 1) * s->pitch + surf_guest_row(s);
        surface_end = (uint64_t)s->va + span;
        sp = physical_range(s->va, span, &surface_phys);
        overlap = physical < 0 || sp < 0 ||
                  (start < surface_end && s->va < end) ||
                  (physical > 0 && sp > 0 && phys < surface_phys + span &&
                   surface_phys < phys + layout->span);
        if (!overlap)
            continue;
        if (!surf_identity_current(s)) {
            uint32_t remaining;
            if (s->identity.layout == xbox_MemoryLayoutGeneration() &&
                s->identity.allocation &&
                !guest_allocation_identity(t->offset, &remaining))
                goto reject;
            continue;
        }
        if (s->alias_uncertain)
            goto reject;
        ++overlaps;
        sync = sync_guest_surface(s, 1);
        if (sync != 1)
            goto reject;
        if (s->gpu_dirty)
            ++dirty_overlap;
        if (!cube && (uint64_t)s->w * s->bpp <= s->pitch &&
            t->offset == s->va && t->width == s->w && t->height == s->h &&
            (t->pitch == s->pitch ||
             (t->color == 0x06 && !t->pitch && s->pitch == s->w * 4)) &&
            ((s->bpp == 4 && (t->color == 0x12 || t->color == 0x06 ||
                              (t->color == 0x2E && !s_depth_texture))) ||
             (s->bpp == 2 && t->color == 0x11)))
            exact = s;
        else {
            s_st.overlap_rtt++;
            if (s->gpu_dirty)
                goto reject;
        }
    }
    if (exact && (overlaps == 1 || !dirty_overlap)) {
        if (s_tex_strict_bounds) {
            GpuTextureLayout base;
            uint32_t remaining;
            uint64_t id = xbox_GuestBoundsIdentity(t->offset, &remaining);
            if (!gpu_texture_layout(t, 1, 0, &base) || !id || base.span > remaining)
                goto reject;
        }
        *view = exact == s_cur ? snapshot_bound_surface(exact) : exact->srv;
        if (!*view)
            goto reject;
        s_st.rtt++;
        return 1;
    }
    if (dirty_overlap)
        goto reject;
    return 0;
reject:
    if (rejected < 32) {
        ++rejected;
        fprintf(stderr, "[GPU-D3D11] color texture alias at %08X is conflicting,"
                        " ambiguous or unsafe; draw rejected%s\n", t->offset,
                rejected == 32 ? " (further reports suppressed)" : "");
    }
    return -1;
}

/* The view a stage samples, and the size its texel-space coordinates are
 * divided by (*tw, *th). */
static ID3D11ShaderResourceView *get_texture(const Nv2aTexture *t, int cube, unsigned stage,
                                             float *tw, float *th, unsigned *mip_levels)
{
    Tex *e = NULL;
    ID3D11ShaderResourceView *alias = NULL;
    Nv2aTexture normalized;
    GpuTextureLayout layout, base;
    uint64_t h, palette_hash = 0;
    size_t source_bytes, palette_bytes, snapshot_bytes;
    uint8_t *snapshot;
    GuestSpanIdentity source_identity = {0}, palette_identity = {0};
    GuestCopyResult copy_result;
    unsigned palette_entries = 0, invalid_index;
    unsigned perf_probes = 0;
    const char *path = NULL;
    TextureUploadResult upload_result;
    uint64_t perf_start = 0;
    int perf = g_bruteforce_perf_counters_enabled;
    int rewritten, approximate, same_lifetime;
    int i, alias_result, faces = cube ? 6 : 1;

    s_texture_hard_reject = 0;
    *mip_levels = 1;
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
    if (s_depth_texture && t->color == 0x2E) {
        uint32_t levels = (nv2a_backend_reg(0x1B04 + stage * 0x40) >> 16) & 0xF;
        if (s_tex_mips && (levels > 1 || mip_filter_requested(t->filter)))
            mip_fallback(levels > 1 ? "multi-level depth alias unsupported; texture rejected"
                                    : "depth alias is level 0 only", t, stage);
        return get_depth_texture(t, cube, levels);
    }
    if (s_stencil_alias && t->color == 0x41) {
        int ambiguous;
        Depth *overlap = overlapping_depth(t, cube, &ambiguous);
        uint32_t format = nv2a_backend_reg(0x1B04 + stage * 0x40);
        uint32_t levels = (format >> 16) & 0xF;
        if (overlap) {
            if (s_tex_mips && (levels > 1 || mip_filter_requested(t->filter)))
                mip_fallback(levels > 1 ? "multi-level stencil alias unsupported; texture rejected"
                                        : "stencil alias is level 0 only", t, stage);
            return get_packed_depth_texture(t, cube || (format & 4) != 0, levels);
        }
    }
    if (depth_views_enabled()) {
        int ambiguous;
        if (overlapping_depth(t, cube, &ambiguous)) {
            static int warned_overlap;
            s_st.depth_rejected++;
            if (!warned_overlap++)
                fprintf(stderr, "[GPU-D3D11] texture overlaps GPU-owned depth with"
                                " an unsupported format or address; texture rejected\n");
            return NULL;
        }
    }
    if (!gpu_texture_layout(t, faces, s_tex_mips, &layout)) {
        fprintf(stderr, "[GPU-D3D11] invalid texture span or layout for format 0x%02X, %ux%u"
                        " faces=%d levels=%u\n", t->color, t->width, t->height, faces, t->levels);
        return NULL;
    }
    alias_result = color_texture_alias(t, cube, &layout, &alias);
    if (alias_result < 0) {
        s_texture_hard_reject = 1;
        return NULL;
    }
    if (alias_result > 0) {
        if (s_tex_mips && (t->levels > 1 || mip_filter_requested(t->filter)))
            mip_fallback("render-target alias is level 0 only", t, stage);
        return alias;
    }
    /* Unsupported-format entries depend on metadata and allocation identity,
     * not the inaccessible bytes. Preserve the existing no-read fallback. */
    if (t->color != 0x0B) {
        uint32_t remaining;
        GuestSpanIdentity current = {0};
        current.layout = xbox_MemoryLayoutGeneration();
        current.allocation = guest_allocation_identity(t->offset, &remaining);
        if (s_tex_strict_bounds)
            current.bounds = xbox_GuestBoundsIdentity(t->offset, &remaining);
        for (i = 0; i < s_ntex; ++i) {
            Tex *negative = &s_tex[i];
            if (negative->unsupported && negative->offset == t->offset &&
                negative->w == t->width && negative->h == t->height &&
                negative->color == t->color && negative->pitch == t->pitch &&
                negative->cube == (uint8_t)cube && negative->palette == t->palette &&
                (!(cube || s_tex_mips) || negative->levels == t->levels) &&
                (!s_tex_mips || negative->uploaded_levels == layout.levels) &&
                (!s_tex_strict_bounds || (current.bounds && layout.span <= remaining)) &&
                same_guest_identity(&negative->source_identity, &current)) {
                negative->last_use = s_gen;
                s_st.tex_unsupported_hits++;
                if (perf)
                    bruteforce_perf_record_texture_unsupported_hit();
                return NULL;
            }
        }
    }
    if (s_tex_mips) {
        if (layout.storage != GPU_TEXTURE_LINEAR && t->levels > layout.levels)
            mip_fallback("declared levels exceed the 1x1 chain; capped", t, stage);
        if (layout.storage == GPU_TEXTURE_LINEAR && mip_filter_requested(t->filter))
            mip_fallback("linear texture has no mip chain; sampling level 0", t, stage);
        else if (layout.levels == 1 && mip_filter_requested(t->filter))
            mip_fallback("no additional mip levels declared; sampling level 0", t, stage);
    }
    if (t->color == 0x0B) {
        palette_entries = nv2a_p8_palette_entries(t->palette_entries);
        if (!palette_entries) {
            fprintf(stderr, "[GPU-D3D11] invalid P8 palette length %u at stage %u\n",
                    t->palette_entries, stage);
            return NULL;
        }
    }
    palette_bytes = (size_t)palette_entries * 4;
    if (palette_bytes > SIZE_MAX - layout.span || !ensure_tex_probe(layout.span + palette_bytes)) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate checked texture snapshot\n");
        return NULL;
    }
    copy_result = checked_guest_copy(t->offset, layout.span, s_tex_probe,
                                     s_tex_strict_bounds, &source_identity);
    if (copy_result != GUEST_COPY_OK && s_tex_mips && layout.levels > 1 &&
        gpu_texture_layout(t, faces, 0, &base) &&
        (!cube || base.face_stride == layout.face_stride) &&
        checked_guest_copy(t->offset, base.span, s_tex_probe, s_tex_strict_bounds,
                           &source_identity) == GUEST_COPY_OK) {
        mip_fallback("mip chain is unreadable or shorter than its tracked allocation;"
                     " falling back to level 0", t, stage);
        layout = base;
        copy_result = GUEST_COPY_OK;
    }
    if (copy_result != GUEST_COPY_OK) {
        if (s_tex_mips)
            mip_fallback("texture span is unreadable or shorter than its tracked allocation;"
                         " texture rejected", t, stage);
        report_texture_copy_failure(copy_result, t->offset, layout.span, 0, t->color == 0x0B);
        s_texture_hard_reject = 1;
        return NULL;
    }
    source_bytes = layout.span;
    if (palette_bytes) {
        copy_result = checked_guest_copy(t->palette, palette_bytes, s_tex_probe + source_bytes,
                                         s_tex_strict_bounds, &palette_identity);
        if (copy_result != GUEST_COPY_OK) {
            report_texture_copy_failure(copy_result, t->palette, palette_bytes, 1, 1);
            return NULL;
        }
    }
    if (!guest_identity_still_valid(t->offset, source_bytes, s_tex_strict_bounds,
                                    &source_identity) ||
        (palette_bytes && !guest_identity_still_valid(t->palette, palette_bytes,
                                                       s_tex_strict_bounds,
                                                       &palette_identity))) {
        report_texture_copy_failure(GUEST_COPY_CHANGED, t->offset, source_bytes, 0,
                                    t->color == 0x0B);
        s_texture_hard_reject = 1;
        return NULL;
    }
    if (palette_bytes > SIZE_MAX - source_bytes)
        return NULL;
    snapshot_bytes = source_bytes + palette_bytes;
    approximate = s_tex_approx && !(s_tex_mips && layout.storage != GPU_TEXTURE_LINEAR &&
                                   layout.declared_levels > 1);
    if (s_tex_approx && !approximate) {
        static int reported;
        if (!reported++)
            fprintf(stderr, "[GPU-MIPS] mipped textures use exact snapshots even when"
                            " the approximate cache is enabled\n");
    }
    if (perf)
        perf_start = bruteforce_perf_clock();
    for (i = 0; i < s_ntex; i++) {
        if (perf)
            perf_probes++;
        if (s_tex[i].offset == t->offset && s_tex[i].w == t->width && s_tex[i].h == t->height
                && s_tex[i].color == t->color && s_tex[i].pitch == t->pitch
                && s_tex[i].cube == (uint8_t)cube && s_tex[i].palette == t->palette
                && s_tex[i].palette_entries == palette_entries
                && (!(cube || s_tex_mips) || s_tex[i].levels == t->levels)
                && (!s_tex_mips || s_tex[i].uploaded_levels == layout.levels)) {
            e = &s_tex[i];
            break;
        }
    }
    if (perf)
        bruteforce_perf_record_texture_lookup(perf_probes, e != NULL,
                                              bruteforce_perf_clock() - perf_start);
    same_lifetime = e &&
        e->source_identity.layout == source_identity.layout &&
        e->source_identity.allocation == source_identity.allocation &&
        e->source_identity.bounds == source_identity.bounds &&
        e->palette_identity.layout == palette_identity.layout &&
        e->palette_identity.allocation == palette_identity.allocation &&
        e->palette_identity.bounds == palette_identity.bounds;
    if (e && e->source_identity.layout == source_identity.layout &&
        e->source_identity.allocation && !source_identity.allocation &&
        !source_identity.bounds) {
        report_texture_copy_failure(GUEST_COPY_CHANGED, t->offset, source_bytes, 0,
                                    t->color == 0x0B);
        s_texture_hard_reject = 1;
        return NULL;
    }
    if (e && palette_bytes && e->palette_identity.layout == palette_identity.layout &&
        e->palette_identity.allocation && !palette_identity.allocation &&
        !palette_identity.bounds) {
        report_texture_copy_failure(GUEST_COPY_CHANGED, t->palette, palette_bytes, 1, 1);
        return NULL;
    }
    if (same_lifetime && e->unsupported) {
        e->last_use = s_gen;
        s_st.tex_unsupported_hits++;
        if (perf)
            bruteforce_perf_record_texture_unsupported_hit();
        return NULL;
    }
    if (approximate && palette_bytes) {
        s_st.tex_approx_hash_bytes += palette_bytes;
        palette_hash = hash_bytes(s_tex_probe + source_bytes, palette_bytes);
    }
    if (!approximate) {
        if (same_lifetime && e->snapshot && e->snapshot_bytes == snapshot_bytes) {
            size_t compared_palette_bytes = 0;
            int source_equal, palette_equal;
            if (perf)
                perf_start = bruteforce_perf_clock();
            source_equal = !memcmp(e->snapshot, s_tex_probe, source_bytes);
            palette_equal = !palette_bytes;
            if (source_equal && palette_bytes) {
                compared_palette_bytes = palette_bytes;
                palette_equal = !memcmp(e->snapshot + source_bytes,
                                        s_tex_probe + source_bytes, palette_bytes);
            }
            if (perf)
                bruteforce_perf_record_texture_exact(
                    source_bytes, compared_palette_bytes, source_equal && palette_equal,
                    bruteforce_perf_clock() - perf_start);
            if (source_equal && palette_equal) {
                e->last_use = s_gen;
                *mip_levels = e->uploaded_levels;
                return e->srv;
            }
        }
    } else {
        if (same_lifetime && e->gen == s_gen && e->palette_hash == palette_hash) {
            e->last_use = s_gen;
            *mip_levels = e->uploaded_levels;
            return e->srv;
        }
        if (same_lifetime && e->palette_hash == palette_hash && e->stable >= 30
                && ((s_gen + (e->offset >> 12)) & 31) != 0
                && tex_sparse(s_tex_probe, source_bytes) == e->sparse) {
            e->gen = s_gen;
            e->last_use = s_gen;
            *mip_levels = e->uploaded_levels;
            return e->srv;
        }
        h = tex_hash(s_tex_probe, t->color, source_bytes);
        if (same_lifetime && e->hash == h && h && e->palette_hash == palette_hash) {
            if (e->stable < 0xFFFFFFFFu)
                e->stable++;
            if (e->stable == 30)
                e->sparse = tex_sparse(s_tex_probe, source_bytes);
            e->gen = s_gen;
            e->last_use = s_gen;
            *mip_levels = e->uploaded_levels;
            return e->srv;
        }
    }
    snapshot = (uint8_t *)malloc(snapshot_bytes);
    if (!snapshot) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate %zu-byte texture snapshot\n", snapshot_bytes);
        fflush(stderr);
        abort();
    }
    if (perf)
        perf_start = bruteforce_perf_clock();
    memcpy(snapshot, s_tex_probe, snapshot_bytes);
    if (perf)
        bruteforce_perf_record_texture_snapshot(snapshot_bytes,
                                                bruteforce_perf_clock() - perf_start);
    if (palette_bytes && !gpu_texture_p8_indices_valid(&layout, snapshot, source_bytes,
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
    if (approximate) {
        s_st.tex_approx_hash_bytes += source_bytes;
        h = hash_bytes(snapshot, source_bytes) ^ ((uint64_t)t->color << 56);
        if (palette_bytes) {
            s_st.tex_approx_hash_bytes += palette_bytes;
            palette_hash = hash_bytes(snapshot + source_bytes, palette_bytes);
        }
    } else {
        h = 0;
    }
    rewritten = e != NULL;
    if (perf && rewritten)
        bruteforce_perf_record_texture_rewrite();
    e = tex_slot(e);
    memset(e, 0, sizeof *e);
    set_tex_key(e, t, cube, palette_entries, layout.levels);
    e->source_identity = source_identity;
    e->palette_identity = palette_identity;
    upload_result = upload(e, t, cube, &layout, snapshot,
                           palette_bytes ? snapshot + source_bytes : NULL,
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
    if (approximate)
        free(snapshot);
    else
        retain_tex_snapshot(e, snapshot, snapshot_bytes);
    if (s_tex_log)
        fprintf(stderr, "[GPU-TEX] %s %s va=0x%08X format=0x%02X %ux%u pitch=%u"
                        " faces=%d levels=%u uploaded=%u palette=0x%08X\n",
                rewritten ? "changed" : "new", path, t->offset, t->color, t->width,
                t->height, t->pitch, faces, t->levels, layout.levels, t->palette);
    *mip_levels = layout.levels;
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

static int depth_state_can_write(const Nv2aRenderState *rs)
{
    D3D11_COMPARISON_FUNC depth = rs->depth_test_enable ? map_cmp(rs->depth_func)
                                                        : D3D11_COMPARISON_ALWAYS;
    D3D11_COMPARISON_FUNC stencil = rs->stencil_enable ? map_cmp(rs->stencil_func)
                                                       : D3D11_COMPARISON_ALWAYS;

    if (rs->depth_write && stencil != D3D11_COMPARISON_NEVER &&
        depth != D3D11_COMPARISON_NEVER)
        return 1;
    if (!rs->stencil_enable || !(rs->stencil_write_mask & 0xFF))
        return 0;
    if (stencil != D3D11_COMPARISON_ALWAYS &&
        map_sop(rs->stencil_fail) != D3D11_STENCIL_OP_KEEP)
        return 1;
    if (stencil == D3D11_COMPARISON_NEVER)
        return 0;
    if (depth != D3D11_COMPARISON_ALWAYS &&
        map_sop(rs->stencil_zfail) != D3D11_STENCIL_OP_KEEP)
        return 1;
    return depth != D3D11_COMPARISON_NEVER &&
           map_sop(rs->stencil_zpass) != D3D11_STENCIL_OP_KEEP;
}

static void depth_written(Depth *depth)
{
    depth->gpu_dirty = 1;
    if (++depth->revision == 0)
        depth->packed_valid = 0;
}

#define MAX_STATE 256
typedef struct { uint32_t key[5]; ID3D11BlendState *bs; } BlendEntry;
typedef struct { uint32_t key[10]; ID3D11DepthStencilState *ds; } DsEntry;
typedef struct { D3D11_SAMPLER_DESC key; ID3D11SamplerState *ss; } SampEntry;
static BlendEntry s_blend[MAX_STATE];
static int s_nblend;
static DsEntry s_ds[MAX_STATE];
static int s_nds;
#define MAX_SAMP 4096
static SampEntry s_samp[MAX_SAMP];
static int s_nsamp;

static ID3D11BlendState *get_blend(const Nv2aRenderState *rs)
{
    uint32_t key[5] = { rs->blend_enable, rs->blend_src, rs->blend_dst, rs->blend_eq,
                        rs->color_mask };
    D3D11_BLEND_DESC bd;
    D3D11_RENDER_TARGET_BLEND_DESC *t = &bd.RenderTarget[0];
    UINT8 mask = 0;
    int i;

    if (scale_test_failure("blend-state"))
        return NULL;
    for (i = 0; i < s_nblend; i++)
        if (!memcmp(s_blend[i].key, key, sizeof key))
            return s_blend[i].bs;
    if (s_nblend == MAX_STATE) {
        fprintf(stderr, "[GPU-D3D11] blend-state cache is full; draw rejected\n");
        return NULL;
    }
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

static ID3D11BlendState *get_required_blend(const Nv2aRenderState *rs)
{
    ID3D11BlendState *state = get_blend(rs);
    if (!state) {
        fprintf(stderr, "[GPU-D3D11] cannot create blend state; refusing to"
                        " draw with a default blend or color mask, stopping\n");
        fflush(stderr);
        abort();
    }
    return state;
}

static ID3D11DepthStencilState *get_ds(const Nv2aRenderState *rs, int has_depth)
{
    uint32_t key[10] = { has_depth && rs->depth_test_enable, rs->depth_func,
                         has_depth && rs->depth_write, has_depth && rs->stencil_enable,
                         rs->stencil_func, rs->stencil_func_mask, rs->stencil_write_mask,
                         rs->stencil_fail, rs->stencil_zfail, rs->stencil_zpass };
    D3D11_DEPTH_STENCIL_DESC dd;
    int i;

    if (scale_test_failure("depth-state"))
        return NULL;
    for (i = 0; i < s_nds; i++)
        if (!memcmp(s_ds[i].key, key, sizeof key))
            return s_ds[i].ds;
    if (s_nds == MAX_STATE) {
        fprintf(stderr, "[GPU-D3D11] depth-stencil state cache is full; draw rejected\n");
        return NULL;
    }
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

static ID3D11DepthStencilState *get_required_ds(const Nv2aRenderState *rs, int has_depth)
{
    ID3D11DepthStencilState *state = get_ds(rs, has_depth);
    if (!state) {
        fprintf(stderr, "[GPU-D3D11] cannot create depth-stencil state; refusing to"
                        " draw with untracked default depth writes, stopping\n");
        fflush(stderr);
        abort();
    }
    return state;
}

static ID3D11SamplerState *get_sampler(uint32_t addr_u, uint32_t addr_v, uint32_t filter,
                                       uint32_t control0, unsigned levels)
{
    D3D11_SAMPLER_DESC sd;
    unsigned warnings;
    int i;

    if (scale_test_failure("sampler-state"))
        return NULL;
    warnings = gpu_texture_sampler_desc(&sd, addr_u, addr_v, filter, control0,
                                        levels, s_tex_mips, s_tex_aniso);
    if (warnings) {
        static unsigned reports;
        if (reports++ < 16) {
            fprintf(stderr, "[GPU-MIPS] sampler filter 0x%08X:", filter);
            if (warnings & GPU_SAMPLER_UNKNOWN_MIN)
                fprintf(stderr, " unknown minification filter, using point;");
            if (warnings & GPU_SAMPLER_UNKNOWN_MAG)
                fprintf(stderr, " unknown magnification filter, using point;");
            if (warnings & GPU_SAMPLER_CONVOLUTION)
                fprintf(stderr, " convolution approximated by linear filtering;");
            if (warnings & GPU_SAMPLER_BIAS_CLAMPED)
                fprintf(stderr, " LOD bias clamped to the D3D11 limit;");
            if (warnings & GPU_SAMPLER_LOD_RANGE)
                fprintf(stderr, " inverted LOD clamps collapsed to the maximum;");
            fprintf(stderr, "%s\n", reports == 16 ? " further reports suppressed" : "");
        }
    }
    for (i = 0; i < s_nsamp; i++)
        if (!memcmp(&s_samp[i].key, &sd, sizeof sd))
            return s_samp[i].ss;
    if (s_nsamp == (s_tex_mips ? MAX_SAMP : MAX_STATE)) {
        static int reported;
        if (!reported++)
            fprintf(stderr, "[GPU-D3D11] sampler cache is full; cannot create sampler\n");
        return NULL;
    }
    if (gpu_call_failed(ID3D11Device_CreateSamplerState(s_dev, &sd, &s_samp[s_nsamp].ss),
                        "creating sampler state"))
        return NULL;
    s_samp[s_nsamp].key = sd;
    return s_samp[s_nsamp++].ss;
}

static ID3D11SamplerState *get_required_sampler(uint32_t addr_u, uint32_t addr_v,
                                                uint32_t filter, uint32_t control0,
                                                unsigned levels)
{
    ID3D11SamplerState *state = get_sampler(addr_u, addr_v, filter, control0, levels);
    if (!state) {
        fprintf(stderr, "[GPU-D3D11] cannot create sampler state; refusing to"
                        " draw with a missing sampler, stopping\n");
        fflush(stderr);
        abort();
    }
    return state;
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
    uint32_t depth_flags = flags & 3;
    uint32_t color_flags = flags & 0xF0;
    D3D11_RECT rect;
    float c[4], z;
    int full_color, full_depth;

    if (!begin(s, rs, flags))
        return;
    surface_clear_rect(s, s_cur, &rect);
    s_st.clears++;
    if (rect.left >= rect.right || rect.top >= rect.bottom)
        return;
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
    if (s_cur_depth && depth_flags)
        depth_written(s_cur_depth);
    if (color_flags) {
        if (color_flags == 0xF0 && full_color) {
            s_cur->alias_uncertain = 0;
            if (!mirror_full_color_clear(s_cur, argb)) {
                s_cur->gpu_dirty = 1;
                s_cur->guest_exact = 0;
                s_cur->alias_uncertain = 1;
            }
        } else {
            s_cur->gpu_dirty = 1;
            s_cur->guest_exact = 0;
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
    ID3D11DepthStencilState *ds;
    float bf[4], tw = 1.0f, th = 1.0f;
    unsigned mip_levels = 1;

    if (!b || !b->count || !b->state || !begin(s, b->state, 0) ||
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
        srv = get_texture(b->texture, 0, 0, &tw, &th, &mip_levels);
        if (!srv && (s_texture_hard_reject || required_depth_alias(b->texture, 0)))
            return;
        if (srv)
            ss = get_required_sampler(b->texture->addr_u, b->texture->addr_v,
                                      b->texture->filter, nv2a_backend_reg(0x1B0C),
                                      mip_levels);
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
    ds = get_required_ds(rs, s_cur_depth != NULL);
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
    ID3D11DeviceContext_OMSetBlendState(s_ctx, get_required_blend(rs), bf, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, ds, rs->stencil_ref & 0xFF);
    ID3D11DeviceContext_Draw(s_ctx, b->count, 0);
    s_cur->gpu_dirty = 1;
    s_cur->guest_exact = 0;
    if (s_cur_depth && depth_state_can_write(rs))
        depth_written(s_cur_depth);
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
    ID3D11DepthStencilState *ds;
    ID3D11ShaderResourceView *srv[4] = { NULL, NULL, NULL, NULL };
    ID3D11SamplerState *ss[4] = { NULL, NULL, NULL, NULL };
    float bf[4];
    int st;

    if (!b || !b->state || !begin(s, b->state, 0) ||
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
        unsigned mip_levels = 1;
        if (s_depth_texture && b->tex_valid[st] && b->tex[st].color == 0x2E &&
            mode != 0 && (b->tex_cube[st] || (mode != 1 && mode != 2))) {
            reject_depth_mode(mode, b->tex_cube[st], 0);
            return 0;
        }
        if (!b->tex_valid[st] || mode < 1 || mode > 3)
            continue;
        srv[st] = get_texture(&b->tex[st], cube, st, &tw, &th, &mip_levels);
        if (!srv[st]) {
            if (s_texture_hard_reject || required_depth_alias(&b->tex[st], cube))
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
        ss[st] = get_required_sampler(b->tex[st].addr_u, b->tex[st].addr_v,
                                      b->tex[st].filter,
                                      nv2a_backend_reg(0x1B0C + st * 0x40), mip_levels);
        c.texinfo[st][0] = 1.0f / tw;
        c.texinfo[st][1] = 1.0f / th;
    }
    ps = get_rc_shader(&key);
    if (!ps)
        return 0;
    ds = get_required_ds(rs, s_cur_depth != NULL);
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
    ID3D11DeviceContext_OMSetBlendState(s_ctx, get_required_blend(rs), bf, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(s_ctx, ds, rs->stencil_ref & 0xFF);
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
    s_cur->guest_exact = 0;
    if (s_cur_depth && depth_state_can_write(b->state))
        depth_written(s_cur_depth);
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
    DWORD queued_tick;
    volatile LONG slow_reported;
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
static int s_shader_diag;
static volatile LONG s_shader_diag_reports;

static void shader_diagnostic(const char *format, ...)
{
    LONG count;
    char message[384];
    va_list args;

    if (!s_shader_diag)
        return;
    count = InterlockedCompareExchange(&s_shader_diag_reports, 0, 0);
    for (;;) {
        LONG previous;
        if (count >= 32)
            return;
        previous = InterlockedCompareExchange(&s_shader_diag_reports, count + 1, count);
        if (previous == count)
            break;
        count = previous;
    }
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    fprintf(stderr, "[GPU-SHADER] %s%s\n", message,
            count == 31 ? " (further shader diagnostics suppressed)" : "");
}

static void vsh_note_pending(Vsh *v)
{
    DWORD now;
    if (!s_shader_diag || v->state != 0)
        return;
    now = GetTickCount();
    if ((DWORD)(now - v->queued_tick) >= 2000 &&
        InterlockedCompareExchange(&v->slow_reported, 1, 0) == 0)
        shader_diagnostic("vertex program %016llX still queued or compiling after %lu ms",
                          (unsigned long long)v->hash,
                          (unsigned long)(DWORD)(now - v->queued_tick));
}

static DWORD WINAPI vsh_worker(LPVOID arg)
{
    (void)arg;
    for (;;) {
        LONG i, n = s_nvsh;
        int did = 0;
        for (i = 0; i < n; i++) {
            Vsh *v = &s_vsh[i];
            ID3DBlob *blob = NULL, *err = NULL;
            DWORD started;
            HRESULT result;
            if (v->state != 0)
                continue;
            started = s_shader_diag ? GetTickCount() : 0;
            shader_diagnostic("compile-start key=%016llX instructions=%u",
                              (unsigned long long)v->hash, v->n);
            result = D3DCompile(v->src, v->len, "vsh", NULL, NULL, "vs_main", "vs_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS,
                                0, &blob, &err);
            if (FAILED(result)) {
                if (s_shader_diag)
                    shader_diagnostic("compile-failed key=%016llX result=0x%08lX elapsed=%lu ms",
                                      (unsigned long long)v->hash, (unsigned long)result,
                                      (unsigned long)(DWORD)(GetTickCount() - started));
                if (InterlockedIncrement(&s_vsh_failures) <= 3)
                    fprintf(stderr, "[GPU-D3D11] vertex shader %ld failed: %s\n", (long)i,
                            err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
                if (err)
                    ID3D10Blob_Release(err);
                InterlockedExchange(&v->state, -1);
            } else {
                if (s_shader_diag)
                    shader_diagnostic("compile-finished key=%016llX elapsed=%lu ms",
                                      (unsigned long long)v->hash,
                                      (unsigned long)(DWORD)(GetTickCount() - started));
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
    shader_diagnostic("prepare-%s key=%016llX bytecode=%s",
                      ok ? "ready" : "failed", (unsigned long long)v->hash,
                      v->code ? "retained" : "unavailable");
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
        if (v->state == 0)
            vsh_note_pending(v);
        return v->state == 2 ? v : NULL;
    }
    if (n == MAX_VSH || b->ninstr > VSH_MAX_INSTR) {
        shader_diagnostic("vertex program rejected: cache limit or instruction count");
        return NULL;
    }
    if (!s_vsh && !(s_vsh = (Vsh *)calloc(MAX_VSH, sizeof *s_vsh))) {
        shader_diagnostic("vertex program cache allocation failed");
        return NULL;
    }
    if (!s_vsh_thread) {
        if (scale_test_failure("vsh-event")) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            s_vsh_wake = NULL;
        } else {
            s_vsh_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
        }
        if (!s_vsh_wake) {
            shader_diagnostic("vertex worker event creation failed: Windows error %lu",
                              (unsigned long)GetLastError());
            return NULL;
        }
        s_vsh_thread = CreateThread(NULL, 0, vsh_worker, NULL, 0, NULL);
        if (!s_vsh_thread) {
            DWORD error = GetLastError();
            CloseHandle(s_vsh_wake);
            s_vsh_wake = NULL;
            shader_diagnostic("vertex worker thread creation failed: Windows error %lu",
                              (unsigned long)error);
            return NULL;
        }
    }
    v = &s_vsh[n];
    memset(v, 0, sizeof *v);
    v->hash = h;
    v->n = b->ninstr;
    v->mask = b->input_mask;
    v->nin = b->ninputs;
    v->queued_tick = s_shader_diag ? GetTickCount() : 0;
    memcpy(v->conv, conv, sizeof conv);
    memcpy(v->words, b->program, b->ninstr * sizeof v->words[0]);
    v->len = gpu_vsh_hlsl(b->program, b->ninstr, b->input_mask, conv, s_vsh_flags,
                          src, sizeof src);
    if (!v->len || !(v->src = (char *)malloc(v->len + 1))) {
        v->state = -1;
        shader_diagnostic("vertex program translation or source allocation failed key=%016llX",
                          (unsigned long long)v->hash);
    } else {
        memcpy(v->src, src, v->len + 1);
        shader_diagnostic("queued key=%016llX instructions=%u",
                          (unsigned long long)v->hash, v->n);
    }
    InterlockedExchange(&s_nvsh, n + 1);
    if (!SetEvent(s_vsh_wake)) {
        shader_diagnostic("vertex worker wake failed: Windows error %lu",
                          (unsigned long)GetLastError());
        InterlockedCompareExchange(&v->state, -1, 0);
    }
    if (s_vsh_sync) {
        /* RECOMP_GPU_VSH_SYNC=1 (tests): wait for the compile rather than
         * handing this batch to the CPU. */
        while (v->state == 0) {
            vsh_note_pending(v);
            Sleep(1);
        }
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
    s_cur->guest_exact = 0;
    if (s_cur_depth && depth_state_can_write(b->xb.state))
        depth_written(s_cur_depth);
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
static double s_q_active_divisor;
static uint64_t s_q_total;
static ID3D11Query *s_event_pool[MAX_REPORTS];
static int s_event_free;

static void query_error(const char *operation, HRESULT hr)
{
    gpu_d3d11_check_device(s_dev, hr, operation);
    fprintf(stderr, "[GPU-D3D11] %s failed 0x%08lX (device removed reason 0x%08lX);"
                    " cannot safely complete outstanding GPU work\n",
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

#define MAX_GPU_WORK_BATCHES 2
#define MAX_GPU_WORK_OPERATIONS 128
static ID3D11Query *s_work_events[MAX_GPU_WORK_BATCHES];
static unsigned s_work_head, s_work_count, s_work_operations;
static unsigned s_work_submitted, s_work_wait_count, s_work_peak;
#ifdef BRUTEFORCE_D3D11_TESTING
static unsigned s_work_test_pending, s_work_test_pending_seen;
#endif

static void d3d_poll(void);

static void gpu_work_retire(void)
{
    ID3D11Query *event = s_work_events[s_work_head];
    BOOL complete;
    HRESULT hr;

    s_st.work_waits++;
    s_work_wait_count++;
    do {
        complete = FALSE;
        if (scale_test_failure("queue-wait"))
            query_error("GetData(GPU work)", E_FAIL);
        if (scale_test_failure("queue-device"))
            gpu_d3d11_check_device(s_dev, DXGI_ERROR_DEVICE_REMOVED, "waiting for GPU work");
        hr = gpu_d3d11_get_data(s_dev, s_ctx, (ID3D11Asynchronous *)event,
                                &complete, sizeof complete, 0, "waiting for GPU work");
#ifdef BRUTEFORCE_D3D11_TESTING
        if (hr == S_OK && complete && s_work_test_pending) {
            --s_work_test_pending;
            ++s_work_test_pending_seen;
            hr = S_FALSE;
        }
#endif
        if (hr == S_FALSE || (hr == S_OK && !complete)) {
            d3d_poll();
            SwitchToThread();
        } else if (hr != S_OK) {
            query_error("GetData(GPU work)", hr);
        }
    } while (hr == S_FALSE || !complete);
    event_put(event);
    s_work_events[s_work_head] = NULL;
    s_work_head = (s_work_head + 1) % MAX_GPU_WORK_BATCHES;
    --s_work_count;
}

static void gpu_work_checkpoint(void)
{
    ID3D11Query *event;
    unsigned slot;

    if (!s_work_operations)
        return;
    while (s_work_count == MAX_GPU_WORK_BATCHES)
        gpu_work_retire();
    if (scale_test_failure("queue-query"))
        query_error("CreateQuery(GPU work)", E_OUTOFMEMORY);
    event = event_get();
    ID3D11DeviceContext_End(s_ctx, (ID3D11Asynchronous *)event);
    slot = (s_work_head + s_work_count) % MAX_GPU_WORK_BATCHES;
    s_work_events[slot] = event;
    ++s_work_count;
    ++s_work_submitted;
    ++s_st.work_batches;
    if (s_work_peak < s_work_count)
        s_work_peak = s_work_count;
    s_work_operations = 0;
}

static void gpu_work_begin(void)
{
    gpu_d3d11_check_device(s_dev, S_OK, "submitting GPU work");
    if (s_work_operations >= MAX_GPU_WORK_OPERATIONS)
        gpu_work_checkpoint();
    if (!s_work_operations && s_work_count == MAX_GPU_WORK_BATCHES)
        gpu_work_retire();
    ++s_work_operations;
}

static void gpu_work_finish(void)
{
    gpu_work_checkpoint();
    while (s_work_count)
        gpu_work_retire();
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

static void query_begin(void)
{
    s_q_active = query_get();
    s_q_active_divisor = query_divisor();
    ID3D11DeviceContext_Begin(s_ctx, (ID3D11Asynchronous *)s_q_active);
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
    s_q_pending_divisor[s_q_npending++] = s_q_active_divisor;
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
    if (counting)
        query_begin();
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
    gpu_work_begin();
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
    if (counting)
        query_begin();
    d3d_poll();
    return 1;
}

static void d3d_zpass(int op)
{
    if (!ensure_device())
        return;
    gpu_work_begin();
    if (s_zpass_async && op == NV2A_ZPASS_CLEAR) {
        /* Discarded queries may still be in flight, so never reuse them. */
        int counting = s_q_active != NULL, i;
        query_end();
        for (i = 0; i < s_q_npending; i++)
            ID3D11Query_Release(s_q_pending[i]);
        s_q_npending = 0;
        s_q_total = 0;
        s_rep_reset_next = 1;
        if (counting)
            query_begin();
        return;
    }
    if (op == NV2A_ZPASS_BEGIN) {
        query_end();
        query_begin();
    } else if (op == NV2A_ZPASS_END) {
        query_end();
    } else {                                /* CLEAR_REPORT_VALUE */
        int counting = s_q_active != NULL;
        query_end();
        query_drain();
        s_q_total = 0;
        if (counting)
            query_begin();
    }
}

static uint32_t d3d_zpass_count(void)
{
    int counting = s_q_active != NULL;
    if (!s_dev)
        return 0;
    gpu_d3d11_check_device(s_dev, S_OK, "reading visibility count");
    gpu_work_begin();
    query_end();
    query_drain();
    if (counting)
        query_begin();
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

typedef struct {
    ID3D11Texture2D *texture;
    ID3D11RenderTargetView *rtv;
    IDXGIKeyedMutex *mutex;
    HANDLE handle;
    uint32_t width, height;
} SharedPresent;

static void release_shared_candidate(SharedPresent *present)
{
    if (present->mutex) IDXGIKeyedMutex_Release(present->mutex);
    if (present->rtv) ID3D11RenderTargetView_Release(present->rtv);
    if (present->texture) ID3D11Texture2D_Release(present->texture);
    memset(present, 0, sizeof *present);
}

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

static int create_shared_present(uint32_t width, uint32_t height, SharedPresent *present)
{
    D3D11_TEXTURE2D_DESC td;
    IDXGIResource *resource = NULL;
    HRESULT hr;

    memset(present, 0, sizeof *present);
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
    hr = scale_test_failure("shared-texture") ? E_OUTOFMEMORY :
         ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &present->texture);
    if (SUCCEEDED(hr))
        hr = scale_test_failure("shared-rtv") ? E_OUTOFMEMORY :
             ID3D11Device_CreateRenderTargetView(s_dev, (ID3D11Resource *)present->texture,
                                                 NULL, &present->rtv);
    if (SUCCEEDED(hr))
        hr = ID3D11Texture2D_QueryInterface(present->texture, &IID_IDXGIResource,
                                            (void **)&resource);
    if (SUCCEEDED(hr))
        hr = scale_test_failure("shared-handle") ? E_OUTOFMEMORY :
             IDXGIResource_GetSharedHandle(resource, &present->handle);
    if (SUCCEEDED(hr))
        hr = scale_test_failure("shared-mutex") ? E_OUTOFMEMORY :
             ID3D11Texture2D_QueryInterface(present->texture, &IID_IDXGIKeyedMutex,
                                            (void **)&present->mutex);
    if (resource)
        IDXGIResource_Release(resource);
    if (FAILED(hr) || !present->handle) {
        gpu_d3d11_check_device(s_dev, hr, "creating shared presentation texture");
        fprintf(stderr, "[GPU-D3D11] shared presentation texture failed 0x%08lX;"
                        " retaining prior shared resources and CPU presentation\n",
                (unsigned long)hr);
        release_shared_candidate(present);
        return 0;
    }
    present->width = width;
    present->height = height;
    return 1;
}

static void install_shared_present(SharedPresent *present)
{
    release_shared_present();
    s_present_shared = present->texture;
    s_present_rtv = present->rtv;
    s_present_mutex = present->mutex;
    s_present_handle = present->handle;
    s_present_w = present->width;
    s_present_h = present->height;
    memset(present, 0, sizeof *present);
}

static void shared_mutex_error(HRESULT hr, const char *operation)
{
    gpu_d3d11_check_device(s_dev, hr, operation);
    fprintf(stderr, "[GPU-D3D11] %s failed 0x%08lX; shared-frame ownership"
                    " cannot be recovered, stopping\n", operation, (unsigned long)hr);
    fflush(stderr);
    abort();
}

static int reclaim_unsubmitted_present(void)
{
    HRESULT hr;
    if (!s_present_unsubmitted)
        return 1;
    hr = IDXGIKeyedMutex_AcquireSync(s_present_mutex, 1, 0);
    gpu_d3d11_check_device(s_dev, hr, "reclaiming shared frame");
    if (hr == WAIT_TIMEOUT) {
        return 0;
    }
    if (hr != S_OK)
        shared_mutex_error(hr, "reclaiming shared frame");
    hr = IDXGIKeyedMutex_ReleaseSync(s_present_mutex, 0);
    gpu_d3d11_check_device(s_dev, hr, "releasing reclaimed shared frame");
    if (hr != S_OK)
        shared_mutex_error(hr, "releasing reclaimed shared frame");
    s_present_unsubmitted = 0;
    return 1;
}

static int prepare_shared_present(uint32_t width, uint32_t height,
                                  SharedPresent *candidate, int *old_locked)
{
    HRESULT hr;
    memset(candidate, 0, sizeof *candidate);
    *old_locked = 0;
    if (s_present_failed)
        return 0;
    if (s_present_shared && s_present_w == width && s_present_h == height)
        return 1;
    if (!reclaim_unsubmitted_present())
        return 0;
    if (s_present_shared) {
        hr = IDXGIKeyedMutex_AcquireSync(s_present_mutex, 0, 0);
        gpu_d3d11_check_device(s_dev, hr, "resizing shared frame");
        if (hr == WAIT_TIMEOUT) {
            s_st.present_busy++;
            fprintf(stderr, "[GPU-D3D11] shared frame resize deferred: prior frame"
                            " is pending or busy\n");
            return 0;
        }
        if (hr != S_OK)
            shared_mutex_error(hr, "shared frame resize acquire");
        *old_locked = 1;
    }
    return create_shared_present(width, height, candidate);
}

static int release_shared_resize_lock(int *old_locked)
{
    HRESULT hr;
    if (!*old_locked)
        return 1;
    *old_locked = 0;
    hr = IDXGIKeyedMutex_ReleaseSync(s_present_mutex, 0);
    gpu_d3d11_check_device(s_dev, hr, "releasing shared frame resize lock");
    if (hr == S_OK)
        return 1;
    shared_mutex_error(hr, "shared frame resize release");
    return 0;
}

static void publish_shared_present(Surf *sf)
{
    uint32_t width = sf->pw, height = sf->ph;
    SharedPresent candidate;
    int old_locked = 0;
    HRESULT hr;

    if (!s_direct_present || s_present_failed)
        return;
    if (!reclaim_unsubmitted_present())
        return;
    if (sf->bpp != 4 || sf->pitch != sf->w * 4)
        return;
    if (gpu_scale_requested()) {
        if (gpu_scale_native() && !gpu_scale_fixed_output(NULL, NULL))
            gpu_scale_output_size(&width, &height);
        else
            gpu_present_output_size(&width, &height);
    }
    if (!gpu_present_shared_ready(width, height))
        return;
    if (!prepare_shared_present(width, height, &candidate, &old_locked)) {
        release_shared_resize_lock(&old_locked);
        return;
    }
    if (!release_shared_resize_lock(&old_locked)) {
        release_shared_candidate(&candidate);
        return;
    }
    if (candidate.texture)
        install_shared_present(&candidate);
    hr = IDXGIKeyedMutex_AcquireSync(s_present_mutex, 0, 0);
    if (scale_test_failure("shared-producer-acquire"))
        hr = E_FAIL;
    if (scale_test_failure("shared-producer-loss"))
        hr = DXGI_ERROR_DEVICE_REMOVED;
    gpu_d3d11_check_device(s_dev, hr, "acquiring producer shared frame");
    if (hr == WAIT_TIMEOUT) {
        s_st.present_busy++;
        return;
    }
    if (hr != S_OK)
        shared_mutex_error(hr, "producer shared frame acquire");
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
            hr = IDXGIKeyedMutex_ReleaseSync(s_present_mutex, 0);
            gpu_d3d11_check_device(s_dev, hr, "releasing failed shared resolve");
            if (hr != S_OK)
                shared_mutex_error(hr, "failed shared resolve release");
            s_present_failed = 1;
            return;
        }
    }
    ID3D11DeviceContext_Flush(s_ctx);
    hr = IDXGIKeyedMutex_ReleaseSync(s_present_mutex, 1);
    if (scale_test_failure("shared-producer-release"))
        hr = E_FAIL;
    gpu_d3d11_check_device(s_dev, hr, "releasing producer shared frame");
    if (hr != S_OK)
        shared_mutex_error(hr, "producer shared frame release");
    if (gpu_present_submit_shared(s_present_handle, width, height,
                                  xbox_FramebufferWindowNextFrameId()))
        s_st.present_shared++;
    else
        s_present_unsubmitted = 1;
}

typedef struct {
    ID3D11Texture2D *texture;
    ID3D11RenderTargetView *rtv;
    uint32_t width, height, bpp;
} ReadbackResolve;

static void release_readback_candidate(ReadbackResolve *resolve)
{
    if (resolve->rtv) ID3D11RenderTargetView_Release(resolve->rtv);
    if (resolve->texture) ID3D11Texture2D_Release(resolve->texture);
    memset(resolve, 0, sizeof *resolve);
}

static int create_readback_resolve(const Surf *sf, ReadbackResolve *resolve)
{
    D3D11_TEXTURE2D_DESC td;
    HRESULT hr;

    memset(resolve, 0, sizeof *resolve);
    memset(&td, 0, sizeof td);
    td.Width = sf->w;
    td.Height = sf->h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = surf_format(sf->bpp);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = scale_test_failure("readback-texture") ? E_OUTOFMEMORY :
         ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &resolve->texture);
    if (SUCCEEDED(hr))
        hr = scale_test_failure("readback-rtv") ? E_OUTOFMEMORY :
             ID3D11Device_CreateRenderTargetView(
                 s_dev, (ID3D11Resource *)resolve->texture, NULL, &resolve->rtv);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(s_dev, hr, "creating scaled readback resolve");
        fprintf(stderr, "[GPU-SCALE] readback resolve creation failed 0x%08lX;"
                        " retaining prior readback resources\n", (unsigned long)hr);
        release_readback_candidate(resolve);
        return 0;
    }
    resolve->width = sf->w;
    resolve->height = sf->h;
    resolve->bpp = sf->bpp;
    return 1;
}

static void install_readback_resolve(ReadbackResolve *resolve)
{
    if (s_readback_resolve_rtv) ID3D11RenderTargetView_Release(s_readback_resolve_rtv);
    if (s_readback_resolve) ID3D11Texture2D_Release(s_readback_resolve);
    s_readback_resolve = resolve->texture;
    s_readback_resolve_rtv = resolve->rtv;
    s_readback_resolve_w = resolve->width;
    s_readback_resolve_h = resolve->height;
    s_readback_resolve_bpp = resolve->bpp;
    memset(resolve, 0, sizeof *resolve);
}

static int create_staging_texture(const Surf *sf, ID3D11Texture2D **texture)
{
    D3D11_TEXTURE2D_DESC td;
    HRESULT hr;
    *texture = NULL;
    memset(&td, 0, sizeof td);
    td.Width = sf->w; td.Height = sf->h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = surf_format(sf->bpp);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = scale_test_failure("staging-texture") ? E_OUTOFMEMORY :
         ID3D11Device_CreateTexture2D(s_dev, &td, NULL, texture);
    if (FAILED(hr)) {
        gpu_d3d11_check_device(s_dev, hr, "creating readback texture");
        fprintf(stderr, "[GPU-D3D11] readback texture creation failed 0x%08lX;"
                        " retaining prior readback resources\n", (unsigned long)hr);
        return 0;
    }
    return 1;
}

static void install_staging_texture(ID3D11Texture2D *texture, const Surf *sf)
{
    if (s_staging) ID3D11Texture2D_Release(s_staging);
    s_staging = texture;
    s_staging_w = sf->w;
    s_staging_h = sf->h;
    s_staging_bpp = sf->bpp;
}

static ID3D11Texture2D *readback_source(Surf *sf)
{
    if (sf->pw == sf->w && sf->ph == sf->h)
        return sf->tex;
    if (!s_readback_resolve || s_readback_resolve_w != sf->w ||
        s_readback_resolve_h != sf->h || s_readback_resolve_bpp != sf->bpp) {
        ReadbackResolve candidate;
        if (!create_readback_resolve(sf, &candidate))
            return NULL;
        install_readback_resolve(&candidate);
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

static int apply_native_output(uint32_t width, uint32_t height, Surf *presented,
                               int require_shared)
{
    GpuScaleUpdate update;
    Surf colors[MAX_SURF];
    Depth depths[MAX_SURF];
    int replace_color[MAX_SURF] = {0}, replace_depth[MAX_SURF] = {0};
    SharedPresent shared = {0};
    ReadbackResolve resolve = {0};
    ID3D11Texture2D *staging = NULL;
    Surf *readback = presented;
    int old_locked = 0, counting, recreated_depths = 0, i;

    if (!gpu_scale_prepare_output(width, height, &update))
        return 0;
    if (!update.changed)
        return 1;
    if (!s_dev) {
        fprintf(stderr, "[GPU-SCALE] output change requires an initialized D3D11 device;"
                        " retaining prior configuration\n");
        return 0;
    }
    memset(colors, 0, sizeof colors);
    memset(depths, 0, sizeof depths);
    counting = query_suspend_clear();
    if (require_shared &&
        !prepare_shared_present(width, height, &shared, &old_locked))
        goto failed;
    if (update.scale_changed) {
        for (i = 0; i < s_nsurf; ++i) {
            Surf *old = &s_surf[i];
            uint32_t pw, ph;
            if (!gpu_scale_surface_size_for(update.scale, old->w, old->h,
                                            old->aa_sx, old->aa_sy, &pw, &ph))
                goto failed;
            if (old->pw == pw && old->ph == ph)
                continue;
            colors[i] = *old;
            colors[i].pw = pw;
            colors[i].ph = ph;
            colors[i].scale_epoch = update.epoch;
            colors[i].tex = NULL;
            colors[i].rtv = NULL;
            colors[i].srv = NULL;
            if (!create_surf(&colors[i], old->srv, old->pw, old->ph))
                goto failed;
            replace_color[i] = 1;
            if (presented == old)
                readback = &colors[i];
        }
        for (i = 0; i < s_ndepth; ++i) {
            Depth *old = &s_depth[i], *next = &depths[i];
            uint32_t w, h;
            if (!depth_size_for(old, update.scale, &w, &h))
                goto failed;
            if (old->w == w && old->h == h)
                continue;
            next->va = old->va;
            next->w = w;
            next->h = h;
            next->pitch = old->pitch;
            next->format = old->format;
            next->surface_type = old->surface_type;
            next->recreated = 1;
            next->scale_epoch = update.epoch;
            memcpy(next->logical_w, old->logical_w, sizeof next->logical_w);
            memcpy(next->logical_h, old->logical_h, sizeof next->logical_h);
            if (!create_depth_resource(next, 1))
                goto failed;
            replace_depth[i] = 1;
        }
    }
    if (readback) {
        if ((readback->pw != readback->w || readback->ph != readback->h) &&
            (!s_readback_resolve || s_readback_resolve_w != readback->w ||
             s_readback_resolve_h != readback->h || s_readback_resolve_bpp != readback->bpp) &&
            !create_readback_resolve(readback, &resolve))
            goto failed;
        if ((!s_staging || s_staging_w != readback->w || s_staging_h != readback->h ||
             s_staging_bpp != readback->bpp) && !create_staging_texture(readback, &staging))
            goto failed;
    }
    if (!release_shared_resize_lock(&old_locked) || !gpu_scale_commit_output(&update))
        goto failed;
    for (i = 0; i < s_nsurf; ++i) {
        if (replace_color[i]) {
            release_surf(&s_surf[i]);
            s_surf[i] = colors[i];
        }
        s_surf[i].scale_epoch = update.epoch;
    }
    for (i = 0; i < s_ndepth; ++i) {
        if (replace_depth[i]) {
            release_depth(&s_depth[i]);
            s_depth[i] = depths[i];
            ++recreated_depths;
        }
        s_depth[i].scale_epoch = update.epoch;
    }
    if (shared.texture)
        install_shared_present(&shared);
    if (resolve.texture)
        install_readback_resolve(&resolve);
    if (staging)
        install_staging_texture(staging, presented);
    if (recreated_depths)
        fprintf(stderr, "[GPU-SCALE] %d resized depth/stencil surface(s) require guest"
                        " reinitialization; depth/stencil migration is not implemented\n",
                recreated_depths);
    if (s_cur)
        bind_targets(s_cur, s_cur_depth);
    else
        ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);
    query_resume_clear(counting);
    return 1;

failed:
    for (i = 0; i < s_nsurf; ++i)
        if (replace_color[i])
            release_surf(&colors[i]);
    for (i = 0; i < s_ndepth; ++i)
        if (replace_depth[i])
            release_depth(&depths[i]);
    if (staging) ID3D11Texture2D_Release(staging);
    release_readback_candidate(&resolve);
    release_shared_candidate(&shared);
    release_shared_resize_lock(&old_locked);
    if (s_cur)
        bind_targets(s_cur, s_cur_depth);
    else
        ID3D11DeviceContext_OMSetRenderTargets(s_ctx, 0, NULL, NULL);
    query_resume_clear(counting);
    fprintf(stderr, "[GPU-SCALE] output picture %ux%u not committed;"
                    " retaining scale %.3f, epoch %u and prior resources\n",
            width, height, gpu_scale_value(), gpu_scale_epoch());
    return 0;
}

static void update_native_output(Surf *presented)
{
    uint32_t width, height;
    if (gpu_scale_native() && !gpu_scale_fixed_output(NULL, NULL) &&
        gpu_present_output_size(&width, &height))
        apply_native_output(width, height, presented,
                            s_direct_present && gpu_present_shared_ready(width, height));
}

#ifdef BRUTEFORCE_D3D11_TESTING
int bruteforce_d3d11_test_native_output(uint32_t width, uint32_t height,
                                        uint32_t color_va, int require_shared)
{
    Surf *presented = color_va ? find_surf(color_va) : NULL;
    if (color_va && !presented)
        return 0;
    return apply_native_output(width, height, presented, require_shared);
}
#endif

static int d3d_readback(uint32_t color_va)
{
    Surf *sf = find_surf(color_va);
    ID3D11Texture2D *source;
    D3D11_MAPPED_SUBRESOURCE m;
    GuestSpanIdentity identity;
    uint32_t y, row;
    double t0 = qpc_ms();
    HRESULT hr;
    int ok = 0, counting, exact, matches = 1;

    if (s_dev)
        gpu_d3d11_check_device(s_dev, S_OK, "starting surface readback");
    if (!sf || !s_dev)
        return 0;
    gpu_work_begin();
    s_last_presented = sf;
    update_native_output(sf);
    counting = query_suspend_clear();
    if (!sync_guest_surface(sf, 1))
        goto done;
    publish_shared_present(sf);
    if (s_skip_clean_readback && !gpu_scale_requested() && sf->guest_exact && !sf->gpu_dirty &&
        !getenv("RECOMP_FRAME_STATS") && !getenv("RECOMP_FB_DUMP") &&
        !getenv("RECOMP_FB_DUMP_FLIPS") && !getenv("RECOMP_PRESENTER_CAPTURE")) {
        gpu_work_finish();
#ifdef BRUTEFORCE_D3D11_TESTING
        if (scale_test_failure("guest-change-wait"))
            *(uint32_t *)GUEST(sf->va) ^= 1u;
#endif
        if (!guest_surface_changed(sf)) {
            s_st.readback_coherent++;
            s_st.readback_ms += qpc_ms() - t0;
            ok = 1;
            goto done;
        }
        sf->guest_exact = 0;
    }
    source = readback_source(sf);
    if (!source)
        goto done;
    if (!s_staging || s_staging_w != sf->w || s_staging_h != sf->h || s_staging_bpp != sf->bpp) {
        ID3D11Texture2D *candidate;
        if (!create_staging_texture(sf, &candidate))
            goto done;
        install_staging_texture(candidate, sf);
    }
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)s_staging,
                                     (ID3D11Resource *)source);
    hr = ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_staging, 0,
                                D3D11_MAP_READ, 0, &m);
    if (scale_test_failure("readback-loss"))
        hr = DXGI_ERROR_DEVICE_REMOVED;
    gpu_d3d11_check_device(s_dev, hr, "mapping surface readback");
    if (FAILED(hr)) {
        fprintf(stderr, "[GPU-D3D11] readback map failed 0x%08lX\n", (unsigned long)hr);
        goto done;
    }
    row = sf->w * sf->bpp;
    if (row > sf->pitch)
        row = sf->pitch;
    if (!checked_write_guest_surface(sf, m.pData, m.RowPitch)) {
        ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_staging, 0);
        sf->guest_check_gen = 0;
        sf->gpu_dirty = 1;
        sf->guest_exact = 0;
        sf->alias_uncertain = 1;
        goto done;
    }
#ifdef BRUTEFORCE_D3D11_TESTING
    if (scale_test_failure("guest-change-readback"))
        *(uint32_t *)GUEST(sf->va) ^= 1u;
    if (scale_test_failure("guest-guard-readback")) {
        DWORD old_protection;
        VirtualProtect(GUEST(sf->va), 1, PAGE_READWRITE | PAGE_GUARD, &old_protection);
    }
#endif
    if (!copy_guest_surface(sf, &identity) || !same_guest_identity(&sf->identity, &identity)) {
        ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_staging, 0);
        sf->guest_check_gen = 0;
        sf->gpu_dirty = 1;
        sf->guest_exact = 0;
        sf->alias_uncertain = 1;
        goto done;
    }
    for (y = 0; y < sf->h; y++) {
        const uint8_t *gpu_row = (const uint8_t *)m.pData + (size_t)y * m.RowPitch;
        if (memcmp(s_surface_probe + (size_t)y * sf->pitch, gpu_row, row))
            matches = 0;
        memcpy(sf->guest_shadow + (size_t)y * row, gpu_row, row);
    }
    exact = matches && s_skip_clean_readback && sf->bpp == 4 && sf->pitch >= sf->w * 4 &&
            sf->pw == sf->w && sf->ph == sf->h;
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_staging, 0);
    sf->guest_check_gen = matches ? s_gen : 0;
    sf->gpu_dirty = !matches;
    sf->guest_exact = exact;
    sf->alias_uncertain = !matches;
    s_st.readbacks++;
    s_st.readback_ms += qpc_ms() - t0;
    gpu_d3d11_check_device(s_dev, S_OK, "completing surface readback");
    if (!matches) {
        static unsigned rejected;
        if (rejected < 16) {
            ++rejected;
            fprintf(stderr, "[GPU-D3D11] guest color surface %08X changed during readback%s\n",
                    sf->va, rejected == 16 ? " (further reports suppressed)" : "");
        }
        goto done;
    }
    ok = 1;
done:
    if (s_cur)
        bind_targets(s_cur, s_cur_depth);
    query_resume_clear(counting);
    return ok;
}

static int d3d_flip_readback(uint32_t color_va)
{
    int tracked = find_surf(color_va) != NULL;
    int ok = d3d_readback(color_va);
    if (tracked && !ok) {
        fprintf(stderr, "[GPU-D3D11] cannot make tracked color surface %08X guest-visible;"
                        " stopping before presentation, dumps or frame statistics\n", color_va);
        fflush(stderr);
        abort();
    }
    return ok;
}

#ifdef BRUTEFORCE_D3D11_TESTING
int bruteforce_d3d11_test_snapshot(uint32_t color_va, uint32_t depth_va,
                                   BruteforceD3D11TestSnapshot *snapshot)
{
    Surf *color = find_surf(color_va);
    Depth *depth = NULL;
    int i;
    if (!snapshot || !color)
        return 0;
    for (i = 0; i < s_ndepth; ++i)
        if (s_depth[i].va == depth_va) { depth = &s_depth[i]; break; }
    if (depth_va && !depth)
        return 0;
    memset(snapshot, 0, sizeof *snapshot);
    snapshot->color = (uintptr_t)color->tex;
    snapshot->color_rtv = (uintptr_t)color->rtv;
    snapshot->color_srv = (uintptr_t)color->srv;
    snapshot->color_width = color->pw;
    snapshot->color_height = color->ph;
    if (depth) {
        snapshot->depth = (uintptr_t)depth->tex;
        snapshot->depth_dsv = (uintptr_t)depth->dsv;
        snapshot->depth_width = depth->w;
        snapshot->depth_height = depth->h;
    }
    snapshot->shared = (uintptr_t)s_present_shared;
    snapshot->shared_rtv = (uintptr_t)s_present_rtv;
    snapshot->shared_mutex = (uintptr_t)s_present_mutex;
    snapshot->shared_width = s_present_w;
    snapshot->shared_height = s_present_h;
    snapshot->resolve = (uintptr_t)s_readback_resolve;
    snapshot->resolve_rtv = (uintptr_t)s_readback_resolve_rtv;
    snapshot->resolve_width = s_readback_resolve_w;
    snapshot->resolve_height = s_readback_resolve_h;
    snapshot->staging = (uintptr_t)s_staging;
    snapshot->bound_color = (uintptr_t)(s_cur ? s_cur->tex : NULL);
    snapshot->bound_depth = (uintptr_t)(s_cur_depth ? s_cur_depth->tex : NULL);
    snapshot->scale = gpu_scale_value();
    snapshot->epoch = gpu_scale_epoch();
    return 1;
}

int bruteforce_d3d11_test_shared_busy(int busy)
{
    HRESULT hr;
    if (!s_present_mutex)
        return 0;
    hr = IDXGIKeyedMutex_AcquireSync(s_present_mutex, busy ? 0 : 1, 0);
    if (hr == S_OK)
        hr = IDXGIKeyedMutex_ReleaseSync(s_present_mutex, busy ? 1 : 0);
    return hr == S_OK;
}

int bruteforce_d3d11_test_readback(uint32_t color_va)
{
    return d3d_readback(color_va);
}

int bruteforce_d3d11_test_pipeline(uint32_t color_va, uint32_t depth_va,
                                    BruteforceD3D11PipelineStats *stats)
{
    Surf *color = color_va ? find_surf(color_va) : NULL;
    Depth *depth = NULL;
    int i;
    if (!stats || (color_va && !color))
        return 0;
    for (i = 0; i < s_ndepth; ++i)
        if (s_depth[i].va == depth_va) { depth = &s_depth[i]; break; }
    if (depth_va && !depth)
        return 0;
    memset(stats, 0, sizeof *stats);
    stats->readbacks = s_st.readbacks;
    stats->coherent = s_st.readback_coherent;
    stats->work_submitted = s_work_submitted;
    stats->work_waits = s_work_wait_count;
    stats->work_queued = s_work_count;
    stats->work_peak = s_work_peak;
    stats->work_operations = s_work_operations;
    stats->injected_pending = s_work_test_pending_seen;
    if (color) {
        stats->gpu_dirty = color->gpu_dirty;
        stats->guest_exact = color->guest_exact;
    }
    if (depth) {
        stats->depth_revision = depth->revision;
        stats->packed_revision = depth->packed_revision;
        stats->packed_valid = depth->packed_valid;
        stats->depth_gpu_dirty = depth->gpu_dirty;
        stats->depth_conflict = depth->guest_conflict;
        stats->depth_initialized = depth->initialized;
        stats->stencil_conversions = s_st.stencil_conversions;
    }
    return 1;
}

void bruteforce_d3d11_test_pending_work(unsigned polls)
{
    s_work_test_pending = polls;
}

void bruteforce_d3d11_test_finish_work(void)
{
    gpu_work_finish();
}
#endif

static void d3d_flip(void)
{
    if (s_dev) {
        gpu_d3d11_check_device(s_dev, S_OK, "completing flip");
        gpu_work_begin();
    }
    update_native_output(s_last_presented ? s_last_presented : s_cur);
    if (s_dev)
        gpu_work_checkpoint();
    s_gen++;
    stats_flip();
}

#ifdef BRUTEFORCE_D3D11_TESTING
void bruteforce_d3d11_test_flip(void) { d3d_flip(); }
ID3D11Device *bruteforce_d3d11_test_device(void) { return s_dev; }
ID3D11DeviceContext *bruteforce_d3d11_test_context(void) { return s_ctx; }
ID3D11ShaderResourceView *bruteforce_d3d11_test_texture(const Nv2aTexture *texture,
                                                         int cube, unsigned stage,
                                                         unsigned *levels)
{
    float width, height;
    return get_texture(texture, cube, stage, &width, &height, levels);
}
ID3D11SamplerState *bruteforce_d3d11_test_sampler(uint32_t addr_u, uint32_t addr_v,
                                                   uint32_t filter, uint32_t control0,
                                                   unsigned levels)
{
    return get_sampler(addr_u, addr_v, filter, control0, levels);
}
#endif

static const Nv2aBackend s_backend = { d3d_clear, d3d_draw, d3d_flip };
static const Nv2aBackendExt s_backend_ext = { d3d_xdraw, d3d_xdraw_vsh, d3d_vsh_readback,
                                              d3d_vsh_raw_ok, d3d_flip_readback, d3d_zpass,
                                              d3d_zpass_count, d3d_zpass_report, d3d_poll,
                                              NV2A_BACKEND_CAP_WINDOW_CLIP };

int bruteforce_d3d11_install(void)
{
    static int installed;
    const char *warp = getenv("RECOMP_D3D11_WARP");
    const char *mulzero = getenv("RECOMP_GPU_VSH_MULZERO");
    const char *tex_gpu = getenv("RECOMP_GPU_TEX_DECODE");
    const char *tex_log = getenv("RECOMP_GPU_TEXLOG");
    const char *tex_approx = getenv("RECOMP_GPU_TEX_CACHE_APPROX");
    const char *tex_bounds = getenv("RECOMP_GPU_TEXTURE_BOUNDS");
    const char *tex_mips = getenv("RECOMP_GPU_MIPS");
    const char *tex_aniso = getenv("RECOMP_GPU_ANISO");
    const char *shader_diag = getenv("RECOMP_GPU_SHADER_DIAG");
    const char *depth_texture = getenv("RECOMP_GPU_DEPTH_TEXTURE");
    const char *stencil_alias = getenv("RECOMP_GPU_STENCIL_ALIAS");
    const char *direct_present = getenv("RECOMP_GPU_DIRECT_PRESENT");
    const char *skip_clean_readback = getenv("RECOMP_GPU_SKIP_CLEAN_READBACK");
    GpuScaleConfig scale;
    Tex *tex;

    if (installed)
        return 1;

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
    if (skip_clean_readback && *skip_clean_readback &&
        strcmp(skip_clean_readback, "0") != 0 && strcmp(skip_clean_readback, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_SKIP_CLEAN_READBACK must be 0 or 1\n");
        return 0;
    }
    if (tex_approx && *tex_approx && strcmp(tex_approx, "0") != 0 &&
        strcmp(tex_approx, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_TEX_CACHE_APPROX must be 0 or 1\n");
        return 0;
    }
    if (tex_bounds && *tex_bounds && strcmp(tex_bounds, "0") != 0 &&
        strcmp(tex_bounds, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_TEXTURE_BOUNDS must be 0 or 1\n");
        return 0;
    }
    if (tex_mips && *tex_mips && strcmp(tex_mips, "0") != 0 &&
        strcmp(tex_mips, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_MIPS must be 0 or 1\n");
        return 0;
    }
    if (tex_aniso && *tex_aniso && strcmp(tex_aniso, "0") != 0 &&
        strcmp(tex_aniso, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_ANISO must be 0 or 1\n");
        return 0;
    }
    if (shader_diag && *shader_diag && strcmp(shader_diag, "0") != 0 &&
        strcmp(shader_diag, "1") != 0) {
        fprintf(stderr, "[GPU-D3D11] RECOMP_GPU_SHADER_DIAG must be 0 or 1\n");
        return 0;
    }
    if (!gpu_scale_prepare_config(&scale))
        return 0;
    if (scale.requested && direct_present && strcmp(direct_present, "0") == 0) {
        fprintf(stderr, "[GPU-SCALE] scaled rendering requires shared GPU presentation;"
                        " RECOMP_GPU_DIRECT_PRESENT cannot be 0\n");
        return 0;
    }
    if (scale.requested &&
        ((depth_texture && strcmp(depth_texture, "1") == 0) ||
         (stencil_alias && strcmp(stencil_alias, "1") == 0))) {
        fprintf(stderr, "[GPU-SCALE] scaled depth/stencil texture aliases are not supported;"
                        " disable RECOMP_GPU_DEPTH_TEXTURE and RECOMP_GPU_STENCIL_ALIAS\n");
        return 0;
    }
    if (!gpu_pscache_init())
        return 0;
    tex = (Tex *)calloc(MAX_TEX, sizeof *tex);
    if (!tex) {
        fprintf(stderr, "[GPU-D3D11] cannot allocate the D3D11 texture cache;"
                        " configuration not committed\n");
        return 0;
    }
    if (!scale.requested && !gpu_scale_commit_config(&scale)) {
        fprintf(stderr, "[GPU-SCALE] cannot commit D3D11 configuration\n");
        free(tex);
        return 0;
    }
    if (scale.requested) {
        s_pending_scale = scale;
        s_scale_pending = 1;
    }
    s_warp = warp && strcmp(warp, "1") == 0;
    s_tex = tex;
    s_depth_texture = depth_texture && strcmp(depth_texture, "1") == 0;
    s_stencil_alias = stencil_alias && strcmp(stencil_alias, "1") == 0;
    s_skip_clean_readback = skip_clean_readback && strcmp(skip_clean_readback, "1") == 0;
    s_direct_present = (direct_present && strcmp(direct_present, "1") == 0) ||
                       (scale.requested && (!direct_present || !*direct_present));
    s_tex_gpu = !tex_gpu || strcmp(tex_gpu, "0") != 0;
    s_tex_log = tex_log && strcmp(tex_log, "1") == 0;
    s_tex_approx = tex_approx && strcmp(tex_approx, "1") == 0;
    s_tex_strict_bounds = tex_bounds && strcmp(tex_bounds, "1") == 0;
    s_tex_mips = tex_mips && strcmp(tex_mips, "1") == 0;
    s_tex_aniso = tex_aniso && strcmp(tex_aniso, "1") == 0;
    s_shader_diag = shader_diag && strcmp(shader_diag, "1") == 0;
    if (s_tex_approx)
        fprintf(stderr, "[GPU-D3D11] experimental approximate texture cache enabled;"
                        " texture rewrites can be missed\n");
    if (s_tex_strict_bounds)
        fprintf(stderr, "[GPU-D3D11] texture spans require registered logical bounds\n");
    if (s_tex_mips)
        fprintf(stderr, "[GPU-MIPS] experimental guest mip chains enabled;"
                        " CPU reference remains level 0\n");
    if (s_tex_aniso)
        fprintf(stderr, "[GPU-MIPS] experimental 8x anisotropic filtering %s\n",
                s_tex_mips ? "enabled for eligible mipped samplers"
                           : "inactive without RECOMP_GPU_MIPS=1");
    s_vsh_flags = mulzero && strcmp(mulzero, "1") == 0 ? GPU_VSH_MULZERO : 0;
    s_vsh_sync = getenv("RECOMP_GPU_VSH_SYNC") && strcmp(getenv("RECOMP_GPU_VSH_SYNC"), "1") == 0;
    s_zpass_async = getenv("RECOMP_GPU_ZPASS") && strcmp(getenv("RECOMP_GPU_ZPASS"), "async") == 0;
    nv2a_backend_ext_register(&s_backend_ext);
    nv2a_backend_register(&s_backend);
    installed = 1;
    fprintf(stderr, "[GPU] Direct3D 11 back end registered (RECOMP_GPU=d3d11%s)\n",
            s_warp ? ", WARP" : "");
    if (s_depth_texture)
        fprintf(stderr, "[GPU-D3D11] experimental D24 depth texture aliases enabled\n");
    if (s_stencil_alias)
        fprintf(stderr, "[GPU-D3D11] experimental packed D24S8 texture aliases enabled\n");
    if (s_direct_present)
        fprintf(stderr, "[GPU-D3D11] shared GPU presentation enabled;"
                        " guest visibility retained\n");
    if (s_skip_clean_readback)
        fprintf(stderr, "[GPU-D3D11] exact guest-coherent 32-bit readback elision enabled;"
                        " dirty, scaled and diagnostic frames retain readback\n");
    return 1;
}
