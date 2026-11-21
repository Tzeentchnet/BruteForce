#include "renderer_startup.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

BruteforceRendererStartupResult bruteforce_renderer_startup_result(
    BruteforceRendererStartupStatus status, const char *stage, const char *format, ...)
{
    BruteforceRendererStartupResult result;
    va_list args;

    memset(&result, 0, sizeof result);
    result.status = status;
    if (stage)
        snprintf(result.stage, sizeof result.stage, "%s", stage);
    if (format) {
        va_start(args, format);
        vsnprintf(result.reason, sizeof result.reason, format, args);
        va_end(args);
    }
    return result;
}

const char *bruteforce_renderer_startup_status_name(
    BruteforceRendererStartupStatus status)
{
    switch (status) {
    case BRUTEFORCE_RENDERER_STARTUP_SUCCESS:
        return "success";
    case BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE:
        return "unavailable";
    case BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION:
        return "invalid configuration";
    case BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED:
        return "initialization failed";
    default:
        return "unknown failure";
    }
}

static BruteforceRendererStartupResult invalid_candidate(void)
{
    return bruteforce_renderer_startup_result(
        BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION, "selection",
        "renderer candidate is incomplete");
}

BruteforceRendererStartupResult bruteforce_renderer_select_explicit(
    const BruteforceRendererCandidate *candidate)
{
    if (!candidate || !candidate->name || !candidate->initialize)
        return invalid_candidate();
    return candidate->initialize(candidate->context);
}

BruteforceRendererStartupResult bruteforce_renderer_select_auto(
    const BruteforceRendererCandidate *candidates, size_t candidate_count,
    size_t *selected_candidate)
{
    BruteforceRendererStartupResult result;
    size_t i;

    if (selected_candidate)
        *selected_candidate = SIZE_MAX;
    if (!candidates || !candidate_count)
        return invalid_candidate();

    for (i = 0; i < candidate_count; ++i) {
        if (!candidates[i].name || !candidates[i].initialize)
            return invalid_candidate();
        result = candidates[i].initialize(candidates[i].context);
        if (result.status == BRUTEFORCE_RENDERER_STARTUP_SUCCESS) {
            if (selected_candidate)
                *selected_candidate = i;
            return result;
        }
        fprintf(stderr, "[GPU] %s startup %s at %s: %s\n",
                candidates[i].name,
                bruteforce_renderer_startup_status_name(result.status),
                result.stage[0] ? result.stage : "unknown stage",
                result.reason[0] ? result.reason : "no reason supplied");
        if (result.status == BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION)
            return result;
    }

    return result;
}
