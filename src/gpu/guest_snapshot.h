#ifndef BRUTEFORCE_GPU_GUEST_SNAPSHOT_H
#define BRUTEFORCE_GPU_GUEST_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

typedef struct GuestSpanIdentity {
    uint64_t layout;
    uint64_t allocation;
    uint64_t bounds;
} GuestSpanIdentity;

typedef enum GuestCopyResult {
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

GuestCopyResult gpu_guest_copy(uint32_t va, size_t bytes, void *destination,
                               int bounds_policy, GuestSpanIdentity *identity);
uint64_t gpu_guest_allocation_identity(uint32_t va, uint32_t *remaining);
int gpu_guest_identity_still_valid(uint32_t va, size_t bytes, int bounds_policy,
                                   const GuestSpanIdentity *expected);

#endif
