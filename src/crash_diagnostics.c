#include <windows.h>
#include <dbghelp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "kernel.h"
#include "xbox_memory_layout.h"

#include "crash_diagnostics.h"

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_ebx, g_ebp, g_esi, g_edi;
extern ptrdiff_t g_xbox_mem_offset;

static LPTOP_LEVEL_EXCEPTION_FILTER previous_filter;
static int symbols_ready;
static volatile LONG reporting;

static void print_native_stack(const CONTEXT *original)
{
    HANDLE process = GetCurrentProcess();
    CONTEXT context = *original;
    STACKFRAME64 frame;
    DWORD64 last_pc = 0;
    DWORD64 last_sp = 0;
    unsigned int i;

    if (!symbols_ready)
        return;
    memset(&frame, 0, sizeof(frame));
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    fprintf(stderr, "  native stack:\n");
    for (i = 0; i < 16 && frame.AddrPC.Offset != 0; ++i) {
        union {
            SYMBOL_INFO symbol;
            char bytes[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        } storage;
        DWORD64 displacement = 0;
        DWORD line_displacement = 0;
        IMAGEHLP_LINE64 line;

        if (frame.AddrPC.Offset != last_pc || frame.AddrStack.Offset != last_sp) {
            memset(&storage, 0, sizeof(storage));
            storage.symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
            storage.symbol.MaxNameLen = MAX_SYM_NAME;
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, &storage.symbol))
                fprintf(stderr, "    0x%llX %s+0x%llX",
                        (unsigned long long)frame.AddrPC.Offset, storage.symbol.Name,
                        (unsigned long long)displacement);
            else
                fprintf(stderr, "    0x%llX", (unsigned long long)frame.AddrPC.Offset);
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line))
                fprintf(stderr, " (%s:%lu)", line.FileName, (unsigned long)line.LineNumber);
            fputc('\n', stderr);
            last_pc = frame.AddrPC.Offset;
            last_sp = frame.AddrStack.Offset;
        }
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(),
                         &frame, &context, NULL, SymFunctionTableAccess64,
                         SymGetModuleBase64, NULL))
            break;
    }
}

static void print_guest_stack(void)
{
    uintptr_t offset = (uintptr_t)g_xbox_mem_offset;
    unsigned int i, found = 0;
    int raw = getenv("BRUTEFORCE_CRASH_STACK") != NULL;

    if (g_esp == 0 || g_esp >= XBOX_CONTIG_SIZE ||
        offset > UINTPTR_MAX - g_esp)
        return;
    fprintf(stderr, raw ? "  raw guest stack (may contain guest data):\n"
                        : "  guest stack candidates (not a frame walk):\n");
    for (i = 0; i < 128 && g_esp <= XBOX_CONTIG_SIZE - sizeof(uint32_t) - i * 4u;
         ++i) {
        uint32_t value;
        SIZE_T read = 0;
        uintptr_t address = offset + g_esp + i * 4u;
        if (address < offset ||
            !ReadProcessMemory(GetCurrentProcess(), (const void *)address,
                               &value, sizeof(value), &read) ||
            read != sizeof(value))
            break;
        if (raw || (value >= g_xbox_code_lo && value < g_xbox_code_hi)) {
            fprintf(stderr, "    [esp+%u] 0x%08X\n", i * 4u, value);
            if (++found == 16 && !raw)
                break;
        }
    }
    if (!found)
        fprintf(stderr, "    (none found)\n");
}

static LONG WINAPI report_unhandled_exception(PEXCEPTION_POINTERS exception)
{
    EXCEPTION_RECORD *record = exception->ExceptionRecord;
    CONTEXT *context = exception->ContextRecord;
    uintptr_t image = (uintptr_t)GetModuleHandleW(NULL);
    uintptr_t rip = (uintptr_t)context->Rip;
    MEMORY_BASIC_INFORMATION memory;
    LONG result;

    if (InterlockedCompareExchange(&reporting, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "[CRASH] exception 0x%08lX at host 0x%llX",
            (unsigned long)record->ExceptionCode, (unsigned long long)rip);
    if (VirtualQuery((const void *)rip, &memory, sizeof(memory)) == sizeof(memory) &&
        (uintptr_t)memory.AllocationBase == image)
        fprintf(stderr, " (image+0x%llX)", (unsigned long long)(rip - image));
    fputc('\n', stderr);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2) {
        ULONG_PTR operation = record->ExceptionInformation[0];
        fprintf(stderr, "  %s address 0x%llX\n",
                operation == 0 ? "read" : operation == 1 ? "write" : "execute",
                (unsigned long long)record->ExceptionInformation[1]);
    }
    fprintf(stderr, "  last published guest registers: "
            "eax=%08X ecx=%08X edx=%08X esp=%08X "
            "ebx=%08X ebp=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_esp, g_ebx, g_ebp, g_esi, g_edi);
    print_native_stack(context);
    print_guest_stack();
    fflush(stderr);
    result = previous_filter != NULL ? previous_filter(exception) : EXCEPTION_CONTINUE_SEARCH;
    InterlockedExchange(&reporting, 0);
    return result;
}

void bruteforce_install_crash_diagnostics(void)
{
    wchar_t directory[32768];
    DWORD capacity = (DWORD)(sizeof(directory) / sizeof(directory[0]));
    DWORD length = GetModuleFileNameW(NULL, directory, capacity);
    wchar_t *separator;

    if (length == 0 || length >= capacity ||
        (separator = wcsrchr(directory, L'\\')) == NULL) {
        fprintf(stderr, "[CRASH] Cannot resolve the executable directory for symbols\n");
    } else {
        *separator = L'\0';
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES |
                      SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS |
                      SYMOPT_SECURE | SYMOPT_IGNORE_NT_SYMPATH);
        if (SymInitializeW(GetCurrentProcess(), directory, TRUE))
            symbols_ready = 1;
        else
            fprintf(stderr, "[CRASH] Cannot initialize local symbols (Windows error %lu)\n",
                    (unsigned long)GetLastError());
    }
    previous_filter = SetUnhandledExceptionFilter(report_unhandled_exception);
}
