#include <windows.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "xbox_memory_layout.h"
#include "platform/host_time.h"
#include "perf_counters.h"

#define BRUTEFORCE_DEVICE_GLOBAL 0x0037DD88u
#define BRUTEFORCE_DEVICE_SEMAPHORE_OFFSET 0x30u
#define BRUTEFORCE_DEVICE_FENCE_EVENT_OFFSET 0x196Cu
#define BRUTEFORCE_DEVICE_NV2A_OFFSET 0x17C8u
#define BRUTEFORCE_MINIPORT_FENCE_EVENT_OFFSET 0x1A4u
#define BRUTEFORCE_NV2A_BASE 0xFD000000u
#define BRUTEFORCE_PFB_FLUSH_OFFSET 0x100410u
#define BRUTEFORCE_PFB_FLUSH_BUSY 0x00010000u
#define BRUTEFORCE_USER_DMA_PUT 0xFD800040u
#define BRUTEFORCE_GPU_VECTOR 3u
#define BRUTEFORCE_XPP_CLI 0x00384FA5u
#define BRUTEFORCE_XPP_STI 0x00384FB6u
#define BRUTEFORCE_EFLAGS_IF 0x00000200u
#define BRUTEFORCE_DAC_RAMP_BYTES (256u * 3u)

typedef void (*recomp_func_t)(void);
extern void sub_0006EE30_gen(void);
extern void sub_00372180_gen(void);
extern void sub_00377F80_gen(void);

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_ebx, g_ebp, g_esi, g_edi;
extern RECOMP_TLS uint32_t g_eflags;

static RECOMP_TLS int xpp_interrupts_masked;

static volatile uint32_t *guest_word(uint32_t va, int writable)
{
    const uint64_t end = (uint64_t)va + sizeof(uint32_t);
    const uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset();
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const DWORD writeable = PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    MEMORY_BASIC_INFORMATION info;
    volatile uint32_t *result = NULL;
    uintptr_t host;
    uintptr_t region;
    uint64_t total_start = 0, query_start = 0, query_ticks = 0;
    unsigned queries = 0, query_failures = 0, regions = 0;
    int perf = g_bruteforce_perf_counters_enabled;

    if (perf)
        total_start = bruteforce_perf_clock();
    if (!va || (va & 3u) ||
        !(end <= XBOX_CONTIG_SIZE ||
          (va >= XBOX_CONTIG_BASE && end <= (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE)) ||
        offset > UINTPTR_MAX - va)
        goto done;
    host = offset + va;
    if (perf)
        query_start = bruteforce_perf_clock();
    queries = 1;
    if (VirtualQuery((const void *)host, &info, sizeof(info)) != sizeof(info)) {
        if (perf)
            query_ticks = bruteforce_perf_clock() - query_start;
        query_failures = 1;
        goto done;
    }
    if (perf)
        query_ticks = bruteforce_perf_clock() - query_start;
    regions = 1;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (writable ? writeable : readable)))
        goto done;
    region = (uintptr_t)info.BaseAddress;
    if (host < region || info.RegionSize < sizeof(uint32_t) ||
        host - region > info.RegionSize - sizeof(uint32_t))
        goto done;
    result = (volatile uint32_t *)host;

done:
    if (perf)
        bruteforce_perf_record_guest_word(result != NULL, queries, query_failures, regions,
                                          bruteforce_perf_clock() - total_start, query_ticks);
    return result;
}

static void invalid_gpu_pointer(const char *name, uint32_t va)
{
    fprintf(stderr, "[GPU] Invalid %s guest address 0x%08X\n", name, va);
    abort();
}

static void read_dac_trace_guest(uint32_t va, void *out, size_t size, const char *name)
{
    const uint64_t end = (uint64_t)va + size;
    const uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset();
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    uintptr_t host, cursor, host_end;
    SIZE_T copied = 0;

    if (!va || !size ||
        !(end <= XBOX_CONTIG_SIZE ||
          (va >= XBOX_CONTIG_BASE && end <= (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE)) ||
        offset > UINTPTR_MAX - va || size > UINTPTR_MAX - (offset + va)) {
        fprintf(stderr, "[DAC-TRACE] invalid %s guest range 0x%08X + %zu\n", name, va, size);
        fflush(stderr);
        abort();
    }
    host = offset + va;
    host_end = host + size;
    cursor = host;
    while (cursor < host_end) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t region;
        if (VirtualQuery((const void *)cursor, &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & readable)) {
            fprintf(stderr, "[DAC-TRACE] cannot read %s at 0x%08X: "
                            "guest memory is inaccessible or guarded\n", name, va);
            fflush(stderr);
            abort();
        }
        region = (uintptr_t)info.BaseAddress;
        if (cursor < region || info.RegionSize > UINTPTR_MAX - region ||
            cursor >= region + info.RegionSize) {
            fprintf(stderr, "[DAC-TRACE] cannot read %s at 0x%08X: invalid host region\n",
                    name, va);
            fflush(stderr);
            abort();
        }
        cursor = region + info.RegionSize;
    }
    if (!ReadProcessMemory(GetCurrentProcess(), (const void *)host, out, size, &copied) ||
        copied != size) {
        fprintf(stderr, "[DAC-TRACE] cannot read %s at 0x%08X (%zu of %zu bytes, "
                        "Windows error %lu)\n",
                name, va, (size_t)copied, size, (unsigned long)GetLastError());
        fflush(stderr);
        abort();
    }
}

