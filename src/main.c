#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xbox/xboxrecomp.h>
#include <ohci.h>

#include "bruteforce_config.h"
#include "crash_diagnostics.h"
#include "gpu/d3d11_backend.h"
#include "gpu/null_backend.h"
#include "profile.h"

typedef void (*recomp_func_t)(void);

extern RECOMP_TLS uint32_t g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern MCPXAPUState *g_apu_state;
extern int recomp_dispatch_init(void);
extern recomp_func_t recomp_lookup(uint32_t xbox_va);

static LONG CALLBACK handle_mmio(PEXCEPTION_POINTERS exception)
{
    return xbox_VehMmioFault(exception) ? EXCEPTION_CONTINUE_EXECUTION
                                        : EXCEPTION_CONTINUE_SEARCH;
}

static char *absolute_path(const char *path, const char *kind)
{
    DWORD needed = GetFullPathNameA(path, 0, NULL, NULL);
    char *result;
    DWORD written;

    if (needed == 0) {
        fprintf(stderr, "Cannot resolve %s (Windows error %lu)\n",
                kind, (unsigned long)GetLastError());
        return NULL;
    }
    result = malloc(needed);
    if (result == NULL) {
        fprintf(stderr, "Cannot allocate the %s path\n", kind);
        return NULL;
    }
    written = GetFullPathNameA(path, needed, result, NULL);
    if (written == 0 || written >= needed) {
        fprintf(stderr, "Cannot resolve %s (Windows error %lu)\n",
                kind, (unsigned long)GetLastError());
        free(result);
        return NULL;
    }
    return result;
}

static int configure_save_root(char **out)
{
    const wchar_t *requested = _wgetenv(L"BRUTEFORCE_SAVE_ROOT");
    const char *user_dir;
    wchar_t *wide_root;
    char *root;
    DWORD needed, written, attributes;
    int utf8_length;

    if (requested == NULL)
        return 1;
    if (*requested == L'\0') {
        fprintf(stderr, "BRUTEFORCE_SAVE_ROOT must name an existing directory\n");
        return 0;
    }
    needed = GetFullPathNameW(requested, 0, NULL, NULL);
    if (needed == 0) {
        fprintf(stderr, "Cannot resolve save root (Windows error %lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }
    if (needed > MAX_PATH - 96) {
        fprintf(stderr, "BRUTEFORCE_SAVE_ROOT must be an existing directory "
                        "with room for save paths\n");
        return 0;
    }
    wide_root = malloc((size_t)needed * sizeof(*wide_root));
    if (wide_root == NULL) {
        fprintf(stderr, "Cannot allocate the save root path\n");
        return 0;
    }
    written = GetFullPathNameW(requested, needed, wide_root, NULL);
    if (written == 0 || written >= needed) {
        fprintf(stderr, "Cannot resolve save root (Windows error %lu)\n",
                (unsigned long)GetLastError());
        free(wide_root);
        return 0;
    }
    attributes = GetFileAttributesW(wide_root);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fprintf(stderr, "BRUTEFORCE_SAVE_ROOT must be an existing directory "
                        "with room for save paths\n");
        free(wide_root);
        return 0;
    }
    utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_root, -1,
                                      NULL, 0, NULL, NULL);
    if (utf8_length == 0) {
        fprintf(stderr, "Cannot encode save root as UTF-8 (Windows error %lu)\n",
                (unsigned long)GetLastError());
        free(wide_root);
        return 0;
    }
    root = malloc((size_t)utf8_length);
    if (root == NULL) {
        fprintf(stderr, "Cannot allocate the UTF-8 save root path\n");
        free(wide_root);
        return 0;
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_root, -1,
                            root, utf8_length, NULL, NULL) != utf8_length) {
        fprintf(stderr, "Cannot encode save root as UTF-8 (Windows error %lu)\n",
                (unsigned long)GetLastError());
        free(wide_root);
        free(root);
        return 0;
    }
    free(wide_root);
    user_dir = getenv("RECOMP_SAVE_DIR");
    if ((user_dir == NULL || *user_dir == '\0') &&
        _putenv_s("RECOMP_SAVE_DIR", root) != 0) {
        fprintf(stderr, "Cannot configure the isolated user-data directory\n");
        free(root);
        return 0;
    }
    *out = root;
    return 1;
}

