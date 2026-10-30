#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

ptrdiff_t usb_test_memory_offset(void);
size_t usb_test_mapped_size(void);
uint32_t usb_test_contiguous_bytes(void);
uint32_t usb_test_alloc_tib(void);
void usb_test_free_tib(uint32_t va);
BOOL WINAPI usb_test_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old);
HANDLE WINAPI usb_test_create_thread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack_size,
                                     LPTHREAD_START_ROUTINE start, LPVOID argument,
                                     DWORD flags, LPDWORD id);
DWORD WINAPI usb_test_wait(HANDLE handle, DWORD timeout);

#define OHCI_SHUTDOWN_WAIT_MS 150u
#define xbox_GetMemoryOffset usb_test_memory_offset
#define xbox_GetMappedSize usb_test_mapped_size
#define xbox_ContiguousAllocatedBytes usb_test_contiguous_bytes
#define xbox_AllocThreadTib usb_test_alloc_tib
#define xbox_HeapFree usb_test_free_tib
#define VirtualProtect usb_test_protect
#define CreateThread usb_test_create_thread
#define WaitForSingleObject usb_test_wait
#include "usb/ohci.c"
#undef xbox_GetMemoryOffset
#undef xbox_GetMappedSize
#undef xbox_ContiguousAllocatedBytes
#undef xbox_AllocThreadTib
#undef xbox_HeapFree
#undef VirtualProtect
#undef CreateThread
#undef WaitForSingleObject

typedef void (*recomp_func_t)(void);

static uint8_t *guest;
static int tib_available = 1;
static int tib_allocations, tib_frees;
static int protect_calls, fail_protect_call, fail_restore_call;
static int fail_thread;
static int fail_wait;
static HANDLE gate_held, gate_release;
static volatile LONG callback_count;

extern RECOMP_TLS uint32_t g_fs_base;

ptrdiff_t usb_test_memory_offset(void)
{
    return (ptrdiff_t)guest;
}

size_t usb_test_mapped_size(void)
{
    return OHCI_CONTIG_SIZE;
}

uint32_t usb_test_contiguous_bytes(void)
{
    return 0x10000u;
}

uint32_t usb_test_alloc_tib(void)
{
    if (!tib_available)
        return 0;
    ++tib_allocations;
    return 0x4000u;
}

void usb_test_free_tib(uint32_t va)
{
    if (va == 0x4000u)
        ++tib_frees;
}

BOOL WINAPI usb_test_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old)
{
    ++protect_calls;
    if (protect_calls == fail_protect_call || protect_calls == fail_restore_call) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return VirtualProtect(address, size, protection, old);
}

HANDLE WINAPI usb_test_create_thread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack_size,
                                     LPTHREAD_START_ROUTINE start, LPVOID argument,
                                     DWORD flags, LPDWORD id)
{
    if (fail_thread) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    return CreateThread(attributes, stack_size, start, argument, flags, id);
}

DWORD WINAPI usb_test_wait(HANDLE handle, DWORD timeout)
{
    if (fail_wait) {
        fail_wait = 0;
        return WAIT_TIMEOUT;
    }
    return WaitForSingleObject(handle, timeout);
}

recomp_func_t recomp_lookup(uint32_t va)
{
    (void)va;
    return NULL;
}

recomp_func_t recomp_lookup_manual(uint32_t va)
{
    (void)va;
    return NULL;
}

