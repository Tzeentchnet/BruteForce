#include <windows.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "xbox_memory_layout.h"
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"

#define LOW_BASE 0x00370000u
#define GLOBAL_BASE 0x00460000u
#define HIGH_BASE 0x80000000u
#define REGION_SIZE 0x10000u
#define OWNER 0x00371000u
#define OWNER_VTABLE 0x00372000u
#define INTERFACE_VTABLE 0x00372100u
#define VIEW_VTABLE 0x00373000u
#define STATE 0x00374000u
#define MANAGER 0x00375000u
#define MATRIX 0x00376000u
#define STACK 0x00377000u
#define FRAME 0x00377800u
#define GUARD 0x0037B000u
#define MANAGER_GLOBAL 0x00469B54u
#define TARGET 0x0006EE30u
#define THREADS 12u
#define THREAD_ITERATIONS 10u

RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_ebp, g_esi, g_edi, g_eflags;
RECOMP_TLS uint32_t g_fs_base, g_seh_ebp, g_mxcsr;
RECOMP_TLS int g_df, g_fp_top, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word, g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3;
RECOMP_TLS RecompXmm g_xmm4, g_xmm5, g_xmm6, g_xmm7;
RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3;
RECOMP_TLS RecompMmx g_mm4, g_mm5, g_mm6, g_mm7;
volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_trace_idx;
volatile uint64_t g_icall_count;
uint32_t g_xbox_code_lo = 0x00010000u, g_xbox_code_hi = 0x00300000u;
ptrdiff_t g_xbox_mem_offset;

static uint8_t *guest;
static uint8_t expected_low[REGION_SIZE], expected_global[REGION_SIZE];
static uint8_t expected_high[REGION_SIZE];
static volatile LONG generated_calls;
static int write_stack, write_matrix;
static RECOMP_TLS int incoming_errno;
static RECOMP_TLS DWORD incoming_last_error;
static RECOMP_TLS unsigned incoming_host_mxcsr;

void sub_0006EE30(void);
void sub_00372180_gen(void) {}
void sub_00377F80_gen(void) {}
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)guest; }
void nv2a_pb_set_semaphore_target(uint32_t value) { (void)value; }
int xbox_IrqSafePointsStrict(void) { return 0; }
int xbox_IrqlCurrent(void) { return 0; }
unsigned long xbox_DispatchGateOwner(void) { return 0; }
uint32_t xbox_GetConnectedInterrupt(uint32_t value) { (void)value; return 0; }
int xbox_KeRegisterTitleEvent(uint32_t value) { (void)value; return 0; }
int xbox_Nv2aGraphAcknowledgeTrap(uint32_t a, uint32_t b) { (void)a; (void)b; return 0; }
void xbox_Nv2aGraphCompleteTrap(void) {}
int xbox_Nv2aPfbFlushWaitEnabled(void) { return 0; }
int xbox_Nv2aPfbFlushWait(void) { return 0; }
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t value) { (void)value; return NULL; }
recomp_func_t recomp_lookup_kernel(uint32_t value) { (void)value; return NULL; }

static void put32(uint32_t va, uint32_t value)
{
    memcpy(guest + va, &value, sizeof(value));
}

void sub_0006EE30_gen(void)
{
    unsigned i;
    incoming_last_error = GetLastError();
    incoming_errno = errno;
    incoming_host_mxcsr = _mm_getcsr();
    InterlockedIncrement(&generated_calls);
    if (write_matrix)
        for (i = 0; i < 16; ++i)
            put32(g_ecx + i * 4u, 0x40000000u + i);
    if (write_stack)
        put32(g_esp + 0x18u, 0xA1B2C3D4u);

    g_eax = g_ecx;
    g_ecx ^= 0x01234567u;
    g_edx ^= 0x76543210u;
    g_ebx ^= 0x13579BDFu;
    g_esp += 20u;
    g_ebp ^= 0x2468ACE0u;
    g_esi ^= 0x13572468u;
    g_edi ^= 0xFEDCBA98u;
    g_eflags ^= 0x40u;
    g_seh_ebp ^= 0x10203040u;
    g_df ^= 1;
    g_fp_stack[2] = 123.5;
    g_fp_top = (g_fp_top + 3) & 7;
    g_fp_cmp = -1;
    g_fp_cc ^= 0x400u;
    g_fp_control_word ^= 0x400u;
    g_mxcsr ^= 0x2000u;
    g_xmm0.u[0] ^= 0xCAFEBABEu;
    g_xmm7.u[3] ^= 0x12345678u;
    g_mm0.q ^= 0x0102030405060708ull;
    g_mm7.q ^= 0x8877665544332211ull;
    _mm_setcsr(0x3FA0u);
    errno = EILSEQ;
    SetLastError(0xA1B2C3D4u);
}