/* RECOMP_GPU selects the renderer: unset or "cpu" keeps the framework's CPU
 * rasterizer, the correctness reference; "d3d11" renders on the GPU
 * (gpu/d3d11_backend.c). "null" is a measurement probe that draws nothing
 * (gpu/null_backend.c). */
static int configure_renderer(void)
{
    const char *gpu = getenv("RECOMP_GPU");

    if (gpu == NULL || *gpu == '\0' || strcmp(gpu, "cpu") == 0)
        return 1;
    if (strcmp(gpu, "d3d11") == 0)
        return bruteforce_d3d11_install();
    if (strcmp(gpu, "null") == 0)
        return bruteforce_null_backend_install();
    fprintf(stderr, "RECOMP_GPU must be cpu, d3d11 or null, not \"%s\"\n", gpu);
    return 0;
}

static int matches_fingerprint(const unsigned char *data, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    BCRYPT_ALG_HANDLE algorithm = NULL;
    unsigned char digest[32];
    char actual[65];
    NTSTATUS status;
    size_t i;

    if (size != BRUTEFORCE_XBE_SIZE) {
        fprintf(stderr, "Unsupported default.xbe size: %zu\n", size);
        return 0;
    }
    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status < 0) {
        fprintf(stderr, "Cannot initialize SHA-256 (NTSTATUS 0x%08lX)\n",
                (unsigned long)status);
        return 0;
    }
    status = BCryptHash(algorithm, NULL, 0, (PUCHAR)data, (ULONG)size,
                        digest, (ULONG)sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        fprintf(stderr, "Cannot hash default.xbe (NTSTATUS 0x%08lX)\n",
                (unsigned long)status);
        return 0;
    }
    for (i = 0; i < sizeof(digest); ++i) {
        actual[i * 2] = digits[digest[i] >> 4];
        actual[i * 2 + 1] = digits[digest[i] & 15];
    }
    actual[64] = '\0';
    if (strcmp(actual, BRUTEFORCE_XBE_SHA256) != 0) {
        fprintf(stderr, "Unsupported default.xbe SHA-256: %s\n", actual);
        return 0;
    }
    return 1;
}

static int load_xbe(const char *directory, unsigned char **out_data, size_t *out_size)
{
    const char suffix[] = "\\default.xbe";
    size_t path_size = strlen(directory) + sizeof(suffix);
    char *path = malloc(path_size);
    FILE *file = NULL;
    long length;
    unsigned char *data;

    if (path == NULL) {
        fprintf(stderr, "Cannot allocate the executable path\n");
        return 0;
    }
    snprintf(path, path_size, "%s%s", directory, suffix);
    if (fopen_s(&file, path, "rb") != 0) {
        fprintf(stderr, "Cannot open game executable: %s\n", path);
        free(path);
        return 0;
    }
    free(path);
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Cannot determine default.xbe size\n");
        fclose(file);
        return 0;
    }
    if ((size_t)length != BRUTEFORCE_XBE_SIZE) {
        fprintf(stderr, "Unsupported default.xbe size: %ld\n", length);
        fclose(file);
        return 0;
    }
    data = malloc((size_t)length);
    if (data == NULL) {
        fprintf(stderr, "Cannot allocate memory for default.xbe\n");
        fclose(file);
        return 0;
    }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "Cannot read all of default.xbe\n");
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    if (!matches_fingerprint(data, (size_t)length)) {
        free(data);
        return 0;
    }
    *out_data = data;
    *out_size = (size_t)length;
    return 1;
}

