#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "xbox_memory_layout.h"
#include "gpu/vulkan_backend.h"

typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern void nv2a_pb_exec_poll(void);

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        return 0; \
    } \
} while (0)

static int initialize_memory(const char *path)
{
    FILE *file = fopen(path, "rb");
    unsigned char *image;
    long length;
    int result;
    CHECK(file != NULL, "open synthetic XBE");
    CHECK(fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > 0 &&
          fseek(file, 0, SEEK_SET) == 0, "size synthetic XBE");
    image = malloc((size_t)length);
    CHECK(image != NULL, "allocate synthetic XBE");
    CHECK(fread(image, 1, (size_t)length, file) == (size_t)length,
          "read synthetic XBE");
    CHECK(fclose(file) == 0, "close synthetic XBE");
    result = xbox_MemoryLayoutInit(image, (size_t)length);
    free(image);
    CHECK(result, "initialize guest memory");
    return 1;
}

static Nv2aRenderState default_state(void)
{
    Nv2aRenderState state;
    memset(&state, 0, sizeof state);
    state.blend_src = 1;
    state.blend_dst = 0;
    state.blend_eq = 0x8006;
    state.alpha_func = 0x207;
    state.depth_func = 0x207;
    state.color_mask = 0x01010101;
    state.cull_face = 0x405;
    state.front_face = 0x900;
    state.stencil_func = 0x207;
    state.stencil_func_mask = 0xFF;
    state.stencil_write_mask = 0xFF;
    state.stencil_fail = 0x1E00;
    state.stencil_zfail = 0x1E00;
    state.stencil_zpass = 0x1E00;
    return state;
}