typedef struct {
    uint32_t regs[8], flags, seh, fs, mxcsr;
    int direction, fp_top, fp_cmp, error;
    uint16_t fp_control, fp_cc;
    double fp_stack[8];
    RecompXmm xmm_state[8];
    RecompMmx mm_state[8];
    int observed_incoming_errno;
    DWORD last_error, observed_incoming_last_error;
    unsigned host_mxcsr, observed_incoming_host_mxcsr;
} GuestState;

static void capture_state(GuestState *state)
{
    DWORD last_error = GetLastError();
    int saved_errno = errno;
    unsigned host_mxcsr = _mm_getcsr();
    memset(state, 0, sizeof(*state));
    state->regs[0] = g_eax; state->regs[1] = g_ecx;
    state->regs[2] = g_edx; state->regs[3] = g_ebx;
    state->regs[4] = g_esp; state->regs[5] = g_ebp;
    state->regs[6] = g_esi; state->regs[7] = g_edi;
    state->flags = g_eflags;
    state->seh = g_seh_ebp;
    state->fs = g_fs_base;
    state->mxcsr = g_mxcsr;
    state->direction = g_df;
    state->fp_top = g_fp_top;
    state->fp_cmp = g_fp_cmp;
    state->fp_control = g_fp_control_word;
    state->fp_cc = g_fp_cc;
    memcpy(state->fp_stack, g_fp_stack, sizeof(state->fp_stack));
    state->xmm_state[0] = g_xmm0; state->xmm_state[1] = g_xmm1;
    state->xmm_state[2] = g_xmm2; state->xmm_state[3] = g_xmm3;
    state->xmm_state[4] = g_xmm4; state->xmm_state[5] = g_xmm5;
    state->xmm_state[6] = g_xmm6; state->xmm_state[7] = g_xmm7;
    state->mm_state[0] = g_mm0; state->mm_state[1] = g_mm1;
    state->mm_state[2] = g_mm2; state->mm_state[3] = g_mm3;
    state->mm_state[4] = g_mm4; state->mm_state[5] = g_mm5;
    state->mm_state[6] = g_mm6; state->mm_state[7] = g_mm7;
    state->error = saved_errno;
    state->last_error = last_error;
    state->host_mxcsr = host_mxcsr;
    state->observed_incoming_errno = incoming_errno;
    state->observed_incoming_last_error = incoming_last_error;
    state->observed_incoming_host_mxcsr = incoming_host_mxcsr;
}

static void initialize_registers(uint32_t stack_va, uint32_t matrix_va, uint32_t frame_va)
{
    RecompXmm *xmm_state[] = { &g_xmm0, &g_xmm1, &g_xmm2, &g_xmm3,
                               &g_xmm4, &g_xmm5, &g_xmm6, &g_xmm7 };
    RecompMmx *mm_state[] = { &g_mm0, &g_mm1, &g_mm2, &g_mm3,
                             &g_mm4, &g_mm5, &g_mm6, &g_mm7 };
    unsigned i, j;
    g_eax = 0x11111111u; g_ecx = matrix_va; g_edx = 0x22222222u;
    g_ebx = 0x33333333u; g_esp = stack_va; g_ebp = frame_va;
    g_esi = OWNER + 0x48u; g_edi = OWNER; g_eflags = 0x246u;
    g_fs_base = 0x55550000u; g_seh_ebp = frame_va;
    g_df = 0; g_fp_top = 5; g_fp_cmp = 1;
    g_fp_control_word = 0x037Fu; g_fp_cc = 0x4000u; g_mxcsr = 0x1F80u;
    for (i = 0; i < 8; ++i) {
        g_fp_stack[i] = 100.0 + i;
        mm_state[i]->q = 0x0101010101010101ull * (i + 1u);
        for (j = 0; j < 4; ++j)
            xmm_state[i]->u[j] = 0x11110000u + i * 4u + j;
    }
}

