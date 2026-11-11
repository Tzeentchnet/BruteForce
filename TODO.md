# Performance TODO

This is the short, checkable queue for the performance work in
[`docs/PERFPLAN.md`](docs/PERFPLAN.md). Remove completed checked items after
their implementation, measurements, and remaining limitations are recorded in
the canonical project documentation.

Current stop point: all three default-off Phase 4 mechanisms have synthetic
coverage, and the corrected segment-only and all-three smokes reached reviewed
movement. Their 24-run comparison is explicitly deferred. The exact
texture-descriptor index is now synthetic-tested without weakening exact
source or palette comparison. A default-on index and diagnostic linear
reference are now available in the same executable. The next active
performance gate is the Milestone 2 matched A/B/A comparison; do not promote
the shader cache or any Phase 4 switch without its separate correctness and
measurement gates.

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
- [ ] Make the shader cache a default candidate only after output identity and
  publication reliability pass.

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
- [ ] Run a matched A/B/A campaign and record whether G2, G3, or NV2A busy time
  improves.

## Decision gate

- [x] Evaluate readback against 10% of the expected native frame budget:
  0.787 ms/flip is below the 3.333 ms threshold, so Phase 3 is not first.
- [x] Evaluate G3 and PFB/KickOff waiting: G3 is 69.1% and the PFB flush poll
  dominates, so Phase 4 is the first new mechanism after existing-option
  isolation.
- [ ] If executor dispatch, exact texture checks, or redundant D3D11 state
  dominates, continue Phase 5 first.
- [ ] If a combined milestone regresses, isolate its existing switches rather
  than accepting an unattributed result.

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
- [ ] Evaluate waitable or multi-slot GPU presentation only after ownership and
  readback are correct.

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
- [ ] Consider asynchronous KickOff only if G3 still fails after the Phase 4
  campaign is explicitly resumed.

## Phase 5: residual work

- [ ] Reprofile before choosing state caching, constant-buffer dirty tracking,
  or executor batching.
- [x] Establish the Brute Force `ARRAY_ELEMENT16/32` census: retained complete
  audit evidence counted 73,787,237 16-bit words and 115,904 32-bit words,
  expanding to 147,690,378 indices exactly matching the requested and
  buffered totals. This proves material dispatch volume, not a speedup.
- [x] Add default-off `RECOMP_PB_INDEX_RUNS=1` in patch 0046 for the staged
  and segment walkers. Exact scalar/run state, CALL, physical wrap, packet
  split, audit fallback, capacity and invalid-option cases pass
  synthetically; audit mode remains scalar.
- [ ] Compare scalar and run modes at matched CPU/D3D11 rendered frames, then
  collect a separate reviewed six-attempt A/B/A campaign at the same Basic
  Controls state. Hold `RECOMP_GPU_TEX_INDEX=0` in both variants so this is
  not combined with the texture-index candidate.
- [ ] Measure `/GL` and LTCG independently.
- [ ] Treat `/arch:AVX2` as an optional compatibility-reducing build, not a
  default assumption.
- [ ] Require bit-exact corpus validation before any native title-function
  rewrite.
