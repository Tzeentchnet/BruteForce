#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel.h"
#include "xbox_memory_layout.h"
#include "../src/crash_diagnostics.h"

RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_ebp, g_esi, g_edi;
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo, g_xbox_code_hi;

static LONG WINAPI previous_exception_filter(PEXCEPTION_POINTERS exception)
{
    (void)exception;
    fprintf(stderr, "[TEST] previous exception filter called\n");
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char **argv)
{
    uint8_t *memory = NULL;
    ULONG_PTR parameters[2] = {0, 0x1234u};

    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if (argc > 1 && strcmp(argv[1], "--zero-offset") == 0) {
        uintptr_t address;
        for (address = 0x10000u; address + 0x2000u < XBOX_CONTIG_SIZE;
             address += 0x10000u) {
            memory = VirtualAlloc((void *)address, 0x2000u, MEM_RESERVE | MEM_COMMIT,
                                  PAGE_READWRITE);
            if (memory != NULL)
                break;
        }
        g_esp = (uint32_t)(uintptr_t)memory;
        g_xbox_mem_offset = 0;
    } else {
        memory = VirtualAlloc(NULL, 0x2000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        g_xbox_mem_offset = (ptrdiff_t)memory;
        g_esp = 0x1000u;
    }
    if (memory == NULL) {
        fprintf(stderr, "[TEST] Cannot allocate the guest stack\n");
        return 2;
    }
    g_eax = 0xABCDEF12u;
    g_xbox_code_lo = 0x2000u;
    g_xbox_code_hi = 0x3000u;
    *(uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp) = 0x2345u;

    SetUnhandledExceptionFilter(previous_exception_filter);
    bruteforce_install_crash_diagnostics();
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 2, parameters);
    return 3;
}
