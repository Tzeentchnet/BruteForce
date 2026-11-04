#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perf_counters.h"

int g_bruteforce_perf_counters_enabled;

static int s_initialized;
static uint64_t s_frequency;

static struct {
    volatile LONG64 guest_word_calls;
    volatile LONG64 guest_word_accepted;
    volatile LONG64 guest_word_rejected;
    volatile LONG64 guest_word_virtual_queries;
    volatile LONG64 guest_word_query_failures;
    volatile LONG64 guest_word_regions;
    volatile LONG64 guest_word_total_ticks;
    volatile LONG64 guest_word_query_ticks;
    volatile LONG64 guest_range_calls;
    volatile LONG64 guest_range_accepted;
    volatile LONG64 guest_range_rejected;
    volatile LONG64 guest_range_virtual_queries;
    volatile LONG64 guest_range_query_failures;
    volatile LONG64 guest_range_regions;
    volatile LONG64 guest_range_total_ticks;
    volatile LONG64 guest_range_query_ticks;
    volatile LONG64 texture_lookups;
    volatile LONG64 texture_probes;
    volatile LONG64 texture_key_hits;
    volatile LONG64 texture_key_misses;
    volatile LONG64 texture_lookup_ticks;
    volatile LONG64 texture_exact_checks;
    volatile LONG64 texture_exact_hits;
    volatile LONG64 texture_source_compare_bytes;
    volatile LONG64 texture_palette_compare_bytes;
    volatile LONG64 texture_exact_ticks;
    volatile LONG64 texture_snapshot_copies;
    volatile LONG64 texture_snapshot_copy_bytes;
    volatile LONG64 texture_snapshot_copy_ticks;
    volatile LONG64 texture_rewrites;
    volatile LONG64 texture_evictions;
    volatile LONG64 texture_snapshot_evictions;
    volatile LONG64 texture_discards;
    volatile LONG64 texture_unsupported_hits;
} s_counters;

static void add(volatile LONG64 *counter, uint64_t value)
{
    if (value)
        InterlockedExchangeAdd64(counter, (LONG64)value);
}

static uint64_t take(volatile LONG64 *counter)
{
    return (uint64_t)InterlockedExchange64(counter, 0);
}

int bruteforce_perf_counters_init(void)
{
    const char *value;
    LARGE_INTEGER frequency;

    if (s_initialized)
        return 1;
    value = getenv("BRUTEFORCE_PERF_COUNTERS");
    if (value == NULL || strcmp(value, "0") == 0) {
        s_initialized = 1;
        return 1;
    }
    if (strcmp(value, "1") != 0) {
        fprintf(stderr, "BRUTEFORCE_PERF_COUNTERS must be 0 or 1\n");
        return 0;
    }
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        fprintf(stderr, "Cannot initialize performance-counter timing (Windows error %lu)\n",
                (unsigned long)GetLastError());
        return 0;
    }
    s_frequency = (uint64_t)frequency.QuadPart;
    g_bruteforce_perf_counters_enabled = 1;
    s_initialized = 1;
    fprintf(stderr, "[PERF] Detailed validation and texture-cache counters enabled;"
                    " diagnostic timing adds overhead\n");
    return 1;
}

uint64_t bruteforce_perf_clock(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)counter.QuadPart;
}

double bruteforce_perf_ticks_ms(uint64_t ticks)
{
    return s_frequency ? (double)ticks * 1000.0 / (double)s_frequency : 0.0;
}

void bruteforce_perf_counters_take(BruteForcePerfCounters *out)
{
    memset(out, 0, sizeof *out);
    out->guest_word.calls = take(&s_counters.guest_word_calls);
    out->guest_word.accepted = take(&s_counters.guest_word_accepted);
    out->guest_word.rejected = take(&s_counters.guest_word_rejected);
    out->guest_word.virtual_queries = take(&s_counters.guest_word_virtual_queries);
    out->guest_word.query_failures = take(&s_counters.guest_word_query_failures);
    out->guest_word.regions = take(&s_counters.guest_word_regions);
    out->guest_word.total_ticks = take(&s_counters.guest_word_total_ticks);
    out->guest_word.query_ticks = take(&s_counters.guest_word_query_ticks);
    out->guest_range.calls = take(&s_counters.guest_range_calls);
    out->guest_range.accepted = take(&s_counters.guest_range_accepted);
    out->guest_range.rejected = take(&s_counters.guest_range_rejected);
    out->guest_range.virtual_queries = take(&s_counters.guest_range_virtual_queries);
    out->guest_range.query_failures = take(&s_counters.guest_range_query_failures);
    out->guest_range.regions = take(&s_counters.guest_range_regions);
    out->guest_range.total_ticks = take(&s_counters.guest_range_total_ticks);
    out->guest_range.query_ticks = take(&s_counters.guest_range_query_ticks);
    out->texture.lookups = take(&s_counters.texture_lookups);
    out->texture.probes = take(&s_counters.texture_probes);
    out->texture.key_hits = take(&s_counters.texture_key_hits);
    out->texture.key_misses = take(&s_counters.texture_key_misses);
    out->texture.lookup_ticks = take(&s_counters.texture_lookup_ticks);
    out->texture.exact_checks = take(&s_counters.texture_exact_checks);
    out->texture.exact_hits = take(&s_counters.texture_exact_hits);
    out->texture.source_compare_bytes = take(&s_counters.texture_source_compare_bytes);
    out->texture.palette_compare_bytes = take(&s_counters.texture_palette_compare_bytes);
    out->texture.exact_ticks = take(&s_counters.texture_exact_ticks);
    out->texture.snapshot_copies = take(&s_counters.texture_snapshot_copies);
    out->texture.snapshot_copy_bytes = take(&s_counters.texture_snapshot_copy_bytes);
    out->texture.snapshot_copy_ticks = take(&s_counters.texture_snapshot_copy_ticks);
    out->texture.rewrites = take(&s_counters.texture_rewrites);
    out->texture.evictions = take(&s_counters.texture_evictions);
    out->texture.snapshot_evictions = take(&s_counters.texture_snapshot_evictions);
    out->texture.discards = take(&s_counters.texture_discards);
    out->texture.unsupported_hits = take(&s_counters.texture_unsupported_hits);
}

