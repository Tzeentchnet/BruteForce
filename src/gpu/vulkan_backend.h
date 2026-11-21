#ifndef BRUTEFORCE_VULKAN_BACKEND_H
#define BRUTEFORCE_VULKAN_BACKEND_H

#include "renderer_startup.h"

BruteforceRendererStartupResult bruteforce_vulkan_install(void *context);
void bruteforce_vulkan_shutdown(void);

#ifdef BRUTEFORCE_VULKAN_TESTING
#include "nv2a_backend.h"
#include "nv2a_backend_ext.h"

int bruteforce_vulkan_test_installed(void);
long bruteforce_vulkan_test_validation_errors(void);
void bruteforce_vulkan_test_clear(const Nv2aSurface *surface,
                                  const Nv2aRenderState *state, uint32_t flags,
                                  uint32_t color, uint32_t zstencil);
void bruteforce_vulkan_test_draw(const Nv2aSurface *surface, const Nv2aBatch *batch);
void bruteforce_vulkan_test_xdraw(const Nv2aSurface *surface,
                                  const Nv2aXBatch *batch);
int bruteforce_vulkan_test_draw_vsh(const Nv2aSurface *surface,
                                    const Nv2aXVsBatch *batch);
int bruteforce_vulkan_test_vsh_readback(Nv2aXVsOut *output,
                                        uint32_t vertex_count);
int bruteforce_vulkan_test_readback(uint32_t color_va);
int bruteforce_vulkan_test_texture_aliases_surface(const Nv2aTexture *texture);
void bruteforce_vulkan_test_texture_cache_stats(uint64_t *hits, uint64_t *uploads,
                                                uint64_t *rewrites);
void bruteforce_vulkan_test_submission_stats(uint64_t *draws,
                                             uint64_t *submissions,
                                             uint64_t *max_batch_operations);
void bruteforce_vulkan_test_zpass_poll_stats(uint64_t *deferred);
void bruteforce_vulkan_test_set_oldest_zpass_age(double age_ms);
void bruteforce_vulkan_test_zpass(int op);
uint32_t bruteforce_vulkan_test_zpass_count(void);
int bruteforce_vulkan_test_zpass_report(uint32_t report_va, uint64_t stamp);
#endif

#endif