static int run_render_test(void)
{
    const uint32_t color_va = XBOX_CONTIG_BASE + 0x20000;
    const uint32_t color16_va = XBOX_CONTIG_BASE + 0x21000;
    const uint32_t depth_va = XBOX_CONTIG_BASE + 0x22000;
    const uint32_t texture_va = XBOX_CONTIG_BASE + 0x23000;
    const uint32_t texture1_va = XBOX_CONTIG_BASE + 0x24000;
    const uint32_t alias_target_va = XBOX_CONTIG_BASE + 0x25000;
    const uint32_t cube_va = XBOX_CONTIG_BASE + 0x26000;
    const uint32_t mip_va = XBOX_CONTIG_BASE + 0x27000;
    Nv2aSurface surface;
    Nv2aSurface surface16;
    Nv2aSurface alias_target;
    Nv2aRenderState state = default_state();
    Nv2aVertex vertices[3];
    Nv2aVertex quad[6];
    Nv2aTexture texture;
    Nv2aTexture mip_texture;
    Nv2aTexture cube_mip_texture;
    Nv2aTexture alias_texture;
    Nv2aBatch batch;
    Nv2aXVertex xquad[6];
    Nv2aXBatch xbatch;
    Nv2aXVsBatch vsh_batch;
    Nv2aXVsOut vsh_output[3];
    Nv2aCombiner combiner;
    uint32_t vsh_program[2][4] = {
        { 0, (1u << 21) | 0x1Bu | (3u << 9), 2u << 26,
          (0xFu << 12) | (1u << 11) | (3u << 3) },
        { 0, (1u << 21) | 0x1Bu, 2u << 26,
          (0xFu << 12) | (1u << 11) | 1u },
    };
    float vsh_constants[192][4];
    float vsh_inputs[3 * 2 * 4];
    uint32_t vsh_indices[3] = { 0, 1, 2 };
    union { uint32_t bits; float value; } nonfinite = { 0x7FC00000u };
    uint32_t *pixels = (uint32_t *)((uintptr_t)color_va + xbox_GetMemoryOffset());
    uint16_t *pixels16 = (uint16_t *)((uintptr_t)color16_va + xbox_GetMemoryOffset());
    uint32_t *texture_pixel = (uint32_t *)((uintptr_t)texture_va + xbox_GetMemoryOffset());
    uint32_t *texture1_pixel =
        (uint32_t *)((uintptr_t)texture1_va + xbox_GetMemoryOffset());
    uint8_t *cube_bytes =
        (uint8_t *)((uintptr_t)cube_va + xbox_GetMemoryOffset());
    uint32_t *mip_pixels =
        (uint32_t *)((uintptr_t)mip_va + xbox_GetMemoryOffset());
    uint32_t *alias_pixels =
        (uint32_t *)((uintptr_t)alias_target_va + xbox_GetMemoryOffset());
    uint8_t *texture_index = (uint8_t *)texture_pixel;
    uint32_t *palette =
        (uint32_t *)((uintptr_t)(texture_va + 0x1000) + xbox_GetMemoryOffset());
    uint64_t texture_hits;
    uint64_t texture_uploads;
    uint64_t texture_rewrites;
    uint64_t draws_before;
    uint64_t draws_after;
    uint64_t submissions_before;
    uint64_t submissions_after;
    uint64_t max_batch_operations;
    unsigned i;

    memset(&surface, 0, sizeof surface);
    surface.color_va = color_va;
    surface.width = 4;
    surface.height = 4;
    surface.pitch = 16;
    surface.bytes_per_pixel = 4;
    surface.aa_sx = surface.aa_sy = 1;
    memset(pixels, 0, 64);
    bruteforce_vulkan_test_submission_stats(
        NULL, &submissions_before, NULL);
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF336699, 0);
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back clear");
    bruteforce_vulkan_test_submission_stats(
        NULL, &submissions_after, NULL);
    CHECK(submissions_after - submissions_before == 1,
          "mirrored full-clear readback still waits for submitted GPU work");
    for (i = 0; i < 16; ++i)
        CHECK(pixels[i] == 0xFF336699, "full color clear matches");

    for (i = 0; i < 16; ++i)
        pixels[i] = 0xFF112233;
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "preserve a CPU-written tracked color surface");
    for (i = 0; i < 16; ++i)
        CHECK(pixels[i] == 0xFF112233,
              "CPU-written tracked color surface remains guest-visible");

    bruteforce_vulkan_test_clear(&surface, &state, 0x40, 0xFF0000CC, 0);
    for (i = 0; i < 16; ++i)
        pixels[i] = 0xFF445566;
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back partial clear");
    for (i = 0; i < 16; ++i)
        CHECK(pixels[i] == 0xFF1122CC,
              "GPU frame wins a conflicting guest surface write");
    memset(&alias_texture, 0, sizeof alias_texture);
    alias_texture.offset = color_va;
    alias_texture.width = alias_texture.height = 4;
    alias_texture.pitch = 16;
    alias_texture.color = 0x12;
    alias_texture.addr_u = alias_texture.addr_v = 3;
    alias_texture.levels = 2;
    alias_texture.filter = (3u << 16) | (1u << 24);
    CHECK(bruteforce_vulkan_test_texture_aliases_surface(&alias_texture),
          "tracked render-target texture alias is detected");

    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    memset(vertices, 0, sizeof vertices);
    vertices[0].x = 0.0f; vertices[0].y = 0.0f;
    vertices[1].x = 4.0f; vertices[1].y = 0.0f;
    vertices[2].x = 0.0f; vertices[2].y = 4.0f;
    for (i = 0; i < 3; ++i) {
        vertices[i].rhw = 1.0f;
        vertices[i].diffuse = 0xFFFF0000;
    }
    memset(&batch, 0, sizeof batch);
    batch.vertices = vertices;
    batch.count = 3;
    batch.state = &state;

    memset(&alias_target, 0, sizeof alias_target);
    alias_target.color_va = alias_target_va;
    alias_target.width = 4;
    alias_target.height = 4;
    alias_target.pitch = 16;
    alias_target.bytes_per_pixel = 4;
    alias_target.aa_sx = alias_target.aa_sy = 1;
    memset(alias_pixels, 0, 64);
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF00FF00, 0);
    batch.texture = &alias_texture;
    for (i = 0; i < 3; ++i)
        vertices[i].diffuse = 0xFFFFFFFF;
    bruteforce_vulkan_test_draw(&alias_target, &batch);
    CHECK(bruteforce_vulkan_test_readback(alias_target_va),
          "read back render-to-texture alias");
    CHECK(alias_pixels[0] == 0xFF00FF00,
          "tracked color surface samples level zero without guest readback");
    alias_texture.offset = color_va + surface.pitch;
    alias_texture.width = alias_texture.height = 2;
    CHECK(bruteforce_vulkan_test_texture_aliases_surface(&alias_texture),
          "tracked render-target subregion alias is detected");
    bruteforce_vulkan_test_clear(&alias_target, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&alias_target, &batch);
    CHECK(bruteforce_vulkan_test_readback(alias_target_va),
          "read back render-to-texture subregion");
    CHECK(alias_pixels[0] == 0xFF00FF00,
          "tracked color subregion samples through an ordered copy");
    alias_texture.offset = color_va;
    alias_texture.width = alias_texture.height = 4;
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back self-sampled render target");
    CHECK(pixels[0] == 0xFF00FF00,
          "bound render target samples through an ordered copy");

    batch.texture = NULL;
    for (i = 0; i < 3; ++i)
        vertices[i].diffuse = 0xFFFF0000;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back triangle");
    CHECK(pixels[0] == 0xFFFF0000, "triangle reaches the top-left pixel");
    CHECK(pixels[15] == 0xFF000000, "triangle preserves the opposite corner");

    memset(&vsh_batch, 0, sizeof vsh_batch);
    memset(vsh_constants, 0, sizeof vsh_constants);
    memset(vsh_inputs, 0, sizeof vsh_inputs);
    for (i = 0; i < 3; ++i) {
        static const float positions[3][4] = {
            { 0.0f, 0.0f, 0.0f, 1.0f },
            { 4.0f, 0.0f, 0.0f, 1.0f },
            { 0.0f, 4.0f, 0.0f, 1.0f },
        };
        memcpy(&vsh_inputs[i * 8], positions[i], sizeof positions[i]);
        vsh_inputs[i * 8 + 4] = 1.0f;
        vsh_inputs[i * 8 + 6] = 1.0f;
        vsh_inputs[i * 8 + 7] = 1.0f;
    }
    vsh_batch.xb.state = &state;
    vsh_batch.program = vsh_program;
    vsh_batch.ninstr = 2;
    vsh_batch.consts = vsh_constants;
    vsh_batch.inputs = vsh_inputs;
    vsh_batch.input_mask = (1u << 0) | (1u << 3);
    vsh_batch.ninputs = 2;
    vsh_batch.nverts = 3;
    vsh_batch.indices = vsh_indices;
    vsh_batch.nindices = 3;
    vsh_batch.aa[0] = vsh_batch.aa[1] = 1.0f;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    CHECK(bruteforce_vulkan_test_draw_vsh(&surface, &vsh_batch),
          "accept gathered-input vertex program");
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back vertex-program triangle");
    CHECK(pixels[0] == 0xFFFF00FF,
          "vertex program transforms position and diffuse color");
    CHECK(pixels[15] == 0xFF000000,
          "vertex-program triangle preserves the opposite corner");
    CHECK(bruteforce_vulkan_test_vsh_readback(vsh_output, 3),
          "read back vertex-program outputs");
    for (i = 0; i < 3; ++i) {
        CHECK(vsh_output[i].pos[0] == vsh_inputs[i * 8] &&
              vsh_output[i].pos[1] == vsh_inputs[i * 8 + 1] &&
              vsh_output[i].pos[2] == 0.0f &&
              vsh_output[i].pos[3] == 1.0f,
              "vertex-program readback preserves surface position");
        CHECK(vsh_output[i].d0[0] == 1.0f &&
              vsh_output[i].d0[1] == 0.0f &&
              vsh_output[i].d0[2] == 1.0f &&
              vsh_output[i].d0[3] == 1.0f,
              "vertex-program readback preserves diffuse color");
    }

    state.cull_enable = 1;
    state.cull_face = 0x408;
    vsh_batch.xb.cull = 1;
    CHECK(bruteforce_vulkan_test_draw_vsh(&surface, &vsh_batch),
          "accept fully culled vertex-program batch");
    CHECK(!bruteforce_vulkan_test_vsh_readback(vsh_output, 3),
          "fully culled batch has no stale verification output");
    state.cull_enable = 0;
    state.cull_face = 0x405;
    vsh_batch.xb.cull = 0;

    vsh_inputs[0] = nonfinite.value;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    CHECK(bruteforce_vulkan_test_draw_vsh(&surface, &vsh_batch),
          "accept non-finite vertex-program triangle");
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back rejected non-finite triangle");
    for (i = 0; i < 16; ++i)
        CHECK(pixels[i] == 0xFF000000,
              "non-finite vertex rejects its whole primitive");
    vsh_inputs[0] = 0.0f;

    *texture_pixel = 0xFF00FF00;
    memset(&texture, 0, sizeof texture);
    texture.offset = texture_va;
    texture.width = texture.height = 1;
    texture.pitch = 4;
    texture.color = 0x12;
    texture.addr_u = texture.addr_v = 3;
    texture.levels = 1;
    texture.filter = (1u << 16) | (1u << 24);
    batch.texture = &texture;
    for (i = 0; i < 3; ++i)
        vertices[i].diffuse = 0xFFFFFFFF;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back textured triangle");
    CHECK(pixels[0] == 0xFF00FF00, "decoded texture modulates diffuse color");
    bruteforce_vulkan_test_texture_cache_stats(
        &texture_hits, &texture_uploads, &texture_rewrites);
    CHECK(texture_hits == 0 && texture_uploads == 1 && texture_rewrites == 0,
          "first texture use uploads one exact snapshot");

    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back cached texture");
    bruteforce_vulkan_test_texture_cache_stats(
        &texture_hits, &texture_uploads, &texture_rewrites);
    CHECK(texture_hits == 1 && texture_uploads == 1 && texture_rewrites == 0,
          "unchanged texture reuses the cached Vulkan image");

    *texture_pixel = 0xFF0000FF;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back rewritten texture");
    CHECK(pixels[0] == 0xFF0000FF, "texture rewrite invalidates the cached image");
    bruteforce_vulkan_test_texture_cache_stats(
        &texture_hits, &texture_uploads, &texture_rewrites);
    CHECK(texture_hits == 1 && texture_uploads == 2 && texture_rewrites == 1,
          "texture rewrite replaces one cached upload");

    memset(palette, 0, 32 * sizeof *palette);
    *texture_index = 0;
    palette[0] = 0xFFFFFF00;
    texture.color = 0x0B;
    texture.pitch = 0;
    texture.palette = texture_va + 0x1000;
    texture.palette_entries = 32;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back P8 texture");
    CHECK(pixels[0] == 0xFFFFFF00, "P8 texture uses its checked palette snapshot");
    palette[0] = 0xFF00FFFF;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back rewritten P8 palette");
    CHECK(pixels[0] == 0xFF00FFFF, "palette rewrite invalidates the cached image");
    bruteforce_vulkan_test_texture_cache_stats(
        &texture_hits, &texture_uploads, &texture_rewrites);
    CHECK(texture_hits == 1 && texture_uploads == 4 && texture_rewrites == 2,
          "P8 palette rewrite replaces one cached upload");

    for (i = 0; i < 4; ++i)
        mip_pixels[i] = 0xFFFF0000;
    mip_pixels[4] = 0xFF00FF00;
    memset(&mip_texture, 0, sizeof mip_texture);
    mip_texture.offset = mip_va;
    mip_texture.width = mip_texture.height = 2;
    mip_texture.color = 0x06;
    mip_texture.addr_u = mip_texture.addr_v = 3;
    mip_texture.levels = 2;
    mip_texture.filter = (3u << 16) | (1u << 24);
    nv2a_pb_exec_method(0, 0x1B0C,
                        (0x100u << 18) | (0x100u << 6));
    batch.texture = &mip_texture;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back forced lower mip");
    CHECK(pixels[0] == 0xFF00FF00,
          "programmed LOD clamps select the supplied lower mip");
    mip_pixels[4] = 0xFF0000FF;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back rewritten lower mip");
    CHECK(pixels[0] == 0xFF0000FF,
          "lower-mip rewrites invalidate the exact texture snapshot");
    nv2a_pb_exec_method(0, 0x1B0C, 0);

    memset(xquad, 0, sizeof xquad);
    xquad[0].x = 0.0f; xquad[0].y = 0.0f;
    xquad[1].x = 4.0f; xquad[1].y = 0.0f;
    xquad[2].x = 0.0f; xquad[2].y = 4.0f;
    xquad[3].x = 4.0f; xquad[3].y = 0.0f;
    xquad[4].x = 4.0f; xquad[4].y = 4.0f;
    xquad[5].x = 0.0f; xquad[5].y = 4.0f;
    for (i = 0; i < 6; ++i) {
        unsigned stage;
        xquad[i].rhw = 1.0f;
        xquad[i].d0[0] = xquad[i].d0[1] =
            xquad[i].d0[2] = xquad[i].d0[3] = 1.0f;
        for (stage = 0; stage < 4; ++stage)
            xquad[i].tc[stage][3] = 1.0f;
    }
    memset(&xbatch, 0, sizeof xbatch);
    xbatch.vertices = xquad;
    xbatch.count = 6;
    xbatch.state = &state;
    xbatch.tex_valid[0] = xbatch.tex_valid[1] = 1;
    xbatch.tex_normalized[0] = xbatch.tex_normalized[1] = 1;
    xbatch.tex[0] = texture;
    xbatch.tex[0].color = 0x12;
    xbatch.tex[0].pitch = 4;
    xbatch.tex[0].palette = 0;
    xbatch.tex[0].palette_entries = 0;
    xbatch.tex[1] = xbatch.tex[0];
    xbatch.tex[1].offset = texture1_va;
    *texture_pixel = 0xFFFF00FF;
    *texture1_pixel = 0xFFFFFF00;
    memset(&combiner, 0, sizeof combiner);
    combiner.control = 1;
    combiner.stage_program = 1u | (1u << 5);
    combiner.color_icw[0] = 0x08u << 24 | 0x09u << 16;
    combiner.alpha_icw[0] = 0x18u << 24 | 0x19u << 16;
    combiner.color_ocw[0] = 0x0Cu << 4;
    combiner.alpha_ocw[0] = 0x0Cu << 4;
    xbatch.rc = &combiner;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_xdraw(&surface, &xbatch);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back extended two-stage combiner");
    CHECK(pixels[0] == 0xFFFF0000,
          "extended combiner multiplies texture stages zero and one");

    memset(cube_bytes, 0, 6 * 128);
    for (i = 0; i < 6; ++i)
        *(uint32_t *)(cube_bytes + i * 128) = 0xFF000000u | (i + 1);
    *(uint32_t *)cube_bytes = 0xFF0000FF;
    memset(&combiner, 0, sizeof combiner);
    combiner.control = 1;
    combiner.stage_program = 3;
    combiner.color_icw[0] = 0x08u << 24 | 0x04u << 16;
    combiner.alpha_icw[0] = 0x18u << 24 | 0x14u << 16;
    combiner.color_ocw[0] = 0x0Cu << 4;
    combiner.alpha_ocw[0] = 0x0Cu << 4;
    xbatch.rc = &combiner;
    xbatch.tex_valid[1] = 0;
    xbatch.tex_cube[0] = 1;
    xbatch.tex[0].offset = cube_va;
    for (i = 0; i < 6; ++i)
        xquad[i].tc[0][0] = 1.0f;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_xdraw(&surface, &xbatch);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back extended cube-map sample");
    CHECK(pixels[0] == 0xFF0000FF,
          "extended cube-map stage selects the positive-X face");

    memset(cube_bytes, 0, 6 * 128);
    for (i = 0; i < 6; ++i) {
        uint32_t *face = (uint32_t *)(cube_bytes + i * 128);
        unsigned j;
        for (j = 0; j < 4; ++j)
            face[j] = 0xFFFF0000;
        face[4] = i == 0 ? 0xFF00FF00 : 0xFF000000u | (i + 1);
    }
    cube_mip_texture = xbatch.tex[0];
    cube_mip_texture.width = cube_mip_texture.height = 2;
    cube_mip_texture.pitch = 0;
    cube_mip_texture.color = 0x06;
    cube_mip_texture.levels = 2;
    cube_mip_texture.filter = (3u << 16) | (1u << 24);
    xbatch.tex[0] = cube_mip_texture;
    nv2a_pb_exec_method(0, 0x1B0C,
                        (0x100u << 18) | (0x100u << 6));
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_xdraw(&surface, &xbatch);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back forced lower cube mip");
    CHECK(pixels[0] == 0xFF00FF00,
          "cube face stride and programmed LOD select the positive-X lower mip");
    nv2a_pb_exec_method(0, 0x1B0C, 0);

    xbatch.rc = NULL;
    xbatch.tex_cube[0] = 0;
    xbatch.tex[0] = texture;
    xbatch.tex[0].color = 0x12;
    xbatch.tex[0].pitch = 4;
    xbatch.tex[0].palette = 0;
    xbatch.tex[0].palette_entries = 0;
    xbatch.tex[0].color = 0x2E;
    xbatch.tex[0].offset = cube_va;
    for (i = 0; i < 6; ++i)
        xquad[i].tc[0][0] = 0.0f;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_xdraw(&surface, &xbatch);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back unsupported packed-depth texture fallback");
    CHECK(pixels[0] == 0xFFFFFFFF,
          "non-aliased packed-depth texture samples white like the CPU path");

    *texture_pixel = 0xFF00FFFF;
    xbatch.tex[0].color = 0x12;
    xbatch.tex[0].offset = texture_va;
    for (i = 0; i < 6; ++i) {
        xquad[i].d0[0] = 1.0f;
        xquad[i].d0[1] = 1.0f;
        xquad[i].d0[2] = 0.0f;
        xquad[i].d0[3] = 1.0f;
        xquad[i].tc[0][0] = 0.0f;
    }
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_BEGIN);
    bruteforce_vulkan_test_xdraw(&surface, &xbatch);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_END);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back extended default combiner");
    CHECK(pixels[0] == 0xFF00FF00,
          "extended null combiner falls back to texture zero times diffuse");
    CHECK(bruteforce_vulkan_test_zpass_count() == 16,
          "visibility query counts extended-draw surface samples");
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);

    batch.texture = NULL;
    state.zeta_va = depth_va;
    state.depth_test_enable = 1;
    state.depth_write = 1;
    state.depth_func = 0x201;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF1, 0xFF000000, 0xFFFFFF00);
    bruteforce_vulkan_test_clear(&surface, &state, 0x40, 0xFF0000CC, 0);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back partial color clear with depth attached");
    CHECK(pixels[0] == 0xFF0000CC,
          "partial color clear uses a depth-compatible pipeline");
    bruteforce_vulkan_test_clear(&surface, &state, 0xF1, 0xFF000000, 0xFFFFFF00);
    for (i = 0; i < 3; ++i) {
        vertices[i].z = 16777215.0f * 0.25f;
        vertices[i].diffuse = 0xFFFF0000;
    }
    bruteforce_vulkan_test_draw(&surface, &batch);
    for (i = 0; i < 3; ++i) {
        vertices[i].z = 16777215.0f * 0.75f;
        vertices[i].diffuse = 0xFF00FF00;
    }
    bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va), "read back depth-tested triangle");
    CHECK(pixels[0] == 0xFFFF0000, "near triangle rejects the later far triangle");

    state = default_state();
    memset(quad, 0, sizeof quad);
    quad[0].x = 0.0f; quad[0].y = 0.0f;
    quad[1].x = 4.0f; quad[1].y = 0.0f;
    quad[2].x = 0.0f; quad[2].y = 4.0f;
    quad[3].x = 4.0f; quad[3].y = 0.0f;
    quad[4].x = 4.0f; quad[4].y = 4.0f;
    quad[5].x = 0.0f; quad[5].y = 4.0f;
    for (i = 0; i < 6; ++i) {
        quad[i].rhw = 1.0f;
        quad[i].diffuse = 0xFFFFFFFF;
    }
    batch.vertices = quad;
    batch.count = 6;
    batch.state = &state;
    batch.texture = NULL;
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_BEGIN);
    bruteforce_vulkan_test_draw(&surface, &batch);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_END);
    CHECK(bruteforce_vulkan_test_zpass_count() == 16,
          "visibility query counts passed surface samples");
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);
    CHECK(bruteforce_vulkan_test_zpass_count() == 0,
          "visibility clear discards prior samples");
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_BEGIN);
    bruteforce_vulkan_test_draw(&surface, &batch);
    bruteforce_vulkan_test_draw(&surface, &batch);
    bruteforce_vulkan_test_draw(&surface, &batch);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_END);
    CHECK(bruteforce_vulkan_test_zpass_count() == 48,
          "batched visibility queries retain each draw result");
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);
    surface.aa_sx = surface.aa_sy = 2;
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_BEGIN);
    bruteforce_vulkan_test_draw(&surface, &batch);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_END);
    CHECK(bruteforce_vulkan_test_zpass_count() == 4,
          "visibility count is normalized to logical guest pixels");
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);
    surface.aa_sx = surface.aa_sy = 1;

    memset(&surface16, 0, sizeof surface16);
    surface16.color_va = color16_va;
    surface16.width = 4;
    surface16.height = 4;
    surface16.pitch = 8;
    surface16.bytes_per_pixel = 2;
    surface16.aa_sx = surface16.aa_sy = 1;
    memset(pixels16, 0, 32);
    state = default_state();
    bruteforce_vulkan_test_clear(&surface16, &state, 0xF0, 0xFFFF0000, 0);
    CHECK(bruteforce_vulkan_test_readback(color16_va), "read back RGB565 clear");
    for (i = 0; i < 16; ++i)
        CHECK(pixels16[i] == 0xF800, "BGRA8 render target converts to RGB565");

    state = default_state();
    batch.vertices = quad;
    batch.count = 6;
    batch.state = &state;
    batch.texture = NULL;
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "establish batching stress-test surface");
    bruteforce_vulkan_test_submission_stats(
        &draws_before, &submissions_before, NULL);
    for (i = 0; i < 300; ++i)
        bruteforce_vulkan_test_draw(&surface, &batch);
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back batched draw stress test");
    for (i = 0; i < 16; ++i)
        CHECK(pixels[i] == 0xFFFFFFFF,
              "batched draw rollover preserves rendered output");
    bruteforce_vulkan_test_submission_stats(
        &draws_after, &submissions_after, &max_batch_operations);
    CHECK(draws_after - draws_before == 300,
          "batching stress test submitted every draw");
    CHECK(submissions_after - submissions_before == 3,
          "300 draws use two full batches and one readback batch");
    CHECK(max_batch_operations == 128,
          "bounded command batch reaches but does not exceed 128 operations");
    return 1;
}

