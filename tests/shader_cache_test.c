#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/shader_cache.h"

int bruteforce_test_shader_cache_keys(void)
{
    static const char source[] = "shader source";
    static const unsigned char expected[] = {0x12, 0x34, 0x56, 0x78};
    void *bytes = NULL;
    size_t size = 0;
    unsigned flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

    if (!gpu_pscache_enabled()) {
        fprintf(stderr, "FAIL: shader cache is not enabled\n");
        return 0;
    }
    gpu_pscache_store(source, sizeof source - 1, "rc", "ps_main", "ps_5_0", flags,
                      expected, sizeof expected);
    if (!gpu_pscache_load(source, sizeof source - 1, "rc", "ps_main", "ps_5_0", flags,
                          &bytes, &size) || size != sizeof expected ||
        memcmp(bytes, expected, sizeof expected)) {
        fprintf(stderr, "FAIL: shader cache exact key\n");
        free(bytes);
        return 0;
    }
    free(bytes);
    if (gpu_pscache_load("shader sourcf", sizeof source - 1, "rc", "ps_main", "ps_5_0",
                         flags, &bytes, &size) ||
        gpu_pscache_load(source, sizeof source - 2, "rc", "ps_main", "ps_5_0",
                         flags, &bytes, &size) ||
        gpu_pscache_load(source, sizeof source - 1, "other", "ps_main", "ps_5_0",
                         flags, &bytes, &size) ||
        gpu_pscache_load(source, sizeof source - 1, "rc", "other", "ps_5_0",
                         flags, &bytes, &size) ||
        gpu_pscache_load(source, sizeof source - 1, "rc", "ps_main", "ps_4_0",
                         flags, &bytes, &size) ||
        gpu_pscache_load(source, sizeof source - 1, "rc", "ps_main", "ps_5_0",
                         flags ^ D3DCOMPILE_DEBUG, &bytes, &size)) {
        fprintf(stderr, "FAIL: shader cache reused a different compiler input\n");
        free(bytes);
        return 0;
    }
    puts("shader cache keys passed");
    return 1;
}

int bruteforce_test_shader_cache_publication(int readonly, int different, int corrupt)
{
    static const char source[] = "shader publication source";
    static const unsigned char expected[] = {0x12, 0x34, 0x56, 0x78};
    static const unsigned char replacement[] = {0x87, 0x65, 0x43, 0x21};
    const wchar_t *directory = _wgetenv(L"RECOMP_GPU_PSCACHE_DIR");
    wchar_t pattern[32768], path[32768];
    WIN32_FIND_DATAW found;
    HANDLE search, held = INVALID_HANDLE_VALUE;
    DWORD attributes;
    void *bytes = NULL;
    size_t size = 0;
    int ok;

    if (!gpu_pscache_enabled() || !directory || !*directory ||
        _snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%ls\\ps_*.bin", directory) < 0) {
        fprintf(stderr, "FAIL: shader publication test setup\n");
        return 0;
    }
    gpu_pscache_store(source, sizeof source - 1, "rc", "ps_main", "ps_5_0", 0,
                      expected, sizeof expected);
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE ||
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls\\%ls", directory,
                     found.cFileName) < 0) {
        fprintf(stderr, "FAIL: locate shader cache entry\n");
        if (search != INVALID_HANDLE_VALUE)
            FindClose(search);
        return 0;
    }
    if (!FindClose(search)) {
        fprintf(stderr, "FAIL: close shader cache search\n");
        return 0;
    }
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "FAIL: inspect shader cache entry\n");
        return 0;
    }
    if (corrupt) {
        static const unsigned char invalid_magic = 0xFF;
        DWORD written = 0;
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE ||
            !WriteFile(file, &invalid_magic, 1, &written, NULL) || written != 1) {
            fprintf(stderr, "FAIL: corrupt shader cache entry\n");
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            return 0;
        }
        if (!CloseHandle(file)) {
            fprintf(stderr, "FAIL: close corrupted shader cache entry\n");
            return 0;
        }
    }
    if (readonly) {
        if (!SetFileAttributesW(path, attributes | FILE_ATTRIBUTE_READONLY)) {
            fprintf(stderr, "FAIL: make shader cache entry read-only\n");
            return 0;
        }
    } else {
        held = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (held == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "FAIL: hold shader cache entry without delete sharing\n");
            return 0;
        }
    }
    gpu_pscache_store(source, sizeof source - 1, "rc", "ps_main", "ps_5_0", 0,
                      different ? replacement : expected, sizeof expected);
    if ((readonly && !SetFileAttributesW(path, attributes)) ||
        (!readonly && !CloseHandle(held))) {
        fprintf(stderr, "FAIL: release shader cache entry\n");
        return 0;
    }
    ok = gpu_pscache_load(source, sizeof source - 1, "rc", "ps_main", "ps_5_0", 0,
                          &bytes, &size);
    if (corrupt)
        ok = !ok && bytes == NULL && size == 0;
    else
        ok = ok && size == sizeof expected && memcmp(bytes, expected, sizeof expected) == 0;
    free(bytes);
    if (!ok) {
        fprintf(stderr, "FAIL: existing shader cache entry changed\n");
        return 0;
    }
    puts("shader cache publication fixture passed");
    return 1;
}

