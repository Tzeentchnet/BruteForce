/* Direct3D 11 render back end (RECOMP_GPU=d3d11); see d3d11_backend.c. */
#ifndef BRUTEFORCE_D3D11_BACKEND_H
#define BRUTEFORCE_D3D11_BACKEND_H

int bruteforce_d3d11_install(void);

#ifdef BRUTEFORCE_D3D11_TESTING
#include <stdint.h>

typedef struct BruteforceD3D11TestSnapshot {
    uintptr_t color, color_rtv, color_srv, depth, depth_dsv;
    uintptr_t shared, shared_rtv, shared_mutex, resolve, resolve_rtv, staging;
    uintptr_t bound_color, bound_depth;
    uint32_t color_width, color_height, depth_width, depth_height;
    uint32_t shared_width, shared_height, resolve_width, resolve_height;
    float scale;
    unsigned epoch;
} BruteforceD3D11TestSnapshot;

void bruteforce_d3d11_test_fail(const char *stage, unsigned occurrence);
int bruteforce_d3d11_test_initialize_device(void);
uint32_t bruteforce_d3d11_test_device_thread(void);
int bruteforce_d3d11_test_native_output(uint32_t width, uint32_t height,
                                        uint32_t color_va, int require_shared);
int bruteforce_d3d11_test_snapshot(uint32_t color_va, uint32_t depth_va,
                                   BruteforceD3D11TestSnapshot *snapshot);
int bruteforce_d3d11_test_shared_busy(int busy);
int bruteforce_d3d11_test_readback(uint32_t color_va);
void bruteforce_d3d11_test_flip(void);
#endif

#endif
