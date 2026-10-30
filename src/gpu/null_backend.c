/*
 * Diagnostic render back ends for the performance plan's Phase 0.
 *
 * RECOMP_GPU=null registers a back end whose clear and draw do nothing and
 * whose flip only counts. With it the executor still walks the pushbuffer,
 * decodes every method and transforms every vertex, but no pixel is
 * rasterised: the frame rate it reaches is the ceiling with CPU
 * rasterisation removed. RECOMP_PROBE_DROP_TRANSFORMED=1 (executor) removes
 * vertex work as well. Neither is a renderer; the picture is wrong. Its
 * visibility tests report everything visible, so the title does not cull
 * the geometry the probe is meant to measure.
 *
 * BRUTEFORCE_SURFACE_GUARD=<report> additionally answers whether anything
 * other than the GPU reads or writes rendered surfaces. Under the null back
 * end the executor never touches colour or depth surfaces, so every colour
 * and zeta surface it names is protected PAGE_NOACCESS and re-protected at
 * each flip. The first access to each page per frame faults; the handler
 * records who made it (instruction pointer, thread, read or write, which
 * surface), unprotects that page and lets the access proceed. A title that
 * reads a rendered surface on the CPU (a screenshot, a save image, a
 * lens-flare test) shows up here, and so does the framework's own presenter.
 * Pages shared with data outside the surface are reported separately.
 *
 * Accesses from kernel mode on a guest thread's behalf (a ReadFile into a
 * surface) fail rather than fault and are not observed; nothing is expected
 * to do that to a render target.
 */
#include <windows.h>
#include <dbghelp.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv2a_backend.h"
#include "nv2a_backend_ext.h"
#include "null_backend.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define GUARD_MAX_RANGES 64
#define GUARD_MAX_SITES 512
#define GUARD_PAGE 4096u

typedef struct {
    uint32_t va, bytes;          /* guest range of the surface */
    uint32_t w, h;
    char kind;                   /* 'C' colour, 'Z' zeta */
    uintptr_t host_lo, host_hi;  /* page-rounded host range */
} GuardRange;

typedef struct {
    uint64_t rip;
    uint32_t first_va;
    uint32_t count;
    char kind, access, thread, inside;
} GuardSite;

static GuardRange s_ranges[GUARD_MAX_RANGES];
static volatile LONG s_nranges;
static GuardSite s_sites[GUARD_MAX_SITES];
static volatile LONG s_nsites;
static SRWLOCK s_site_lock = SRWLOCK_INIT;
static uint64_t s_faults, s_site_overflow;
static DWORD s_nv2a_thread;
static const char *s_report;
static uint32_t s_flips, s_draws, s_clears;
static uint32_t s_excluded_page;
static int s_has_excluded_page;
static ULONGLONG s_last_report_ms;

static void record_site(uint64_t rip, uint32_t va, char kind, char access, int inside)
{
    char thread = GetCurrentThreadId() == s_nv2a_thread ? 'N' : 'O';
    LONG i, n;

    AcquireSRWLockExclusive(&s_site_lock);
    s_faults++;
    n = s_nsites;
    for (i = 0; i < n; i++) {
        GuardSite *s = &s_sites[i];
        if (s->rip == rip && s->kind == kind && s->access == access && s->thread == thread
            && s->inside == inside) {
            s->count++;
            ReleaseSRWLockExclusive(&s_site_lock);
            return;
        }
    }
    if (n < GUARD_MAX_SITES) {
        GuardSite *s = &s_sites[n];
        s->rip = rip;
        s->first_va = va;
        s->count = 1;
        s->kind = kind;
        s->access = access;
        s->thread = thread;
        s->inside = (char)inside;
        s_nsites = n + 1;
    } else {
        s_site_overflow++;
    }
    ReleaseSRWLockExclusive(&s_site_lock);
}

