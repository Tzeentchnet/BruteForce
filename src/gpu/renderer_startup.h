#ifndef BRUTEFORCE_RENDERER_STARTUP_H
#define BRUTEFORCE_RENDERER_STARTUP_H

#include <stddef.h>

typedef enum BruteforceRendererStartupStatus {
    BRUTEFORCE_RENDERER_STARTUP_SUCCESS,
    BRUTEFORCE_RENDERER_STARTUP_UNAVAILABLE,
    BRUTEFORCE_RENDERER_STARTUP_INVALID_CONFIGURATION,
    BRUTEFORCE_RENDERER_STARTUP_INITIALIZATION_FAILED
} BruteforceRendererStartupStatus;

typedef struct BruteforceRendererStartupResult {
    BruteforceRendererStartupStatus status;
    char stage[48];
    char reason[256];
} BruteforceRendererStartupResult;

typedef BruteforceRendererStartupResult (*BruteforceRendererInitialize)(void *context);

typedef struct BruteforceRendererCandidate {
    const char *name;
    BruteforceRendererInitialize initialize;
    void *context;
} BruteforceRendererCandidate;

BruteforceRendererStartupResult bruteforce_renderer_startup_result(
    BruteforceRendererStartupStatus status, const char *stage, const char *format, ...);
BruteforceRendererStartupResult bruteforce_renderer_select_explicit(
    const BruteforceRendererCandidate *candidate);
BruteforceRendererStartupResult bruteforce_renderer_select_auto(
    const BruteforceRendererCandidate *candidates, size_t candidate_count,
    size_t *selected_candidate);
const char *bruteforce_renderer_startup_status_name(
    BruteforceRendererStartupStatus status);

#endif