typedef struct TestVisibilityReport {
    uint64_t stamp;
    uint32_t count;
    uint32_t status;
} TestVisibilityReport;

static int run_async_visibility_test(void)
{
    enum { REPORT_COUNT = 300, REPORT_RESET = 150 };
    const uint32_t color_va = XBOX_CONTIG_BASE + 0x20000;
    const uint32_t report_va = XBOX_CONTIG_BASE + 0x29000;
    Nv2aSurface surface;
    Nv2aRenderState state = default_state();
    Nv2aVertex quad[6];
    Nv2aBatch batch;
    TestVisibilityReport *reports =
        (TestVisibilityReport *)((uintptr_t)report_va + xbox_GetMemoryOffset());
    uint32_t *pixels =
        (uint32_t *)((uintptr_t)color_va + xbox_GetMemoryOffset());
    uint64_t submissions_before;
    uint64_t submissions_after;
    uint64_t deferred_before;
    uint64_t deferred_after;
    unsigned i;

    memset(&surface, 0, sizeof surface);
    surface.color_va = color_va;
    surface.width = 4;
    surface.height = 4;
    surface.pitch = 16;
    surface.bytes_per_pixel = 4;
    surface.aa_sx = surface.aa_sy = 1;
    memset(quad, 0, sizeof quad);
    quad[0].x = 0.0f; quad[0].y = 0.0f;
    quad[1].x = 4.0f; quad[1].y = 0.0f;
    quad[2].x = 0.0f; quad[2].y = 4.0f;
    quad[3].x = 4.0f; quad[3].y = 0.0f;
    quad[4].x = 4.0f; quad[4].y = 4.0f;
    quad[5].x = 0.0f; quad[5].y = 4.0f;
    for (i = 0; i < 6; ++i) {
        quad[i].rhw = 1.0f;
        quad[i].diffuse = 0xFFFFFFFF;
    }
    memset(&batch, 0, sizeof batch);
    batch.vertices = quad;
    batch.count = 6;
    batch.state = &state;
    memset(reports, 0, (REPORT_COUNT + 1) * sizeof *reports);
    for (i = 0; i <= REPORT_COUNT; ++i)
        reports[i].status = UINT32_MAX;

    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_BEGIN);
    bruteforce_vulkan_test_draw(&surface, &batch);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_END);
    CHECK(bruteforce_vulkan_test_zpass_report(
              report_va + REPORT_COUNT * sizeof *reports, 10),
          "queue deferred visibility report");
    bruteforce_vulkan_test_set_oldest_zpass_age(-1000.0);
    bruteforce_vulkan_test_submission_stats(
        NULL, &submissions_before, NULL);
    bruteforce_vulkan_test_zpass_poll_stats(&deferred_before);
    nv2a_pb_exec_poll();
    bruteforce_vulkan_test_submission_stats(
        NULL, &submissions_after, NULL);
    bruteforce_vulkan_test_zpass_poll_stats(&deferred_after);
    CHECK(submissions_after == submissions_before,
          "unactionable visibility poll does not flush");
    CHECK(deferred_after == deferred_before + 1,
          "unactionable visibility poll is deferred");
    CHECK(reports[REPORT_COUNT].status == UINT32_MAX,
          "deferred visibility report remains pending");
    bruteforce_vulkan_test_set_oldest_zpass_age(1.0);
    for (i = 0;
         i < 100000 && reports[REPORT_COUNT].status == UINT32_MAX;
         ++i) {
        nv2a_pb_exec_poll();
        SwitchToThread();
    }
    CHECK(reports[REPORT_COUNT].status == 0,
          "aged visibility report completes");
    CHECK(reports[REPORT_COUNT].stamp == 10,
          "aged visibility report preserves its stamp");
    CHECK(reports[REPORT_COUNT].count == 16,
          "aged visibility report preserves its exact count");

    bruteforce_vulkan_test_submission_stats(
        NULL, &submissions_before, NULL);
    bruteforce_vulkan_test_clear(&surface, &state, 0xF0, 0xFF000000, 0);
    bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);
    for (i = 0; i < REPORT_COUNT; ++i) {
        if (i == REPORT_RESET)
            bruteforce_vulkan_test_zpass(NV2A_ZPASS_CLEAR);
        bruteforce_vulkan_test_zpass(NV2A_ZPASS_BEGIN);
        bruteforce_vulkan_test_draw(&surface, &batch);
        bruteforce_vulkan_test_zpass(NV2A_ZPASS_END);
        CHECK(bruteforce_vulkan_test_zpass_report(
                  report_va + i * sizeof *reports, i + 11),
              "queue asynchronous visibility report");
    }

    for (i = 0;
         i < 100000 && reports[REPORT_COUNT - 1].status == UINT32_MAX;
         ++i) {
        nv2a_pb_exec_poll();
        SwitchToThread();
    }
    for (i = 0; i < REPORT_COUNT; ++i) {
        uint32_t expected = (i < REPORT_RESET
                           ? i + 1 : i - REPORT_RESET + 1) * 16;
        CHECK(reports[i].status == 0,
              "asynchronous visibility report completes");
        CHECK(reports[i].stamp == i + 11,
              "asynchronous visibility report preserves its stamp");
        CHECK(reports[i].count == expected,
              "asynchronous visibility report has an ordered exact count");
    }
    CHECK(bruteforce_vulkan_test_readback(color_va),
          "read back asynchronous visibility draws");
    bruteforce_vulkan_test_submission_stats(
        NULL, &submissions_after, NULL);
    CHECK(submissions_after - submissions_before == 4,
          "300 asynchronous reports share bounded draw and readback batches");
    for (i = 0; i < 16; ++i)
        CHECK(pixels[i] == 0xFFFFFFFF,
              "asynchronous visibility preserves rendered output");
    return 1;
}

