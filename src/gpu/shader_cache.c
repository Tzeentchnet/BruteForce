/*
 * Persistent D3D11 combiner pixel-shader bytecode.
 *
 * The design follows Blood Wake's bw_pscache.c (bloodwake-recomp at
 * e631edddfe56d22344e9f696923aeb6d78a9e7ba, MIT; THIRD_PARTY_NOTICES.md).
 * Cache keys cover the generated HLSL, compiler binary, entry point, target
 * and flags. Entries are bounded, checked and atomically published.
 */
#include <windows.h>
#include <bcrypt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "shader_cache.h"

#define CACHE_PATH_CAP 32768
#define CACHE_HASH_SIZE 32
#define CACHE_MAX_BLOB (4u * 1024u * 1024u)

typedef struct {
    uint8_t magic[8];
    uint8_t key[CACHE_HASH_SIZE];
    uint8_t checksum[CACHE_HASH_SIZE];
    uint32_t size;
} CacheHeader;

static const uint8_t s_magic[8] = {'R', 'C', 'P', 'S', '0', '0', '0', '1'};
static const char s_compile_tag[] = "recomp-shader-cache-v1";
static BCRYPT_ALG_HANDLE s_sha;
static uint8_t s_compiler_hash[CACHE_HASH_SIZE];
static wchar_t s_directory[CACHE_PATH_CAP];
static int s_enabled;
static volatile LONG s_temp_serial;

static int hash_file(const wchar_t *path, uint8_t digest[CACHE_HASH_SIZE])
{
    uint8_t buffer[65536];
    BCRYPT_HASH_HANDLE hash = NULL;
    HANDLE file;
    DWORD read;
    NTSTATUS status;
    int ok = 0;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[GPU-PSCACHE] cannot open D3DCompiler for hashing (Windows error %lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }
    status = BCryptCreateHash(s_sha, &hash, NULL, 0, NULL, 0, 0);
    if (status < 0) {
        fprintf(stderr, "[GPU-PSCACHE] cannot initialize SHA-256 (NTSTATUS 0x%08lX)\n",
                (unsigned long)status);
        goto done;
    }
    for (;;) {
        if (!ReadFile(file, buffer, sizeof buffer, &read, NULL)) {
            fprintf(stderr, "[GPU-PSCACHE] cannot read D3DCompiler (Windows error %lu)\n",
                    (unsigned long)GetLastError());
            goto done;
        }
        if (!read)
            break;
        status = BCryptHashData(hash, buffer, read, 0);
        if (status < 0) {
            fprintf(stderr, "[GPU-PSCACHE] cannot hash D3DCompiler (NTSTATUS 0x%08lX)\n",
                    (unsigned long)status);
            goto done;
        }
    }
    status = BCryptFinishHash(hash, digest, CACHE_HASH_SIZE, 0);
    if (status < 0) {
        fprintf(stderr, "[GPU-PSCACHE] cannot finish compiler hash (NTSTATUS 0x%08lX)\n",
                (unsigned long)status);
        goto done;
    }
    ok = 1;
done:
    if (hash)
        BCryptDestroyHash(hash);
    CloseHandle(file);
    return ok;
}

static int digest_bytes(const void *bytes, size_t size, uint8_t digest[CACHE_HASH_SIZE])
{
    NTSTATUS status;

    if (size > ULONG_MAX) {
        fprintf(stderr, "[GPU-PSCACHE] data is too large to hash\n");
        return 0;
    }
    status = BCryptHash(s_sha, NULL, 0, (PUCHAR)bytes, (ULONG)size, digest, CACHE_HASH_SIZE);
    if (status < 0) {
        fprintf(stderr, "[GPU-PSCACHE] SHA-256 failed (NTSTATUS 0x%08lX)\n",
                (unsigned long)status);
        return 0;
    }
    return 1;
}

static NTSTATUS hash_field(BCRYPT_HASH_HANDLE hash, const void *bytes, ULONG length)
{
    NTSTATUS status = BCryptHashData(hash, (PUCHAR)&length, sizeof length, 0);
    if (status >= 0)
        status = BCryptHashData(hash, (PUCHAR)bytes, length, 0);
    return status;
}

