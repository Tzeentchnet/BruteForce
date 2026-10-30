#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "xbox_memory_layout.h"

#define DEVICE_GLOBAL 0x0037DD88u
#define DEVICE_A 0x0037D000u
#define DEVICE_B 0x0037D100u
#define SEMAPHORE_A 0x80001000u
#define SEMAPHORE_B 0x80002000u
#define LOW_SEMAPHORE 0x0037D200u
#define GUEST_RETURN 0x0037D300u
#define INTERRUPT 0x0037D400u
#define MINIPORT (DEVICE_A + 0x17C8u)
#define FENCE_EVENT (DEVICE_A + 0x196Cu)
#define THREADS 8
#define ITERATIONS 200

RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_ebp, g_esi, g_edi;
RECOMP_TLS uint32_t g_eflags;
static uint8_t *guest;
static volatile LONG target;
static volatile LONG calls;
static volatile LONG event_calls, acknowledged, completed, generated;
static uint32_t connected_interrupt;
static int acknowledge_result;

void sub_00371AB0_enter(void);
void sub_00377F80(void);
void recomp_icall_fail_log(uint32_t va);

ptrdiff_t xbox_GetMemoryOffset(void)
{
    return (ptrdiff_t)guest;
}

void nv2a_pb_set_semaphore_target(uint32_t va)
{
    InterlockedExchange(&target, (LONG)va);
    InterlockedIncrement(&calls);
}

int xbox_IrqSafePointsStrict(void)
{
    return 0;
}

int xbox_IrqlCurrent(void)
{
    return 0;
}

unsigned long xbox_DispatchGateOwner(void)
{
    return 0;
}

uint32_t xbox_GetConnectedInterrupt(uint32_t vector)
{
    return vector == 3 ? connected_interrupt : 0;
}

int xbox_KeRegisterTitleEvent(uint32_t va)
{
    if (va != FENCE_EVENT)
        abort();
    InterlockedIncrement(&event_calls);
    return 1;
}

int xbox_Nv2aGraphAcknowledgeTrap(uint32_t context, uint32_t data)
{
    if (context != MINIPORT || data != 5)
        abort();
    InterlockedIncrement(&acknowledged);
    return acknowledge_result;
}

void xbox_Nv2aGraphCompleteTrap(void)
{
    if (generated != 1)
        abort();
    InterlockedIncrement(&completed);
}

void sub_00377F80_gen(void)
{
    if (completed)
        abort();
    InterlockedIncrement(&generated);
    g_esp += 12;
}

static void put32(uint32_t va, uint32_t value)
{
    *(uint32_t *)(guest + va) = value;
}

