/*
 * Sampling profiler (BRUTEFORCE_PROFILE=<file>).
 *
 * Adapted from Blood Wake's bw_profile.c (bloodwake-recomp at
 * e631edddfe56d22344e9f696923aeb6d78a9e7ba, MIT licence; see
 * THIRD_PARTY_NOTICES.md). Generic code only.
 *
 * About once a millisecond a time-critical thread suspends every other
 * thread of the process, reads its instruction pointer and resumes it.
 * Samples are resolved with DbgHelp when the report is written, so build
 * with BRUTEFORCE_LINK_SYMBOLS=ON for names in the framework libraries.
 *
 * The sampling window is either a time range, BRUTEFORCE_PROFILE_FROM and
 * BRUTEFORCE_PROFILE_TO in seconds since start, or the life of a file:
 * with BRUTEFORCE_PROFILE_GATE=<file> samples are taken only while it
 * exists. The report is written by the sampler itself as soon as the window
 * closes (TO reached, or the gate file removed after it existed), because a
 * watchdog exit leaves through _exit() and never runs atexit handlers. A
 * process that exits with the window still open writes it at exit.
 *
 * A thread blocked in the kernel is sampled inside a wait function in ntdll
 * or KERNELBASE; those samples are classified as waiting and the rest as
 * busy. Spin loops, including guest code that polls a register, count as
 * busy here: a reader who knows which guest functions poll has to add them
 * (tools/bench.py does, for this title's known polls).
 *
 * BRUTEFORCE_PROFILE_CALLERS=1 additionally walks the stack of samples
 * outside the executable to the first frame inside it, which answers "who
 * waits". It takes the loader's function-table lock, so it is off by
 * default.
 */
#include <windows.h>
#include <timeapi.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profile.h"

#define PROF_MAX_THREADS 192
#define PROF_HASH_BITS 20
#define PROF_HASH (1u << PROF_HASH_BITS)
#define PROF_CHASH (1u << 16)
#define PROF_NAME 160

typedef struct {
    uint64_t rip;
    uint32_t tidx;      /* thread index + 1; 0 = empty slot */
    uint32_t count;
} ProfSlot;

typedef struct {
    uint64_t rip, caller;
    uint32_t tidx;
    uint32_t count;
} ProfCallerSlot;

static ProfSlot *s_tab;
static ProfCallerSlot *s_ctab;
static DWORD s_tid[PROF_MAX_THREADS];
static HANDLE s_th[PROF_MAX_THREADS];
static uint64_t s_th_start[PROF_MAX_THREADS];
static uint64_t s_th_samples[PROF_MAX_THREADS];
static int s_nth;
static DWORD s_self;
static volatile LONG s_running, s_dumped;
static char s_out[MAX_PATH];
static const char *s_gate;
static double s_from, s_to;
static ULONGLONG s_t0;
static double s_window_start = -1, s_window_end = -1;
static uint64_t s_ticks, s_dropped;
static int s_callers;
static uint64_t s_img_lo, s_img_hi;

typedef LONG (NTAPI *NtQueryInformationThreadFn)(HANDLE, int, PVOID, ULONG, PULONG);

static uint64_t thread_start_address(HANDLE h)
{
    static NtQueryInformationThreadFn query;
    uint64_t addr = 0;
    if (!query)
        query = (NtQueryInformationThreadFn)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    if (query)
        query(h, 9 /* ThreadQuerySetWin32StartAddress */, &addr, sizeof(addr), NULL);
    return addr;
}

