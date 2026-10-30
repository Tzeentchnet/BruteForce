#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel.h"
#include "xbox_memory_layout.h"

typedef void (*recomp_func_t)(void);

extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);
extern RECOMP_TLS uint32_t g_eax, g_esp;
extern ptrdiff_t g_xbox_mem_offset;

recomp_func_t recomp_lookup(uint32_t xbox_va)
{
    (void)xbox_va;
    return NULL;
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    (void)xbox_va;
    return NULL;
}

int main(int argc, char **argv)
{
    const uint32_t thunk_va = 0x10000u;
    const uint32_t stack_va = 0x20000u;
    const uint32_t status_va = 0x30000u;
    const uint32_t buffer_va = 0x40000u;
    uint8_t *memory;
    uint32_t *stack;
    recomp_func_t read_file;
    HANDLE file = INVALID_HANDLE_VALUE;
    char directory[MAX_PATH];
    char path[MAX_PATH] = "";
    DWORD written;
    int failure;
    int result = 1;

    if (argc != 2 || (strcmp(argv[1], "success") != 0 && strcmp(argv[1], "failure") != 0))
        return 2;
    failure = strcmp(argv[1], "failure") == 0;
    memory = VirtualAlloc(NULL, 16u * 1024u * 1024u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == NULL) {
        fprintf(stderr, "Cannot allocate synthetic guest memory\n");
        return 2;
    }
    g_xbox_mem_offset = (ptrdiff_t)memory;
    *(uint32_t *)(memory + thunk_va) = 0x80000000u | 219u;
    xbox_kernel_set_thunk_address(thunk_va, 1);
    xbox_kernel_bridge_init();
    read_file = recomp_lookup_kernel(*(uint32_t *)(memory + thunk_va));
    if (read_file == NULL) {
        fprintf(stderr, "Cannot resolve the NtReadFile bridge\n");
        goto cleanup;
    }
    if (!failure) {
        if (!GetTempPathA(MAX_PATH, directory) || !GetTempFileNameA(directory, "bfr", 0, path)) {
            fprintf(stderr, "Cannot create a temporary synthetic file\n");
            goto cleanup;
        }
        file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
        if (file == INVALID_HANDLE_VALUE ||
            !WriteFile(file, "ABCD", 4, &written, NULL) || written != 4 ||
            SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
            (uintptr_t)file > UINT32_MAX) {
            fprintf(stderr, "Cannot prepare the synthetic file\n");
            goto cleanup;
        }
    }

    stack = (uint32_t *)(memory + stack_va);
    stack[0] = 0x00112233u;
    stack[1] = failure ? 0xFFFFFFFFu : (uint32_t)(uintptr_t)file;
    stack[2] = 0;
    stack[3] = 0;
    stack[4] = 0;
    stack[5] = status_va;
    stack[6] = buffer_va;
    stack[7] = 4;
    stack[8] = 0;
    g_esp = stack_va;
    read_file();

    if (g_esp != stack_va + 36u ||
        (failure ? (int32_t)g_eax >= 0 :
                   (g_eax != 0 || memcmp(memory + buffer_va, "ABCD", 4) != 0))) {
        fprintf(stderr, "Unexpected read status or guest state: eax=%08X esp=%08X\n", g_eax, g_esp);
        goto cleanup;
    }
    result = 0;

cleanup:
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (path[0] != '\0' && !DeleteFileA(path)) {
        fprintf(stderr, "Cannot remove the synthetic temporary file\n");
        result = 1;
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    return result;
}
