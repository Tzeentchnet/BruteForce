#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "xbox_memory_layout.h"

typedef void (*recomp_func_t)(void);
extern recomp_func_t recomp_lookup_kernel(uint32_t va);
extern RECOMP_TLS uint32_t g_eax, g_esp;
extern volatile uint32_t *xbox_Nv2aRegPtr(uint32_t offset);

#define ISR_VA 0x00012000u
#define ISR_CONTEXT 0x00123450u
#define GRAPH_INTR 0x400100u
#define GRAPH_ENABLE 0x400140u
#define GRAPH_SOURCE 0x400108u
#define GRAPH_ADDRESS 0x400704u
#define GRAPH_DATA 0x400708u
#define PMC_INTR 0x000100u
#define PMC_ENABLE 0x000140u
#define PMC_GRAPH (1u << 12)
#define OTHER_GRAPH 0x40u

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

enum { S_CONNECT, S_SET, S_WAIT, S_RESET, N_SLOTS };
static const uint32_t ordinals[N_SLOTS] = { 98, 145, 159, 138 };
static uint32_t slots[N_SLOTS];
static uint32_t scratch, interrupt_va, event_va, wait_status;
static HANDLE acknowledged_event, complete_event, waiter_ready;
static volatile LONG isr_count, isr_failed, other_preserved, trap_result;
static int decline_first, bad_ack;
static const uint32_t trap_data = 0xA3u;

static uint32_t *guest(uint32_t va)
{
    return (uint32_t *)((uintptr_t)va + xbox_GetMemoryOffset());
}

static volatile uint32_t *reg(uint32_t offset)
{
    volatile uint32_t *p = xbox_Nv2aRegPtr(offset);
    if (!p) {
        fprintf(stderr, "FAIL: missing NV2A register %08X\n", offset);
        abort();
    }
    return p;
}

static uint32_t bridge_call(int slot, uint32_t stack, int nargs, const uint32_t *args)
{
    uint32_t *sp = guest(stack);
    recomp_func_t bridge = recomp_lookup_kernel(slots[slot]);
    int i;
    if (!bridge)
        abort();
    sp[0] = 0xBEEF0001u;
    for (i = 0; i < nargs; ++i)
        sp[i + 1] = args[i];
    g_esp = stack;
    g_eax = 0xDEADBEEFu;
    bridge();
    if (g_esp != stack + 4u + (uint32_t)nargs * 4u) {
        fprintf(stderr, "FAIL: bridge stack cleanup\n");
        abort();
    }
    return g_eax;
}

static uint32_t wait_event(uint32_t event, uint32_t stack, uint32_t timeout)
{
    uint32_t args[5] = {event, 0, 0, 0, timeout};
    return bridge_call(S_WAIT, stack, 5, args);
}

static uint32_t set_event(uint32_t event, uint32_t stack)
{
    uint32_t args[3] = {event, 0, 0};
    return bridge_call(S_SET, stack, 3, args);
}

static void build_event(uint32_t event, int type)
{
    uint32_t *p = guest(event);
    p[0] = (uint32_t)type | (4u << 16);
    p[1] = 0;
    p[2] = p[3] = event + 8;
}

static void fake_isr(void)
{
    uint32_t stack = g_esp;
    uint32_t context = *guest(stack + 8);
    LONG count = InterlockedIncrement(&isr_count);

    if (*guest(stack + 4) != interrupt_va || context != ISR_CONTEXT ||
        !(*reg(GRAPH_INTR) & 1u) || !(*reg(PMC_INTR) & PMC_GRAPH) ||
        *reg(GRAPH_SOURCE) != 1 || *reg(GRAPH_ADDRESS) != 0x100u ||
        *reg(GRAPH_DATA) != trap_data)
        InterlockedExchange(&isr_failed, 1);
    if (decline_first && count == 1) {
        g_esp = stack + 12;
        g_eax = 0;
        return;
    }
    InterlockedOr((volatile LONG *)reg(GRAPH_INTR), OTHER_GRAPH);
    InterlockedOr((volatile LONG *)reg(GRAPH_ENABLE), OTHER_GRAPH);
    if (!xbox_Nv2aGraphAcknowledgeTrap(context + (bad_ack ? 4u : 0u), trap_data))
        InterlockedExchange(&isr_failed, 1);
    if ((*reg(GRAPH_INTR) & OTHER_GRAPH) && (*reg(PMC_INTR) & PMC_GRAPH))
        InterlockedExchange(&other_preserved, 1);
    if (!xbox_Nv2aGraphTrapPending())
        InterlockedExchange(&isr_failed, 1);
    set_event(event_va, stack - 16);
    SetEvent(acknowledged_event);
    if (WaitForSingleObject(complete_event, 5000) != WAIT_OBJECT_0) {
        fprintf(stderr, "FAIL: test did not release simulated handler\n");
        abort();
    }
    *guest(scratch + 0x7000) = 0xAABBCCDDu;
    xbox_Nv2aGraphCompleteTrap();
    g_esp = stack + 12;
    g_eax = 1;
}