void sub_0037834E_enter(void)
{
    static SRWLOCK lock = SRWLOCK_INIT;
    static uint64_t sequence;
    static const char digits[] = "0123456789abcdef";
    const char *enabled = getenv("BRUTEFORCE_DAC_TRACE");
    uint8_t ramp[BRUTEFORCE_DAC_RAMP_BYTES];
    char hex[BRUTEFORCE_DAC_RAMP_BYTES * 2u + 1u];
    uint32_t base;
    size_t i;
    int written;

    if (!enabled || !*enabled || strcmp(enabled, "0") == 0)
        return;
    if (strcmp(enabled, "1") != 0) {
        fprintf(stderr, "[DAC-TRACE] BRUTEFORCE_DAC_TRACE must be 0 or 1\n");
        fflush(stderr);
        abort();
    }

    /* This entry receives the miniport in EAX and planar RGB bytes in EDX. */
    read_dac_trace_guest(g_eax, &base, sizeof(base), "miniport");
    read_dac_trace_guest(g_edx, ramp, sizeof(ramp), "ramp");
    for (i = 0; i < sizeof(ramp); ++i) {
        hex[i * 2u] = digits[ramp[i] >> 4];
        hex[i * 2u + 1u] = digits[ramp[i] & 15u];
    }
    hex[sizeof(hex) - 1u] = '\0';

    AcquireSRWLockExclusive(&lock);
    if (sequence == UINT64_MAX) {
        fprintf(stderr, "[DAC-TRACE] sequence overflow\n");
        fflush(stderr);
        abort();
    }
    written = fprintf(stderr, "[DAC-TRACE] seq=%llu t_us=%llu miniport_va=%08X "
                              "base=%08X ramp_va=%08X planar_rgb=%s\n",
                      (unsigned long long)++sequence,
                      (unsigned long long)(xbox_HostNowNs() / 1000u),
                      g_eax, base, g_edx, hex);
    if (written < 0 || fflush(stderr) != 0)
        abort();
    ReleaseSRWLockExclusive(&lock);
}

#define CAMERA_TRACE_DEFAULT_LIMIT 256u
#define CAMERA_TRACE_MAX_LIMIT 4096u
#define CAMERA_TRACE_MAX_EVERY 1000000u
#define CAMERA_VIEW_MANAGER_GLOBAL 0x00469B54u
#define CAMERA_VIEW_RECT_GETTER 0x00067D60u
#define CAMERA_VIEW_ENABLE_GETTER 0x00067D30u

typedef struct {
    uint32_t value;
    int valid;
} CameraTraceWord;

typedef struct {
    uint64_t call, time_us;
    unsigned long thread;
    uint32_t regs[8], eflags;
    CameraTraceWord return_va, caller_va, parent_return_va, args[4];
    CameraTraceWord owner_va, owner_vtable, interface_vtable, state_va;
    CameraTraceWord fov_h, fov_v, tangent_h, tangent_v, aspect;
    CameraTraceWord view_manager, view_vtable, view_getter, view_enable_getter;
    uint32_t matrix_before[16], matrix_after[16], view_rect[4];
    int before_valid, after_valid, view_rect_valid, view_enabled_valid, view_enabled;
    const char *owner_source, *view_scope;
} CameraTraceSnapshot;

typedef struct {
    char data[4096];
    size_t used;
    int valid;
} CameraTraceLine;

static INIT_ONCE camera_trace_once = INIT_ONCE_STATIC_INIT;
static SRWLOCK camera_trace_lock = SRWLOCK_INIT;
static int camera_trace_enabled;
static uint32_t camera_trace_limit, camera_trace_every, camera_trace_reserved;
static uint32_t camera_trace_emitted;
static uint64_t camera_trace_calls;
static volatile LONG camera_trace_full;