static void refresh_threads(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    DWORD pid = GetCurrentProcessId();
    if (snap == INVALID_HANDLE_VALUE)
        return;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            int known = 0, i;
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == s_self)
                continue;
            for (i = 0; i < s_nth; i++)
                if (s_tid[i] == te.th32ThreadID) { known = 1; break; }
            if (!known && s_nth < PROF_MAX_THREADS) {
                HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                      THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (h) {
                    s_tid[s_nth] = te.th32ThreadID;
                    s_th[s_nth] = h;
                    s_th_start[s_nth] = thread_start_address(h);
                    s_nth++;
                }
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static void record(int t, uint64_t rip)
{
    uint64_t h = (rip * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)t << 56);
    uint32_t i = (uint32_t)(h >> (64 - PROF_HASH_BITS)), n;
    for (n = 0; n < 64; n++, i = (i + 1) & (PROF_HASH - 1)) {
        ProfSlot *s = &s_tab[i];
        if (s->tidx == 0) {
            s->rip = rip;
            s->tidx = (uint32_t)t + 1;
            s->count = 1;
            return;
        }
        if (s->rip == rip && s->tidx == (uint32_t)t + 1) {
            s->count++;
            return;
        }
    }
    s_dropped++;
}

static void record_caller(int t, const CONTEXT *ctx)
{
    CONTEXT c = *ctx;
    uint64_t caller = 0, h;
    uint32_t i, n;
    int depth;
    for (depth = 0; depth < 32 && c.Rip; depth++) {
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &base, NULL);
        if (!fn) {
            if (!c.Rsp)
                break;
            c.Rip = *(const DWORD64 *)(uintptr_t)c.Rsp;     /* a leaf */
            c.Rsp += 8;
        } else {
            PVOID handler_data = NULL;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, fn, &c, &handler_data,
                             &frame, NULL);
        }
        if (c.Rip >= s_img_lo && c.Rip < s_img_hi) {
            caller = c.Rip;
            break;
        }
    }
    h = (ctx->Rip * 0x9E3779B97F4A7C15ull) ^ (caller * 0xC2B2AE3D27D4EB4Full)
        ^ ((uint64_t)t << 56);
    i = (uint32_t)(h >> 48) & (PROF_CHASH - 1);
    for (n = 0; n < 64; n++, i = (i + 1) & (PROF_CHASH - 1)) {
        ProfCallerSlot *s = &s_ctab[i];
        if (!s->tidx) {
            s->rip = ctx->Rip; s->caller = caller; s->tidx = (uint32_t)t + 1; s->count = 1;
            return;
        }
        if (s->rip == ctx->Rip && s->caller == caller && s->tidx == (uint32_t)t + 1) {
            s->count++;
            return;
        }
    }
}

