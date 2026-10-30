#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "nv2a_backend.h"
#include "xbox_memory_layout.h"

#define BRUTEFORCE_DEVICE_GLOBAL 0x0037DD88u
#define BRUTEFORCE_DEVICE_SEMAPHORE_OFFSET 0x30u
#define BRUTEFORCE_DEVICE_FENCE_EVENT_OFFSET 0x196Cu
#define BRUTEFORCE_MINIPORT_FENCE_EVENT_OFFSET 0x1A4u
#define BRUTEFORCE_GPU_VECTOR 3u
#define BRUTEFORCE_XPP_CLI 0x00384FA5u
#define BRUTEFORCE_XPP_STI 0x00384FB6u
#define BRUTEFORCE_EFLAGS_IF 0x00000200u

typedef void (*recomp_func_t)(void);
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
    uintptr_t host;
    uintptr_t region;

    if (!va || (va & 3u) ||
        !(end <= XBOX_CONTIG_SIZE ||
          (va >= XBOX_CONTIG_BASE && end <= (uint64_t)XBOX_CONTIG_BASE + XBOX_CONTIG_SIZE)) ||
        offset > UINTPTR_MAX - va)
        return NULL;
    host = offset + va;
    if (VirtualQuery((const void *)host, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (writable ? writeable : readable)))
        return NULL;
    region = (uintptr_t)info.BaseAddress;
    if (host < region || info.RegionSize < sizeof(uint32_t) ||
        host - region > info.RegionSize - sizeof(uint32_t))
        return NULL;
    return (volatile uint32_t *)host;
}

static void invalid_gpu_pointer(const char *name, uint32_t va)
{
    fprintf(stderr, "[GPU] Invalid %s guest address 0x%08X\n", name, va);
    abort();
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