static uint32_t camera_trace_setting(const char *name, uint32_t fallback, uint32_t maximum)
{
    const char *text = getenv(name), *p;
    uint32_t value = 0;

    if (!text)
        return fallback;
    for (p = text; *p; ++p) {
        unsigned digit = (unsigned char)*p - (unsigned)'0';
        if (digit > 9u || value > (maximum - digit) / 10u)
            break;
        value = value * 10u + digit;
    }
    if (p == text || *p || value == 0) {
        fprintf(stderr, "[CAMERA-TRACE] %s must be an integer in 1..%u\n",
                name, maximum);
        fflush(stderr);
        abort();
    }
    return value;
}

static BOOL CALLBACK initialize_camera_trace(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const char *enabled = getenv("BRUTEFORCE_CAMERA_TRACE");
    (void)once; (void)parameter; (void)context;

    if (enabled && *enabled && strcmp(enabled, "0") != 0 && strcmp(enabled, "1") != 0) {
        fprintf(stderr, "[CAMERA-TRACE] BRUTEFORCE_CAMERA_TRACE must be 0 or 1\n");
        fflush(stderr);
        abort();
    }
    camera_trace_limit = camera_trace_setting("BRUTEFORCE_CAMERA_TRACE_LIMIT",
                                              CAMERA_TRACE_DEFAULT_LIMIT,
                                              CAMERA_TRACE_MAX_LIMIT);
    camera_trace_every = camera_trace_setting("BRUTEFORCE_CAMERA_TRACE_EVERY", 1u,
                                              CAMERA_TRACE_MAX_EVERY);
    camera_trace_enabled = enabled && strcmp(enabled, "1") == 0;
    return TRUE;
}

static int camera_trace_reserve(uint64_t *call, uint64_t *time_us, unsigned long *thread)
{
    int selected = 0;
    if (InterlockedCompareExchange(&camera_trace_full, 0, 0))
        return 0;

    AcquireSRWLockExclusive(&camera_trace_lock);
    if (camera_trace_reserved < camera_trace_limit) {
        ++camera_trace_calls;
        if ((camera_trace_calls - 1u) % camera_trace_every == 0) {
            *call = camera_trace_calls;
            *time_us = xbox_HostNowNs() / 1000u;
            *thread = (unsigned long)GetCurrentThreadId();
            selected = 1;
            if (++camera_trace_reserved == camera_trace_limit)
                InterlockedExchange(&camera_trace_full, 1);
        }
    }
    ReleaseSRWLockExclusive(&camera_trace_lock);
    return selected;
}

static int camera_trace_read(uint32_t va, void *out, size_t size)
{
    const uint64_t end = (uint64_t)va + size;
    const uintptr_t offset = (uintptr_t)xbox_GetMemoryOffset();
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    uintptr_t host, host_end, cursor;
    SIZE_T copied = 0;

    if (!va || !size ||
        !(end <= XBOX_CONTIG_SIZE ||
          (va >= XBOX_CONTIG_BASE && end <= (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE)) ||
        offset > UINTPTR_MAX - va || size > UINTPTR_MAX - (offset + va))
        return 0;
    host = offset + va;
    host_end = host + size;
    for (cursor = host; cursor < host_end;) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t region;
        if (VirtualQuery((const void *)cursor, &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & readable))
            return 0;
        region = (uintptr_t)info.BaseAddress;
        if (cursor < region || info.RegionSize > UINTPTR_MAX - region ||
            cursor >= region + info.RegionSize)
            return 0;
        cursor = region + info.RegionSize;
    }
    return ReadProcessMemory(GetCurrentProcess(), (const void *)host, out, size, &copied) &&
           copied == size;
}

static CameraTraceWord camera_trace_word(uint32_t base, uint32_t offset)
{
    CameraTraceWord word = { 0, 0 };
    if (base && offset <= UINT32_MAX - base)
        word.valid = camera_trace_read(base + offset, &word.value, sizeof(word.value));
    return word;
}

static int camera_trace_read_offset(uint32_t base, uint32_t offset, void *out, size_t size)
{
    return base && offset <= UINT32_MAX - base &&
           camera_trace_read(base + offset, out, size);
}

