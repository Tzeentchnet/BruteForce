/*
 * Renderer-neutral render-scale authority adapted from Blood Wake's bw_vfx.c
 * (MIT; THIRD_PARTY_NOTICES.md). D3D11 resolves are in render_scale_d3d11.c.
 */
#include <windows.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_scale.h"

static int s_configured;
static int s_requested;
static int s_native;
static int s_fixed_output;
static float s_scale = 1.0f;
static unsigned s_epoch;
static uint64_t s_revision;
static uint32_t s_output_width, s_output_height;

static int picture_size(uint32_t width, uint32_t height, uint32_t *pw, uint32_t *ph)
{
    if (!width || !height || width > 16384 || height > 16384)
        return 0;
    if ((uint64_t)width * 3 <= (uint64_t)height * 4) {
        *pw = width;
        *ph = (uint32_t)((uint64_t)width * 3 / 4);
    } else {
        *pw = (uint32_t)((uint64_t)height * 4 / 3);
        *ph = height;
    }
    return *pw && *ph;
}

static int parse_scale(const char *text, float *value)
{
    char *end;
    float parsed;

    errno = 0;
    parsed = strtof(text, &end);
    if (end == text || *end || errno == ERANGE || !isfinite(parsed) ||
        parsed < 0.5f || parsed > 10.0f)
        return 0;
    *value = parsed;
    return 1;
}

static int parse_output(const char *text, uint32_t *width, uint32_t *height)
{
    uint32_t dimensions[2] = {0, 0};
    int i;

    for (i = 0; i < 2; ++i) {
        if (*text < '0' || *text > '9')
            return 0;
        do {
            unsigned digit = (unsigned)(*text++ - '0');
            if (dimensions[i] > (16384u - digit) / 10u)
                return 0;
            dimensions[i] = dimensions[i] * 10u + digit;
        } while (*text >= '0' && *text <= '9');
        if (!dimensions[i] || (i == 0 ? *text++ != 'x' : *text != '\0'))
            return 0;
    }
    return picture_size(dimensions[0], dimensions[1], width, height);
}

int gpu_scale_prepare_config(GpuScaleConfig *config)
{
    const char *scale = getenv("RECOMP_GPU_SCALE");
    const char *output = getenv("RECOMP_GPU_OUTPUT");

    if (!config)
        return 0;
    memset(config, 0, sizeof *config);
    if (s_configured) {
        config->requested = s_requested;
        config->native = s_native;
        config->fixed_output = s_fixed_output;
        config->scale = s_scale;
        config->width = s_output_width;
        config->height = s_output_height;
        config->valid = 1;
        return 1;
    }
    config->scale = 1.0f;
    if (!scale || !*scale) {
        if (output && *output) {
            fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT is only valid with"
                            " RECOMP_GPU_SCALE=native\n");
            return 0;
        }
    } else if (_stricmp(scale, "native") == 0) {
        config->native = 1;
        config->requested = 1;
        config->scale = 2.0f;
        if (output && *output) {
            if (!parse_output(output, &config->width, &config->height)) {
                fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT must be WxH with dimensions"
                                " from 1 to 16384\n");
                return 0;
            }
            config->scale = (float)config->height / 480.0f;
            if (config->scale < 0.5f || config->scale > 10.0f) {
                fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT produces a native scale"
                                " outside the supported 0.5 to 10 range\n");
                return 0;
            }
            config->fixed_output = 1;
        }
    } else {
        if (output && *output) {
            fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_OUTPUT is only valid with"
                            " RECOMP_GPU_SCALE=native\n");
            return 0;
        }
        if (!parse_scale(scale, &config->scale)) {
            fprintf(stderr, "[GPU-SCALE] RECOMP_GPU_SCALE must be native or a number"
                            " from 0.5 to 10\n");
            return 0;
        }
        config->requested = fabsf(config->scale - 1.0f) > 0.001f;
        if (!config->requested)
            config->scale = 1.0f;
    }
    config->valid = 1;
    return 1;
}

int gpu_scale_commit_config(const GpuScaleConfig *config)
{
    if (!config || s_configured || config->valid != 1 ||
        (config->requested != 0 && config->requested != 1) ||
        (config->native != 0 && config->native != 1) ||
        (config->fixed_output != 0 && config->fixed_output != 1) ||
        !isfinite(config->scale) || config->scale < 0.5f || config->scale > 10.0f ||
        (config->native && !config->requested) ||
        (!config->native && (config->fixed_output || config->width || config->height ||
                             config->requested != (fabsf(config->scale - 1.0f) > 0.001f))) ||
        (config->native && !config->fixed_output &&
         (config->width || config->height || config->scale != 2.0f)) ||
        (config->fixed_output &&
         (!config->width || !config->height || config->width > 16384 ||
          config->height > 16384 || config->scale != (float)config->height / 480.0f))) {
        fprintf(stderr, "[GPU-SCALE] invalid configuration; no scale change committed\n");
        return 0;
    }
    s_native = config->native;
    s_requested = config->requested;
    s_fixed_output = config->fixed_output;
    s_scale = config->scale;
    s_output_width = config->width;
    s_output_height = config->height;
    s_configured = 1;
    ++s_revision;
    if (s_native)
        fprintf(stderr, "[GPU-SCALE] native resolution enabled: initial picture %ux%u%s,"
                        " render scale %.3f (%0.fx%0.f for the 640x480 screen)\n",
                s_output_width, s_output_height, s_fixed_output ? " (fixed)" : "",
                s_scale, 640.0f * s_scale, 480.0f * s_scale);
    else if (s_requested)
        fprintf(stderr, "[GPU-SCALE] fixed render scale %.3f"
                        " (%0.fx%0.f for the 640x480 screen)\n",
                s_scale, 640.0f * s_scale, 480.0f * s_scale);
    return 1;
}

