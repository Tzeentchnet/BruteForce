# Upstream contribution candidates

This is a worklist, not a record of filed pull requests. The local framework
changes are reproducible through [toolkit.py](../tools/toolkit.py); they have
not all been prepared or validated as independent upstream contributions.
Recheck the current upstream tree and open work before filing, and use
synthetic reproducers rather than game data or generated game code.

The current guest-to-host graphics pipeline and backend ownership boundaries
are described in [RENDERING.md](./RENDERING.md).

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
local-patch tree is verified as `fe8216622a8195fd7a2531a7fc4f1f723f402aa3`.
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
   before both axes are programmed is an unverified compatibility choice.
   Patch 0009 alone rejects hardware backends; patch 0021 later permits
   backends that explicitly advertise clip support.
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
   not been established. The original patch rejected registered draw
   backends; the later extended interface supports D3D11, while draw backends
   without that interface are still rejected.
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
   A synthetic hardware-D3D11 fixture now checks that zero-depth HUD-like and
   glow-like quads can disappear together under the legacy plane and remain
   visible under the experiment, including positive- and negative-depth
   controls on GPU and CPU-transformed vertex paths. It does not establish
   the cause of the user's live-window observation. GPU-program frame traces
   may print stale CPU-transformed vertices and count submitted rather than
   post-clip triangles; fix or bypass that diagnostic before relying on
   those fields for a GPU trace.
   [0029-hud-safe-near-plane.patch](../patches/xboxrecomp/0029-hud-safe-near-plane.patch)
   adds a project-specific third mode after the user confirmed that the
   HUD-visible 1440p state bloomed: non-additive and conventional
   alpha-blended programmable draws use screen zero, while additive draws
   with destination factor `ONE` use at least a small positive near plane.
   CPU and WARP D3D11 fixtures retain HUD-like geometry, reject zero-depth
   additive glow, and retain positive-depth additive geometry. This heuristic
   is suitable for local title validation, not a generic hardware behavior
   claim; do not fold it into the upstream screen-zero experiment without
   matched original-hardware evidence.

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
    clipping was initially software-only; patch 0021 later passes its state
    to capable GPU backends. Generic; no title knowledge.
    *Tests:* the [frame-stats fixture](../tests/frame_stats_test.c) checks
    that a transformed batch reaches the extended draw with surface position,
    1/w, colours, fog, combiners and near clipping, that the readback hook
    gets the presented surface before the frame is hashed, and that both
    experiments are accepted. The [D3D11 fixture](../tests/d3d11_test.c)
    checks a visibility test's count (1,296 pixels for a 36×36 quad) on the
    CPU path and on the GPU path. Later D3D11 fixtures also cover GPU clear
    rectangles and query suspension around internal clear draws. Occlusion
    counts while an anti-aliased surface is bound remain uncovered.
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
    and a float2 coordinate set) on the CPU and through `draw_vsh`. WARP and
    hardware fixtures also exercise raw and gathered SHORT3/4, UBYTE3/4 and
    packed 11:11:10 normals, fractional and out-of-range relative constant
    indices, and front/back/both culling and triangle-strip winding. They
    compare to the CPU interpreter and renderer, not original NV2A hardware.
    A fully culled D3D11 batch has no GPU vertex output; its readback is
    explicitly skipped instead of comparing a previous batch.
    *Overlap:* [#272](https://github.com/sp00nznet/xboxrecomp/pull/272)
    (open, decode programs once) and
    [#273](https://github.com/sp00nznet/xboxrecomp/pull/273) (fast path for
    constant uploads) speed up the CPU path. They complement this change, and
    the CPU fallback still benefits.
    *Before filing:* establish title and hardware-equivalent conversion and
    culling behavior, cover remaining cases, and coordinate the interface
    with item 12 and #184.
    *Status:* local patch.

14. **Provide a reference D3D11 back end in the framework.**
    *Status:* project code, not a framework patch yet. `src/gpu/` holds a
    D3D11 back end adapted from Blood Wake's generic parts
    (`d3d11_backend.c`, `rc_hlsl.c`, `vsh_hlsl.c`). It matches the CPU path
    on synthetic WARP and hardware fixtures within listed differences
    (FINDINGS), but the title-frame comparison still has unexplained
    differences. The optional persistent combiner pixel-shader cache is
    project code and remains off by default.
    *Problem:* the framework's `src/nv2a/nv2a_pgraph_d3d11.c` is a
    title-coupled menu translator built on the D3D8 layer, not a general
    back end. The combiner HLSL in `d3d8_combiners.c` serves the HLE D3D8
    path, not the pushbuffer executor.
    *Change:* a general back end with:
    - GPU-authoritative colour and depth surfaces;
    - render-to-texture without readback;
    - a snapshot-validated texture cache, direct BC uploads, and GPU compute decoding
      of common swizzled, linear, and P8 formats;
    - state-object caches;
    - pixel shaders from combiner state and vertex shaders from vertex
      programs, matching the framework's CPU evaluators rule for rule;
    - an opt-in persistent combiner pixel-shader cache keyed by HLSL,
      compiler binary and compile inputs, with checksummed, atomically
      published entries and a logged compile fallback.

    The generic parts of Blood Wake's `bw_d3d11.c`, `bw_rc_hlsl.c`,
    `bw_vsh_hlsl.c`, and `bw_pscache.c` are prior art. The cache's design
    follows `bw_pscache.c`, but uses SHA-256 for compiler identity, keys and
    entry checksums, and does not eagerly load the whole cache. Its stale-pitch
    normalization and `fast_texture` also motivated avoiding the CPU
    decoder for formats that can be transferred or decoded on the GPU.
    *Tests:* WARP and hardware parity fixtures against the CPU renderer;
    140 exact CPU/GPU texture-decode cases per device cover rectangular
    swizzles, padded linear rows, P8, and cube faces with mip padding.
    The palette-rewrite fixture exercises cache invalidation within a frame.
    Cold/warm pixel-shader cache fixtures on WARP and hardware compare exact
    frames, exercise CPU and GPU vertex paths, and repair corrupt entries.
    Additional tests cover distinct compiler inputs, Unicode directories,
    concurrent writers, blocked publication with identical, differing and
    damaged existing entries, temporary-name collision retry and exhaustion,
    and an unavailable cache. Synthetic timing and an alternating
    stationary-tutorial cold/warm comparison are recorded in FINDINGS; the
    latter's small capture differences remain unexplained.
    *Overlap:* #184's D3D11 back end, and
    [#274](https://github.com/sp00nznet/xboxrecomp/pull/274) (compile once
    per HLSL text), which applies the same keying idea to the D3D8 layer;
    overlap checked 2026-10-30.
    Ordinary cache use now compares retained texture and palette snapshots,
    addressing the synthetic same-frame and sparse-hash rewrite cases. The
    optional approximate mode can still miss those rewrites. Failed uploads
    now release their slots; deterministic unsupported decodes have bounded
    negative entries, checked after surface aliases. A P8 page-query reuse
    experiment had no hits in the recorded candidate runs and was removed.
    Synthetic failure, alias and reprotection fixtures cover these paths;
    untracked allocations, concurrent protection/write races and gameplay
    cost remain unresolved. Coordinate with #184 and Blood Wake's authors
    before preparing anything.

15. **Present from the back end, with readback on demand.**
    *Problem:* the presenter copies or converts the flipped guest frame on
    the CPU, and a GPU back end must read its surface back into guest memory
    every flip to feed it. Blood Wake measured about 4 ms per flip for that
    wait.
    *Change:* the opt-in project back end publishes a 32-bit D3D11 shared
    texture to the existing window device through
    [0024-gpu-shared-presentation.patch](../patches/xboxrecomp/0024-gpu-shared-presentation.patch).
    A keyed mutex and one pending slot bound snapshots; frame IDs prevent a
    stale GPU image from replacing a newer CPU snapshot. Pinned framebuffers
    and PVIDEO overlays drain the slot and use the CPU frame. The producer
    retains guest readback on every flip; no general frame-in-flight bound
    or on-demand readback is implemented.
    [0028-scaled-gpu-presentation.patch](../patches/xboxrecomp/0028-scaled-gpu-presentation.patch)
    extends that local protocol for the Blood Wake-derived internal-resolution
    milestone: the shared source may have dimensions independent of the guest
    framebuffer, those dimensions are verified against the opened texture,
    and the renderer can query the current 4:3 client picture. The project
    backend still owns the exact/box/Catmull-Rom final resolve; this patch
    does not import Blood Wake's direct swap-chain ownership.
    *Tests:* WARP and hardware fixtures check exact shared pixels, busy,
    future and stale slots, arbitrary valid source dimensions, dimension
    propagation, pinned and overlay fallback, and snapshot consistency.
    Existing guest readback fixtures still pass.
    *Overlap:* #285 covers ordering the readback against snapshots;
    [#215](https://github.com/sp00nznet/xboxrecomp/pull/215) (merged) added
    the D3D11 presenter and #220 covers PVIDEO work (checked 2026-10-30).
    *Before filing:* separate the generic arbitrary-source contract from the
    project-specific output-size policy; investigate the variable combined title performance
    and frequent busy slots; test device loss, other drivers and window
    lifecycles; establish guest readers before eliminating readback; and
    reconcile the presentation interface with #184 and #285.
    *Status:* experimental local framework patch and project back end.

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
    *Status:* local patch (census, probe); project tooling (profiler). Item 28
    complements the census with earlier request and method accounting.

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
    The palette address is now passed, but the executor still discards its
    programmed length bits. The CPU P8 sampler indexes the palette without
    that length, so it cannot check indices against the programmed range.
    *Evidence:* the Brute Force tutorial census recorded P8 and cube use;
    the CPU decoder already samples P8 through the guest palette. Synthetic
    guarded allocations exercise all four palette lengths in the D3D11 path;
    no short-palette safety or out-of-range hardware behavior is established
    for the CPU path.
    *Change:* [0020-texture-metadata.patch](../patches/xboxrecomp/0020-texture-metadata.patch)
    passes the palette through both backend paths and the reference decoder,
    and preserves the programmed level count for format-sized textures.
    The D3D11 back end ignores stale pitches for swizzled and DXT data, as
    Blood Wake's `drop_stale_pitch` does, and includes mip padding in cube
    face offsets. The project renderer separately reads the raw stage
    register, snapshots the programmed palette length, and rejects invalid
    indices and known-short or unreadable ranges. This does not fix the
    framework's CPU decoder or prove actual hardware behavior.
    *Tests:* CPU/GPU P8 decoding and same-frame palette rewrite, plus cube
    faces with multiple levels on WARP and hardware. Project D3D11 fixtures
    also cover guarded 32/64/128/256-entry palettes, invalid indices and
    known-short allocations. They do not prove safety for untracked
    suballocations, concurrent guest writes or render-target aliases.
    *Overlap:* [#145](https://github.com/sp00nznet/xboxrecomp/pull/145)
    added CPU P8 sampling; #184 tracks the GPU interface. Checked
    2026-10-30. [xemu's texture code](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/texture.c)
    derives a 32/64/128/256-entry palette length from the register; its
    OpenGL backend hashes and bounds-checks that selected range. Its
    conversion directly indexes the palette, so it does not establish
    hardware behavior for out-of-range indices.
    *Before filing:* coordinate the backend interface with #184, preserve
    the palette length in generic texture state and bound CPU reads, establish
    hardware behavior for out-of-range indices, and test shorter allocations,
    cube layouts and guest-write synchronization on other titles.
    *Status:* local patch and planned framework follow-up.

25. **Support depth textures and depth-surface aliases.**
    *Problem:* the CPU reference decoder and this project's D3D11 backend
    did not sample `LIN_D24S8` (`0x2E`); GPU depth surfaces were not exposed
    as shader resources.
    *Evidence:* both variants of the 2026-10-29 movement campaign repeatedly
    logged failed `0x2E` decodes. Whether those reads explain a visible
    rendering difference has not been determined. A bright pass also binds
    a tracked GPU depth allocation as a `0x41` texture; its effect on glare
    is not yet established.
    *Change:* the opt-in project renderer can sample an exact matching
    GPU-created linear D24S8 surface through an R24 view. New eligible surfaces
    are initialized from guest memory, converting the Z24S8 field order;
    full-surface GPU depth clears also initialize them. Recreated surfaces
    cannot be reseeded from stale guest data and require a full-surface depth
    clear. When bound for depth writes, a separate snapshot avoids simultaneous
    DSV/SRV binding. An independent opt-in reconstructs the three packed
    depth bytes and stencil alpha through a GPU compute shader for exact
    `0x41` overlaps, requiring both planes to be initialized. Ordinary
    `0x41` textures are unchanged.
    PROJECT2D and PROJECT3D shadow comparisons follow
    [xemu's shader translation](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/glsl/psh.c).
    Guest writes, partial aliases, other formats and CPU reference decoding
    remain unsupported; incompatible aliases are rejected.
    *Tests:* WARP and hardware fixtures cover every comparison above, below
    and at the reference depth, a bound depth target that is written between
    draws, an unbound target, base draws, and rejected pitch, size, offset,
    mip, cube, format, swizzled layout, recreated depth and stage-mode cases,
    plus guest depth and stencil initialization without a GPU clear. Packed
    alias fixtures cover exact stencil keys, filtering, GPU stencil writes,
    initialization, invalid overlaps and base/vertex-program paths. A
    synthetic replay of the traced bright-pass combiner demonstrates the
    effect of stale versus GPU-cleared stencil on its threshold and an
    additive composite, without replaying gameplay or its blur.
    *Overlap:* Blood Wake's optional ambient-occlusion path in
    `bw_d3d11.c` creates an `R24G8_TYPELESS` resource with D24S8 and R24
    views. It is not a guest-depth-alias implementation. The
    [brute-force-2003-reloaded recreation](https://github.com/kduong-dev/brute-force-2003-reloaded)
    describes a stencil-aware glare pipeline but is not an xboxrecomp
    renderer or an original-hardware oracle. Recheck #184 and emulator
    implementations before preparing a generic contribution.
    *Before filing:* validate the much darker alias-only stationary image
    and repeated conversions against a trusted reference, preserve GPU
    contents across surface recreation, track guest writes and validate
    matched gameplay. One hardware diagnostic with shared presentation off
    reproduced the earlier combined candidate's darker stationary image
    within 20–473 pixels beyond 8/255; those captures are not frame-locked.
    *Status:* experimental project-only implementation.

26. **Pass window clips to capable GPU back ends.**
    *Change:* [0021-gpu-window-clip.patch](../patches/xboxrecomp/0021-gpu-window-clip.patch)
    carries the existing rectangle state and an explicit backend capability.
    Back ends without the capability still reject the opt-in. The D3D11 base,
    transformed and GPU-vertex draws apply the same inclusive/exclusive union
    and AA coordinate shifts as the software helper; patch 0021 does not
    change clears. The project D3D11 renderer separately honors clear
    rectangles and color masks.
    *Tests:* CPU, WARP and hardware fixtures check all eight rectangles,
    empty and incomplete state, inclusive/exclusive masks, state changes,
    both AA shifts, clear behavior and GPU vertex programs.
    *Status:* local patch; original-hardware behavior still needs validation.

27. **Honor rectangular GPU clears and avoid redundant software depth clears.**
    *Problem:* treating a partial clear as a whole-target clear can erase
    scene color or depth; clearing a large unused software depth buffer also
    wastes CPU time when a GPU backend owns rendering.
    *Evidence:* a traced Brute Force frame uses two black/zero-depth
    letterbox strips; the old D3D11 backend cleared the entire surface for
    each. Single diagnostics before and after the fix are described in
    [FINDINGS](./FINDINGS.md#rectangular-clears-and-software-depth-ownership).
    *Change:* the project D3D11 backend implements bounded color, depth and
    stencil clears with channel masks and excludes internal clear draws from
    visibility queries. Full clears use native D3D11 operations.
    [0022-gpu-depth-clear-offload.patch](../patches/xboxrecomp/0022-gpu-depth-clear-offload.patch)
    skips the executor's software Z clear only when the registered backend
    has both draw and clear callbacks; incomplete backends keep it.
    *Tests:* WARP and hardware fixtures cover partial clears, masks, depth,
    stencil, AA coordinates and query continuity. The executor fixture checks
    CPU, draw-only, clear-only and complete backend ownership.
    *Overlap:* [#222](https://github.com/sp00nznet/xboxrecomp/pull/222)
    also models clear rectangles (open, checked 2026-10-29).
    *Before filing:* establish hardware reset semantics for unwritten clear
    axes, reconcile CPU rectangle behavior and test other backends, formats
    and titles. Backend registration is assumed to occur before title work.
    *Status:* local patch and project-only D3D11 implementation.

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
    *Change:* [0023-flip-only-frame-captures.patch](../patches/xboxrecomp/0023-flip-only-frame-captures.patch)
    adds `RECOMP_FB_DUMP_FLIPS_ONLY=1` to suppress report-time and after-draw
    dumps while retaining gated flip captures. This lets census reports run
    without polluting a numbered flip sequence. The default is unchanged.
    Making report-time dumps show the last presented frame remains planned.
    *Tests:* the native frame-statistics fixture checks default report dumps
    and gated flip-only output before, during and after capture.
    *Status:* local mitigation; finished report-time dumps are planned.

28. **Observe GPU requests independently of the rendering backend.**
    *Problem:* the existing feature census observes batches that reach its
    draw path, but not every BEGIN/END request, short or discarded input, or
    early-return method. A backend callback alone cannot distinguish
    submission from rejected, culled, or visible geometry, and registering a
    dummy backend to observe it changes the CPU path.
    *Evidence:* source inspection and synthetic executor fixtures show
    short immediate and inline requests, rejected GPU vertex programs,
    software culls, and semaphore and DMA methods that return before ordinary
    dispatch. This is not a tutorial inventory.
    *Change:* [0025-gpu-audit.patch](../patches/xboxrecomp/0025-gpu-audit.patch)
    adds the generic, default-off `RECOMP_GPU_AUDIT=1` observer without
    registering a backend. It records methods by subchannel, executor
    unhandled and texture-mirror-only methods, semaphore outcomes, clears,
    flip events, requested and buffered input, routes, backend callbacks,
    fallback, rejection and software-cull counters. Non-dropped routes have
    **unknown** visibility; callback invocation is not evidence of pixels.
    Tables are bounded, counters saturate and overflow is reported. The
    existing PFB flush, semaphore and flip-hold behavior is unchanged.
    *Tests:* the patch includes a standalone audit on/off collector fixture.
    The project's [executor fixture](../tests/gpu_audit_test.c) and
    [tests](../tests/test_gpu_audit.py) cover CPU/no-backend, fake-backend,
    WARP and D3D11 hardware-mode paths, off/on output and callback ordering,
    early returns, short batches, caps, overflow, concurrent report
    snapshots, rejection and culling. On Windows/MSVC, 65 focused Python
    cases passed, including frame-statistics, window-clip and CPU/GPU parity
    regressions; three toolkit-metadata and four standalone framework CTest
    cases also passed. Hardware mode did not log its adapter identity.
    Coverage and limitations are in
    [FINDINGS](./FINDINGS.md#gpu-audit-instrumentation).
    *Overlap:* public PR-title and tracking-index metadata checked 2026-10-30:
    merged [#152](https://github.com/sp00nznet/xboxrecomp/pull/152) and
    [#162](https://github.com/sp00nznet/xboxrecomp/pull/162) touch related
    executor/backend work; open tracking index
    [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) holds a GPU
    series. No matching audit/census PR was found by title; this was not an
    exhaustive content review. Generic accounting draws on Blood Wake's
    `src/gpu/bw_gpu_audit.{c,h}` at `e631edddfe56d22344e9f696923aeb6d78a9e7ba`;
    the MIT notice is retained and credited in
    [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
    *Before filing:* recheck upstream and coordinate with prior authors; move
    the executor integration cases into a standalone framework test, test
    Linux/pthread and other backends, assess disabled-path overhead and
    snapshot durability, and review the bounded-table loss semantics. No
    title-specific data or captured game content belongs in a contribution.
    *Status:* local patch; not prepared or filed.

29. **Associate flip dumps with frame-statistics records.**
    *Problem:* sequential framebuffer-dump names do not identify their
    `RECOMP_FRAME_STATS` flip when capture gates open or close, so a numbered
    image cannot safely be assigned a timestamp by its sequence alone.
    *Evidence:* the executor writes a gated guest-surface BMP immediately before
    recording the flip statistics; report-time dumps and skipped or failed
    captures make the two counters independent.
    *Change:* [0026-flip-id-frame-captures.patch](../patches/xboxrecomp/0026-flip-id-frame-captures.patch)
    adds default-off `RECOMP_FB_DUMP_FLIP_ID=1`. Gated flip dumps use the
    corresponding ten-digit statistics flip ID in their names. The mode
    requires frame statistics and available guest pixels, suppresses non-flip
    dumps, rejects invalid spans and oversized images, and fails explicitly
    on invalid settings, existing output files or write errors. Legacy names
    and dump behavior are unchanged when it is off.
    *Tests:* synthetic executor cases cover matching IDs and pixels, a
    reopened gate, unavailable statistics or readback, invalid input and
    existing-file protection. Benchmark fixtures check that a tagged image
    cannot be attributed to a different reviewed flip, while legacy
    reviewer-validated captures remain usable.
    *Overlap:* checked 2026-10-30. Merged
    [#156](https://github.com/sp00nznet/xboxrecomp/pull/156) introduced gated
    flip dumps; open [#285](https://github.com/sp00nznet/xboxrecomp/pull/285)
    addresses readback before snapshots and coordinates with the separate
    [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) GPU series. A
    title-keyword search did not find an equivalent flip-ID mapping; this was
    not an exhaustive content review.
    *Before filing:* test additional hosts and real titles, assess diagnostic
    capture cost, and establish how guest snapshots relate to asynchronous
    presentation, PVIDEO overlays and direct GPU presentation. Patch 0027
    separately records supported presenter inputs, but does not prove a guest
    BMP matches those inputs or physical scanout.
    *Status:* local patch; not prepared or filed.

30. **Investigate DAC palette writes from statically linked D3D code.**
    *Problem:* the guest NV2A register aperture is ordinary RAM and the
    `PRMDIO` register handler is a stub, so sequential DAC palette writes
    through that aperture do not build a scanout ramp.
    *Evidence:* a static scan found a conditional caller and a D3D-section
    writer using the DAC palette offsets. One unmatched retail diagnostic
    logged one writer entry at the NV2A base with an identity RGB ramp, before
    the first recorded flip; it does not prove completed writes or establish
    the ramp at the opening fade or its visible impact. See
    [FINDINGS](./FINDINGS.md#static-gamma-ramp-investigation).
    *Change:* no framework patch. A default-off title-side entry probe captures
    the candidate base and planar ramp before the writer, without changing
    presentation; it does not prove completed DAC writes. Evaluate a generic
    DAC model and opt-in presentation handling only if runtime evidence
    supports them.
    *Tests:* synthetic probe tests cover disabled behavior, RGB byte order,
    guest-state preservation, bad or guarded spans and concurrent records.
    One unreviewed actual-game entry trace exists; no synthetic DAC-model
    tests yet. A model needs index, channel ordering, partial updates,
    concurrent snapshots, unchanged guest memory, and presentation-path
    coverage.
    *Overlap:* checked 2026-10-30. Merged
    [#46](https://github.com/sp00nznet/xboxrecomp/pull/46) handles gamma
    through the D3D8 wrapper, not demonstrated guest DAC writes; merged
    [#215](https://github.com/sp00nznet/xboxrecomp/pull/215) concerns the
    framebuffer presenter. Blood Wake's `bw_gamma.c` and xemu's `prmdio.c`
    are prior art. Keyword searches for DAC and RAMDAC found no equivalent
    upstream PR, but were not exhaustive.
    *Before filing:* verify the actual write sequence and matched ramp
    changes, establish title and hardware behavior, coordinate with #184 and
    the presentation paths, and test other hosts and titles without using
    game data.
    *Status:* planned investigation; no patch or pull request.

31. **Record the actual CPU presenter input and its flip provenance.**
    *Problem:* a flip-tagged guest dump can differ from an asynchronous
    presenter's selected frame, converted pixels or PVIDEO composition.
    *Evidence:* a presenter can repeat or skip published flips, pin a guest
    surface, select a shared GPU image, or fail or occlude a submission.
    *Change:* [0027-presenter-source-captures.patch](../patches/xboxrecomp/0027-presenter-source-captures.patch)
    adds default-off Windows capture and a verifier for GDI and D3D11
    CPU-upload inputs. It records submission status and tags, refuses shared
    GPU and SDL capture, protects existing outputs and rejects incomplete or
    inconsistent captures. It does not claim visibility, scanout or pixel
    identity with a guest dump.
    *Tests:* 17 synthetic capture cases passed on MSVC, including WARP and
    hardware shared rejection, CPU-upload readback, concurrent snapshots,
    repeat/skip alignment, overlays and error paths. Capture-off project
    presentation fixtures, four framework CTests and 174 integrated project
    GPU/statistics/audit/clip/presentation cases passed. Two bounded,
    unclassified retail probes verified one flip link each for GDI and D3D11
    CPU-upload; other submissions were no-client or occluded. The successful
    submissions may predate minimization. Matched scenes, physical visibility,
    shared-GPU capture and SDL validation remain open.
    *Overlap:* checked 2026-10-30. Merged
    [#156](https://github.com/sp00nznet/xboxrecomp/pull/156) covers flip
    dumps, [#220](https://github.com/sp00nznet/xboxrecomp/pull/220) PVIDEO,
    and [#215](https://github.com/sp00nznet/xboxrecomp/pull/215) the Windows
    presenter. Open [#285](https://github.com/sp00nznet/xboxrecomp/pull/285)
    covers backend readback and [#184](https://github.com/sp00nznet/xboxrecomp/pull/184)
    tracks the GPU series.
    *Before filing:* review lifecycle, portability, disk usage and statistics
    durability; validate real titles and host devices, and decide how to
    support shared-GPU and SDL sources without mislabeling them.
    *Status:* experimental local framework patch; not prepared or filed.

32. **Support an optional accurate external APU DSP.**
    *Problem:* the framework fold-down cannot execute the GP/EP programs that
    implement title effects, and one physical page number can identify either
    heap RAM or the contiguous window.
    *Evidence:* Brute Force bootstraps and runs both DSP programs. Incorrect
    copy selection previously put scratch/FIFO traffic in unrelated storage.
    The final integration produced nonzero stereo PCM through menus,
    cinematics, tutorial loading, and scripted gameplay. The user later
    reported crackly, mono-sounding dialogue and gunfire while music remained
    acceptable; matched captures show substantially higher left/right
    correlation than the framework fold-down despite healthy host delivery.
    *Change:* [0030-accurate-apu-dsp-hooks.patch](../patches/xboxrecomp/0030-accurate-apu-dsp-hooks.patch)
    adds compile-time optional DSP hooks plus tagged physical-translation
    intent; [0031-external-apu-dsp-lifecycle.patch](../patches/xboxrecomp/0031-external-apu-dsp-lifecycle.patch)
    calls the external finalizer after the frame worker joins. Builds without
    `RECOMP_EXTERNAL_APU_DSP` retain the existing fold-down and symbols.
    *Tests:* the `apu_phys` fixture covers RAM/window tags. The project
    lifecycle fixture completed six accurate-DSP init/shutdown cycles with no
    leaked thread handles. Release builds and bounded retail captures passed.
    *Overlap:* upstream PR #264 is selected separately for the SSL restart
    gap. Open #261/#286 concern RAM aliases; #266/#270 concern sequencer-off
    frames. They were not adopted because the observed issues are narrower.
    *Before filing:* separate generic physical-intent and lifecycle changes
    from the GPL project DSP, add non-Windows concurrency coverage, and
    coordinate the alias model with #261/#286. The accurate mix defect must
    also be isolated before proposing the project DSP as a quality path.
    *Status:* local framework patches; Accurate is experimental and no longer
    the project default; not prepared or filed.

33. **Run independent cleanup hooks before firmware exits.**
    *Problem:* `HalReturnToFirmware` and `HalInitiateShutdown` use
    `ExitProcess`, which skips C `atexit` handlers. Audio WAV headers and
    external workers therefore remained unfinalized.
    *Evidence:* a retail firmware-return capture previously had bytes beyond
    its RIFF size. With the hook, full APU shutdown ran and the 33.73-second
    file length exactly matched its RIFF header.
    *Change:* [0032-run-hooks-before-firmware-exit.patch](../patches/xboxrecomp/0032-run-hooks-before-firmware-exit.patch)
    adds bounded independent hook registration while preserving the kernel's
    replaceable primary report hook, then runs the registry before firmware
    and shutdown exits.
    *Tests:* a native fixture covers registration, deduplication, primary
    replacement, ordering scope, and reason propagation; the retail exit path
    finalized and shut down cleanly.
    *Before filing:* review registration concurrency and whether bugcheck
    paths need an additional-only hook runner without duplicating the missing
    summary.
    *Status:* local framework patch; not prepared or filed.

34. **Prioritize the Windows APU frame worker.**
    *Problem:* the accurate DSP stayed below one quarter of a host core, but
    a normal-priority APU worker could miss the device queue during
    logging/render stalls.
    *Evidence:* a six-buffer gameplay capture saw one device dry spell beside
    a 44.9 ms submit gap. After raising the worker and using the project's
    eight-buffer default, 115- and 260-second probes reported no underruns,
    starvation events, or refused submissions.
    *Change:* [0033-prioritize-apu-frame-worker.patch](../patches/xboxrecomp/0033-prioritize-apu-frame-worker.patch)
    requests `THREAD_PRIORITY_HIGHEST` inside the Windows frame worker and
    reports failure explicitly.
    *Tests:* Release build, repeated lifecycle fixture, and the bounded retail
    probes above. Non-Windows behavior is unchanged.
    *Before filing:* measure other titles, battery/CPU scheduling effects, and
    whether MMCSS is preferable to a fixed Win32 priority.
    *Status:* local framework patch; not prepared or filed.

35. **Retain more queued audio before refilling.**
    *Problem:* the queue pacer waited until half of the configured maximum
    remained. Even with eight 512-sample buffers, that left only 42.7 ms at
    the low-water mark and could not cover an observed 44.9 ms host pause.
    *Evidence:* a fresh default build reproduced dry detections around a
    heavy diagnostic burst. A traced comparison saw the queue fall from
    2,048 to 1,024 samples during a 33.6 ms submit gap.
    *Change:* [0034-raise-apu-queue-low-water.patch](../patches/xboxrecomp/0034-raise-apu-queue-low-water.patch)
    changes the common XAudio2/SDL pacing low-water mark from one half to
    three quarters. With Brute Force's eight-buffer policy, pacing becomes
    3,072-4,096 samples (64.0-85.3 ms) without increasing the maximum.
    *Tests:* the framework XAudio2 fixture covers default and non-period-sized
    buffers with the new marks. The final 164.47-second rebuilt-default retail
    probe reached the tutorial-world signature and scripted input while
    reporting zero underruns, starvation events, and refused submissions; its
    WAV header finalized exactly. The complete patch stack also applied and
    verified from a fresh checkout.
    *Before filing:* measure multiple titles and SDL devices, and consider
    making the ratio a validated setting rather than changing the framework
    default.
    *Status:* local framework patch; not prepared or filed.

36. **Define CPU writes to GPU-owned colour surfaces.**
    *Problem:* the backend readback contract assumes a GPU backend owns the
    completed colour surface. A title can instead clear through NV2A, decode a
    movie directly into the same guest allocation on the CPU, and then flip.
    Unconditional GPU readback erases that CPU image; scaled shared
    presentation publishes the stale GPU clear as well.
    *Evidence:* Brute Force opened its Microsoft, Digital Anvil and intro Bink
    files while hardware D3D11 reports showed hundreds of clears and zero GPU
    draws. No PVIDEO bank was submitted. Before the workaround, 4.5x captures
    were black. After synchronizing guest changes, bounded 4.5x captures show
    all three movie stages. One report recorded 386 guest uploads in 392 flips,
    zero conflicts and 156 accepted shared submissions.
    *Change:* no framework patch yet. The project D3D11 backend retains a
    logical guest shadow, mirrors full colour clears, and uploads a changed
    logical surface before the first GPU draw or at flip. Scaled modes filter
    that upload into the physical target before shared submission. A detected
    guest/GPU ordering conflict is reported and preserves the GPU frame.
    *Tests:* CPU-written colour frames survive scale 1 and scale 2, both alone
    and with a later GPU overlay. The complete D3D11 matrix passes 190 cases
    on hardware and 123 with 67 hardware cases skipped under WARP. Bounded
    scale-1 and 4.5x retail opening probes retained visible movie content.
    *Overlap:* checked 2026-10-30. Open
    [#285](https://github.com/sp00nznet/xboxrecomp/pull/285) establishes
    backend readback before snapshots but does not define guest writes after a
    GPU clear. Tracking index
    [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) covers the GPU
    series. Merged
    [#220](https://github.com/sp00nznet/xboxrecomp/pull/220) covers PVIDEO,
    which this Bink path did not use.
    *Before filing:* design a generic ownership/write-notification contract
    instead of requiring a backend to poll guest bytes. Cover partial CPU
    writes, writes between multiple GPU batches, clear masks and rectangles,
    render-target aliases, races, non-Windows backends and page-protection
    interactions. Preserve the CPU reference path.
    *Status:* framework gap with a tested project-side workaround; not
    prepared or filed.

### Open upstream work to evaluate for selection

These existing PRs may help Brute Force directly. Measure each with the
[benchmark protocol](./PERFPLAN.md#phase-0-measurement-and-ceiling-probes)
before adding it to the selected list above. The performance PRs below were
checked on 2026-10-30; #222 was last checked on 2026-10-29:

| PR | Purpose | Relevance | Status |
| --- | --- | --- | --- |
| [#231](https://github.com/sp00nznet/xboxrecomp/pull/231) | Pipeline the CPU rasterizer behind the executor | CPU reference path; overlaps 0008 and 0013 | Open, stacked on merged #228; not measured |
| [#272](https://github.com/sp00nznet/xboxrecomp/pull/272) | Decode vertex programs once per upload | CPU reference and GPU vertex fallback | Open; not measured |
| [#273](https://github.com/sp00nznet/xboxrecomp/pull/273) | Fast path for constant uploads | Walk cost, whichever renderer is used | Open; not measured |
| [#222](https://github.com/sp00nznet/xboxrecomp/pull/222) | Window clip, clear rect, face culling, re-sent state | Overlaps 0009 and 0010; may reduce the 149 unhandled methods | Open |

Read-only applicability checks showed that #231, #272 and #273 do not apply
cleanly to the selected pre-audit tree `a4489cd`; rebase and recheck them
against the current tree rather than applying them directly. Merged #228 is
already in the pinned base. These upstream optimizations default on, but any
local adaptations should remain opt-in until correctness and title-specific
gains are established.

- **#231:** The upstream [review](https://github.com/sp00nznet/xboxrecomp/pull/231#issuecomment-6270034277)
  flags counter wrap, incomplete worker-state snapshots, and publishing GET
  before workers drain. Cover queue saturation, render-to-texture dependencies,
  and GET, semaphore, flip and flush ordering before evaluating it. Patch 0012
  is complementary rather than overlapping.
- **#272:** Preserve `s_const_gen` on decoded constant writes so GPU constant
  buffers do not become stale. Compare both vertex outputs and the constant
  file/generation, including context-writing programs; upstream's existing
  randomized fixture does not exercise those writes.
- **#273:** Preserve initialization, register writes, `frame_constants_mix`
  and constant-generation updates at both walker dispatch sites. Extend
  the walker fixture for accepted, split, non-increasing, CALL and
  other-subchannel packets, and coordinate bypasses with any #231 drains.

The separate GPU series in [#184](https://github.com/sp00nznet/xboxrecomp/pull/184)
is related prior art, not an interchangeable implementation: equivalence of
its constant handling for non-increasing packets is unverified. No gameplay
benchmark has been run for these candidates; compare at least three matched,
alternating A/B/A runs per variant and report distinct world frames and visual
fidelity, not flips alone.

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