static void camera_trace_capture(CameraTraceSnapshot *snapshot)
{
    uint32_t i;
    uint8_t view_flags;

    snapshot->regs[0] = g_eax; snapshot->regs[1] = g_ecx;
    snapshot->regs[2] = g_edx; snapshot->regs[3] = g_ebx;
    snapshot->regs[4] = g_esp; snapshot->regs[5] = g_ebp;
    snapshot->regs[6] = g_esi; snapshot->regs[7] = g_edi;
    snapshot->eflags = g_eflags;
    snapshot->return_va = camera_trace_word(snapshot->regs[4], 0);
    for (i = 0; i < 4; ++i)
        snapshot->args[i] = camera_trace_word(snapshot->regs[4], 4u + i * 4u);
    snapshot->before_valid = camera_trace_read(snapshot->regs[1], snapshot->matrix_before,
                                               sizeof(snapshot->matrix_before));

    snapshot->owner_source = "unknown";
    snapshot->view_scope = "global_context_only";
    if (snapshot->return_va.valid && snapshot->return_va.value == 0x0006BFA2u) {
        snapshot->caller_va.value = 0x0006BE90u;
        snapshot->caller_va.valid = 1;
        snapshot->parent_return_va = camera_trace_word(snapshot->regs[5], 4);
        snapshot->owner_va.value = snapshot->regs[7];
        snapshot->owner_va.valid = 1;
        snapshot->owner_source = "edi";
        snapshot->view_scope = "caller_context_candidate";
    } else if (snapshot->return_va.valid && snapshot->return_va.value == 0x0006D291u) {
        snapshot->caller_va.value = 0x0006D240u;
        snapshot->caller_va.valid = 1;
        snapshot->parent_return_va = camera_trace_word(snapshot->regs[4], 0x130u);
        if (snapshot->regs[6] >= 0x48u) {
            snapshot->owner_va.value = snapshot->regs[6] - 0x48u;
            snapshot->owner_va.valid = 1;
        }
        snapshot->owner_source = "esi_minus_0x48";
    }

    if (snapshot->owner_va.valid) {
        uint32_t owner = snapshot->owner_va.value;
        snapshot->owner_vtable = camera_trace_word(owner, 0);
        snapshot->interface_vtable = camera_trace_word(owner, 0x48u);
        snapshot->state_va = camera_trace_word(owner, 0x50u);
        if (snapshot->state_va.valid) {
            uint32_t state = snapshot->state_va.value;
            /* Static interpretation only; the camera ownership and field roles
             * still need runtime classification for each caller and scene. */
            snapshot->fov_h = camera_trace_word(state, 0x14u);
            snapshot->fov_v = camera_trace_word(state, 0x18u);
            snapshot->tangent_h = camera_trace_word(state, 0x24u);
            snapshot->tangent_v = camera_trace_word(state, 0x28u);
            snapshot->aspect = camera_trace_word(state, 0x2Cu);
        }
    }

    /* The retail 0x6BE90 caller uses this global's +0x38 virtual getter
     * after the builder. Its 0x67D60 implementation returns manager+0x24.
     * The rectangle's units and its association with other callers are
     * unverified; never apply that layout to a different getter. */
    snapshot->view_manager = camera_trace_word(CAMERA_VIEW_MANAGER_GLOBAL, 0);
    if (snapshot->view_manager.valid)
        snapshot->view_vtable = camera_trace_word(snapshot->view_manager.value, 0);
    if (snapshot->view_vtable.valid) {
        snapshot->view_getter = camera_trace_word(snapshot->view_vtable.value, 0x38u);
        snapshot->view_enable_getter = camera_trace_word(snapshot->view_vtable.value, 0x30u);
    }
    if (snapshot->view_getter.valid && snapshot->view_getter.value == CAMERA_VIEW_RECT_GETTER)
        snapshot->view_rect_valid =
            camera_trace_read_offset(snapshot->view_manager.value, 0x24u,
                                     snapshot->view_rect, sizeof(snapshot->view_rect));
    if (snapshot->view_enable_getter.valid &&
        snapshot->view_enable_getter.value == CAMERA_VIEW_ENABLE_GETTER &&
        camera_trace_read_offset(snapshot->view_manager.value, 4u, &view_flags, 1u)) {
        snapshot->view_enabled_valid = 1;
        snapshot->view_enabled = (view_flags & 2u) != 0;
    }
}

static void camera_trace_append(CameraTraceLine *line, const char *format, ...)
{
    va_list args;
    int written;
    if (!line->valid)
        return;
    va_start(args, format);
    written = vsnprintf(line->data + line->used, sizeof(line->data) - line->used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(line->data) - line->used) {
        line->valid = 0;
        return;
    }
    line->used += (size_t)written;
}

static void camera_trace_field(CameraTraceLine *line, const char *name, CameraTraceWord word)
{
    camera_trace_append(line, ",\"%s\":", name);
    if (word.valid)
        camera_trace_append(line, "\"%08X\"", word.value);
    else
        camera_trace_append(line, "null");
}

static void camera_trace_array(CameraTraceLine *line, const char *name,
                               const uint32_t *values, size_t count, int valid)
{
    size_t i;
    camera_trace_append(line, ",\"%s\":", name);
    if (!valid) {
        camera_trace_append(line, "null");
        return;
    }
    camera_trace_append(line, "[");
    for (i = 0; i < count; ++i)
        camera_trace_append(line, "%s\"%08X\"", i ? "," : "", values[i]);
    camera_trace_append(line, "]");
}