static int shader_key(const char *source, size_t length, const char *name, const char *entry,
                      const char *target, unsigned flags, uint8_t key[CACHE_HASH_SIZE])
{
    BCRYPT_HASH_HANDLE hash = NULL;
    NTSTATUS status;
    size_t name_length = strlen(name), entry_length = strlen(entry), target_length = strlen(target);

    if (length > ULONG_MAX || name_length > ULONG_MAX || entry_length > ULONG_MAX ||
        target_length > ULONG_MAX) {
        fprintf(stderr, "[GPU-PSCACHE] shader parameters are too large to hash\n");
        return 0;
    }
    status = BCryptCreateHash(s_sha, &hash, NULL, 0, NULL, 0, 0);
    if (status >= 0)
        status = BCryptHashData(hash, (PUCHAR)s_compile_tag, (ULONG)sizeof s_compile_tag, 0);
    if (status >= 0)
        status = BCryptHashData(hash, s_compiler_hash, CACHE_HASH_SIZE, 0);
    if (status >= 0)
        status = BCryptHashData(hash, (PUCHAR)&flags, sizeof flags, 0);
    if (status >= 0)
        status = hash_field(hash, source, (ULONG)length);
    if (status >= 0)
        status = hash_field(hash, name, (ULONG)name_length);
    if (status >= 0)
        status = hash_field(hash, entry, (ULONG)entry_length);
    if (status >= 0)
        status = hash_field(hash, target, (ULONG)target_length);
    if (status >= 0)
        status = BCryptFinishHash(hash, key, CACHE_HASH_SIZE, 0);
    if (hash)
        BCryptDestroyHash(hash);
    if (status < 0) {
        fprintf(stderr, "[GPU-PSCACHE] shader key failed (NTSTATUS 0x%08lX)\n",
                (unsigned long)status);
        return 0;
    }
    return 1;
}

static int directory_exists(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int make_directory(const wchar_t *path)
{
    if (CreateDirectoryW(path, NULL))
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS && directory_exists(path))
        return 1;
    fprintf(stderr, "[GPU-PSCACHE] cannot create cache directory (Windows error %lu)\n",
            (unsigned long)GetLastError());
    return 0;
}

static int configure_directory(void)
{
    const wchar_t *custom = _wgetenv(L"RECOMP_GPU_PSCACHE_DIR");
    const wchar_t *local = _wgetenv(L"LOCALAPPDATA");
    wchar_t parent[CACHE_PATH_CAP];
    int written;

    if (custom) {
        DWORD length;
        if (!*custom) {
            fprintf(stderr, "[GPU-PSCACHE] RECOMP_GPU_PSCACHE_DIR is empty\n");
            return 0;
        }
        length = GetFullPathNameW(custom, CACHE_PATH_CAP, s_directory, NULL);
        if (!length || length >= CACHE_PATH_CAP || !directory_exists(s_directory)) {
            fprintf(stderr, "[GPU-PSCACHE] RECOMP_GPU_PSCACHE_DIR must be an existing directory\n");
            return 0;
        }
        return 1;
    }
    if (!local || !*local) {
        fprintf(stderr, "[GPU-PSCACHE] LOCALAPPDATA is not set\n");
        return 0;
    }
    written = _snwprintf_s(parent, CACHE_PATH_CAP, _TRUNCATE, L"%ls\\BruteForce", local);
    if (written < 0) {
        fprintf(stderr, "[GPU-PSCACHE] cache directory path is too long\n");
        return 0;
    }
    if (!make_directory(parent))
        return 0;
    written = _snwprintf_s(s_directory, CACHE_PATH_CAP, _TRUNCATE, L"%ls\\shader-cache", parent);
    if (written < 0) {
        fprintf(stderr, "[GPU-PSCACHE] cache directory path is too long\n");
        return 0;
    }
    if (!make_directory(s_directory))
        return 0;
    return 1;
}