static LONG CALLBACK guard_handler(PEXCEPTION_POINTERS ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    uintptr_t addr;
    LONG i, n = s_nranges;

    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    addr = (uintptr_t)er->ExceptionInformation[1];
    for (i = 0; i < n; i++) {
        const GuardRange *r = &s_ranges[i];
        if (addr >= r->host_lo && addr < r->host_hi) {
            uintptr_t page = addr & ~(uintptr_t)(GUARD_PAGE - 1);
            uint32_t va = (uint32_t)(addr - (uintptr_t)xbox_GetMemoryOffset());
            DWORD old;
            int inside = va >= r->va && va - r->va < r->bytes;
            char access = er->ExceptionInformation[0] == 1 ? 'W'
                          : er->ExceptionInformation[0] == 8 ? 'X' : 'R';
            if (!VirtualProtect((LPVOID)page, GUARD_PAGE, PAGE_READWRITE, &old))
                return EXCEPTION_CONTINUE_SEARCH;
            record_site((uint64_t)ep->ContextRecord->Rip, va, r->kind, access, inside);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void guard_note(uint32_t va, uint32_t pitch, uint32_t w, uint32_t h, char kind)
{
    uintptr_t base = (uintptr_t)xbox_GetMemoryOffset();
    uint32_t bytes = pitch * h;
    LONG i, n = s_nranges;

    if (!s_report || !va || !bytes)
        return;
    for (i = 0; i < n; i++)
        if (s_ranges[i].va == va && s_ranges[i].kind == kind) {
            if (bytes > s_ranges[i].bytes) {
                s_ranges[i].bytes = bytes;
                s_ranges[i].host_hi = (base + va + bytes + GUARD_PAGE - 1)
                                      & ~(uintptr_t)(GUARD_PAGE - 1);
            }
            return;
        }
    if (n >= GUARD_MAX_RANGES)
        return;
    s_ranges[n].va = va;
    s_ranges[n].bytes = bytes;
    s_ranges[n].w = w;
    s_ranges[n].h = h;
    s_ranges[n].kind = kind;
    s_ranges[n].host_lo = (base + va) & ~(uintptr_t)(GUARD_PAGE - 1);
    s_ranges[n].host_hi = (base + va + bytes + GUARD_PAGE - 1) & ~(uintptr_t)(GUARD_PAGE - 1);
    MemoryBarrier();
    s_nranges = n + 1;
}

static void note_surfaces(const Nv2aSurface *s, const Nv2aRenderState *rs)
{
    if (!s_nv2a_thread)
        s_nv2a_thread = GetCurrentThreadId();
    guard_note(s->color_va, s->pitch, s->width, s->height, 'C');
    /* The zeta pitch is the top half of SET_SURFACE_PITCH, which a back end
     * is not given; a 32-bit depth buffer the size of the colour surface is
     * the common case and the guard rounds to pages anyway. */
    if (rs && rs->zeta_va)
        guard_note(rs->zeta_va, s->width * 4, s->width, s->height, 'Z');
}

static void guard_protect(uintptr_t lo, uintptr_t hi)
{
    DWORD old;
    if (hi > lo && !VirtualProtect((LPVOID)lo, hi - lo, PAGE_NOACCESS, &old)) {
        fprintf(stderr, "[GPU] surface guard protection failed at %p (%lu)\n",
                (void *)lo, (unsigned long)GetLastError());
        abort();
    }
}

static void guard_arm(void)
{
    uintptr_t excluded = (uintptr_t)xbox_GetMemoryOffset() + s_excluded_page;
    LONG i, n = s_nranges;
    for (i = 0; i < n; i++) {
        uintptr_t lo = s_ranges[i].host_lo, hi = s_ranges[i].host_hi;
        if (!s_has_excluded_page || excluded >= hi || excluded + GUARD_PAGE <= lo) {
            guard_protect(lo, hi);
        } else {
            if (excluded > lo)
                guard_protect(lo, excluded);
            if (excluded + GUARD_PAGE < hi)
                guard_protect(excluded + GUARD_PAGE, hi);
        }
    }
}

static void guard_write_report(void)
{
    static int sym_ready;
    HANDLE proc = GetCurrentProcess();
    char temp[MAX_PATH + 8];
    FILE *f;
    LONG i, n;

    if (!sym_ready) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        sym_ready = SymInitialize(proc, NULL, TRUE) ? 1 : -1;
    }
    snprintf(temp, sizeof temp, "%s.tmp", s_report);
    f = fopen(temp, "w");
    if (!f)
        return;
    AcquireSRWLockShared(&s_site_lock);
    fprintf(f, "# surface guard: %u flips, %u draws, %u clears, %llu faults,"
               " %llu sites dropped\n", s_flips, s_draws, s_clears,
            (unsigned long long)s_faults, (unsigned long long)s_site_overflow);
    if (s_has_excluded_page)
        fprintf(f, "# EXCLUDED_PAGE,%08X,%u: accesses on this page are not observed\n",
                s_excluded_page, GUARD_PAGE);
    n = s_nranges;
    for (i = 0; i < n; i++)
        fprintf(f, "SURFACE,%c,%08X,%u,%ux%u\n", s_ranges[i].kind, s_ranges[i].va,
                s_ranges[i].bytes, s_ranges[i].w, s_ranges[i].h);
    fprintf(f, "kind,access,thread,inside,count,first_va,rip,symbol\n");
    n = s_nsites;
    for (i = 0; i < n; i++) {
        const GuardSite *s = &s_sites[i];
        char buf[sizeof(SYMBOL_INFO) + 256], name[300];
        SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
        DWORD64 disp = 0;
        memset(buf, 0, sizeof buf);
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 255;
        if (sym_ready > 0 && SymFromAddr(proc, s->rip, &disp, si))
            snprintf(name, sizeof name, "%s+0x%llx", si->Name, (unsigned long long)disp);
        else {
            HMODULE module;
            char path[MAX_PATH];
            DWORD length;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(uintptr_t)s->rip, &module) &&
                (length = GetModuleFileNameA(module, path, sizeof path)) != 0 &&
                length < sizeof path) {
                const char *base = strrchr(path, '\\');
                snprintf(name, sizeof name, "%s+0x%llx", base ? base + 1 : path,
                         (unsigned long long)(s->rip - (uintptr_t)module));
            } else {
                snprintf(name, sizeof name, "?");
            }
        }
        fprintf(f, "%c,%c,%c,%d,%u,%08X,%016llx,%s\n", s->kind, s->access, s->thread,
                s->inside, s->count, s->first_va, (unsigned long long)s->rip, name);
    }
    ReleaseSRWLockShared(&s_site_lock);
    if (fclose(f) == 0) {
        remove(s_report);
        rename(temp, s_report);
    }
}