static void camera_trace_emit(const CameraTraceSnapshot *snapshot)
{
    CameraTraceLine line = { { 0 }, 0, 1 };
    uint32_t sequence;

    AcquireSRWLockExclusive(&camera_trace_lock);
    sequence = ++camera_trace_emitted;
    camera_trace_append(&line, "[CAMERA-TRACE] {\"version\":1,\"seq\":%u,"
                        "\"call\":%llu,\"t_us\":%llu,\"thread\":%lu",
                        sequence, (unsigned long long)snapshot->call,
                        (unsigned long long)snapshot->time_us, snapshot->thread);
    camera_trace_field(&line, "return_va", snapshot->return_va);
    camera_trace_field(&line, "caller_va", snapshot->caller_va);
    camera_trace_field(&line, "parent_return_va", snapshot->parent_return_va);
    camera_trace_append(&line, ",\"entry_regs\":{\"eax\":\"%08X\",\"ecx\":\"%08X\","
                        "\"edx\":\"%08X\",\"ebx\":\"%08X\",\"esp\":\"%08X\","
                        "\"ebp\":\"%08X\",\"esi\":\"%08X\",\"edi\":\"%08X\","
                        "\"eflags\":\"%08X\"}",
                        snapshot->regs[0], snapshot->regs[1], snapshot->regs[2],
                        snapshot->regs[3], snapshot->regs[4], snapshot->regs[5],
                        snapshot->regs[6], snapshot->regs[7], snapshot->eflags);
    camera_trace_append(&line, ",\"matrix_va\":\"%08X\"", snapshot->regs[1]);
    camera_trace_append(&line, ",\"args\":{");
    /* The first member has no leading comma. */
    camera_trace_append(&line, "\"fov_bits\":");
    if (snapshot->args[0].valid)
        camera_trace_append(&line, "\"%08X\"", snapshot->args[0].value);
    else
        camera_trace_append(&line, "null");
    camera_trace_field(&line, "aspect_bits", snapshot->args[1]);
    camera_trace_field(&line, "near_bits", snapshot->args[2]);
    camera_trace_field(&line, "far_bits", snapshot->args[3]);
    camera_trace_append(&line, "}");
    camera_trace_array(&line, "input_matrix_bits", snapshot->matrix_before, 16,
                       snapshot->before_valid);
    camera_trace_array(&line, "output_matrix_bits", snapshot->matrix_after, 16,
                       snapshot->after_valid);
    camera_trace_append(&line, ",\"owner\":{\"source\":\"%s\"", snapshot->owner_source);
    camera_trace_field(&line, "candidate_va", snapshot->owner_va);
    camera_trace_field(&line, "candidate_class_vtable", snapshot->owner_vtable);
    camera_trace_field(&line, "candidate_interface_vtable", snapshot->interface_vtable);
    camera_trace_field(&line, "candidate_state_va", snapshot->state_va);
    camera_trace_field(&line, "hyp_fov_h_bits", snapshot->fov_h);
    camera_trace_field(&line, "hyp_fov_v_bits", snapshot->fov_v);
    camera_trace_field(&line, "hyp_tangent_h_bits", snapshot->tangent_h);
    camera_trace_field(&line, "hyp_tangent_v_bits", snapshot->tangent_v);
    camera_trace_field(&line, "hyp_aspect_bits", snapshot->aspect);
    camera_trace_append(&line, "},\"viewport\":{\"scope\":\"%s\"", snapshot->view_scope);
    camera_trace_field(&line, "manager_va", snapshot->view_manager);
    camera_trace_field(&line, "manager_vtable", snapshot->view_vtable);
    camera_trace_field(&line, "rect_getter_va", snapshot->view_getter);
    camera_trace_field(&line, "enable_getter_va", snapshot->view_enable_getter);
    camera_trace_append(&line, ",\"enabled_candidate\":%s",
                        !snapshot->view_enabled_valid ? "null" :
                        snapshot->view_enabled ? "true" : "false");
    camera_trace_array(&line, "rect_candidate_bits", snapshot->view_rect, 4,
                       snapshot->view_rect_valid);
    camera_trace_append(&line, "}}\n");
    if (line.valid) {
        (void)fwrite(line.data, 1, line.used, stderr);
        (void)fflush(stderr);
    } else {
        fputs("[CAMERA-TRACE] internal formatting overflow\n", stderr);
        (void)fflush(stderr);
    }
    ReleaseSRWLockExclusive(&camera_trace_lock);
}

