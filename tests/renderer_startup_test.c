#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gpu/renderer_startup.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct FakeCandidate {
    BruteforceRendererStartupResult result;
    unsigned calls;
} FakeCandidate;

static BruteforceRendererStartupResult fake_initialize(void *context)
{
    FakeCandidate *candidate = context;
    candidate->calls++;
    return candidate->result;
}

static FakeCandidate fake(BruteforceRendererStartupStatus status, const char *stage,
                          const char *reason)
{
    FakeCandidate candidate;
    candidate.result = bruteforce_renderer_startup_result(status, stage, "%s", reason);
    candidate.calls = 0;
    return candidate;
}

int main(void)
{
    FakeCandidate success = fake(BRUTEFORCE_RENDERER_STARTUP_SUCCESS, "complete", "ready");
    FakeCandidate unavailable = fake(BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE,
                                     "loader", "vulkan-1.dll is unavailable");
    FakeCandidate failed = fake(BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED,
                                "device", "device creation failed");
    FakeCandidate invalid = fake(BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION,
                                 "options", "invalid device index");
    BruteforceRendererCandidate explicit_candidate = {
        "Vulkan", fake_initialize, &failed
    };
    BruteforceRendererCandidate fallback[] = {
        { "Vulkan", fake_initialize, &unavailable },
        { "Direct3D 12", fake_initialize, &success },
    };
    BruteforceRendererCandidate stop[] = {
        { "Vulkan", fake_initialize, &invalid },
        { "Direct3D 12", fake_initialize, &success },
    };
    BruteforceRendererCandidate both_fail[] = {
        { "Vulkan", fake_initialize, &unavailable },
        { "Direct3D 12", fake_initialize, &failed },
    };
    BruteforceRendererStartupResult result;
    size_t selected;

    result = bruteforce_renderer_select_explicit(&explicit_candidate);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED);
    CHECK(failed.calls == 1);
    CHECK(success.calls == 0);

    result = bruteforce_renderer_select_auto(fallback, 2, &selected);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_SUCCESS);
    CHECK(selected == 1);
    CHECK(unavailable.calls == 1);
    CHECK(success.calls == 1);

    success.calls = 0;
    result = bruteforce_renderer_select_auto(stop, 2, &selected);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION);
    CHECK(selected == SIZE_MAX);
    CHECK(invalid.calls == 1);
    CHECK(success.calls == 0);

    unavailable.calls = 0;
    failed.calls = 0;
    result = bruteforce_renderer_select_auto(both_fail, 2, &selected);
    CHECK(result.status == BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED);
    CHECK(selected == SIZE_MAX);
    CHECK(unavailable.calls == 1);
    CHECK(failed.calls == 1);

    CHECK(strcmp(bruteforce_renderer_startup_status_name(result.status),
                 "initialization failed") == 0);
    puts("Renderer startup lifecycle: ok");
    return 0;
}