static void null_clear(const Nv2aSurface *s, const Nv2aRenderState *rs, uint32_t flags,
                       uint32_t argb, uint32_t zstencil)
{
    (void)flags; (void)argb; (void)zstencil;
    s_clears++;
    note_surfaces(s, rs);
}

static void null_draw(const Nv2aSurface *s, const Nv2aBatch *b)
{
    s_draws++;
    note_surfaces(s, b->state);
}

static void null_flip(void)
{
    s_flips++;
    if (!s_report)
        return;
    {
        ULONGLONG now = GetTickCount64();
        if (now - s_last_report_ms >= 2000) {
            s_last_report_ms = now;
            guard_write_report();
        }
    }
    guard_arm();
}

static const Nv2aBackend s_null_backend = {null_clear, null_draw, null_flip};

/* Visibility tests read as fully visible (a 640x480 surface's worth of
 * pixels): a title that culls by them then draws everything, which keeps the
 * probe from measuring a scene with its geometry culled away. */
static void null_zpass(int op)
{
    (void)op;
}

static uint32_t null_zpass_count(void)
{
    return 640u * 480u;
}

static const Nv2aBackendExt s_null_ext = {NULL, NULL, NULL, NULL, NULL, null_zpass,
                                          null_zpass_count};

int bruteforce_null_backend_install(void)
{
    const char *exclude = getenv("BRUTEFORCE_SURFACE_GUARD_EXCLUDE_PAGE");

    s_report = getenv("BRUTEFORCE_SURFACE_GUARD");
    if (s_report && !*s_report)
        s_report = NULL;
    if (exclude && *exclude) {
        char *end;
        unsigned long long value;
        errno = 0;
        value = strtoull(exclude, &end, 0);
        if (!s_report || errno || *end || end == exclude || value > UINT32_MAX ||
            (value & (GUARD_PAGE - 1))) {
            fprintf(stderr, "[GPU] BRUTEFORCE_SURFACE_GUARD_EXCLUDE_PAGE requires a"
                            " page-aligned guest address and an enabled surface guard\n");
            return 0;
        }
        s_excluded_page = (uint32_t)value;
        s_has_excluded_page = 1;
        fprintf(stderr, "[GPU] surface guard excludes guest page 0x%08X; accesses on it"
                        " will not be observed\n", s_excluded_page);
    }
    if (s_report && !AddVectoredExceptionHandler(1, guard_handler)) {
        fprintf(stderr, "Cannot register the surface guard handler\n");
        return 0;
    }
    nv2a_backend_ext_register(&s_null_ext);
    nv2a_backend_register(&s_null_backend);
    fprintf(stderr, "[GPU] null back end registered (RECOMP_GPU=null): nothing is drawn%s%s\n",
            s_report ? "; surface guard report -> " : "", s_report ? s_report : "");
    return 1;
}