static int run_tests(const char *xbe_path)
{
    BruteforceRendererStartupResult result;
    if (!initialize_memory(xbe_path))
        return 0;
    CHECK(_putenv_s("RECOMP_GPU_MIPS", "invalid") == 0,
          "set invalid mip option");
    result = bruteforce_vulkan_install(NULL);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION,
          "invalid mip option fails before Vulkan startup");
    CHECK(!bruteforce_vulkan_test_installed(),
          "invalid mip option does not register");
    CHECK(_putenv_s("RECOMP_GPU_MIPS", "0") == 0,
          "disable mip sampling for startup failure tests");
    CHECK(_putenv_s("BRUTEFORCE_VULKAN_TEST_FAIL", "loader") == 0,
          "set failure injection");
    result = bruteforce_vulkan_install(NULL);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE,
          "injected loader failure is unavailable");
    CHECK(!bruteforce_vulkan_test_installed(), "failed startup does not register");
    CHECK(_putenv_s("BRUTEFORCE_VULKAN_TEST_FAIL", "probe-pipeline") == 0,
          "set probe failure injection");
    result = bruteforce_vulkan_install(NULL);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
          "injected probe failure is an initialization failure");
    CHECK(!bruteforce_vulkan_test_installed(),
          "probe failure leaves no partial registration");
    CHECK(_putenv_s("BRUTEFORCE_VULKAN_TEST_FAIL", "graphics-pipeline") == 0,
          "set graphics pipeline failure injection");
    result = bruteforce_vulkan_install(NULL);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
          "graphics pipeline failure is an initialization failure");
    CHECK(!bruteforce_vulkan_test_installed(),
          "graphics pipeline failure rolls startup back");
    CHECK(_putenv_s("BRUTEFORCE_VULKAN_TEST_FAIL", "") == 0,
          "clear failure injection");
    CHECK(_putenv_s("RECOMP_GPU_MIPS", "1") == 0,
          "enable Vulkan mip sampling");
    CHECK(_putenv_s("RECOMP_GPU_VSH_VERIFY", "1") == 0,
          "enable Vulkan vertex-program verification");
    result = bruteforce_vulkan_install(NULL);
    if (result.status == BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE) {
        fprintf(stderr, "SKIP: Vulkan unavailable at %s: %s\n",
                result.stage, result.reason);
        xbox_MemoryLayoutShutdown();
        return 77;
    }
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_SUCCESS, result.reason);
    CHECK(bruteforce_vulkan_test_installed(), "successful startup registers");
    CHECK(run_render_test(), "run Vulkan rendering fixture");
    CHECK(bruteforce_vulkan_test_validation_errors() == 0,
          "Vulkan validation reported no errors");
    bruteforce_vulkan_shutdown();
    CHECK(!bruteforce_vulkan_test_installed(), "shutdown removes registration");
    CHECK(_putenv_s("RECOMP_GPU_VSH_VERIFY", "") == 0,
          "clear Vulkan vertex-program verification");
    CHECK(_putenv_s("RECOMP_GPU_ZPASS", "async") == 0,
          "enable asynchronous Vulkan visibility reports");
    result = bruteforce_vulkan_install(NULL);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_SUCCESS, result.reason);
    CHECK(run_async_visibility_test(),
          "run asynchronous Vulkan visibility fixture");
    CHECK(bruteforce_vulkan_test_validation_errors() == 0,
          "asynchronous Vulkan visibility reported no validation errors");
    bruteforce_vulkan_shutdown();
    CHECK(_putenv_s("RECOMP_GPU_ZPASS", "") == 0,
          "clear asynchronous Vulkan visibility reports");
    xbox_MemoryLayoutShutdown();
    CHECK(_putenv_s("RECOMP_GPU_MIPS", "") == 0,
          "clear Vulkan mip sampling");
    puts("Vulkan clear/draw/readback: ok");
    return 1;
}

int main(int argc, char **argv)
{
    int result;
    if (argc != 2) {
        fprintf(stderr, "usage: vulkan_test <synthetic.xbe>\n");
        return 2;
    }
    result = run_tests(argv[1]);
    if (result == 77)
        return 77;
    if (!result)
        return 1;
    return 0;
}
