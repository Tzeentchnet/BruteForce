#ifndef BRUTEFORCE_PERF_COUNTERS_H
#define BRUTEFORCE_PERF_COUNTERS_H

#include <stdint.h>

typedef struct {
    uint64_t calls;
    uint64_t accepted;
    uint64_t rejected;
    uint64_t virtual_queries;
    uint64_t query_failures;
    uint64_t regions;
    uint64_t total_ticks;
    uint64_t query_ticks;
} BruteForceValidationPerfCounters;

typedef struct {
    uint64_t lookups;
    uint64_t probes;
    uint64_t key_hits;
    uint64_t key_misses;
    uint64_t lookup_ticks;
    uint64_t exact_checks;
    uint64_t exact_hits;
    uint64_t source_compare_bytes;
    uint64_t palette_compare_bytes;
    uint64_t exact_ticks;
    uint64_t snapshot_copies;
    uint64_t snapshot_copy_bytes;
    uint64_t snapshot_copy_ticks;
    uint64_t rewrites;
    uint64_t evictions;
    uint64_t snapshot_evictions;
    uint64_t discards;
    uint64_t unsupported_hits;
} BruteForceTexturePerfCounters;

#define BRUTEFORCE_PERF_MAX_CALLERS 32u
typedef struct {
    uint32_t site, caller;
    uint64_t calls, ticks;
} BruteForceSubmitPerfCounter;

typedef struct {
    uint64_t calls, failures, ticks;
} BruteForceShaderPerfCounters;

typedef struct {
    BruteForceValidationPerfCounters guest_word;
    BruteForceValidationPerfCounters guest_range;
    BruteForceTexturePerfCounters texture;
    BruteForceShaderPerfCounters pixel_shader_compile;
    BruteForceSubmitPerfCounter pfb[BRUTEFORCE_PERF_MAX_CALLERS];
    BruteForceSubmitPerfCounter put[BRUTEFORCE_PERF_MAX_CALLERS];
    uint64_t pfb_overflow, put_overflow;
} BruteForcePerfCounters;

extern int g_bruteforce_perf_counters_enabled;

int bruteforce_perf_counters_init(void);
uint64_t bruteforce_perf_clock(void);
double bruteforce_perf_ticks_ms(uint64_t ticks);
void bruteforce_perf_counters_take(BruteForcePerfCounters *out);

void bruteforce_perf_record_guest_word(int accepted, unsigned queries,
                                       unsigned query_failures, unsigned regions,
                                       uint64_t total_ticks, uint64_t query_ticks);
void bruteforce_perf_record_guest_range(int accepted, unsigned queries,
                                        unsigned query_failures, unsigned regions,
                                        uint64_t total_ticks, uint64_t query_ticks);
void bruteforce_perf_record_texture_lookup(unsigned probes, int key_hit, uint64_t ticks);
void bruteforce_perf_record_texture_exact(uint64_t source_bytes, uint64_t palette_bytes,
                                          int hit, uint64_t ticks);
void bruteforce_perf_record_texture_snapshot(uint64_t bytes, uint64_t ticks);
void bruteforce_perf_record_texture_rewrite(void);
void bruteforce_perf_record_texture_eviction(void);
void bruteforce_perf_record_texture_snapshot_eviction(void);
void bruteforce_perf_record_texture_discard(void);
void bruteforce_perf_record_texture_unsupported_hit(void);
void bruteforce_perf_record_submit(int is_put, uint32_t site, uint32_t caller,
                                   uint64_t ticks);
void bruteforce_perf_record_pixel_shader_compile(int success, uint64_t ticks);

#endif