int gpu_scale_configured(void)
{
    return s_configured;
}

int gpu_scale_requested(void)
{
    return s_requested;
}

int gpu_scale_native(void)
{
    return s_native;
}

float gpu_scale_value(void)
{
    return s_requested ? s_scale : 1.0f;
}

unsigned gpu_scale_epoch(void)
{
    return s_epoch;
}

int gpu_scale_fixed_output(uint32_t *width, uint32_t *height)
{
    if (!s_fixed_output)
        return 0;
    if (width) *width = s_output_width;
    if (height) *height = s_output_height;
    return 1;
}

int gpu_scale_output_size(uint32_t *width, uint32_t *height)
{
    if (!s_output_width || !s_output_height)
        return 0;
    if (width) *width = s_output_width;
    if (height) *height = s_output_height;
    return 1;
}

int gpu_scale_prepare_output(uint32_t width, uint32_t height, GpuScaleUpdate *update)
{
    float scale;

    if (!update || !s_configured)
        return 0;
    update->scale = s_scale;
    update->width = s_output_width;
    update->height = s_output_height;
    update->epoch = s_epoch;
    update->revision = s_revision;
    update->changed = update->scale_changed = 0;
    if (!s_native || s_fixed_output)
        return 1;
    scale = (float)height / 480.0f;
    if (!width || !height || width > 16384 || height > 16384 ||
        scale < 0.5f || scale > 10.0f) {
        fprintf(stderr, "[GPU-SCALE] output picture %ux%u produces unsupported"
                        " native scale %.3f; no scale change committed\n",
                width, height, scale);
        return 0;
    }
    if (width == s_output_width && height == s_output_height)
        return 1;
    update->width = width;
    update->height = height;
    update->changed = 1;
    if (fabsf(scale - s_scale) > 0.001f) {
        if (s_epoch == UINT_MAX) {
            fprintf(stderr, "[GPU-SCALE] scale epoch exhausted; no scale change committed\n");
            return 0;
        }
        update->scale = scale;
        update->epoch = s_epoch + 1;
        update->scale_changed = 1;
    }
    return 1;
}

int gpu_scale_commit_output(const GpuScaleUpdate *update)
{
    float scale;
    int scale_changed;
    if (!update || !s_configured || update->revision != s_revision) {
        fprintf(stderr, "[GPU-SCALE] stale output update; no scale change committed\n");
        return 0;
    }
    if (!update->changed) {
        if (!update->scale_changed && update->width == s_output_width &&
            update->height == s_output_height && update->scale == s_scale &&
            update->epoch == s_epoch)
            return 1;
        fprintf(stderr, "[GPU-SCALE] invalid output update; no scale change committed\n");
        return 0;
    }
    scale = (float)update->height / 480.0f;
    scale_changed = fabsf(scale - s_scale) > 0.001f;
    if (update->changed != 1 || !s_native || s_fixed_output ||
        !update->width || !update->height ||
        update->width > 16384 || update->height > 16384 || scale < 0.5f || scale > 10.0f ||
        (update->width == s_output_width && update->height == s_output_height) ||
        update->scale_changed != scale_changed ||
        (scale_changed && s_epoch == UINT_MAX) ||
        update->epoch != s_epoch + (unsigned)scale_changed ||
        update->scale != (scale_changed ? scale : s_scale)) {
        fprintf(stderr, "[GPU-SCALE] invalid output update; no scale change committed\n");
        return 0;
    }
    s_output_width = update->width;
    s_output_height = update->height;
    s_scale = update->scale;
    s_epoch = update->epoch;
    ++s_revision;
    fprintf(stderr, "[GPU-SCALE] output picture %ux%u: render scale %.3f"
                    " (%0.fx%0.f for the 640x480 screen), epoch %u\n",
            update->width, update->height, s_scale, 640.0f * s_scale,
            480.0f * s_scale, s_epoch);
    return 1;
}

int gpu_scale_surface_size_for(float scale, uint32_t width, uint32_t height,
                               uint32_t aa_sx, uint32_t aa_sy,
                               uint32_t *physical_width, uint32_t *physical_height)
{
    double sx = scale, sy = sx;
    double pw, ph;

    if (!physical_width || !physical_height || !width || !height ||
        !isfinite(scale) || scale < 0.5f || scale > 10.0f)
        return 0;
    if (fabsf(scale - 1.0f) > 0.001f) {
        if (aa_sx > 1)
            sx /= aa_sx;
        if (aa_sy > 1)
            sy /= aa_sy;
    }
    pw = floor(width * sx + 0.5);
    ph = floor(height * sy + 0.5);
    if (pw < 1.0 || ph < 1.0 || pw > 16384.0 || ph > 16384.0) {
        fprintf(stderr, "[GPU-SCALE] logical surface %ux%u at scale %.3f"
                        " exceeds the 1..16384 texture limit\n",
                width, height, scale);
        return 0;
    }
    *physical_width = (uint32_t)pw;
    *physical_height = (uint32_t)ph;
    return 1;
}

int gpu_scale_surface_size(uint32_t width, uint32_t height,
                           uint32_t aa_sx, uint32_t aa_sy,
                           uint32_t *physical_width, uint32_t *physical_height)
{
    return gpu_scale_surface_size_for(gpu_scale_value(), width, height, aa_sx, aa_sy,
                                      physical_width, physical_height);
}