void sub_0006EE30(void)
{
    DWORD last_error = GetLastError();
    int saved_errno = errno;

    if (!InitOnceExecuteOnce(&camera_trace_once, initialize_camera_trace, NULL, NULL))
        abort();
    if (!camera_trace_enabled) {
        errno = saved_errno;
        SetLastError(last_error);
        sub_0006EE30_gen();
        return;
    }

    {
        CameraTraceSnapshot snapshot = { 0 };
        if (!camera_trace_reserve(&snapshot.call, &snapshot.time_us, &snapshot.thread)) {
            errno = saved_errno;
            SetLastError(last_error);
            sub_0006EE30_gen();
            return;
        }
        camera_trace_capture(&snapshot);
        errno = saved_errno;
        SetLastError(last_error);
        sub_0006EE30_gen();
        last_error = GetLastError();
        saved_errno = errno;
        snapshot.after_valid = camera_trace_read(snapshot.regs[1], snapshot.matrix_after,
                                                 sizeof(snapshot.matrix_after));
        camera_trace_emit(&snapshot);
        errno = saved_errno;
        SetLastError(last_error);
    }
}

static uint32_t registered_miniport;

static BOOL CALLBACK register_fence_event(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    uint32_t miniport = (uint32_t)(uintptr_t)parameter;
    uint32_t event;
    (void)once; (void)context;

    if (miniport > UINT32_MAX - BRUTEFORCE_MINIPORT_FENCE_EVENT_OFFSET)
        invalid_gpu_pointer("graphics miniport", miniport);
    event = miniport + BRUTEFORCE_MINIPORT_FENCE_EVENT_OFFSET;
    if (guest_word(event, 1) == NULL || guest_word(event + 4, 1) == NULL)
        invalid_gpu_pointer("graphics fence event", event);
    if (!xbox_KeRegisterTitleEvent(event))
        abort();
    registered_miniport = miniport;
    return TRUE;
}

static void register_fence_interrupt(uint32_t device_va)
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    volatile uint32_t *field;
    uint32_t interrupt, miniport;
    const char *walk = getenv("RECOMP_PB_WALK");

    if (!walk || strcmp(walk, "staged") != 0) {
        fprintf(stderr, "[GPU] RECOMP_PGRAPH_IRQ=1 requires RECOMP_PB_WALK=staged\n");
        abort();
    }
    if (device_va > UINT32_MAX - BRUTEFORCE_DEVICE_FENCE_EVENT_OFFSET)
        invalid_gpu_pointer("graphics device", device_va);
    interrupt = xbox_GetConnectedInterrupt(BRUTEFORCE_GPU_VECTOR);
    if (!interrupt || interrupt > UINT32_MAX - 4)
        invalid_gpu_pointer("graphics interrupt", interrupt);
    field = guest_word(interrupt + 4, 0);
    if (field == NULL || guest_word(*field, 0) == NULL)
        invalid_gpu_pointer("graphics miniport", field ? *field : interrupt);
    miniport = *field;
    if (miniport > UINT32_MAX - BRUTEFORCE_MINIPORT_FENCE_EVENT_OFFSET ||
        miniport + BRUTEFORCE_MINIPORT_FENCE_EVENT_OFFSET !=
            device_va + BRUTEFORCE_DEVICE_FENCE_EVENT_OFFSET) {
        fprintf(stderr, "[GPU] Fence event does not match the connected graphics miniport\n");
        abort();
    }
    if (!InitOnceExecuteOnce(&once, register_fence_event, (PVOID)(uintptr_t)miniport, NULL))
        abort();
    if (registered_miniport != miniport) {
        fprintf(stderr, "[GPU] Graphics miniport changed from 0x%08X to 0x%08X\n",
                registered_miniport, miniport);
        abort();
    }
}

void sub_00371AB0_enter(void)
{
    static volatile LONG registered_target;
    volatile uint32_t *global;
    volatile uint32_t *device;
    volatile uint32_t *semaphore;
    uint32_t device_va;
    uint32_t semaphore_va;
    LONG previous;

    if (getenv("RECOMP_PB_EXEC") == NULL)
        return;
    global = guest_word(BRUTEFORCE_DEVICE_GLOBAL, 0);
    if (global == NULL)
        invalid_gpu_pointer("graphics device global", BRUTEFORCE_DEVICE_GLOBAL);
    device_va = *global;
    if (device_va > UINT32_MAX - BRUTEFORCE_DEVICE_SEMAPHORE_OFFSET ||
        guest_word(device_va, 0) == NULL)
        invalid_gpu_pointer("graphics device", device_va);
    device = guest_word(device_va + BRUTEFORCE_DEVICE_SEMAPHORE_OFFSET, 0);
    if (device == NULL)
        invalid_gpu_pointer("graphics device semaphore field", device_va);
    semaphore_va = *device;
    semaphore = guest_word(semaphore_va, 1);
    if (semaphore == NULL)
        invalid_gpu_pointer("GPU semaphore", semaphore_va);

    previous = InterlockedCompareExchange(&registered_target, (LONG)semaphore_va, 0);
    if (previous != 0 && (uint32_t)previous != semaphore_va) {
        fprintf(stderr, "[GPU] Semaphore changed from 0x%08X to 0x%08X; "
                "device reset with pending releases is unsupported\n",
                (uint32_t)previous, semaphore_va);
        abort();
    }
    nv2a_pb_set_semaphore_target(semaphore_va);
    if (previous == 0)
        fprintf(stderr, "[GPU] Registered guest semaphore 0x%08X\n", semaphore_va);
    {
        const char *irq = getenv("RECOMP_PGRAPH_IRQ");
        if (irq && *irq == '1')
            register_fence_interrupt(device_va);
    }
}