static void initialize_stack_and_matrix(uint32_t stack_va, uint32_t matrix_va,
                                        uint32_t frame_va, uint32_t return_va)
{
    unsigned i;
    put32(stack_va, return_va);
    put32(stack_va + 4u, 0x42700000u);
    put32(stack_va + 8u, 0x3FAAAAABu);
    put32(stack_va + 12u, 0x3F800000u);
    put32(stack_va + 16u, 0x447A0000u);
    put32(stack_va + 0x130u, 0x00223344u);
    put32(frame_va + 4u, 0x00112233u);
    for (i = 0; i < 16; ++i)
        put32(matrix_va + i * 4u, 0x3F000000u + i);
}

static void initialize_common(void)
{
    memset(guest + LOW_BASE, 0xCC, REGION_SIZE);
    memset(guest + GLOBAL_BASE, 0, REGION_SIZE);
    memset(guest + HIGH_BASE, 0xDD, REGION_SIZE);
    put32(OWNER, OWNER_VTABLE);
    put32(OWNER + 0x48u, INTERFACE_VTABLE);
    put32(OWNER + 0x50u, STATE);
    put32(STATE + 0x14u, 0x41F00000u);
    put32(STATE + 0x18u, 0x41C00000u);
    put32(STATE + 0x24u, 0x3F000000u);
    put32(STATE + 0x28u, 0x3EC00000u);
    put32(STATE + 0x2Cu, 0x3FAAAAABu);
    put32(MANAGER_GLOBAL, MANAGER);
    put32(MANAGER, VIEW_VTABLE);
    put32(MANAGER + 4u, 2u);
    put32(VIEW_VTABLE + 0x30u, 0x00067D30u);
    put32(VIEW_VTABLE + 0x38u, 0x00067D60u);
    put32(MANAGER + 0x24u, 0u);
    put32(MANAGER + 0x28u, 0u);
    put32(MANAGER + 0x2Cu, 0x3F800000u);
    put32(MANAGER + 0x30u, 0x3F800000u);
    write_stack = write_matrix = 1;
}

static int has_guard(const char *mode)
{
    return strstr(mode, "guard") != NULL || strcmp(mode, "other-getter") == 0;
}