static int gate_open(void)
{
    DWORD attributes = GetFileAttributesA(s_gate);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

static DWORD WINAPI sampler(LPVOID arg)
{
    ULONGLONG last_refresh = 0;
    int was_open = 0;
    (void)arg;
    timeBeginPeriod(1);
    while (InterlockedCompareExchange(&s_running, 1, 1)) {
        ULONGLONG now = GetTickCount64();
        double secs = (now - s_t0) / 1000.0;
        int open = s_gate ? gate_open() : secs >= s_from && (s_to <= 0 || secs < s_to);
        if (open) {
            int t;
            if (!was_open)
                s_window_start = secs;
            if (now - last_refresh >= 200) {
                refresh_threads();
                last_refresh = now;
            }
            s_ticks++;
            for (t = 0; t < s_nth; t++) {
                CONTEXT ctx;
                if (SuspendThread(s_th[t]) == (DWORD)-1)
                    continue;
                ctx.ContextFlags = s_callers ? CONTEXT_FULL : CONTEXT_CONTROL;
                if (GetThreadContext(s_th[t], &ctx)) {
                    record(t, ctx.Rip);
                    if (s_callers && (ctx.Rip < s_img_lo || ctx.Rip >= s_img_hi))
                        record_caller(t, &ctx);
                    s_th_samples[t]++;
                }
                ResumeThread(s_th[t]);
            }
        } else if (was_open) {
            s_window_end = secs;
            timeEndPeriod(1);
            bruteforce_profile_dump(s_gate ? "gate closed" : "window ended");
            return 0;
        }
        was_open = open;
        Sleep(1);
    }
    timeEndPeriod(1);
    return 0;
}

static void profile_atexit(void)
{
    bruteforce_profile_dump("exit");
}

void bruteforce_profile_init(void)
{
    const char *e = getenv("BRUTEFORCE_PROFILE");
    const char *from = getenv("BRUTEFORCE_PROFILE_FROM");
    const char *to = getenv("BRUTEFORCE_PROFILE_TO");
    const char *callers = getenv("BRUTEFORCE_PROFILE_CALLERS");
    HMODULE image = GetModuleHandleA(NULL);
    HANDLE h;

    if (!e || !*e)
        return;
    snprintf(s_out, sizeof(s_out), "%s", strcmp(e, "1") ? e : "profile.txt");
    s_gate = getenv("BRUTEFORCE_PROFILE_GATE");
    if (s_gate && !*s_gate)
        s_gate = NULL;
    s_from = from ? atof(from) : 0.0;
    s_to = to ? atof(to) : 0.0;
    s_tab = (ProfSlot *)calloc(PROF_HASH, sizeof(ProfSlot));
    if (!s_tab)
        return;
    s_callers = callers && strcmp(callers, "1") == 0 && image;
    if (s_callers) {
        const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
        const IMAGE_NT_HEADERS64 *nt =
            (const IMAGE_NT_HEADERS64 *)((const uint8_t *)image + dos->e_lfanew);
        s_img_lo = (uint64_t)(uintptr_t)image;
        s_img_hi = s_img_lo + nt->OptionalHeader.SizeOfImage;
        s_ctab = (ProfCallerSlot *)calloc(PROF_CHASH, sizeof(ProfCallerSlot));
        s_callers = s_ctab != NULL;
    }
    s_t0 = GetTickCount64();
    s_running = 1;
    h = CreateThread(NULL, 0, sampler, NULL, 0, &s_self);
    if (!h) {
        s_running = 0;
        return;
    }
    SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL);
    CloseHandle(h);
    atexit(profile_atexit);
    if (s_gate)
        fprintf(stderr, "[PROFILE] sampling while %s exists -> %s\n", s_gate, s_out);
    else
        fprintf(stderr, "[PROFILE] sampling %.1f..%s s -> %s\n", s_from,
                s_to > 0 ? to : "end", s_out);
}

/* ---- report ------------------------------------------------------------ */

typedef struct {
    char name[PROF_NAME];
    uint32_t tidx;      /* 0 = all threads */
    uint64_t count;
} ProfAgg;

static int agg_cmp(const void *a, const void *b)
{
    uint64_t x = ((const ProfAgg *)a)->count, y = ((const ProfAgg *)b)->count;
    return x < y ? 1 : x > y ? -1 : 0;
}

static void symbol_name(HANDLE proc, uint64_t addr, char *out, size_t n)
{
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
    DWORD64 disp = 0;
    IMAGEHLP_MODULE64 mi;
    memset(buf, 0, sizeof(buf));
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 255;
    mi.SizeOfStruct = sizeof(mi);
    if (!SymGetModuleInfo64(proc, addr, &mi))
        mi.ModuleName[0] = 0;
    if (SymFromAddr(proc, addr, &disp, si))
        snprintf(out, n, "%s!%s", mi.ModuleName[0] ? mi.ModuleName : "?", si->Name);
    else if (mi.ModuleName[0])
        snprintf(out, n, "%s!+0x%llx", mi.ModuleName,
                 (unsigned long long)(addr - mi.BaseOfImage));
    else
        snprintf(out, n, "0x%llx", (unsigned long long)addr);
}

/* A sample inside one of these is a thread blocked in the kernel. */
static int is_wait_symbol(const char *name)
{
    static const char *const waits[] = {
        "!NtWaitFor", "!ZwWaitFor", "!NtDelayExecution", "!ZwDelayExecution",
        "!NtYieldExecution", "!ZwYieldExecution", "!NtRemoveIoCompletion",
        "!ZwRemoveIoCompletion", "!NtWaitForAlertByThreadId",
        "!ZwWaitForAlertByThreadId", "!NtSignalAndWaitForSingleObject",
        "!NtWaitForWorkViaWorkerFactory", "!ZwWaitForWorkViaWorkerFactory",
        "!NtReplyWaitReceivePort", "!NtAlertThreadByThreadId",
        "!NtUserMsgWaitForMultipleObjectsEx", "!NtUserGetMessage",
        "!NtUserWaitMessage", "!NtGdiDdDDIWaitFor", "!NtDxgkWait",
    };
    size_t i;
    for (i = 0; i < sizeof waits / sizeof waits[0]; i++)
        if (strstr(name, waits[i]))
            return 1;
    return 0;
}