static volatile uint32_t *pfb_device_global;
static volatile uint32_t *pfb_nv2a_field;
static uint32_t pfb_device_va;
static int pfb_wait_enabled;
static int pfb_idle_enabled;

static uint32_t submit_caller(uint32_t stack_offset)
{
    volatile uint32_t *word;
    if (g_esp > UINT32_MAX - stack_offset)
        return 0;
    word = guest_word(g_esp + stack_offset, 0);
    return word ? *word : 0;
}

static void store32(uint32_t address, uint32_t value, uint32_t site,
                    uint32_t stack_offset)
{
    int perf = g_bruteforce_perf_counters_enabled;
    uint32_t caller = 0;
    uint64_t started = 0;
    if (address != BRUTEFORCE_USER_DMA_PUT) {
        *(uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + address) = value;
        return;
    }
    if (perf) {
        started = bruteforce_perf_clock();
        caller = submit_caller(stack_offset);
    }
    if (xbox_Nv2aSegmentsEnabled())
        xbox_Nv2aPublishPut(value, caller ? caller : site);
    else {
        *(volatile uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + address) = value;
        if (xbox_Nv2aIdleWaitEnabled())
            xbox_Nv2aIdleWake();
    }
    if (perf)
        bruteforce_perf_record_submit(1, site, caller,
                                      bruteforce_perf_clock() - started);
}

void sub_00371FA0_store32(uint32_t address, uint32_t value)
{
    store32(address, value, 0x00371FA0u, 4);
}

void sub_00372050_store32(uint32_t address, uint32_t value)
{
    store32(address, value, 0x00372050u, 0);
}

static BOOL CALLBACK initialize_pfb_wait(PINIT_ONCE once, PVOID parameter,
                                         PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;

    pfb_wait_enabled = xbox_Nv2aPfbFlushWaitEnabled();
    pfb_idle_enabled = xbox_Nv2aIdleWaitEnabled();
    if (!pfb_wait_enabled)
        return TRUE;

    pfb_device_global = guest_word(BRUTEFORCE_DEVICE_GLOBAL, 0);
    if (pfb_device_global == NULL)
        invalid_gpu_pointer("graphics device global", BRUTEFORCE_DEVICE_GLOBAL);
    pfb_device_va = *pfb_device_global;
    if (pfb_device_va > UINT32_MAX - BRUTEFORCE_DEVICE_NV2A_OFFSET ||
        guest_word(pfb_device_va, 0) == NULL)
        invalid_gpu_pointer("graphics device", pfb_device_va);
    pfb_nv2a_field =
        guest_word(pfb_device_va + BRUTEFORCE_DEVICE_NV2A_OFFSET, 0);
    if (pfb_nv2a_field == NULL)
        invalid_gpu_pointer("graphics device NV2A field", pfb_device_va);
    return TRUE;
}

void sub_00372180(void)
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    volatile uint32_t *flush;
    LONG previous;
    int perf = g_bruteforce_perf_counters_enabled;
    uint64_t started = 0;
    uint32_t site = 0, caller = 0;

    if (perf) {
        started = bruteforce_perf_clock();
        site = submit_caller(0);
        if (site == 0x00371FBCu)
            caller = submit_caller(8);
        else if (site == 0x0037206Bu)
            caller = submit_caller(4);
    }

    if (!InitOnceExecuteOnce(&once, initialize_pfb_wait, NULL, NULL))
        abort();
    if (!pfb_wait_enabled) {
        if (pfb_idle_enabled)
            xbox_Nv2aIdleWake();
        sub_00372180_gen();
        if (perf)
            bruteforce_perf_record_submit(0, site, caller,
                                          bruteforce_perf_clock() - started);
        return;
    }
    if (*pfb_device_global != pfb_device_va) {
        fprintf(stderr, "[GPU] Graphics device changed from 0x%08X to 0x%08X "
                        "during a PFB flush wait\n",
                pfb_device_va, *pfb_device_global);
        abort();
    }

    g_eax = *pfb_nv2a_field;
    if (g_eax != BRUTEFORCE_NV2A_BASE) {
        fprintf(stderr, "[GPU] Unexpected NV2A register base 0x%08X\n", g_eax);
        abort();
    }
    flush = xbox_Nv2aRegPtr(BRUTEFORCE_PFB_FLUSH_OFFSET);
    if (flush == NULL)
        invalid_gpu_pointer("PFB flush register",
                            BRUTEFORCE_NV2A_BASE + BRUTEFORCE_PFB_FLUSH_OFFSET);

    MemoryBarrier();
    previous = InterlockedOr((volatile LONG *)flush,
                             (LONG)BRUTEFORCE_PFB_FLUSH_BUSY);
    g_ecx = (uint32_t)previous | BRUTEFORCE_PFB_FLUSH_BUSY;
    if (pfb_idle_enabled)
        xbox_Nv2aIdleWake();
    if (!xbox_Nv2aPfbFlushWait())
        abort();
    if (perf)
        bruteforce_perf_record_submit(0, site, caller,
                                      bruteforce_perf_clock() - started);
    g_esp += 4;
}