int gpu_pscache_init(void)
{
    const char *enabled = getenv("RECOMP_GPU_PSCACHE");
    wchar_t compiler[CACHE_PATH_CAP];
    HMODULE module;
    DWORD length;
    NTSTATUS status;

    if (!enabled || !*enabled || strcmp(enabled, "0") == 0)
        return 1;
    if (strcmp(enabled, "1") != 0) {
        fprintf(stderr, "[GPU-PSCACHE] RECOMP_GPU_PSCACHE must be 0 or 1\n");
        return 0;
    }
    status = BCryptOpenAlgorithmProvider(&s_sha, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status < 0) {
        fprintf(stderr, "[GPU-PSCACHE] cannot open SHA-256 (NTSTATUS 0x%08lX)\n",
                (unsigned long)status);
        goto unavailable;
    }
    module = GetModuleHandleW(L"d3dcompiler_47.dll");
    length = module ? GetModuleFileNameW(module, compiler, CACHE_PATH_CAP) : 0;
    if (!length || length >= CACHE_PATH_CAP) {
        fprintf(stderr, "[GPU-PSCACHE] cannot identify the loaded D3DCompiler\n");
        goto unavailable;
    }
    if (!hash_file(compiler, s_compiler_hash) || !configure_directory())
        goto unavailable;
    s_enabled = 1;
    fprintf(stderr, "[GPU-PSCACHE] persistent combiner cache enabled\n");
    return 1;

unavailable:
    if (s_sha) {
        BCryptCloseAlgorithmProvider(s_sha, 0);
        s_sha = NULL;
    }
    fprintf(stderr, "[GPU-PSCACHE] cache unavailable; shaders will compile normally\n");
    return 1;
}

int gpu_pscache_enabled(void)
{
    return s_enabled;
}

static int cache_path(const uint8_t key[CACHE_HASH_SIZE], wchar_t path[CACHE_PATH_CAP])
{
    static const wchar_t digits[] = L"0123456789abcdef";
    wchar_t hex[CACHE_HASH_SIZE * 2 + 1];
    unsigned i;

    for (i = 0; i < CACHE_HASH_SIZE; ++i) {
        hex[i * 2] = digits[key[i] >> 4];
        hex[i * 2 + 1] = digits[key[i] & 15];
    }
    hex[CACHE_HASH_SIZE * 2] = 0;
    if (_snwprintf_s(path, CACHE_PATH_CAP, _TRUNCATE, L"%ls\\ps_%ls.bin", s_directory, hex) < 0) {
        fprintf(stderr, "[GPU-PSCACHE] cache path is too long\n");
        return 0;
    }
    return 1;
}

int gpu_pscache_load(const char *source, size_t length, const char *name, const char *entry,
                     const char *target, unsigned flags, void **bytes, size_t *size)
{
    uint8_t key[CACHE_HASH_SIZE], checksum[CACHE_HASH_SIZE];
    wchar_t path[CACHE_PATH_CAP];
    CacheHeader header;
    LARGE_INTEGER file_size;
    HANDLE file;
    DWORD read, error;
    void *blob = NULL;
    int ok = 0;

    *bytes = NULL;
    *size = 0;
    if (!s_enabled || !shader_key(source, length, name, entry, target, flags, key) ||
        !cache_path(key, path))
        return 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            fprintf(stderr, "[GPU-PSCACHE] cannot open cache entry (Windows error %lu)\n",
                    (unsigned long)error);
        return 0;
    }
    if (!GetFileSizeEx(file, &file_size) ||
        !ReadFile(file, &header, sizeof header, &read, NULL) || read != sizeof header ||
        memcmp(header.magic, s_magic, sizeof s_magic) ||
        memcmp(header.key, key, sizeof key) || !header.size || header.size > CACHE_MAX_BLOB ||
        file_size.QuadPart != (LONGLONG)sizeof header + header.size) {
        fprintf(stderr, "[GPU-PSCACHE] invalid or truncated cache entry; recompiling\n");
        goto done;
    }
    blob = malloc(header.size);
    if (!blob) {
        fprintf(stderr, "[GPU-PSCACHE] cannot allocate cached bytecode; recompiling\n");
        goto done;
    }
    if (!ReadFile(file, blob, header.size, &read, NULL) || read != header.size ||
        !digest_bytes(blob, header.size, checksum) ||
        memcmp(checksum, header.checksum, sizeof checksum)) {
        fprintf(stderr, "[GPU-PSCACHE] damaged cache bytecode; recompiling\n");
        goto done;
    }
    *bytes = blob;
    *size = header.size;
    blob = NULL;
    ok = 1;