static void agg_add(ProfAgg *agg, size_t cap, uint32_t tidx, const char *name, uint64_t count)
{
    uint64_t h = 1469598103934665603ull ^ tidx;
    size_t i, n;
    const char *p;
    for (p = name; *p; p++)
        h = (h ^ (uint8_t)*p) * 1099511628211ull;
    for (i = h % cap, n = 0; n < cap; n++, i = (i + 1) % cap) {
        if (agg[i].count == 0 && agg[i].name[0] == 0) {
            snprintf(agg[i].name, sizeof(agg[i].name), "%s", name);
            agg[i].tidx = tidx;
            agg[i].count = count;
            return;
        }
        if (agg[i].tidx == tidx && !strcmp(agg[i].name, name)) {
            agg[i].count += count;
            return;
        }
    }
}

void bruteforce_profile_dump(const char *why)
{
    HANDLE proc = GetCurrentProcess();
    FILE *f;
    size_t cap = 1u << 17, k;
    ProfAgg *agg, *mod;
    uint64_t total = 0;
    static uint64_t waits[PROF_MAX_THREADS];
    char temp[MAX_PATH + 8];
    uint32_t i;
    int t;

    if (!s_tab || InterlockedExchange(&s_dumped, 1))
        return;
    InterlockedExchange(&s_running, 0);
    agg = (ProfAgg *)calloc(cap, sizeof(ProfAgg));
    mod = (ProfAgg *)calloc(4096, sizeof(ProfAgg));
    snprintf(temp, sizeof(temp), "%s.tmp", s_out);
    f = agg && mod ? fopen(temp, "w") : NULL;
    if (!f) {
        free(agg);
        free(mod);
        return;
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, NULL, TRUE);

    memset(waits, 0, sizeof waits);
    for (i = 0; i < PROF_HASH; i++) {
        ProfSlot *s = &s_tab[i];
        char name[PROF_NAME];
        IMAGEHLP_MODULE64 mi;
        if (!s->tidx)
            continue;
        symbol_name(proc, s->rip, name, sizeof(name));
        agg_add(agg, cap, s->tidx, name, s->count);
        agg_add(agg, cap, 0, name, s->count);
        if (is_wait_symbol(name))
            waits[s->tidx - 1] += s->count;
        mi.SizeOfStruct = sizeof(mi);
        agg_add(mod, 4096, s->tidx, SymGetModuleInfo64(proc, s->rip, &mi) ? mi.ModuleName : "?",
                s->count);
        agg_add(mod, 4096, 0, SymGetModuleInfo64(proc, s->rip, &mi) ? mi.ModuleName : "?",
                s->count);
        total += s->count;
    }
    qsort(agg, cap, sizeof(ProfAgg), agg_cmp);
    qsort(mod, 4096, sizeof(ProfAgg), agg_cmp);

    fprintf(f, "Profile (%s): %llu samples over %llu ticks (~1 ms each), %d threads, "
               "%llu dropped, window %.1f..%.1f s\n\n",
            why, (unsigned long long)total, (unsigned long long)s_ticks, s_nth,
            (unsigned long long)s_dropped, s_window_start, s_window_end);

    /* Machine-readable: one line per sampled thread. */
    for (t = 0; t < s_nth; t++) {
        char start[PROF_NAME];
        if (!s_th_samples[t])
            continue;
        symbol_name(proc, s_th_start[t], start, sizeof(start));
        fprintf(f, "THREAD,%d,%lu,%llu,%llu,%llu,%s\n", t, (unsigned long)s_tid[t],
                (unsigned long long)s_th_samples[t],
                (unsigned long long)(s_th_samples[t] - waits[t]),
                (unsigned long long)waits[t], start);
    }
    fprintf(f, "\n");

    fprintf(f, "== Threads (samples = ticks the thread was sampled; busy excludes kernel waits)\n");
    for (t = 0; t < s_nth; t++) {
        char start[PROF_NAME];
        int shown;
        if (!s_th_samples[t])
            continue;
        symbol_name(proc, s_th_start[t], start, sizeof(start));
        fprintf(f, "T%-3d tid %-6lu %8llu samples  busy %5.1f%%  start %s\n", t,
                (unsigned long)s_tid[t], (unsigned long long)s_th_samples[t],
                100.0 * (double)(s_th_samples[t] - waits[t]) / (double)s_th_samples[t], start);
        for (k = 0, shown = 0; k < 4096 && mod[k].count && shown < 6; k++)
            if (mod[k].tidx == (uint32_t)t + 1) {
                fprintf(f, "      module %5.1f%%  %s\n",
                        100.0 * mod[k].count / s_th_samples[t], mod[k].name);
                shown++;
            }
        for (k = 0, shown = 0; k < cap && agg[k].count && shown < 25; k++)
            if (agg[k].tidx == (uint32_t)t + 1) {
                fprintf(f, "      %5.1f%%  %s\n",
                        100.0 * agg[k].count / s_th_samples[t], agg[k].name);
                shown++;
            }
    }
    fprintf(f, "\n== All threads: top modules\n");
    for (k = 0; k < 4096 && mod[k].count && k < 15; k++)
        if (mod[k].tidx == 0)
            fprintf(f, "  %5.1f%%  %s\n", 100.0 * mod[k].count / (total ? total : 1),
                    mod[k].name);
    if (s_callers && s_ctab) {
        size_t ccap = 1u << 16;
        ProfAgg *cagg = (ProfAgg *)calloc(ccap, sizeof(ProfAgg));
        if (cagg) {
            for (i = 0; i < PROF_CHASH; i++) {
                ProfCallerSlot *s = &s_ctab[i];
                char a[PROF_NAME], b[PROF_NAME], ab[PROF_NAME];
                if (!s->tidx)
                    continue;
                symbol_name(proc, s->rip, a, sizeof(a));
                if (s->caller)
                    symbol_name(proc, s->caller, b, sizeof(b));
                else
                    snprintf(b, sizeof(b), "(no exe frame)");
                snprintf(ab, sizeof(ab), "%.70s <- %.80s", a, b);
                agg_add(cagg, ccap, s->tidx, ab, s->count);
            }
            qsort(cagg, ccap, sizeof(ProfAgg), agg_cmp);
            fprintf(f, "\n== Outside the exe, by first exe caller\n");
            for (t = 0; t < s_nth; t++) {
                int shown = 0;
                if (!s_th_samples[t])
                    continue;
                for (k = 0; k < ccap && cagg[k].count && shown < 20; k++)
                    if (cagg[k].tidx == (uint32_t)t + 1) {
                        if (!shown)
                            fprintf(f, "T%d\n", t);
                        fprintf(f, "      %5.1f%%  %s\n",
                                100.0 * cagg[k].count / s_th_samples[t], cagg[k].name);
                        shown++;
                    }
            }
            free(cagg);
        }
    }
    fprintf(f, "\n== All threads: top functions\n");
    for (k = 0; k < cap && agg[k].count && k < 160; k++)
        if (agg[k].tidx == 0)
            fprintf(f, "  %5.1f%%  %s\n", 100.0 * agg[k].count / (total ? total : 1), agg[k].name);
    SymCleanup(proc);
    free(agg);
    free(mod);
    if (fclose(f) == 0) {
        remove(s_out);
        rename(temp, s_out);
    }
    fprintf(stderr, "[PROFILE] written: %s (%llu samples, %s)\n", s_out,
            (unsigned long long)total, why);
}
