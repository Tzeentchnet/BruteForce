#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdint.h>

#include "guest_snapshot.h"
#include "kernel.h"
#include "xbox_memory_layout.h"

static int guest_data_span(uint32_t va, size_t bytes, uint64_t *physical)
{
    uint64_t mapped = xbox_GetMappedSize();
    uint64_t end;
    uint64_t relative;
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

uint64_t gpu_guest_allocation_identity(uint32_t va, uint32_t *remaining)
{
    uint64_t physical;
    int domain = guest_data_span(va, 1, &physical);

    if (domain == 1)
        return xbox_HeapBlockIdentity((uint32_t)physical, remaining);
    if (domain == 2)
        return xbox_ContiguousBlockIdentity(XBOX_CONTIG_BASE + (uint32_t)physical,
                                            remaining);
    if (remaining)
        *remaining = 0;
    return 0;
}

GuestCopyResult gpu_guest_copy(uint32_t va, size_t bytes, void *destination,
                               int bounds_policy, GuestSpanIdentity *identity)
{
    GuestSpanIdentity before = {0};
    GuestSpanIdentity after = {0};
    uint32_t remaining;
    uint32_t after_remaining;
    uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset();
    uintptr_t host;
    uint64_t physical;
    SIZE_T copied = 0;

    if (!destination || !guest_data_span(va, bytes, &physical) ||
        offset > UINTPTR_MAX - va)
        return GUEST_COPY_INVALID;
    host = offset + va;
    if (bytes > UINTPTR_MAX - host)
        return GUEST_COPY_INVALID;
    before.layout = xbox_MemoryLayoutGeneration();
    if (!before.layout)
        return GUEST_COPY_INVALID;
    before.allocation = gpu_guest_allocation_identity(va, &remaining);
    if (remaining && (!before.allocation || bytes > remaining))
        return GUEST_COPY_SHORT;
    if (bounds_policy != GUEST_BOUNDS_IGNORE) {
        before.bounds = xbox_GuestBoundsIdentity(va, &remaining);
        if (!before.bounds && bounds_policy == GUEST_BOUNDS_REQUIRED)
            return GUEST_COPY_UNREGISTERED;
        if (before.bounds && bytes > remaining)
            return GUEST_COPY_SHORT;
    }
    if (!ReadProcessMemory(GetCurrentProcess(), (const void *)host, destination,
                           bytes, &copied) ||
        copied != bytes)
        return GUEST_COPY_UNREADABLE;
    after.layout = xbox_MemoryLayoutGeneration();
    after.allocation = gpu_guest_allocation_identity(va, &after_remaining);
    if (bounds_policy != GUEST_BOUNDS_IGNORE)
        after.bounds = xbox_GuestBoundsIdentity(va, &remaining);
    if (before.layout != after.layout || before.allocation != after.allocation ||
        before.bounds != after.bounds ||
        (after_remaining && bytes > after_remaining) ||
        (after.bounds && bytes > remaining))
        return GUEST_COPY_CHANGED;
    if (identity)
        *identity = before;
    return GUEST_COPY_OK;
}

int gpu_guest_identity_still_valid(uint32_t va, size_t bytes, int bounds_policy,
                                   const GuestSpanIdentity *expected)
{
    uint32_t remaining;
    uint64_t allocation;
    uint64_t bounds = 0;

    if (!expected || !expected->layout ||
        xbox_MemoryLayoutGeneration() != expected->layout)
        return 0;
    allocation = gpu_guest_allocation_identity(va, &remaining);
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