done:
    free(blob);
    CloseHandle(file);
    return ok;
}

static int cache_entry_identical(const char *source, size_t length, const char *name,
                                 const char *entry, const char *target, unsigned flags,
                                 const void *bytes, size_t size)
{
    void *existing = NULL;
    size_t existing_size = 0;
    int identical = gpu_pscache_load(source, length, name, entry, target, flags,
                                     &existing, &existing_size) &&
                    existing_size == size && memcmp(existing, bytes, size) == 0;
    free(existing);
    return identical;
}

void gpu_pscache_store(const char *source, size_t length, const char *name, const char *entry,
                       const char *target, unsigned flags, const void *bytes, size_t size)
{
    CacheHeader header;
    wchar_t path[CACHE_PATH_CAP], temporary[CACHE_PATH_CAP];
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD wrote, error = ERROR_SUCCESS;
    int attempt;

    if (!s_enabled)
        return;
    if (!bytes || !size || size > CACHE_MAX_BLOB) {
        fprintf(stderr, "[GPU-PSCACHE] compiled bytecode has an invalid cache size\n");
        return;
    }
    memset(&header, 0, sizeof header);
    memcpy(header.magic, s_magic, sizeof s_magic);
    header.size = (uint32_t)size;
    if (!shader_key(source, length, name, entry, target, flags, header.key) ||
        !digest_bytes(bytes, size, header.checksum) ||
        !cache_path(header.key, path))
        return;
    for (attempt = 0; attempt < 32; ++attempt) {
        LONG serial = InterlockedIncrement(&s_temp_serial);
        if (_snwprintf_s(temporary, CACHE_PATH_CAP, _TRUNCATE, L"%ls.%lu.%lu.%ld.tmp", path,
                         (unsigned long)GetCurrentProcessId(), (unsigned long)GetCurrentThreadId(),
                         (long)serial) < 0) {
            fprintf(stderr, "[GPU-PSCACHE] temporary cache path is too long\n");
            return;
        }
        file = CreateFileW(temporary, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            error = ERROR_SUCCESS;
            break;
        }
        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            break;
    }
    if (file == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[GPU-PSCACHE] cannot create cache entry (Windows error %lu)\n",
                (unsigned long)error);
        return;
    }
    if (!WriteFile(file, &header, sizeof header, &wrote, NULL))
        error = GetLastError();
    else if (wrote != sizeof header)
        error = ERROR_WRITE_FAULT;
    else if (!WriteFile(file, bytes, (DWORD)size, &wrote, NULL))
        error = GetLastError();
    else if (wrote != size)
        error = ERROR_WRITE_FAULT;
    else if (!FlushFileBuffers(file))
        error = GetLastError();
    if (!CloseHandle(file) && error == ERROR_SUCCESS)
        error = GetLastError();
    if (error == ERROR_SUCCESS) {
        for (attempt = 0; attempt < 8; ++attempt) {
            if (MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return;
            error = GetLastError();
            if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION &&
                error != ERROR_LOCK_VIOLATION)
                break;
            if (cache_entry_identical(source, length, name, entry, target, flags, bytes, size)) {
                if (!DeleteFileW(temporary))
                    fprintf(stderr, "[GPU-PSCACHE] cannot remove temporary cache entry "
                                    "(Windows error %lu)\n", (unsigned long)GetLastError());
                fprintf(stderr, "[GPU-PSCACHE] publication blocked (Windows error %lu); "
                                "identical entry already present\n", (unsigned long)error);
                return;
            }
            if (attempt + 1 < 8)
                Sleep(5);
        }
    }
    if (error != ERROR_SUCCESS) {
        fprintf(stderr, "[GPU-PSCACHE] cannot publish cache entry (Windows error %lu)\n",
                (unsigned long)error);
        if (!DeleteFileW(temporary))
            fprintf(stderr, "[GPU-PSCACHE] cannot remove temporary cache entry (Windows error %lu)\n",
                    (unsigned long)GetLastError());
    }
}