static void prepare_case(const char *mode)
{
    uint32_t stack_va = STACK, matrix_va = MATRIX;
    uint32_t return_va = strcmp(mode, "indirect-d") == 0 ? 0x0006D291u :
                         strcmp(mode, "unknown") == 0 ? 0x00123456u : 0x0006BFA2u;
    initialize_common();
    initialize_stack_and_matrix(STACK, MATRIX, FRAME, return_va);
    if (strcmp(mode, "high") == 0) {
        unsigned i;
        matrix_va = HIGH_BASE + 0x1000u;
        for (i = 0; i < 16; ++i)
            put32(matrix_va + i * 4u, 0x3F000000u + i);
        put32(OWNER + 0x50u, HIGH_BASE + 0x2000u);
        put32(HIGH_BASE + 0x2014u, 0x41F00000u);
        put32(HIGH_BASE + 0x2018u, 0x41C00000u);
        put32(HIGH_BASE + 0x2024u, 0x3F000000u);
        put32(HIGH_BASE + 0x2028u, 0x3EC00000u);
        put32(HIGH_BASE + 0x202Cu, 0x3FAAAAABu);
    } else if (strcmp(mode, "partial-stack") == 0) {
        stack_va = LOW_BASE + REGION_SIZE - 4u;
        put32(stack_va, return_va);
        write_stack = 0;
    } else if (strcmp(mode, "special-floats") == 0) {
        put32(STACK + 4u, 0x7FC12345u);
        put32(STACK + 8u, 0xFF800000u);
        put32(MATRIX, 0x7FC54321u);
        put32(STATE + 0x24u, 0x7F800000u);
    } else if (strcmp(mode, "wrap-stack") == 0) {
        stack_va = 0xFFFFFFFCu;
        write_stack = 0;
    } else if (strcmp(mode, "unmapped-matrix") == 0) {
        matrix_va = 0x01000000u;
        write_matrix = 0;
    } else if (strcmp(mode, "partial-matrix") == 0) {
        matrix_va = LOW_BASE + REGION_SIZE - 16u;
        write_matrix = 0;
    } else if (strcmp(mode, "outside-matrix") == 0) {
        matrix_va = HIGH_BASE + REGION_SIZE * 1024u - 32u;
        write_matrix = 0;
    } else if (strcmp(mode, "guard-stack") == 0) {
        stack_va = GUARD;
        write_stack = 0;
    } else if (strcmp(mode, "guard-matrix") == 0) {
        matrix_va = GUARD;
        write_matrix = 0;
    } else if (strcmp(mode, "guard-both") == 0) {
        stack_va = matrix_va = GUARD;
        write_stack = write_matrix = 0;
    } else if (strcmp(mode, "guard-state") == 0) {
        put32(OWNER + 0x50u, GUARD);
    } else if (strcmp(mode, "partial-state") == 0) {
        uint32_t state = LOW_BASE + REGION_SIZE - 0x28u;
        put32(OWNER + 0x50u, state);
        put32(state + 0x14u, 0x41F00000u);
        put32(state + 0x18u, 0x41C00000u);
        put32(state + 0x24u, 0x3F000000u);
    } else if (strcmp(mode, "guard-viewport") == 0 ||
               strcmp(mode, "other-getter") == 0) {
        uint32_t manager = GUARD - 0x24u;
        put32(MANAGER_GLOBAL, manager);
        put32(manager, VIEW_VTABLE);
        put32(manager + 4u, 2u);
        if (strcmp(mode, "other-getter") == 0)
            put32(VIEW_VTABLE + 0x38u, 0x00012345u);
    }
    initialize_registers(stack_va, matrix_va, FRAME);
    if (strcmp(mode, "invalid-owner") == 0)
        g_edi = 0xFFFFFFF0u;
}

