#include "apu.h"
#include "apu_state.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void *recomp_lookup(unsigned long address)
{
    (void)address;
    abort();
}

void *recomp_lookup_manual(unsigned long address)
{
    (void)address;
    abort();
}

static int s_device_inits;
static int s_device_shutdowns;
static int s_device_open;

int xa2_init(void)
{
    s_device_inits++;
    s_device_open = 1;
    return 1;
}

void xa2_shutdown(void)
{
    s_device_shutdowns++;
    s_device_open = 0;
}

int xa2_is_active(void) { return s_device_open; }
int xa2_get_buffer_size(void) { return 512; }
int xa2_get_buffer_count(void) { return 4; }
unsigned xa2_underruns(void) { return 0; }
int xa2_queued_samples(void) { return 0; }
uint64_t xa2_samples_played(void) { return 0; }
void xa2_expect_drain(void) {}
int xa2_submit_samples(const int16_t *samples, int count)
{
    (void)samples;
    (void)count;
    return 1;
}

static int check(int condition, const char *what)
{
    printf("%-62s %s\n", what, condition ? "PASS" : "FAIL");
    return condition ? 0 : 1;
}

int main(void)
{
    enum { CYCLES = 6, FAKE_RAM_SIZE = 256 * 1024 };
    uint8_t *ram = (uint8_t *)calloc(1, FAKE_RAM_SIZE);
    DWORD baseline_handles = 0;
    int failures = 0;
    int i;

    if (!ram) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    if (_putenv_s("RECOMP_APU_DSP", "xemu") != 0 ||
        _putenv_s("RECOMP_APU_DSP_THREAD", "1") != 0) {
        fprintf(stderr, "cannot configure DSP test environment\n");
        free(ram);
        return 2;
    }
    if (!GetProcessHandleCount(GetCurrentProcess(), &baseline_handles)) {
        fprintf(stderr, "GetProcessHandleCount failed (Windows error %lu)\n",
                (unsigned long)GetLastError());
        free(ram);
        return 2;
    }

    for (i = 0; i < CYCLES; i++) {
        MCPXAPUState *d = mcpx_apu_init_standalone(ram);
        DWORD handles = 0;

        if (!d) {
            fprintf(stderr, "APU initialization failed in cycle %d\n", i + 1);
            failures++;
            break;
        }
        mcpx_apu_shutdown(d);
        if (!GetProcessHandleCount(GetCurrentProcess(), &handles)) {
            fprintf(stderr, "GetProcessHandleCount failed in cycle %d\n", i + 1);
            failures++;
            break;
        }
        failures += check(handles == baseline_handles,
                          "shutdown joins threads and closes their handles");
    }

    failures += check(s_device_inits == CYCLES && s_device_shutdowns == CYCLES,
                      "each APU instance opens and closes the audio device once");
    failures += check(!s_device_open, "audio device is closed after final shutdown");
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    free(ram);
    return failures ? 1 : 0;
}