recomp_func_t recomp_lookup(uint32_t va)
{
    return va == ISR_VA ? fake_isr : NULL;
}

recomp_func_t recomp_lookup_manual(uint32_t va)
{
    (void)va;
    return NULL;
}

static DWORD WINAPI trap_thread(LPVOID unused)
{
    (void)unused;
    InterlockedExchange(&trap_result, xbox_Nv2aGraphSoftwareTrap(0, 0x100, trap_data));
    return 0;
}

static DWORD WINAPI event_waiter(LPVOID unused)
{
    (void)unused;
    SetEvent(waiter_ready);
    wait_status = wait_event(event_va, scratch + 0x3000, scratch + 0x100);
    return 0;
}

static int initialize(const char *path, int connect, int callable, int tls)
{
    FILE *f = fopen(path, "rb");
    unsigned char *image;
    long length;
    int i;
    uint32_t args[1];
    uint32_t tls_directory = 0x00010800u;

    if (!f || fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "FAIL: cannot open synthetic XBE\n");
        return 0;
    }
    image = (unsigned char *)malloc((size_t)length);
    if (!image || fread(image, 1, (size_t)length, f) != (size_t)length || fclose(f) != 0)
        return 0;
    if (length < 0x814)
        return 0;
    if (tls) {
        memcpy(image + 0x12C, &tls_directory, sizeof(tls_directory));
        memset(image + 0x800, 0, 20);
    }
    if (!xbox_MemoryLayoutInit(image, (size_t)length))
        return 0;
    free(image);
    scratch = xbox_HeapAlloc(0x10000, 4096);
    if (!scratch)
        return 0;
    for (i = 0; i < N_SLOTS; ++i)
        guest(scratch)[i] = 0x80000000u | ordinals[i];
    xbox_kernel_set_thunk_address(scratch, N_SLOTS);
    xbox_kernel_bridge_init();
    for (i = 0; i < N_SLOTS; ++i) {
        slots[i] = guest(scratch)[i];
        if (!recomp_lookup_kernel(slots[i]))
            return 0;
    }
    interrupt_va = scratch + 0x4000;
    guest(interrupt_va)[0] = callable ? ISR_VA : 0;
    guest(interrupt_va)[1] = ISR_CONTEXT;
    guest(interrupt_va)[2] = 3;
    if (connect) {
        args[0] = interrupt_va;
        if (bridge_call(S_CONNECT, scratch + 0x1000, 1, args) != 1)
            return 0;
    }
    event_va = scratch + 0x5000;
    build_event(event_va, 1);
    guest(scratch + 0x100)[0] = (uint32_t)-30000000LL;
    guest(scratch + 0x100)[1] = (uint32_t)((uint64_t)-30000000LL >> 32);
    guest(scratch + 0x110)[0] = guest(scratch + 0x110)[1] = 0;
    acknowledged_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    complete_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    waiter_ready = CreateEventA(NULL, FALSE, FALSE, NULL);
    return acknowledged_event && complete_event && waiter_ready;
}

