# Upstream contribution candidates

This is a worklist, not a record of filed pull requests. The local framework
changes are reproducible through [toolkit.py](../tools/toolkit.py); they have
not all been prepared or validated as independent upstream contributions.
Recheck the current upstream tree and open work before filing, and use
synthetic reproducers rather than game data or generated game code.

The goal is that the next project built on xboxrecomp does not rediscover
these problems. Every framework change this project makes, and every
framework-level cause found while diagnosing Brute Force, gets an entry here,
even if the local fix is title-specific or not yet written.

## Entry format

Each candidate states:

- **Problem:** what goes wrong in the framework, independent of Brute Force.
- **Evidence:** how it was observed, separating observation from hypothesis.
  Name the generation or run for title evidence, but keep logs local.
- **Change:** the local patch or planned change, and whether it is generic or
  depends on title-specific knowledge.
- **Tests:** synthetic fixtures that would accompany a PR, and what they do
  not cover.
- **Overlap:** open or merged upstream PRs and other projects' prior art, with
  the date checked.
- **Before filing:** remaining work.
- **Status:** one of *planned*, *local patch*, *prepared*, *filed* (with PR
  number), *merged*, or *dropped* (with reason).

Credit prior art. Where a candidate adapts another project's work (for
example, Blood Wake's carried toolkit patches), name the source. Check
whether its authors are already upstreaming it before preparing a separate
PR.

## Already selected upstream work

These are existing upstream pull requests, not new proposals. Their pinned
commits are applied on top of xboxrecomp base
`f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b`; the resulting selected-commit and
local-patch tree is verified as `ec3fa862999fa1f2bd58d5eaffbaee04d7baf68d`.
The exact commit list and application order are in
[toolkit.py](../tools/toolkit.py).