int main(int argc, char **argv)
{
    const char *input_dir = argc == 2 ? argv[1] : "game\\disc";
    char *game_dir;
    char *save_root = NULL;
    unsigned char *xbe_data = NULL;
    size_t xbe_size = 0;
    recomp_func_t entry;
    int result = 1;
    int memory_ready = 0;
    int kernel_ready = 0;
    int usb_attempted = 0;
    int usb_requested = getenv("RECOMP_USB") != NULL;
    PVOID mmio_handler = NULL;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc > 2) {
        fprintf(stderr, "Usage: bruteforce.exe [game directory]\n");
        return 2;
    }
    if ((getenv("RECOMP_VBLANK") == NULL && _putenv_s("RECOMP_VBLANK", "1") != 0) ||
        (getenv("RECOMP_STUB_LOG") == NULL && _putenv_s("RECOMP_STUB_LOG", "1") != 0) ||
        (usb_requested && getenv("RECOMP_DISPATCH_GATE") == NULL &&
         _putenv_s("RECOMP_DISPATCH_GATE", "1") != 0)) {
        fprintf(stderr, "Cannot configure the bring-up diagnostics\n");
        return 1;
    }
    game_dir = absolute_path(input_dir, "game directory");
    if (!configure_save_root(&save_root))
        goto cleanup;
    if (game_dir == NULL || !load_xbe(game_dir, &xbe_data, &xbe_size))
        goto cleanup;

    printf("Brute Force static recompilation: D3D11 bring-up\n");
    printf("Loading verified game from %s\n", game_dir);
    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        fprintf(stderr, "Cannot initialize the Xbox memory layout\n");
        goto cleanup;
    }
    memory_ready = 1;
    g_xbox_mem_offset = xbox_GetMemoryOffset();
    if (usb_requested || getenv("RECOMP_AC97_READY") != NULL) {
        mmio_handler = AddVectoredExceptionHandler(1, handle_mmio);
        if (mmio_handler == NULL) {
            fprintf(stderr, "Cannot register the MMIO handler (Windows error %lu)\n",
                    (unsigned long)GetLastError());
            goto cleanup;
        }
    }
    if (getenv("RECOMP_AC97_READY") != NULL) {
        g_apu_state = mcpx_apu_init_standalone((uint8_t *)xbox_GetMemoryBase());
        if (g_apu_state == NULL) {
            fprintf(stderr, "Cannot initialize the requested APU model\n");
            goto cleanup;
        }
    }

    xbox_kernel_init();
    kernel_ready = 1;
    xbox_path_init(game_dir, save_root);
    xbox_kernel_bridge_init();
    g_esp = XBOX_STACK_TOP;
    if (!recomp_dispatch_init())
        fprintf(stderr, "Flat dispatch allocation failed; using the runtime's binary search\n");
    entry = recomp_lookup(BRUTEFORCE_XBE_ENTRY_POINT);
    if (entry == NULL) {
        fprintf(stderr, "No recompiled entry point at 0x%08X\n", BRUTEFORCE_XBE_ENTRY_POINT);
        goto cleanup;
    }
    if (usb_requested) {
        usb_attempted = 1;
        if (xbox_OhciInit() != 1) {
            fprintf(stderr, "Cannot initialize the requested USB controller model\n");
            goto cleanup;
        }
    }
    if (!configure_renderer())
        goto cleanup;
    bruteforce_install_crash_diagnostics();
    bruteforce_profile_init();
    xbox_WatchdogStart();
    printf("Starting guest entry point 0x%08X\n", BRUTEFORCE_XBE_ENTRY_POINT);
    entry();
    printf("Guest entry point returned\n");
    result = 0;
    if (usb_attempted) {
        printf("Terminating with USB active; guest worker threads cannot be joined safely\n");
        ExitProcess(0);
    }

cleanup:
    if (usb_attempted && !xbox_OhciShutdown()) {
        fprintf(stderr, "USB shutdown failed; terminating without unmapping guest memory\n");
        ExitProcess(1);
    }
    if (kernel_ready)
        xbox_kernel_shutdown();
    if (g_apu_state != NULL) {
        MCPXAPUState *apu = g_apu_state;
        g_apu_state = NULL;
        mcpx_apu_shutdown(apu);
    }
    if (mmio_handler != NULL && !RemoveVectoredExceptionHandler(mmio_handler)) {
        fprintf(stderr, "Cannot remove the MMIO handler (Windows error %lu)\n",
                (unsigned long)GetLastError());
        ExitProcess(1);
    }
    if (memory_ready)
        xbox_MemoryLayoutShutdown();
    free(xbe_data);
    free(save_root);
    free(game_dir);
    return result;
}