int main(int argc, char **argv)
{
    const char *mode;
    HANDLE gpu, waiter;
    DWORD old_protection;
    uint32_t unrelated, notification, args[1];
    int i, loops;

    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (argc != 3)
        return 2;
    mode = argv[2];
    if (_putenv_s("RECOMP_PGRAPH_IRQ", strcmp(mode, "disabled") ? "1" : "") ||
        _putenv_s("RECOMP_DISPATCH_GATE", strcmp(mode, "no-gate") ? "1" : "0") ||
        _putenv_s("RECOMP_IRQ_SAFE_POINTS", "1") ||
        _putenv_s("RECOMP_IRQ_SAFE_FORCE", "") ||
        _putenv_s("RECOMP_TITLE_KEVENTS", "") ||
        _putenv_s("RECOMP_PB_EXEC", "") ||
        _putenv_s("RECOMP_VBLANK", ""))
        return 2;
    if (!strcmp(mode, "disabled"))
        return xbox_Nv2aGraphSoftwareTrap(0, 0x100, trap_data) != 0;
    REQUIRE(initialize(argv[1], strcmp(mode, "no-isr") != 0,
                       strcmp(mode, "uncallable-isr") != 0,
                       strcmp(mode, "no-tib") != 0),
            "initialize synthetic guest and interrupt");
    *reg(GRAPH_ENABLE) = !strcmp(mode, "masked-graph") ? 0 : 1;
    *reg(PMC_ENABLE) = !strcmp(mode, "masked-pmc") ? 0 : 1;
    if (!strcmp(mode, "invalid-trap"))
        return xbox_Nv2aGraphSoftwareTrap(0, 0x104, trap_data);
    if (!strcmp(mode, "invalid-completion")) {
        xbox_Nv2aGraphCompleteTrap();
        return 2;
    }
    if (!strcmp(mode, "no-isr") || !strcmp(mode, "no-gate") || !strcmp(mode, "no-tib")) {
        xbox_Nv2aGraphSoftwareTrap(0, 0x100, trap_data);
        return 2;
    }

    REQUIRE(xbox_KeRegisterTitleEvent(event_va), "register synchronization event");
    REQUIRE(wait_event(event_va, scratch + 0x1000, scratch + 0x110) == STATUS_TIMEOUT,
            "registered synchronization event begins unset");
    unrelated = scratch + 0x5100;
    build_event(unrelated, 1);
    REQUIRE(wait_event(unrelated, scratch + 0x1000, scratch + 0x110) != STATUS_TIMEOUT,
            "registration does not enable unrelated title events");
    REQUIRE(!xbox_KeRegisterTitleEvent(0x03FFFFFCu), "reject event crossing RAM boundary");
    REQUIRE(VirtualProtect(guest(scratch + 0xA000), 4096, PAGE_NOACCESS, &old_protection),
            "protect synthetic event page");
    REQUIRE(!xbox_KeRegisterTitleEvent(scratch + 0xA000), "reject inaccessible event");
    REQUIRE(wait_event(scratch + 0xA000, scratch + 0x1000, scratch + 0x110) != STATUS_TIMEOUT,
            "unrelated inaccessible event does not fault");
    REQUIRE(VirtualProtect(guest(scratch + 0xA000), 4096, old_protection, &old_protection),
            "restore synthetic event page");

    notification = scratch + 0x5200;
    build_event(notification, 0);
    REQUIRE(xbox_KeRegisterTitleEvent(notification), "register notification event");
    set_event(notification, scratch + 0x1000);
    REQUIRE(wait_event(notification, scratch + 0x1000, scratch + 0x110) == 0 &&
            wait_event(notification, scratch + 0x1000, scratch + 0x110) == 0,
            "notification event remains signaled");
    args[0] = notification;
    bridge_call(S_RESET, scratch + 0x1000, 1, args);
    REQUIRE(wait_event(notification, scratch + 0x1000, scratch + 0x110) == STATUS_TIMEOUT,
            "notification reset clears the event");

    decline_first = !strcmp(mode, "declined");
    bad_ack = !strcmp(mode, "bad-ack");
    loops = !strcmp(mode, "stress") ? 20 : 1;
    for (i = 0; i < loops; ++i) {
        *guest(scratch + 0x7000) = 0;
        waiter = CreateThread(NULL, 0, event_waiter, NULL, 0, NULL);
        REQUIRE(waiter && WaitForSingleObject(waiter_ready, 5000) == WAIT_OBJECT_0,
                "start a waiter for the registered fence event");
        REQUIRE(WaitForSingleObject(waiter, 20) == WAIT_TIMEOUT, "fence waiter blocks");
        gpu = CreateThread(NULL, 0, trap_thread, NULL, 0, NULL);
        REQUIRE(gpu, "start GPU software trap");
        if (!strcmp(mode, "masked-graph") || !strcmp(mode, "masked-pmc")) {
            REQUIRE(WaitForSingleObject(acknowledged_event, 100) == WAIT_TIMEOUT &&
                    isr_count == 0 && xbox_Nv2aGraphTrapPending(),
                    "masked interrupt remains pending");
            if (!strcmp(mode, "masked-graph"))
                *reg(GRAPH_ENABLE) = 1;
            else
                *reg(PMC_ENABLE) = 1;
        }
        if (!strcmp(mode, "nested")) {
            while (!xbox_Nv2aGraphTrapPending()) Sleep(1);
            xbox_Nv2aGraphSoftwareTrap(0, 0x100, trap_data);
            return 2;
        }
        REQUIRE(WaitForSingleObject(acknowledged_event, 5000) == WAIT_OBJECT_0,
                "guest acknowledges the trap");
        REQUIRE(WaitForSingleObject(waiter, 5000) == WAIT_OBJECT_0 && wait_status == 0,
                "guest method signals and wakes the fence waiter");
        REQUIRE(WaitForSingleObject(gpu, 25) == WAIT_TIMEOUT &&
                xbox_Nv2aGraphTrapPending() && *guest(scratch + 0x7000) == 0,
                "GPU remains stalled until guest handler completes");
        REQUIRE(other_preserved && !isr_failed, "other enabled PGRAPH status remains pending");
        SetEvent(complete_event);
        REQUIRE(WaitForSingleObject(gpu, 5000) == WAIT_OBJECT_0 && trap_result == 1 &&
                *guest(scratch + 0x7000) == 0xAABBCCDDu &&
                !xbox_Nv2aGraphTrapPending(),
                "GPU resumes only after guest memory changes");
        CloseHandle(gpu);
        CloseHandle(waiter);
    }
    REQUIRE(!decline_first || isr_count >= 2, "declined interrupt is retried");
    puts("PGRAPH interrupt and event tests passed");
    return 0;
}