static int initialize(void)
{
    SIZE_T size = (SIZE_T)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE;
    guest = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    if (guest == NULL ||
        VirtualAlloc(guest + 0x00370000u, 0x10000u, MEM_COMMIT, PAGE_READWRITE) == NULL ||
        VirtualAlloc(guest + XBOX_CONTIG_BASE, 0x10000u, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        fprintf(stderr, "Cannot allocate synthetic guest memory (Windows error %lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }
    put32(DEVICE_GLOBAL, DEVICE_A);
    put32(DEVICE_A + 0x30u, SEMAPHORE_A);
    put32(DEVICE_B + 0x30u, SEMAPHORE_A);
    return 1;
}

static DWORD WINAPI register_many(LPVOID unused)
{
    int i;
    (void)unused;
    for (i = 0; i < ITERATIONS; ++i)
        sub_00371AB0_enter();
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "";
    HANDLE threads[THREADS];
    DWORD old_protection;
    int i;

    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (!initialize() || _putenv_s("RECOMP_PB_EXEC", "") != 0 ||
        _putenv_s("RECOMP_PGRAPH_IRQ", "") != 0)
        return 2;
    if (strcmp(mode, "trap-disabled") == 0) {
        g_esp = GUEST_RETURN;
        sub_00377F80();
        return generated != 1 || acknowledged || completed || g_esp != GUEST_RETURN + 12;
    }
    if (!strncmp(mode, "trap-", 5)) {
        if (_putenv_s("RECOMP_PGRAPH_IRQ", "1") != 0)
            return 2;
        put32(GUEST_RETURN + 4, MINIPORT);
        put32(GUEST_RETURN + 8, 5);
        g_esp = strcmp(mode, "trap-bad-stack") == 0 ? 0x01000000u : GUEST_RETURN;
        acknowledge_result = strcmp(mode, "trap-matched") == 0;
        sub_00377F80();
        if (strcmp(mode, "trap-bad-stack") == 0)
            return 2;
        return generated != 1 || acknowledged != 1 ||
               completed != acknowledge_result || g_esp != GUEST_RETURN + 12;
    }
    if (strcmp(mode, "disabled") == 0) {
        put32(DEVICE_GLOBAL, 0);
        sub_00371AB0_enter();
        return calls != 0;
    }
    if (_putenv_s("RECOMP_PB_EXEC", "1") != 0)
        return 2;

    if (!strncmp(mode, "event-", 6)) {
        if (_putenv_s("RECOMP_PGRAPH_IRQ", "1") != 0 ||
            _putenv_s("RECOMP_PB_WALK",
                      strcmp(mode, "event-wrong-walk") == 0 ? "" : "staged") != 0)
            return 2;
        connected_interrupt = strcmp(mode, "event-no-interrupt") == 0 ? 0 : INTERRUPT;
        put32(INTERRUPT + 4, strcmp(mode, "event-wrong-miniport") == 0
                              ? MINIPORT + 4 : MINIPORT);
        if (strcmp(mode, "event-concurrent") == 0) {
            for (i = 0; i < THREADS; ++i) {
                threads[i] = CreateThread(NULL, 0, register_many, NULL, 0, NULL);
                if (!threads[i]) return 2;
            }
            if (WaitForMultipleObjects(THREADS, threads, TRUE, 10000) != WAIT_OBJECT_0)
                return 2;
            for (i = 0; i < THREADS; ++i) CloseHandle(threads[i]);
            return event_calls != 1 || calls != THREADS * ITERATIONS;
        }
        sub_00371AB0_enter();
        if (strcmp(mode, "event-valid") == 0)
            return event_calls != 1;
        fprintf(stderr, "Expected event test mode to abort\n");
        return 2;
    }

    if (strcmp(mode, "valid") == 0) {
        sub_00371AB0_enter();
        put32(DEVICE_GLOBAL, DEVICE_B);
        sub_00371AB0_enter();
        return (uint32_t)target != SEMAPHORE_A || calls != 2;
    }
    if (strcmp(mode, "low-semaphore") == 0) {
        put32(DEVICE_A + 0x30u, LOW_SEMAPHORE);
        sub_00371AB0_enter();
        return (uint32_t)target != LOW_SEMAPHORE || calls != 1;
    }
    if (strcmp(mode, "concurrent") == 0) {
        for (i = 0; i < THREADS; ++i) {
            threads[i] = CreateThread(NULL, 0, register_many, NULL, 0, NULL);
            if (threads[i] == NULL)
                return 2;
        }
        if (WaitForMultipleObjects(THREADS, threads, TRUE, 10000) != WAIT_OBJECT_0)
            return 2;
        for (i = 0; i < THREADS; ++i)
            CloseHandle(threads[i]);
        return (uint32_t)target != SEMAPHORE_A || calls != THREADS * ITERATIONS;
    }
    if (strcmp(mode, "null-device") == 0)
        put32(DEVICE_GLOBAL, 0);
    else if (strcmp(mode, "unaligned-device") == 0)
        put32(DEVICE_GLOBAL, DEVICE_A + 1);
    else if (strcmp(mode, "overflow-device") == 0)
        put32(DEVICE_GLOBAL, 0xFFFFFFF0u);
    else if (strcmp(mode, "unmapped-device") == 0)
        put32(DEVICE_GLOBAL, 0x01000000u);
    else if (strcmp(mode, "null-semaphore") == 0)
        put32(DEVICE_A + 0x30u, 0);
    else if (strcmp(mode, "unaligned-semaphore") == 0)
        put32(DEVICE_A + 0x30u, SEMAPHORE_A + 2);
    else if (strcmp(mode, "outside-semaphore") == 0)
        put32(DEVICE_A + 0x30u, XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE);
    else if (strcmp(mode, "unmapped-semaphore") == 0)
        put32(DEVICE_A + 0x30u, XBOX_CONTIG_BASE + 0x10000u);
    else if (strcmp(mode, "readonly-semaphore") == 0) {
        if (!VirtualProtect(guest + XBOX_CONTIG_BASE, 0x10000u, PAGE_READONLY, &old_protection))
            return 2;
    } else if (strcmp(mode, "changed-semaphore") == 0) {
        sub_00371AB0_enter();
        put32(DEVICE_A + 0x30u, SEMAPHORE_B);
    } else if (strcmp(mode, "icall") == 0) {
        put32(GUEST_RETURN, 0x00112233u);
        g_esp = GUEST_RETURN;
        g_eax = 0x55667788u;
        recomp_icall_fail_log(0x12345678u);
    } else if (strcmp(mode, "icall-bad-stack") == 0) {
        g_esp = 0x01000000u;
        recomp_icall_fail_log(0x12345678u);
    } else {
        fprintf(stderr, "Unknown test mode: %s\n", mode);
        return 2;
    }
    sub_00371AB0_enter();
    fprintf(stderr, "Expected the test mode to abort\n");
    return 2;
}
