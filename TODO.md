# Performance TODO

This is the short, checkable queue for the performance work in
[`docs/PERFPLAN.md`](docs/PERFPLAN.md). Remove completed checked items after
their implementation, measurements, and remaining limitations are recorded in
the canonical project documentation.

Current stop point: all three default-off Phase 4 mechanisms have synthetic
coverage, and the corrected segment-only and all-three smokes reached reviewed
movement. Their 24-run comparison is explicitly deferred. The first
texture-descriptor-index campaign completed six timing-matched, fully reviewed
attempts and proved that the exact index removes almost all linear descriptor
probes, but its strict result is inconclusive: historical PFB interval records
omit callers at three title call sites, one of three cadence pairs regressed,
and G2/G4/G5 evidence is absent. Those call-site stack mappings are now fixed
and synthetic-tested in generation `20261113-160606-ad4c0e30`. Its partial
repeat completed three clean attempts before run 04 locked during Loading.
That lock has since been traced to a title loader queue polling a completion
byte through zero-delay `KeDelayExecutionThread`. Brute Force now selects the
existing framework `RECOMP_GUEST_CPUS=one` control by default; one explicit
and three default-path bounded boots advanced to movie draws, while
long-session reliability remains unproved. Review covered all 3,574 raw
captures, and all 64 movement-contained PFB/PUT intervals now have callers,
but the only A/B pair fails input matching because its final live command
differs by 1,656 ms at runtime. The 2026-11-13 operator decision is not to
restart another six-run sequence: retain the mechanism evidence and close the
title-throughput result as inconclusive. Retained audit evidence also
establishes material index-word dispatch volume, and patch 0046 adds a separate
default-off exact batching candidate with synthetic coverage. Do not combine
that independent comparison with the texture index, and do not promote the
shader cache or any Phase 4 switch without its separate correctness and
measurement gates. A retained synthetic experiment permits exact scaled
packed-stencil aliases and passes its narrow fixtures, but a fresh 4.5x retail
comparison invalidated that path: the player was absent in two
alias-on boots and present in the otherwise matched alias-off spawn control.
The renderer and launcher now fail closed on both alias options at non-1
internal scales. Continue brightness work at scale 1 or add a fuller
render-sequence fixture before reconsidering scaled aliases. The post-index
Windows write-watch/guard candidate is also closed as incompatible with the
framework's file-mapped alias layout. Every remaining unchecked item below is
explicitly blocked, deferred, or conditional on a new operator-reviewed
capture/profile; there is no additional unattended campaign to start.

## Milestone 0: trustworthy baseline

- [x] Write the performance roadmap as a removable checklist.
- [x] Report reviewer-approved world-frame cadence and 1% lows from labeled
  movement captures.
- [x] Separate known GPU/NV2A waits from unrelated game-thread kernel waits.
- [x] Cover the reviewed cadence and GPU-wait attribution changes with focused
  benchmark tests.
- [x] Add periodic counters for guest-memory validation and exact texture-cache
  work.
- [x] Rebuild the final Release executable and record its generation, framework
  tree, adapter, driver, thread count, and enabled options.
- [x] Run three matched default-D3D11 movement attempts with fixed input and
  reviewed flip-ID-aligned captures.
- [x] Reproduce or explicitly close the reported default movement regression.
- [x] Record the G2/G3 baseline, readback time, visibility time, texture work,
  and validation limits in `docs/FINDINGS.md`.

## Milestone 1: isolate existing options

- [x] Compare synchronous and asynchronous visibility with every other option
  fixed, using at least three alternating runs per variant.
- [x] Keep asynchronous visibility opt-in unless ordered reports, reliability,
  matched captures, G2, and G3 all pass.
- [x] Measure cold and warm persistent pixel-shader-cache frame-time hitches
  separately from steady-state throughput.
- [ ] **Blocked on reviewed output identity and publication reliability:**
  make the shader cache a default candidate only after both pass.

## Milestone 2: safe CPU-side overhead

- [x] Count and time `guest_word`, `readable_guest_range`, and `VirtualQuery`
  use before changing validation.
- [x] Replace repeated texture and colour-shadow preflight queries with checked
  accesses that retain invalid-address, guard-page, reprotection, allocation,
  and reset detection.
- [x] Add an exact, collision-safe descriptor-key index to the texture cache.
- [x] Preserve full source and palette comparison until tracked guest writes
  can prove safe invalidation.
- [x] Cover descriptor-index collisions and lifecycle, same-frame and
  unsampled rewrites, palette rewrites, protected P8 ranges, exact colour
  aliases, and failed uploads synthetically.
- [x] Provide a same-executable exact linear reference behind
  `RECOMP_GPU_TEX_INDEX=0` for an attributable comparison.
- [x] **Closed as inconclusive on available retail evidence:** the first
  linear/indexed alternation passed timing and capture review but had
  historical zero-caller PFB intervals and one regressing cadence pair. The
  caller-corrected repeat reviewed 3,574 raw captures across three completed
  attempts and proved complete attribution in all 64 movement-contained
  PFB/PUT intervals. Run 04 then locked before readiness, and the only
  completed A/B pair exceeded the 250 ms input tolerance at its final command
  (1,656 ms runtime drift). Do not claim a title speedup or use its descriptive
  +0.247 world changes/s as an A/B result; G2/G4/G5 remain open.