static LONG CALLBACK on_fault(PEXCEPTION_POINTERS exception)
{
    uintptr_t fault;
    uint64_t va;

    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    fault = (uintptr_t)exception->ExceptionRecord->ExceptionInformation[1];
    if (fault < (uintptr_t)guest)
        return EXCEPTION_CONTINUE_SEARCH;
    va = (uint64_t)(fault - (uintptr_t)guest);
    if (va <= UINT32_MAX && xbox_OhciOwnsAddress((uint32_t)va) &&
        xbox_OhciHandleMmio(exception->ContextRecord, (uint32_t)va))
        return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

static int setup_guest(void)
{
    guest = VirtualAlloc(NULL, (SIZE_T)0x100000000ull, MEM_RESERVE, PAGE_NOACCESS);
    if (!guest ||
        !VirtualAlloc(guest + XBOX_OHCI0_BASE, XBOX_OHCI_SIZE, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(guest + XBOX_OHCI1_BASE, XBOX_OHCI_SIZE, MEM_COMMIT, PAGE_READWRITE)) {
        fprintf(stderr, "Cannot allocate synthetic USB apertures (error %lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }
    return 1;
}

static int page_is(uint32_t address, DWORD protection)
{
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(guest + address, &info, sizeof info) == sizeof info &&
           info.State == MEM_COMMIT && info.Protect == protection;
}

static int restored(void)
{
    return !xbox_OhciOwnsAddress(XBOX_OHCI0_BASE) &&
           !xbox_OhciOwnsAddress(XBOX_OHCI1_BASE) &&
           page_is(XBOX_OHCI0_BASE, PAGE_READWRITE) &&
           page_is(XBOX_OHCI1_BASE, PAGE_READWRITE) &&
           tib_allocations == tib_frees;
}

static void on_irq(void)
{
    InterlockedIncrement(&callback_count);
}

static DWORD WINAPI hold_gate(LPVOID unused)
{
    (void)unused;
    g_fs_base = 0;
    xbox_DispatchGateEnter();
    SetEvent(gate_held);
    if (WaitForSingleObject(gate_release, 5000) != WAIT_OBJECT_0)
        ExitProcess(3);
    xbox_DispatchGateLeave();
    return 0;
}

static HANDLE start_gate_holder(void)
{
    HANDLE thread;
    gate_held = CreateEventA(NULL, TRUE, FALSE, NULL);
    gate_release = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!gate_held || !gate_release)
        return NULL;
    thread = CreateThread(NULL, 0, hold_gate, NULL, 0, NULL);
    if (!thread || WaitForSingleObject(gate_held, 5000) != WAIT_OBJECT_0)
        return NULL;
    return thread;
}

static int release_gate_holder(HANDLE thread)
{
    int ok = SetEvent(gate_release) &&
             WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0;
    CloseHandle(thread);
    CloseHandle(gate_held);
    CloseHandle(gate_release);
    return ok;
}

static void set_td(uint32_t td, uint32_t direction, uint32_t buffer, uint32_t length)
{
    wr32(td, direction << 19);
    wr32(td + 4, length ? buffer : 0);
    wr32(td + 8, 0);
    wr32(td + 12, length ? buffer + length - 1 : 0);
}

static int enumeration_report(void)
{
    const uint32_t td = 0x5000u, setup_va = 0x1100u, data_va = 0x1200u;
    const uint32_t report_va = 0x2BBu;
    uint8_t *setup, *data, *report;
    static const uint8_t get_device[] = {0x80, 0x06, 0, 1, 0, 0, 8, 0};
    static const uint8_t set_address[] = {0, 0x05, 1, 0, 0, 0, 0, 0};
    static const uint8_t set_config[] = {0, 0x09, 1, 0, 0, 0, 0, 0};

    if (!VirtualAlloc(guest + OHCI_CONTIG_BASE, 0x10000u, MEM_COMMIT, PAGE_READWRITE) ||
        _putenv_s("RECOMP_PAD_SCRIPT", "0:start+a:10000") != 0)
        return 1;
    setup = guest_ptr(setup_va, 8);
    data = guest_ptr(data_va, 8);
    report = guest_ptr(report_va - 1, 22);
    if (!setup || !data || !report)
        return 1;
    s_plugged_pads = 1;
    memcpy(setup, get_device, sizeof get_device);
    set_td(td, TD_DP_SETUP, setup_va, 8);
    if (ohci_do_td(&s_hc[0], 0, td) != TD_CC_NOERROR || rd32(td + 4) != 0)
        return 1;
    set_td(td, TD_DP_IN, data_va, 8);
    if (ohci_do_td(&s_hc[0], 0, td) != TD_CC_NOERROR ||
        rd32(td + 4) != 0 || data[0] != 18 || data[1] != 1)
        return 1;
    set_td(td, TD_DP_OUT, 0, 0);
    if (ohci_do_td(&s_hc[0], 0, td) != TD_CC_NOERROR)
        return 1;

    memcpy(setup, set_address, sizeof set_address);
    set_td(td, TD_DP_SETUP, setup_va, 8);
    if (ohci_do_td(&s_hc[0], 0, td) != TD_CC_NOERROR)
        return 1;
    set_td(td, TD_DP_IN, 0, 0);
    if (ohci_do_td(&s_hc[0], 0, td) != TD_CC_NOERROR || usb_gamepad_address(0) != 1)
        return 1;
    memcpy(setup, set_config, sizeof set_config);
    set_td(td, TD_DP_SETUP, setup_va, 8);
    if (ohci_do_td(&s_hc[0], 1, td) != TD_CC_NOERROR)
        return 1;
    set_td(td, TD_DP_IN, 0, 0);
    if (ohci_do_td(&s_hc[0], 1, td) != TD_CC_NOERROR || !usb_gamepad_configured(0))
        return 1;

    memset(report, 0xA5, 22);
    set_td(td, TD_DP_IN, report_va, 20);
    if (ohci_do_td(&s_hc[0], 1 | (1u << 7), td) != TD_CC_NOERROR ||
        rd32(td + 4) != 0 || s_n_report != 1 ||
        report[0] != 0xA5 || report[1] != 0 || report[2] != 20 ||
        !(report[3] & 0x10) || report[5] != 0xFF || report[21] != 0xA5)
        return 1;
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "";
    PVOID handler = NULL;
    HANDLE holder;
    int result = 1;

    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    g_fs_base = 0;
    if (_putenv_s("RECOMP_USB", "1") != 0 ||
        _putenv_s("RECOMP_DISPATCH_GATE", "1") != 0 ||
        _putenv_s("RECOMP_IRQ_SAFE_POINTS", "1") != 0 ||
        _putenv_s("RECOMP_IRQ_SAFE_FORCE", "") != 0)
        return 2;
    if (strcmp(mode, "disabled") == 0) {
        _putenv_s("RECOMP_USB", "");
        return xbox_OhciInit() != 0 || !xbox_OhciShutdown() || protect_calls != 0;
    }
    if (strcmp(mode, "gate-off") == 0 || strcmp(mode, "safe-points-off") == 0 ||
        strcmp(mode, "force-on") == 0) {
        if (strcmp(mode, "gate-off") == 0)
            _putenv_s("RECOMP_DISPATCH_GATE", "0");
        else if (strcmp(mode, "safe-points-off") == 0)
            _putenv_s("RECOMP_IRQ_SAFE_POINTS", "0");
        else
            _putenv_s("RECOMP_IRQ_SAFE_FORCE", "1");
        return xbox_OhciInit() != -1 || protect_calls != 0 || tib_allocations != 0;
    }
    if (strcmp(mode, "missing-memory") == 0)
        return xbox_OhciInit() != -1 || protect_calls != 0 || tib_allocations != 0;
    if (!setup_guest())
        return 2;
    handler = AddVectoredExceptionHandler(1, on_fault);
    if (!handler)
        return 2;

    if (strcmp(mode, "enumeration-report") == 0) {
        result = enumeration_report();
    } else if (strcmp(mode, "missing-tib") == 0) {
        tib_available = 0;
        result = xbox_OhciInit() != -1 || protect_calls != 0 || !restored();
    } else if (strcmp(mode, "protect-first") == 0 ||
               strcmp(mode, "protect-second") == 0) {
        fail_protect_call = strcmp(mode, "protect-first") == 0 ? 1 : 2;
        result = xbox_OhciInit() != -1 || !restored() || !xbox_OhciShutdown();
    } else if (strcmp(mode, "rollback-retry") == 0) {
        fail_protect_call = 2;
        fail_restore_call = 3;
        if (xbox_OhciInit() == -1 && xbox_OhciOwnsAddress(XBOX_OHCI0_BASE) &&
            page_is(XBOX_OHCI0_BASE, PAGE_NOACCESS)) {
            fail_protect_call = fail_restore_call = 0;
            result = !xbox_OhciShutdown() || !restored();
        }
    } else if (strcmp(mode, "thread-retry") == 0) {
        fail_thread = 1;
        if (xbox_OhciInit() == -1 && restored()) {
            fail_thread = 0;
            result = xbox_OhciInit() != 1 || !xbox_OhciShutdown() || !restored();
        }
    } else if (strcmp(mode, "lifecycle") == 0 || strcmp(mode, "late-start") == 0) {
        volatile uint32_t *hc0 = (volatile uint32_t *)(guest + XBOX_OHCI0_BASE);
        volatile uint32_t *hc1 = (volatile uint32_t *)(guest + XBOX_OHCI1_BASE);
        uint32_t value;
        if (xbox_OhciInit() != 1 || xbox_OhciInit() != 1 ||
            hc0[0] != 0x10u || hc1[0] != 0x10u)
            goto cleanup;
        hc0[HcInterruptEnable / 4] = INTR_SF;
        value = hc0[HcInterruptEnable / 4];
        if (!(value & INTR_SF))
            goto cleanup;
        hc0[HcInterruptDisable / 4] = INTR_SF;
        if (hc0[HcInterruptEnable / 4] & INTR_SF)
            goto cleanup;
        if (strcmp(mode, "late-start") == 0) {
            Sleep(6500);
            if (WaitForSingleObject(s_worker, 0) != WAIT_TIMEOUT)
                goto cleanup;
            hc0[HcControl / 4] = 0x80u;
            hc0[HcInterruptEnable / 4] = INTR_MIE | INTR_RHSC;
            Sleep(100);
            if (!(hc0[(HcRhPortStatus1 + 8) / 4] & PORT_CCS))
                goto cleanup;
        }
        xbox_IrqSetHandler(XBOX_IRQ_OHCI, on_irq);
        if (!xbox_OhciShutdown() || !restored() || !xbox_OhciShutdown() ||
            xbox_OhciInit() != -1)
            goto cleanup;
        holder = start_gate_holder();
        if (!holder)
            goto cleanup;
        if (xbox_IrqPost(XBOX_IRQ_OHCI) != XBOX_IRQ_POSTED ||
            !release_gate_holder(holder) || callback_count != 0)
            goto cleanup;
        result = 0;
    } else if (strcmp(mode, "gate-timeout") == 0) {
        if (xbox_OhciInit() != 1)
            goto cleanup;
        holder = start_gate_holder();
        if (!holder)
            goto cleanup;
        if (xbox_OhciShutdown() || !xbox_OhciOwnsAddress(XBOX_OHCI0_BASE) ||
            !page_is(XBOX_OHCI0_BASE, PAGE_NOACCESS) ||
            !release_gate_holder(holder))
            goto cleanup;
        result = !xbox_OhciShutdown() || !restored();
    } else if (strcmp(mode, "gate-owned") == 0) {
        if (xbox_OhciInit() != 1)
            goto cleanup;
        xbox_DispatchGateEnter();
        if (xbox_OhciShutdown() || !xbox_OhciOwnsAddress(XBOX_OHCI0_BASE)) {
            xbox_DispatchGateLeave();
            goto cleanup;
        }
        xbox_DispatchGateLeave();
        result = !xbox_OhciShutdown() || !restored();
    } else if (strcmp(mode, "worker-timeout") == 0) {
        if (xbox_OhciInit() != 1)
            goto cleanup;
        fail_wait = 1;
        if (xbox_OhciShutdown() || !xbox_OhciOwnsAddress(XBOX_OHCI0_BASE) ||
            !page_is(XBOX_OHCI0_BASE, PAGE_NOACCESS))
            goto cleanup;
        result = !xbox_OhciShutdown() || !restored();
    } else if (strcmp(mode, "restore-retry") == 0) {
        if (xbox_OhciInit() != 1)
            goto cleanup;
        fail_restore_call = protect_calls + 1;
        if (xbox_OhciShutdown() || !xbox_OhciOwnsAddress(XBOX_OHCI1_BASE) ||
            !page_is(XBOX_OHCI1_BASE, PAGE_NOACCESS))
            goto cleanup;
        fail_restore_call = 0;
        result = !xbox_OhciShutdown() || !restored();
    } else if (strcmp(mode, "worker-failure") == 0) {
        volatile uint32_t *hc0 = (volatile uint32_t *)(guest + XBOX_OHCI0_BASE);
        if (xbox_OhciInit() != 1)
            goto cleanup;
        hc0[HcControl / 4] = 0x80u;
        hc0[HcInterruptEnable / 4] = INTR_MIE | INTR_RHSC;
        if (WaitForSingleObject(s_worker, 5000) != WAIT_OBJECT_0 ||
            xbox_OhciInit() != -1 || xbox_OhciShutdown() || !restored())
            goto cleanup;
        result = 0;
    } else {
        fprintf(stderr, "Unknown USB test mode: %s\n", mode);
        result = 2;
    }

cleanup:
    if (s_worker || s_protected[0] || s_protected[1]) {
        if (!xbox_OhciShutdown())
            ExitProcess(3);
    }
    if (!RemoveVectoredExceptionHandler(handler))
        result = 2;
    if (guest && !VirtualFree(guest, 0, MEM_RELEASE))
        result = 2;
    if (result)
        fprintf(stderr, "USB fixture failed in mode %s\n", mode);
    return result;
}