| PR | Purpose | Status checked 2026-10-29 |
| --- | --- | --- |
| [#262](https://github.com/sp00nznet/xboxrecomp/pull/262) | Preserve alias bodies during the disassembler gap sweep. | Open |
| [#280](https://github.com/sp00nznet/xboxrecomp/pull/280) | Publish general EBP across guest calls. | Open |
| [#277](https://github.com/sp00nznet/xboxrecomp/pull/277) | Correct shift counts, narrow products, and x87 save/restore. | Open |
| [#278](https://github.com/sp00nznet/xboxrecomp/pull/278) | Read narrow immediates at the destination width. | Open |

These statuses can change. They do not imply that any candidate below has been
filed or accepted.

## Local patches to evaluate

1. **Bound the kernel thunk lookbehind scan.**
   [0001-bound-extra-thunks.patch](../patches/xboxrecomp/0001-bound-extra-thunks.patch)
   rejects zero and out-of-range ordinals rather than rewriting arbitrary
   high-bit-set data. The patch includes a synthetic kernel-bridge regression
   for valid and invalid candidates. Before filing, check current upstream
   behavior and whether in-range ordinary data can still be mistaken for an
   import; add any stronger table-boundary evidence that is available.

2. **Model `WBINVD` ordering for the lifted guest.**
   [0002-model-wbinvd.patch](../patches/xboxrecomp/0002-model-wbinvd.patch)
   emits a C11 sequentially consistent fence and keeps EFLAGS tracking intact.
   Its lifter tests check emission, a subsequent conditional branch, and that
   `INVD` remains unimplemented. This is a partial model: guest caches are not
   modeled, and a C11 fence alone is not proof of all device, DMA,
   non-temporal-store, or host memory-ordering semantics. Before filing,
   establish the intended contract across supported compilers and hosts and
   cover their build requirements; this project needs MSVC's
   `/experimental:c11atomics` option.

3. **Synchronize publication of the GPU semaphore target.**
   [0003-synchronize-semaphore-target.patch](../patches/xboxrecomp/0003-synchronize-semaphore-target.patch)
   makes the target address atomic on Windows and other supported compiler
   paths. The title-specific hook and its
   [fixture](../tests/test_fence.py) validate registration and reject a changed
   target, but the fixture substitutes the setter and does **not** stress the
   real executor's cross-thread publication. Add an executor-level concurrency
   test and evaluate pending releases and device-reset behavior before
   proposing this as a general fix.

4. **Make successful-read byte tracing opt-in.**
   [0004-gate-read-tracing.patch](../patches/xboxrecomp/0004-gate-read-tracing.patch)
   suppresses successful-read logs unless `RECOMP_READ_TRACE` is set, while
   still logging failed reads. The real-bridge
   [fixture](../tests/test_read_trace.py) checks quiet successes, explicitly
   traced bytes, and default failure diagnostics. Explicit tracing can also
   print guest bytes on failures; update the patch's misleading comment before
   filing and describe the logging/privacy implications.

5. **Protect Windows framebuffer snapshots against reuse.**
   [0005-synchronize-presented-frames.patch](../patches/xboxrecomp/0005-synchronize-presented-frames.patch)
   locks both publication and copying: two flips can otherwise reuse a buffer
   while the window thread is reading it. The
   [native fixture](../tests/present_test.c) tests format conversion,
   reader/writer exclusion, and concurrent whole-frame snapshots; a later
   local capture also matched a completed flip in the checked text region.
   Before filing, make the fixture easy to run upstream, check other presenter
   paths and lock cost, and avoid presenting the limited capture as proof of
   general rendering correctness.

6. **Give the experimental OHCI controller a safe lifecycle.**
   [0006-harden-usb-lifecycle.patch](../patches/xboxrecomp/0006-harden-usb-lifecycle.patch)
   reports initialization failures, rolls back MMIO protection, retains and
   joins the worker, retires the IRQ callback under the strict gate, and
   reports failed shutdown. [test_usb.py](../tests/test_usb.py) covers
   enumeration, rollback, late startup, and shutdown failure paths using the
   Windows native fixture; the patch also updates the upstream POSIX fixture.
   The API change and the requirement to stop guest threads before unmapping
   need explicit caller migration, cross-platform tests, and lifecycle review.
   [#252](https://github.com/sp00nznet/xboxrecomp/pull/252) is related open
   Linux x86-64 MMIO work, not an established duplicate. Do not generalize
   this project's title-specific XPP instruction guard into a USB lifecycle
   fix.

7. **Experiment with NV2A window clipping.**
   [0009-experimental-window-clipping.patch](../patches/xboxrecomp/0009-experimental-window-clipping.patch)
   adds opt-in software-rasterizer clipping with eight propagated rectangles,
   inclusive/exclusive modes, and anti-aliasing scaling. The
   [native fixture](../tests/window_clip_test.c) exercises rectangle boundaries,
   mode switching, depth writes, and both software draw paths. Initialization
   before both axes are programmed is an unverified compatibility choice;
   hardware back ends are explicitly rejected while the option is enabled.
   Compare with more hardware tests and real game captures before proposing it
   upstream or enabling it by default. **Overlap:**
   [#222](https://github.com/sp00nznet/xboxrecomp/pull/222) (open, checked
   2026-10-29) records the eight rectangles but folds only slot 0 into a
   scissor and passes multi-rectangle unions unclipped. It also models the
   clear rectangle. Reconcile with #222 rather than filing a competing
   implementation. This patch's union handling and fixture may be the useful
   contribution.

8. **Experiment with software face culling.**
   [0010-experimental-face-culling.patch](../patches/xboxrecomp/0010-experimental-face-culling.patch)
   adds independent opt-in culling and corrects triangle-strip winding while
   culling is active. The [native fixture](../tests/window_clip_test.c) covers
   both raster paths, face modes, winding, strips, fans, quads, near-plane
   clipping, depth ordering, and interaction with window clipping. The
   implementation waits for both face settings because their reset state has
   not been established, and rejects registered hardware draw backends.
   Verify winding, reset state, and title output against original hardware
   before proposing it upstream or enabling it by default. **Overlap:**
   [#222](https://github.com/sp00nznet/xboxrecomp/pull/222) also adds face
   culling, from projected winding, with an unwritten front face treated as
   counterclockwise (the GL reset). It differs from this patch's choice to
   wait until both settings are programmed. Compare the two reset assumptions
   and the strip-winding handling, and contribute fixtures or corrections to
   #222 instead of a duplicate. [#249](https://github.com/sp00nznet/xboxrecomp/pull/249)
   (strip winding) is related.

9. **Experiment with programmable screen-zero near clipping.**
   [0011-experimental-program-near-plane.patch](../patches/xboxrecomp/0011-experimental-program-near-plane.patch)
   adds an opt-in screen-space `z = 0` near plane for programmable vertices;
   the legacy `c[59].z` plane remains the default and fixed-function behavior
   is unchanged. The [native fixture](../tests/window_clip_test.c) covers
   depth and eye-plane clipping, triangle strips, depth writes, anti-aliasing,
   window clipping, face culling, and configuration guards. The hypothesis
   is informed by [xemu's programmable output conversion](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/glsl/vsh-prog.c):
   with its positive format-dependent zeta maximum, its normalized output clips
   at screen `z = 0`. Its behavior has not been verified against original hardware,
   all depth-range modes, or all shaders. Do not propose it as an established
   hardware fix or enable it by default without those checks.

10. **Vectorize software combiner color unpacking.**
    [0012-vectorize-color-unpack.patch](../patches/xboxrecomp/0012-vectorize-color-unpack.patch)
    evaluates the four existing divisions together on SSE2 hosts and retains
    the scalar implementation elsewhere. The
    [native fixture](../tests/color_unpack_test.c) compares against the
    scalar expression, including all channel values, unaligned output, and
    SSE rounding modes. Before proposing upstream, repeat validation on
    additional compilers and supported non-x86 hosts.

11. **Avoid redundant work in software texture sampling.**
    [0013-optimize-texture-sampling.patch](../patches/xboxrecomp/0013-optimize-texture-sampling.patch)
    fast-paths in-range texture coordinates and uses shifts for the current
    8- and 16-byte DXT cache indices while retaining the division fallback
    for other block sizes. The [math fixture](../tests/texture_math_test.c)
    checks wrap, mirror, clamp, edge coordinates, and cache-slot equivalence;
    the [raster fixture](../tests/window_clip_test.c) checks DXT1/3/5 decoding
    and blocks rewritten at the same address.

## Performance and GPU back-end candidates

These come from the [performance plan](./PERFPLAN.md) and from reading the
pinned framework and Blood Wake at
[`e631edd`](https://github.com/minimalice92/bloodwake-recomp/tree/e631edddfe56d22344e9f696923aeb6d78a9e7ba).
Blood Wake pins the same base (`f0f8a461`) and carries most of these as
patched toolkit files under `src/toolkit_patches/`. Unless an item says
otherwise, its status is *planned*: no local patch exists yet. Upstream
overlap was checked on 2026-10-29 with a keyword search of all 273 PR titles
(newest #285), not an exhaustive review of their contents. Adapted Blood
Wake code is listed in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md);
before preparing any of these, ask its authors whether they are upstreaming
the same parts.

The common problem is that the framework's GPU path is built around the CPU
rasterizer. Every title that needs playable frame rates currently writes its
own GPU back end, an extended interface for it, and its own pacing fixes.

12. **Extend the back-end interface to the full pixel pipeline.**
    *Problem:* `nv2a_backend.h` passes texture stage 0, the diffuse colour,
    and one coordinate set; its own comment says combiner state and stages
    1–3 are not passed. A hardware back end cannot reproduce what the CPU
    rasterizer draws for any title that uses register combiners. Two further
    gaps surfaced while bringing up the Brute Force back end:
    - **Visibility tests read zero with any registered back end.** The
      executor answers `GET_REPORT` with pixel counts that only the CPU
      rasterizer accumulates. With a back end the count stays 0, and a title
      that culls by D3D's visibility tests draws nothing behind them.
    - **A back end that renders on the GPU has to put the frame into guest
      memory before the presenter, frame dumps and frame statistics read
      it,** which happens before `flip()`.
    *Evidence:* Brute Force's CPU profile includes `nv2a_rc_eval` and
    `nv2a_rc_unpack` (FINDINGS), and the census shows combiners in every
    draw. Blood Wake had to add `bw_backend_ext.h` for the same reason. With
    the first D3D11 back end and no visibility support, the tutorial's world
    frames carried about 8,000 program vertices instead of about 130,000,
    and only the HUD and the clear colour were visible (local run
    `trial-d3d11-1`, generation `20261029-175500-dfadbdd9`). With the counts
    answered from occlusion queries, the world is submitted and drawn.
    *Change:*
    [0016-extended-render-backend.patch](../patches/xboxrecomp/0016-extended-render-backend.patch),
    adapted from Blood Wake's `bw_backend_ext.h` and carried executor, adds
    `nv2a_backend_ext.h`. For transformed batches it passes, per vertex,
    both colours, the fog factor after the fog unit and four coordinate sets.
    Per batch it passes the four stages, the stage program, the combiner
    registers, the fog colour, the clip-plane mode and the near plane. It
    also adds:
    - `readback(color_va)`: called at a flip before the frame is presented,
      dumped or hashed; it returns whether guest memory now holds the frame;
    - `zpass(op)` and `zpass_count()`: bracket visibility counting and answer
      `GET_REPORT`; without them the executor warns once that tests read 0;
    - `nv2a_backend_clear_rect()` and `nv2a_backend_reg()`: the clear
      rectangle and the register mirror.

    With an extended back end registered, the face-culling and
    programmable near-plane experiments no longer abort, because the executor
    applies both before CPU-transformed triangles reach the back end. Window
    clipping still requires the software rasterizer. Generic; no title
    knowledge.
    *Tests:* the [frame-stats fixture](../tests/frame_stats_test.c) checks
    that a transformed batch reaches the extended draw with surface position,
    1/w, colours, fog, combiners and near clipping, that the readback hook
    gets the presented surface before the frame is hashed, and that both
    experiments are accepted. The [D3D11 fixture](../tests/d3d11_test.c)
    checks a visibility test's count (1,296 pixels for a 36×36 quad) on the
    CPU path and on the GPU path. Not covered: occlusion counts while an
    anti-aliased surface is bound, and clear rectangles on the GPU path.
    *Overlap:* [#184](https://github.com/sp00nznet/xboxrecomp/pull/184)
    indexes a D3D11 and Metal pushbuffer back-end series awaiting maintainer
    direction. [#285](https://github.com/sp00nznet/xboxrecomp/pull/285)
    (open, this project's account, checked 2026-10-29) adds
    `nv2a_backend_register_with_present_sync(backend, void (*)(void))`, a
    pre-snapshot callback without the presented address or a result.
    `readback` here does the same job and additionally tells the frame
    statistics whether the frame is in guest memory. Reconcile by amending
    #285 (pass the address, return a result) rather than proposing a second
    hook.
    *Before filing:* agree on one interface with #184 and #285; version the
    structure; test the visibility path on the CPU rasterizer and on a
    fake back end without the optional entries.
    *Status:* local patch.

13. **Hand vertex-program batches to the back end untransformed.**
    *Problem:* vertex programs run per vertex in `nv2a_vsh_interp.c` on the
    NV2A thread, which D3D's KickOff and fence waits depend on.
    *Evidence:* in the stationary tutorial profile, `nv2a_vsh_run` was 34% of
    the NV2A thread's samples (28% while moving); the census found every
    tutorial draw on the vertex-program path and no program writing
    constants (FINDINGS). Blood Wake reports that the interpreter was 42% of
    its NV2A thread before this change.
    *Change:*
    [0017-gpu-vertex-programs.patch](../patches/xboxrecomp/0017-gpu-vertex-programs.patch),
    adapted from Blood Wake's carried executor. The interpreter exposes the
    program from its start slot to FINAL, a constant-file generation counter
    and the constant-write switch. The executor offers each program batch to
    an optional `draw_vsh` before transforming it, passing:
    - the program and the constants with their generation;
    - each vertex stream's span over the batch's index range as the title's
      bytes with a conversion descriptor where `fetch_attr` has an exact GPU
      twin, otherwise the gathered floats;
    - a triangle list, with strip triangles in one winding for culling, and
      whether to cull as the CPU path would.

    A back end returns 0 to have the CPU transform the batch, for example
    while its shader compiles. Programs that write constants always stay on
    the CPU. `RECOMP_GPU_VSH=0` disables the hand-off,
    `RECOMP_GPU_VSH_RAW=0` always gathers, and `RECOMP_GPU_VSH_VERIFY=<n>`
    reads back every n-th GPU batch's vertices (through the optional
    `vsh_readback`) and compares them with the interpreter. Generic.
    *Tests:* the [D3D11 fixture](../tests/d3d11_test.c) draws a scene with a
    vertex program (`mov` of a float4 position, a D3DCOLOR diffuse stream
    and a float2 coordinate set) on the CPU and through `draw_vsh` on WARP.
    It requires the images to match apart from listed edge pixels, and every
    GPU batch to verify with 0 differing vertices. Not covered: the SHORT,
    UBYTE and packed-normal conversions, relative constant addressing and
    culling on the GPU path.
    *Overlap:* [#272](https://github.com/sp00nznet/xboxrecomp/pull/272)
    (open, decode programs once) and
    [#273](https://github.com/sp00nznet/xboxrecomp/pull/273) (fast path for
    constant uploads) speed up the CPU path. They complement this change, and
    the CPU fallback still benefits.
    *Before filing:* fixtures for the remaining conversions and for culling;
    coordinate the interface with item 12 and #184.
    *Status:* local patch.

14. **Provide a reference D3D11 back end in the framework.**
    *Status:* project code, not a framework patch yet. `src/gpu/` holds a
    D3D11 back end adapted from Blood Wake's generic parts
    (`d3d11_backend.c`, `rc_hlsl.c`, `vsh_hlsl.c`). It matches the CPU path
    on synthetic WARP and hardware fixtures within listed differences
    (FINDINGS), but the title-frame comparison still has unexplained
    differences. It has no persistent shader-bytecode cache yet.
    *Problem:* the framework's `src/nv2a/nv2a_pgraph_d3d11.c` is a
    title-coupled menu translator built on the D3D8 layer, not a general
    back end. The combiner HLSL in `d3d8_combiners.c` serves the HLE D3D8
    path, not the pushbuffer executor.
    *Change:* a general back end with:
    - GPU-authoritative colour and depth surfaces;
    - render-to-texture without readback;
    - a hashed texture cache, direct BC uploads, and GPU compute decoding
      of common swizzled, linear, and P8 formats;
    - state-object caches;
    - pixel shaders from combiner state and vertex shaders from vertex
      programs, matching the framework's CPU evaluators rule for rule;
    - a persistent shader-bytecode cache (planned).

    The generic parts of Blood Wake's `bw_d3d11.c`, `bw_rc_hlsl.c`,
    `bw_vsh_hlsl.c`, and `bw_pscache.c` are prior art. Its stale-pitch
    normalization and `fast_texture` also motivated avoiding the CPU
    decoder for formats that can be transferred or decoded on the GPU.
    *Tests:* WARP and hardware parity fixtures against the CPU renderer;
    140 exact CPU/GPU texture-decode cases per device cover rectangular
    swizzles, padded linear rows, P8, and cube faces with mip padding.
    The palette-rewrite fixture exercises cache invalidation within a frame.
    *Overlap:* #184's D3D11 back end, and
    [#274](https://github.com/sp00nznet/xboxrecomp/pull/274) (compile once
    per HLSL text), which applies the same keying idea to the D3D8 layer.
    Same-frame raw texture rewrites and changes between sparse-hash samples
    can still be missed. This is the largest candidate. Resolve cache
    invalidation and coordinate with #184 and Blood Wake's authors before
    preparing anything.

15. **Present from the back end, with readback on demand.**
    *Problem:* the presenter copies or converts the flipped guest frame on
    the CPU, and a GPU back end must read its surface back into guest memory
    every flip to feed it. Blood Wake measured about 4 ms per flip for that
    wait.
    *Change:* let a registered back end take over the framebuffer window's
    swap chain (`fb_present.c` hands over the window and stops painting),
    and make per-flip readback opt-in for dumps, diagnostics, or titles
    whose CPU reads rendered surfaces. Bound the number of GPU frames in
    flight.
    *Overlap:* #285 covers ordering the readback against snapshots;
    [#215](https://github.com/sp00nznet/xboxrecomp/pull/215) (merged) added
    the D3D11 presenter.

16. **Let the NV2A thread sleep when idle.**
    *Problem:* the ack thread in `xbox_memory_layout.c` ends each pass with
    `Sleep(0)`, holding a host core even between frames.
    *Change:* spin while work is recent, then nap on a high-resolution
    waitable timer or until a KickOff wakes it. Blood Wake's `bw_ack_idle`
    is prior art.
    *Tests:* latency from PUT to walk start, and an idle CPU-time bound.

17. **Signal the PFB flush acknowledgement.**
    *Problem:* XDK KickOff sets bit 16 of `0x100410` and spins until it
    clears. A title hook that replaces the spin can only poll, and each poll
    loses up to a timer period after the ack.
    *Evidence:* the Brute Force flush poll at `0x00372180` appeared 210 times
    in the CPU sample. Blood Wake measured its game thread at 49% in this
    wait before signalling it.
    *Change:* the framework signals an event or callback when it clears the
    bit, and exposes a wait helper that also returns at least every ~200 µs
    for IRQ safe points. The protocol and registers are unchanged.
    *Status:* the title-side KickOff hook is title-specific; only the signal
    and wait helper are generic.

18. **Walk one KickOff segment at a time.**
    *Problem:* the walk runs from the last PUT to the register's current PUT
    before moving GET. A KickOff waiting for GET therefore waits for the
    whole batch, and the walker then idles while the title fills the next
    segment.
    *Change:* an API for a KickOff hook to queue each published PUT. The walk
    stops at each, and GET advances after each. This changes when a held
    flip takes effect, so it must be opt-in and tested against flip pacing
    and PGRAPH trap ordering.
    *Overlap:* [#231](https://github.com/sp00nznet/xboxrecomp/pull/231)
    pipelines the CPU rasterizer behind the executor. That addresses a
    related stall on the CPU path, not segment granularity.

19. **Report distinct frames, not only flips.**
    *Problem:* the executor's periodic report and the window title count
    flips. A flip can repeat a frame, so every Brute Force performance
    figure so far needs a caveat that it is not world-frame FPS.
    *Evidence:* in Brute Force's stationary tutorial view, every flip is a
    full re-render (about 1,600 draws) whose picture is byte-identical to
    the previous one (FINDINGS); flips and distinct frames differ by design.
    *Change:*
    [0014-frame-statistics.patch](../patches/xboxrecomp/0014-frame-statistics.patch)
    adds `RECOMP_FRAME_STATS=<file>`: one CSV line per flip with host time,
    draws and program vertices since the previous flip, a hash of the
    presented surface's visible pixels (unknown when a back end owns the
    pixels and did not read them back), whether the picture changed, and a
    hash of the vertex constants uploaded since the previous flip, a
    renderer-independent hint for runs without pictures. The periodic report
    gains a flips/distinct line. Generic.
    *Tests:* the [frame-stats fixture](../tests/frame_stats_test.c) checks
    the hash (padding and pixels outside the rectangle ignored, first and
    last visible bytes counted, shape included), the distinct logic, and
    the executor at real flips (repeat, change, back end, constants).
    *Before filing:* decide whether hashing belongs in the executor or the
    presenter; consider a GPU-side hash for back ends.
    *Status:* local patch.

20. **Per-thread profiling and a GPU feature census.**
    *Problem:* bring-up projects guess where time goes and which NV2A
    features a title uses.
    *Change:*
    [0015-gpu-census-and-ceiling-probe.patch](../patches/xboxrecomp/0015-gpu-census-and-ceiling-probe.patch)
    adds `RECOMP_GPU_CENSUS=<file>`, optionally gated by
    `RECOMP_GPU_CENSUS_GATE=<file>` (count only while it exists). Per draw
    it counts the transform path, primitive, source, surface size, format
    and AA, combiner control and stage count, stage programs and modes,
    texture formats, layouts, sizes, addressing, filters and levels,
    render-to-texture, blend, alpha, depth, stencil, fog, cull, window-clip
    and polygon-offset state, and constant-writing programs. It also counts
    clears, presented surfaces and every unhandled method, in the registers'
    own encodings. The table is rewritten at each report. The same patch adds
    `RECOMP_PROBE_DROP_TRANSFORMED=1`, a measurement aid that drops
    transformed batches before any vertex is transformed. The sampling
    profiler is project tooling for now: [src/profile.c](../src/profile.c),
    adapted from Blood Wake's `bw_profile.c`. It writes its report when its
    time or file gate closes, because a watchdog exit skips `atexit`, and it
    classifies kernel waits.
    *Tests:* the [frame-stats fixture](../tests/frame_stats_test.c) checks
    gated counting, render-to-texture detection, clears and unhandled
    methods, and that the probe drops exactly the transformed batches. The
    profiler and the census were exercised on the title (FINDINGS); the
    profiler has no synthetic fixture.
    *Before filing:* decide whether these belong in the runtime or in
    tooling. Keep output free of title data by default.
    *Status:* local patch (census, probe); project tooling (profiler).

22. **Write visibility reports asynchronously.**
    *Problem:* with item 12's synchronous answer, every `GET_REPORT` waits
    for the GPU. Brute Force reads 200–600 reports per frame; waiting took
    1.5–3.9 s per 10 s of NV2A-thread time on the D3D11 path (FINDINGS).
    *Change:*
    [0018-async-visibility-reports.patch](../patches/xboxrecomp/0018-async-visibility-reports.patch)
    adds optional `zpass_report(report_va, stamp)` and `poll()` entries: a
    back end may take over writing a report once its count is known, as the
    GPU writes it after the preceding draws, and `poll()` runs on every pass
    of the NV2A thread so pending reports complete while the title waits.
    The D3D11 back end implements it behind `RECOMP_GPU_ZPASS=async`, off by
    default. Generic.
    The D3D11 implementation now polls only on its device-owning thread,
    retires discarded in-flight queries instead of reusing them, and puts
    an event query after each report's preceding GPU work. Query failures
    stop explicitly rather than writing fabricated successful results.
    *Evidence:* the old fixture let the framework's ack thread poll the
    single-threaded D3D device while the fixture's main thread drew. That is
    an API violation and a plausible contributor to the WARP device loss;
    the separate effects of the fixes have not been isolated.
    *Tests:* the [D3D11 fixture](../tests/d3d11_test.c) checks ordered
    reports, active counting, clears, empty intervals, and more than 64
    counting intervals on WARP and hardware. CPU and D3D counts differ on
    the shared triangle edge, as expected from their rasterization rules.
    *Overlap:* #184's GPU series; no matching visibility-report PR was found
    in the upstream PR listing checked 2026-10-29.
    *Before filing:* measure on the title; verify report lifetime, device
    reset, and polling behavior across drivers.
    *Status:* local patch and opt-in project implementation.

23. **Avoid `getenv` in per-draw and per-CALL code.**
    *Problem:* `draw_primitive` called `getenv("RECOMP_PB_EXEC_VERBOSE")` for
    every draw, and the pushbuffer walker evaluated
    `getenv("RECOMP_PB_TRACE_CALLS")` before a cheap counter test on every
    CALL. `strchr` (from `getenv`) was 4.5% of the NV2A thread on the D3D11
    path.
    *Change:*
    [0019-cache-hot-getenv.patch](../patches/xboxrecomp/0019-cache-hot-getenv.patch)
    reads both once. Plain efficiency fix; no behaviour change.
    *Tests:* existing pushbuffer and executor fixtures; not yet re-profiled.
    *Status:* local patch.

24. **Expose palette and cube-layout metadata to render back ends.**
    *Problem:* `Nv2aTexture` omitted the P8 palette, and a stale linear
    pitch incorrectly made a swizzled or compressed texture report one mip
    level. A back end could not decode P8 or locate cube faces correctly.
    *Evidence:* the Brute Force tutorial census recorded P8 and cube use;
    the CPU decoder already samples P8 through the guest palette.
    *Change:* [0020-texture-metadata.patch](../patches/xboxrecomp/0020-texture-metadata.patch)
    passes the palette through both backend paths and the reference decoder,
    and preserves the programmed level count for format-sized textures.
    The D3D11 back end ignores stale pitches for swizzled and DXT data, as
    Blood Wake's `drop_stale_pitch` does, and includes mip padding in cube
    face offsets. Generic.
    *Tests:* CPU/GPU P8 decoding and same-frame palette rewrite, plus cube
    faces with multiple levels on WARP and hardware.
    *Overlap:* [#145](https://github.com/sp00nznet/xboxrecomp/pull/145)
    added CPU P8 sampling; #184 tracks the GPU interface. Checked
    2026-10-29. [xemu's texture code](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/texture.c)
    derives a 32/64/128/256-entry palette length from the register; its
    OpenGL backend hashes and bounds-checks that selected range. Its
    conversion directly indexes the palette, so it does not establish
    hardware behavior for out-of-range indices.
    *Before filing:* reconcile the backend interface with #184, model the
    palette-length bits rather than always reading 256 entries, and test
    shorter allocations and cube layouts on other titles.
    *Status:* local patch.

25. **Support depth textures and depth-surface aliases.**
    *Problem:* the CPU reference decoder and this project's D3D11 backend
    do not sample `LIN_D24S8` (`0x2E`). GPU depth surfaces are not exposed
    as shader resources.
    *Evidence:* both variants of the 2026-10-29 movement campaign repeatedly
    logged failed `0x2E` decodes. Whether those reads explain a visible
    rendering difference has not been determined.
    *Change:* investigate NV2A depth sampling semantics and aliasing first;
    then add the appropriate CPU reference behavior and GPU depth views.
    *Tests:* synthetic format, depth-render-to-texture, and ordering cases,
    plus matched title captures.
    *Overlap:* Blood Wake's optional ambient-occlusion path in
    `bw_d3d11.c` creates an `R24G8_TYPELESS` resource with D24S8 and R24
    views. It is not a guest-depth-alias implementation. Recheck #184 and
    emulator implementations before preparing a generic contribution.
    *Status:* planned.

21. **Make report-time frame dumps show finished frames.**
    *Problem:* `RECOMP_FB_DUMP` writes a picture at each periodic report.
    The report runs at an arbitrary point in a frame, so the dump can show a
    back buffer before the title has composited its post-processing or
    drawn its HUD.
    *Evidence:* a traced Brute Force frame draws the scene into the back
    buffer, then a shadow pass, more scene, two downsampled copies, and
    finally the glow composite and HUD into the same back buffer (FINDINGS).
    Report-time dumps alternate between finished frames and dark frames
    without the HUD. The presenter and flip-time hashes read the frame at
    `FLIP_STALL`.
    *Change:* planned: dump the last presented frame at reports, or label
    report dumps as possibly mid-frame. `RECOMP_FB_DUMP_FLIPS` already dumps
    at flips.
    *Status:* planned.

### Open upstream work to evaluate for selection

These existing PRs may help Brute Force directly. Measure each with the
[benchmark protocol](./PERFPLAN.md#phase-0-measurement-and-ceiling-probes)
before adding it to the selected list above. Status as checked on
2026-10-29:

| PR | Purpose | Relevance | Status |
| --- | --- | --- | --- |
| [#231](https://github.com/sp00nznet/xboxrecomp/pull/231) | Pipeline the CPU rasterizer behind the executor | Speeds up the CPU reference path; overlaps 0012 and 0013 | Open, stacked on merged #228; not measured |
| [#272](https://github.com/sp00nznet/xboxrecomp/pull/272) | Decode vertex programs once per upload | CPU fallback for vertex programs | Open (checked 2026-10-29); not measured |
| [#273](https://github.com/sp00nznet/xboxrecomp/pull/273) | Fast path for constant uploads | Walk cost, whichever renderer is used | Open |
| [#222](https://github.com/sp00nznet/xboxrecomp/pull/222) | Window clip, clear rect, face culling, re-sent state | Overlaps 0009 and 0010; may reduce the 149 unhandled methods | Open |

## Further investigation, not yet patches

- **Recover missed indirect callback entries safely.** The observed XPP
  callback at `0x0037EE7C` required a
  [title seed](../fingerprints/retail-seeds.json); the indirect call and
  function prologue were observed locally. Investigate whether a general
  function-discovery improvement can recover analogous code without
  misclassifying mixed code/data. A synthetic reproducer and negative cases
  are needed before proposing an upstream change.
- **Correct the unimplemented-instruction report.** At the pinned revision,
  the recompiler summary says sites are emitted as "no-op comments", while
  the lifter actually emits `RECOMP_UNIMPL` calls. Runtime behavior depends on
  the handler; this project's handler normally aborts. Check whether current
  upstream already fixed the wording, then add a synthetic reporting test if
  needed.

Public searches on 2026-10-29 also found
[#285](https://github.com/sp00nznet/xboxrecomp/pull/285), filed from this
project's account, concerning backend readback before frame snapshots. Its
relationship to the Windows snapshot race (item 5) needs review, and it bears
on items 12 and 15. These searches were limited and are not an exhaustive
duplicate check.

For reproducing the project's configured native fixtures and tests, see the
[README test instructions](../README.md#tests). Keep any disc-derived
evidence, logs, and captures local.