- [x] **Closed as infeasible with the current guest-memory layout:** Windows
  write watching (`MEM_WRITE_WATCH`) only covers specially reserved
  `VirtualAlloc` regions. Xbox RAM and contiguous memory use pagefile-backed
  mapped views so RAM mirrors and the contiguous/tiled apertures alias the
  same bytes. A local API probe confirmed `GetWriteWatch` rejects both an
  ordinary allocation and a pagefile-backed mapped view with error 87.
  Per-view page guards cannot prove physical-page cleanliness across those
  aliases and retain the existing reprotection/concurrency gaps. No
  `RECOMP_*` experiment was added; exact source and palette comparison stays
  mandatory pending a generic framework write-notification contract.

## Decision gate

- [x] Evaluate readback against 10% of the expected native frame budget:
  0.787 ms/flip is below the 3.333 ms threshold, so Phase 3 is not first.
- [x] Evaluate G3 and PFB/KickOff waiting: G3 is 69.1% and the PFB flush poll
  dominates, so Phase 4 is the first new mechanism after existing-option
  isolation.
- [ ] **Conditional on the next reviewed profile:** if executor dispatch,
  exact texture checks, or redundant D3D11 state dominates, continue Phase 5
  first.
- [ ] **Conditional on a measured regression:** isolate a combined milestone's
  existing switches rather than accepting an unattributed result.

## Phase 3: readback and presentation

- [x] Re-arm and complete the diagnostic surface-read investigation, recording
  every excluded range and blind spot; retain readback because accumulated
  ranges and arbitrary guest accesses cannot establish live ownership.
- [x] Inventory known framework surface readers, CPU presentation, PVIDEO,
  screenshots, captures and frame-statistics consumers, while retaining the
  unresolved title-reader gate.
- [ ] **Blocked:** implement on-demand readback only after live surface
  lifetimes and arbitrary guest reads/writes can be intercepted without
  weakening semaphore or flip ordering.
- [x] Bound unfinished GPU frames, initially to two, with event queries.
- [ ] **Blocked with on-demand readback:** evaluate waitable or multi-slot GPU
  presentation only after ownership and readback are correct.

## Phase 4: bounded run-ahead

- [x] Wait on the real flush acknowledgement behind the default-off
  `RECOMP_PFB_FLUSH_WAIT=1` switch.
- [x] Preserve the generated default path and the guest `ret` stack effect in
  both disabled and enabled manual-hook fixtures.
- [x] Validate spurious wakeups, timer recovery, PGRAPH traps, semaphores, flip
  holds, ring reuse, and D3D11 device-failure behavior synthetically.
- [x] Complete one enabled title movement smoke on the corrected generation;
  treat its unreviewed whole-frame cadence only as bring-up evidence.
- [ ] **Deferred:** run poll/event/poll/event/poll/event on the corrected
  generation with 12 raster threads, fixed camera-only input, caller
  profiling, `BRUTEFORCE_PERF_COUNTERS=1`, and continuous world capture.
- [ ] **Deferred:** review every selected capture and compare reviewed world
  cadence, 1% low, G3, NV2A busy time, PFB event/timer wakes, rechecks, and
  visible/input alignment.
- [ ] **Deferred:** explain the smoke's high timer-wake and recheck counts and
  its non-quiescent wait/ack difference before accepting the mechanism.
- [x] Keep all three options default-off while correctness, G2, G3, and CPU
  reference gates remain open.
- [x] Replace idle `Sleep(0)` behavior with bounded spin-then-wait behavior.
- [x] Walk and retire one published PUT segment at a time behind its own
  switch.
- [ ] **Blocked on resuming Phase 4 and its G3 result:** consider asynchronous
  KickOff only if G3 still fails.

## Phase 5: residual work

- [ ] **Blocked on the next reviewed campaign:** reprofile before choosing
  state caching or constant-buffer dirty tracking. Include a RenderDoc
  capture of a matched D3D11 frame to count redundant state changes, clears,
  render-target switches, and the cost of scaled offscreen passes; keep the
  capture local and record only the counts and conclusions.
- [x] Establish the Brute Force `ARRAY_ELEMENT16/32` census: retained complete
  audit evidence counted 73,787,237 16-bit words and 115,904 32-bit words,
  expanding to 147,690,378 indices exactly matching the requested and
  buffered totals. This proves material dispatch volume, not a speedup.
- [x] Add default-off `RECOMP_PB_INDEX_RUNS=1` in patch 0046 for the staged
  and segment walkers. Exact scalar/run state, CALL, physical wrap, packet
  split, audit fallback, capacity and invalid-option cases pass
  synthetically; audit mode remains scalar.
- [ ] **Blocked on operator-confirmed correctness and performance captures:**
  compare scalar and run modes at matched CPU/D3D11 rendered frames, then
  collect a separate reviewed six-attempt A/B/A campaign at the same Basic
  Controls state. Hold `RECOMP_GPU_TEX_INDEX=0` in both variants so this is
  not combined with the texture-index candidate.
- [x] Add isolated, mutually exclusive Release profiles for title-executable
  `/GL` plus LTCG and `/arch:AVX2`, with exact build metadata and a guarded
  multi-binary benchmark/review protocol.
- [ ] **Blocked on the higher-priority reviewed gates:** run separate
  operator-reviewed default/LTCG and default/AVX2 campaigns, keeping all
  runtime performance switches off and collecting matched CPU/D3D11 frames.
- [x] Keep `/arch:AVX2` optional and compatibility-reducing; it is not part of
  the supported default build.
- [x] Require bit-exact generated/native lockstep over a large recorded corpus
  before any native title-function rewrite. No rewrite is selected.