int bruteforce_test_shader_cache_temp_collision(int exhausted)
{
    static const char source[] = "shader source";
    static const unsigned char expected[] = {0x12, 0x34, 0x56, 0x78};
    static const unsigned char replacement[] = {0x87, 0x65, 0x43, 0x21};
    const wchar_t *directory = _wgetenv(L"RECOMP_GPU_PSCACHE_DIR");
    wchar_t pattern[32768], path[32768], temporary[32768];
    WIN32_FIND_DATAW found;
    HANDLE search;
    void *bytes = NULL;
    size_t size = 0;
    unsigned i, collisions = exhausted ? 32u : 1u;
    int ok;

    if (!bruteforce_test_shader_cache_keys() || !directory || !*directory ||
        _snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%ls\\ps_*.bin", directory) < 0) {
        fprintf(stderr, "FAIL: shader temporary collision setup\n");
        return 0;
    }
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE ||
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls\\%ls", directory,
                     found.cFileName) < 0) {
        fprintf(stderr, "FAIL: locate shader cache entry\n");
        if (search != INVALID_HANDLE_VALUE)
            FindClose(search);
        return 0;
    }
    if (!FindClose(search)) {
        fprintf(stderr, "FAIL: close shader cache search\n");
        return 0;
    }
    for (i = 0; i < collisions; ++i) {
        static const unsigned char sentinel = 0xA5;
        DWORD written = 0;
        HANDLE file;
        if (_snwprintf_s(temporary, _countof(temporary), _TRUNCATE, L"%ls.%lu.%lu.%ld.tmp",
                         path, (unsigned long)GetCurrentProcessId(),
                         (unsigned long)GetCurrentThreadId(), (long)(i + 2u)) < 0)
            return 0;
        file = CreateFileW(temporary, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE || !WriteFile(file, &sentinel, 1, &written, NULL) ||
            written != 1) {
            fprintf(stderr, "FAIL: create colliding shader cache temporary\n");
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            return 0;
        }
        if (!CloseHandle(file)) {
            fprintf(stderr, "FAIL: close colliding shader cache temporary\n");
            return 0;
        }
    }
    gpu_pscache_store(source, sizeof source - 1, "rc", "ps_main", "ps_5_0",
                      D3DCOMPILE_OPTIMIZATION_LEVEL3, replacement, sizeof replacement);
    ok = gpu_pscache_load(source, sizeof source - 1, "rc", "ps_main", "ps_5_0",
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, &bytes, &size) &&
         size == sizeof expected &&
         memcmp(bytes, exhausted ? expected : replacement, sizeof expected) == 0;
    free(bytes);
    for (i = 0; i < collisions; ++i) {
        unsigned char sentinel = 0;
        DWORD read = 0;
        HANDLE file;
        if (_snwprintf_s(temporary, _countof(temporary), _TRUNCATE, L"%ls.%lu.%lu.%ld.tmp",
                         path, (unsigned long)GetCurrentProcessId(),
                         (unsigned long)GetCurrentThreadId(), (long)(i + 2u)) < 0)
            return 0;
        file = CreateFileW(temporary, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE || !ReadFile(file, &sentinel, 1, &read, NULL) ||
            read != 1 || sentinel != 0xA5) {
            fprintf(stderr, "FAIL: colliding shader cache temporary changed\n");
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            return 0;
        }
        if (!CloseHandle(file) || !DeleteFileW(temporary)) {
            fprintf(stderr, "FAIL: remove fixture shader cache temporary\n");
            return 0;
        }
    }
    if (!ok) {
        fprintf(stderr, "FAIL: shader cache temporary collision handling\n");
        return 0;
    }
    puts("shader cache temporary collision fixture passed");
    return 1;
}