static int initialize(void)
{
    SIZE_T size = (SIZE_T)HIGH_BASE + XBOX_CONTIG_SIZE;
    guest = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    if (!guest ||
        !VirtualAlloc(guest + LOW_BASE, REGION_SIZE, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(guest + GLOBAL_BASE, REGION_SIZE, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(guest + HIGH_BASE, REGION_SIZE, MEM_COMMIT, PAGE_READWRITE)) {
        fprintf(stderr, "Cannot allocate synthetic guest memory: %lu\n",
                (unsigned long)GetLastError());
        return 0;
    }
    g_xbox_mem_offset = (ptrdiff_t)guest;
    return 1;
}

static int run_cases(const char *mode)
{
    GuestState expected, actual;
    unsigned i, iterations = strcmp(mode, "many-default") == 0 ? 300u :
                             strcmp(mode, "many") == 0 ? 12u :
                             (strcmp(mode, "after-cap-guard") == 0 ||
                              strcmp(mode, "sample-gap-guard") == 0) ? 2u : 1u;
    for (i = 0; i < iterations; ++i) {
        const char *test_mode = (strcmp(mode, "after-cap-guard") == 0 ||
                                 strcmp(mode, "sample-gap-guard") == 0) ?
                                (i == 0 ? "direct-be" : "guard-both") : mode;
        DWORD old_protection;
        MEMORY_BASIC_INFORMATION info;
        prepare_case(test_mode);
        errno = EDOM;
        SetLastError(0x13572468u);
        _mm_setcsr(0x1FA0u);
        sub_0006EE30_gen();
        capture_state(&expected);
        memcpy(expected_low, guest + LOW_BASE, REGION_SIZE);
        memcpy(expected_global, guest + GLOBAL_BASE, REGION_SIZE);
        memcpy(expected_high, guest + HIGH_BASE, REGION_SIZE);

        prepare_case(test_mode);
        if (has_guard(test_mode) &&
            !VirtualProtect(guest + GUARD, 0x1000u, PAGE_READWRITE | PAGE_GUARD,
                            &old_protection))
            return 2;
        errno = EDOM;
        SetLastError(0x13572468u);
        _mm_setcsr(0x1FA0u);
        if (strcmp(test_mode, "indirect-d") == 0) {
            if (recomp_lookup(TARGET) != sub_0006EE30)
                return 2;
            RECOMP_ICALL_SAFE(TARGET, g_esp + 20u);
        } else {
            RECOMP_ABI_CALL(TARGET, sub_0006EE30);
        }
        capture_state(&actual);
        if (has_guard(test_mode)) {
            if (VirtualQuery(guest + GUARD, &info, sizeof(info)) != sizeof(info) ||
                !(info.Protect & PAGE_GUARD) ||
                !VirtualProtect(guest + GUARD, 0x1000u, PAGE_READWRITE, &old_protection)) {
                fprintf(stderr, "Diagnostic touched a guarded page\n");
                return 1;
            }
        }
        if (memcmp(&actual, &expected, sizeof(actual)) != 0 ||
            memcmp(expected_low, guest + LOW_BASE, REGION_SIZE) != 0 ||
            memcmp(expected_global, guest + GLOBAL_BASE, REGION_SIZE) != 0 ||
            memcmp(expected_high, guest + HIGH_BASE, REGION_SIZE) != 0) {
            fprintf(stderr, "Wrapper changed generated guest or host state in %s\n", test_mode);
            return 1;
        }
    }
    if (generated_calls != (LONG)(2u * iterations)) {
        fprintf(stderr, "Generated body was not called exactly once per invocation\n");
        return 1;
    }
    return 0;
}

typedef struct {
    uint32_t stack_va, matrix_va, frame_va;
    HANDLE start;
} ThreadContext;

static DWORD WINAPI run_thread(LPVOID parameter)
{
    ThreadContext *context = (ThreadContext *)parameter;
    unsigned i, j;
    if (WaitForSingleObject(context->start, 5000) != WAIT_OBJECT_0)
        return 2;
    for (i = 0; i < THREAD_ITERATIONS; ++i) {
        for (j = 0; j < 16; ++j)
            put32(context->matrix_va + j * 4u, 0x3F000000u + j);
        initialize_registers(context->stack_va, context->matrix_va, context->frame_va);
        RECOMP_ABI_CALL(TARGET, sub_0006EE30);
        if (g_eax != context->matrix_va || g_esp != context->stack_va + 20u ||
            g_fp_stack[2] != 123.5 || g_xmm0.u[0] != (0x11110000u ^ 0xCAFEBABEu))
            return 1;
    }
    return 0;
}

static int run_concurrent(void)
{
    HANDLE threads[THREADS], start;
    ThreadContext contexts[THREADS];
    unsigned i;
    initialize_common();
    start = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!start)
        return 2;
    for (i = 0; i < THREADS; ++i) {
        contexts[i].stack_va = 0x00378000u + i * 0x180u;
        contexts[i].frame_va = contexts[i].stack_va + 0x80u;
        contexts[i].matrix_va = 0x0037A000u + i * 0x40u;
        contexts[i].start = start;
        initialize_stack_and_matrix(contexts[i].stack_va, contexts[i].matrix_va,
                                    contexts[i].frame_va, 0x0006BFA2u);
        threads[i] = CreateThread(NULL, 0, run_thread, &contexts[i], 0, NULL);
        if (!threads[i])
            return 2;
    }
    SetEvent(start);
    if (WaitForMultipleObjects(THREADS, threads, TRUE, 10000) != WAIT_OBJECT_0)
        return 2;
    for (i = 0; i < THREADS; ++i) {
        DWORD result;
        if (!GetExitCodeThread(threads[i], &result) || result != 0 ||
            !CloseHandle(threads[i]))
            return 1;
    }
    CloseHandle(start);
    return generated_calls != (LONG)(THREADS * THREAD_ITERATIONS);
}

int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "direct-be";
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (!initialize())
        return 2;
    return strcmp(mode, "concurrent") == 0 ? run_concurrent() : run_cases(mode);
}