void sub_00377F80(void)
{
    const char *irq = getenv("RECOMP_PGRAPH_IRQ");
    volatile uint32_t *context, *data;
    int trapped = 0;

    if (irq && *irq == '1') {
        if (g_esp > UINT32_MAX - 8)
            invalid_gpu_pointer("software-trap stack", g_esp);
        context = guest_word(g_esp + 4, 0);
        data = guest_word(g_esp + 8, 0);
        if (context == NULL || data == NULL)
            invalid_gpu_pointer("software-trap stack", g_esp);
        trapped = xbox_Nv2aGraphAcknowledgeTrap(*context, *data);
    }
    sub_00377F80_gen();
    if (trapped)
        xbox_Nv2aGraphCompleteTrap();
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    (void)xbox_va;
    return NULL;
}

void recomp_icall_fail_log(uint32_t va)
{
    volatile uint32_t *return_address = guest_word(g_esp, 0);
    fprintf(stderr, "[ICALL] No recompiled function for guest address 0x%08X\n", va);
    fprintf(stderr, "  guest esp=%08X eax=%08X ecx=%08X edx=%08X "
            "ebx=%08X ebp=%08X esi=%08X edi=%08X\n",
            g_esp, g_eax, g_ecx, g_edx, g_ebx, g_ebp, g_esi, g_edi);
    if (return_address != NULL)
        fprintf(stderr, "  guest return=%08X\n", *return_address);
    else
        fprintf(stderr, "  guest return unavailable\n");
    abort();
}

void recomp_icall_not_code_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Guest target 0x%08X is not executable code\n", va);
    abort();
}

void recomp_unimpl(const char *text, uint32_t va)
{
    int cli = va == BRUTEFORCE_XPP_CLI && strcmp(text, "cli") == 0;
    int sti = va == BRUTEFORCE_XPP_STI && strcmp(text, "sti") == 0;
    if ((cli || sti) && getenv("RECOMP_USB") != NULL) {
        static volatile LONG announced;
        int irql = xbox_IrqlCurrent();
        /* This validated sequence contains only loads until STI and a register
         * move after it. Trapped MMIO reads do not enter IRQ safe points, so
         * the strict gate defers delivery through the STI shadow. */
        if (!xbox_IrqSafePointsStrict() ||
            (irql != DISPATCH_LEVEL && irql != 16) ||
            xbox_DispatchGateOwner() != (unsigned long)GetCurrentThreadId()) {
            fprintf(stderr, "[USB] XPP interrupt-mask instruction at 0x%08X "
                    "requires the strict gate at DISPATCH or device IRQL "
                    "(strict=%d irql=%d owner=%lu thread=%lu IF=%u)\n",
                    va, xbox_IrqSafePointsStrict(), irql,
                    xbox_DispatchGateOwner(), (unsigned long)GetCurrentThreadId(),
                    (unsigned)((g_eflags & BRUTEFORCE_EFLAGS_IF) != 0));
            abort();
        }
        if (cli) {
            if (xpp_interrupts_masked || !(g_eflags & BRUTEFORCE_EFLAGS_IF)) {
                fprintf(stderr, "[USB] Unexpected XPP CLI state at 0x%08X\n", va);
                abort();
            }
            xpp_interrupts_masked = 1;
            g_eflags &= ~BRUTEFORCE_EFLAGS_IF;
            if (!InterlockedExchange(&announced, 1))
                fprintf(stderr, "[USB] Modeling the guarded XPP CLI/STI pair\n");
        } else {
            if (!xpp_interrupts_masked || (g_eflags & BRUTEFORCE_EFLAGS_IF)) {
                fprintf(stderr, "[USB] Unexpected XPP STI state at 0x%08X\n", va);
                abort();
            }
            g_eflags |= BRUTEFORCE_EFLAGS_IF;
            xpp_interrupts_masked = 0;
        }
        return;
    }
    fprintf(stderr, "[UNIMPL] Guest instruction at 0x%08X: %s\n", va, text);
    abort();
}
