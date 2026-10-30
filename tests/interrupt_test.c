#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"
#include "xbox_memory_layout.h"
#include "recomp_types.h"

#define CLI_VA 0x00384FA5u
#define STI_VA 0x00384FB6u
#define IF_BIT 0x00000200u

typedef void (*recomp_func_t)(void);

static volatile LONG irq_count, shadow_done, irq_too_early;
static int post_result;

void recomp_unimpl(const char *text, uint32_t va);
void sub_00377F80_gen(void) {}

recomp_func_t recomp_lookup(uint32_t va)
{
    (void)va;
    return NULL;
}

static void on_irq(void)
{
    if (!InterlockedCompareExchange(&shadow_done, 0, 0))
        InterlockedExchange(&irq_too_early, 1);
    InterlockedIncrement(&irq_count);
}

static DWORD WINAPI post_irq(LPVOID unused)
{
    (void)unused;
    post_result = xbox_IrqPost(XBOX_IRQ_OHCI);
    if (post_result == XBOX_IRQ_GATED)
        xbox_DispatchGateLeave();
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "";
    uint32_t flags;
    void *guest;
    int saved;
    HANDLE thread;

    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    g_fs_base = 0;
    guest = VirtualAlloc(NULL, XBOX_WORKER_STACK_END, MEM_RESERVE | MEM_COMMIT,
                         PAGE_READWRITE);
    if (!guest)
        return 2;
    g_xbox_mem_offset = (ptrdiff_t)guest;
    if (_putenv_s("RECOMP_USB", "1") != 0 ||
        _putenv_s("RECOMP_DISPATCH_GATE", "1") != 0 ||
        _putenv_s("RECOMP_IRQ_SAFE_POINTS", "1") != 0 ||
        _putenv_s("RECOMP_IRQ_SAFE_FORCE", "") != 0)
        return 2;
    if (strcmp(mode, "disabled") == 0)
        _putenv_s("RECOMP_USB", "");
    else if (strcmp(mode, "gate-off") == 0)
        _putenv_s("RECOMP_DISPATCH_GATE", "0");
    else if (strcmp(mode, "force-on") == 0)
        _putenv_s("RECOMP_IRQ_SAFE_FORCE", "1");
    else if (strcmp(mode, "safe-points-off") == 0)
        _putenv_s("RECOMP_IRQ_SAFE_POINTS", "0");

    if (strcmp(mode, "wrong-owner") == 0) {
        xbox_IrqlEnterInterrupt(16);
        recomp_unimpl("cli", CLI_VA);
        return 2;
    }
    xbox_DispatchGateEnter();
    if (strcmp(mode, "wrong-irql") == 0) {
        recomp_unimpl("cli", CLI_VA);
        return 2;
    }
    saved = xbox_IrqlEnterInterrupt(
        strcmp(mode, "valid-dispatch") == 0 ? DISPATCH_LEVEL : 16);
    recomp_popfd(0x00000ED7u);
    flags = recomp_pushfd();

    if (strcmp(mode, "sti-unpaired") == 0) {
        recomp_unimpl("sti", STI_VA);
    } else if (strcmp(mode, "pre-masked") == 0) {
        g_eflags &= ~IF_BIT;
        recomp_unimpl("cli", CLI_VA);
    } else if (strcmp(mode, "other-address") == 0) {
        recomp_unimpl("cli", CLI_VA + 1);
    } else if (strcmp(mode, "other-opcode") == 0) {
        recomp_unimpl("nop", CLI_VA);
    } else {
        recomp_unimpl("cli", CLI_VA);
        if (strcmp(mode, "duplicate-cli") == 0)
            recomp_unimpl("cli", CLI_VA);
        if (strcmp(mode, "sti-flags-changed") == 0) {
            g_eflags |= IF_BIT;
            recomp_unimpl("sti", STI_VA);
        }
        if (strcmp(mode, "valid-dispatch") != 0 &&
            strcmp(mode, "valid-device") != 0)
            return 2;
        if (recomp_pushfd() != (flags & ~IF_BIT))
            return 1;
        xbox_IrqSetHandler(XBOX_IRQ_OHCI, on_irq);
        thread = CreateThread(NULL, 0, post_irq, NULL, 0, NULL);
        if (!thread || WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0)
            return 2;
        CloseHandle(thread);
        if (post_result != XBOX_IRQ_POSTED || irq_count != 0)
            return 1;
        if (strcmp(mode, "valid-device") == 0) {
            xbox_IrqSafePoint();
            if (irq_count != 0)
                return 1;
        }
        recomp_unimpl("sti", STI_VA);
        if (recomp_pushfd() != flags || irq_count != 0)
            return 1;
        g_eax = 0x12345678u;
        g_esi = g_eax;
        InterlockedExchange(&shadow_done, 1);
        xbox_IrqSafePoint();
        if (irq_count != (strcmp(mode, "valid-dispatch") == 0 ? 1 : 0) ||
            g_esi != 0x12345678u)
            return 1;
        xbox_IrqlLeaveInterrupt(saved);
        xbox_DispatchGateLeave();
        xbox_IrqSetHandler(XBOX_IRQ_OHCI, NULL);
        if (irq_count != 1 || irq_too_early != 0)
            return 1;
        return !VirtualFree(guest, 0, MEM_RELEASE);
    }
    fprintf(stderr, "Expected interrupt test mode to abort\n");
    return 2;
}