static void record_validation(volatile LONG64 *calls, volatile LONG64 *accepted_count,
                              volatile LONG64 *rejected_count, volatile LONG64 *queries_count,
                              volatile LONG64 *failure_count, volatile LONG64 *region_count,
                              volatile LONG64 *total_count, volatile LONG64 *query_count,
                              int accepted, unsigned queries, unsigned query_failures,
                              unsigned regions, uint64_t total_ticks, uint64_t query_ticks)
{
    InterlockedIncrement64(calls);
    InterlockedIncrement64(accepted ? accepted_count : rejected_count);
    add(queries_count, queries);
    add(failure_count, query_failures);
    add(region_count, regions);
    add(total_count, total_ticks);
    add(query_count, query_ticks);
}

void bruteforce_perf_record_guest_word(int accepted, unsigned queries,
                                       unsigned query_failures, unsigned regions,
                                       uint64_t total_ticks, uint64_t query_ticks)
{
    record_validation(&s_counters.guest_word_calls, &s_counters.guest_word_accepted,
                      &s_counters.guest_word_rejected,
                      &s_counters.guest_word_virtual_queries,
                      &s_counters.guest_word_query_failures,
                      &s_counters.guest_word_regions,
                      &s_counters.guest_word_total_ticks,
                      &s_counters.guest_word_query_ticks,
                      accepted, queries, query_failures, regions, total_ticks, query_ticks);
}

void bruteforce_perf_record_guest_range(int accepted, unsigned queries,
                                        unsigned query_failures, unsigned regions,
                                        uint64_t total_ticks, uint64_t query_ticks)
{
    record_validation(&s_counters.guest_range_calls, &s_counters.guest_range_accepted,
                      &s_counters.guest_range_rejected,
                      &s_counters.guest_range_virtual_queries,
                      &s_counters.guest_range_query_failures,
                      &s_counters.guest_range_regions,
                      &s_counters.guest_range_total_ticks,
                      &s_counters.guest_range_query_ticks,
                      accepted, queries, query_failures, regions, total_ticks, query_ticks);
}

void bruteforce_perf_record_texture_lookup(unsigned probes, int key_hit, uint64_t ticks)
{
    InterlockedIncrement64(&s_counters.texture_lookups);
    add(&s_counters.texture_probes, probes);
    InterlockedIncrement64(key_hit ? &s_counters.texture_key_hits
                                  : &s_counters.texture_key_misses);
    add(&s_counters.texture_lookup_ticks, ticks);
}

void bruteforce_perf_record_texture_exact(uint64_t source_bytes, uint64_t palette_bytes,
                                          int hit, uint64_t ticks)
{
    InterlockedIncrement64(&s_counters.texture_exact_checks);
    if (hit)
        InterlockedIncrement64(&s_counters.texture_exact_hits);
    add(&s_counters.texture_source_compare_bytes, source_bytes);
    add(&s_counters.texture_palette_compare_bytes, palette_bytes);
    add(&s_counters.texture_exact_ticks, ticks);
}

void bruteforce_perf_record_texture_snapshot(uint64_t bytes, uint64_t ticks)
{
    InterlockedIncrement64(&s_counters.texture_snapshot_copies);
    add(&s_counters.texture_snapshot_copy_bytes, bytes);
    add(&s_counters.texture_snapshot_copy_ticks, ticks);
}

void bruteforce_perf_record_texture_rewrite(void)
{
    InterlockedIncrement64(&s_counters.texture_rewrites);
}

void bruteforce_perf_record_texture_eviction(void)
{
    InterlockedIncrement64(&s_counters.texture_evictions);
}

void bruteforce_perf_record_texture_snapshot_eviction(void)
{
    InterlockedIncrement64(&s_counters.texture_snapshot_evictions);
}

void bruteforce_perf_record_texture_discard(void)
{
    InterlockedIncrement64(&s_counters.texture_discards);
}

void bruteforce_perf_record_texture_unsupported_hit(void)
{
    InterlockedIncrement64(&s_counters.texture_unsupported_hits);
}
