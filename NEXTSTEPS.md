# Next steps

The performance plan is in [docs/PERFPLAN.md](docs/PERFPLAN.md), measured
results are in [docs/FINDINGS.md](docs/FINDINGS.md), and framework work is
tracked in [docs/UPSTREAM.md](docs/UPSTREAM.md). State as of 2026-10-30:
framework tree `a4489cd69600a1489edea98ac19cf20db242b6d2`, generation
`20261029-175500-dfadbdd9`.

## Current boundary

- The previous stationary campaign measured about 3.2 rendered frames per
  second on the CPU and 30.4 on D3D11 with GPU vertex programs, three runs
  each. The hold is visually static, so these are **rendered**, not distinct,
  frames. Native 30 Hz remains a hypothesis.
- An alternating three-run GPU comparison reported stationary rendered fps
  from 32.60 to 45.06 and movement pixel-change fps from 25.15 to 35.23
  with GPU texture decoding and async visibility together. A subsequent
  audit found that 58.9–60.2% of movement flips in every run matched a
  solid-black 640×480 image, so neither movement figure establishes world
  fps or a playable improvement. One boot stalled after 12 flips and
  succeeded on retry. The whole-frame hash also includes HUD changes.
- The new GPU-vertex tutorial capture differs from the earlier CPU capture
  by more than 8/255 in 1,612 pixels (0.52%); the two GPU variants differ
  in 285 pixels (0.09%). An older D3D11/CPU-vertex capture differs from the
  CPU by 21.64%, with much darker background regions. The causes are not
  established, and the comparisons span different framework trees. G4 is
  not met, and the CPU renderer remains the default reference.
- Common swizzled, linear, and P8 textures now decode on the GPU; compressed
  textures with stale pitch state can upload as BC blocks. The compute
  decoder matches the CPU decoder on synthetic WARP and hardware cases.
  `RECOMP_GPU_TEX_DECODE=0` restores the CPU decoder for comparison.
  The D3D11 P8 cache reads 256 palette entries even for shorter palettes;
  those lengths and allocations remain unverified.
- Ordered asynchronous visibility reports pass synthetic WARP and hardware
  fixtures and the combined title campaign. They remain opt-in with
  `RECOMP_GPU_ZPASS=async` pending broader gameplay and reliability checks.
- In those movement profiles, the estimated game-thread GPU wait dropped
  from 33.5% to 24.7% of samples, but the black-frame intervals limit their
  relevance to visible gameplay. `NtQueryVirtualMemory` remains a major
  game-thread hotspot, and texture hashing remains visible on the NV2A
  thread. The two GPU changes were measured together, not individually.
- Both variants of the new movement campaign logged unsupported texture
  format `0x2E` (`LIN_D24S8`). Its effect on the visible image is not yet
  established; the CPU reference also lacks a decoder for that format.
- Experimental GPU depth aliases now cover exact matching D24S8 surfaces,
  with guest-memory initialization and snapshots for a bound depth target;
  other aliases and guest writes remain unsupported. An initial combined
  run rejected 29,261 depth samples in periodic reports and was stopped
  after a missing-character observation. After guest-depth seeding, one
  diagnostic recorded 19,029 accepted samples and no rejections in periodic
  reports, and a captured gameplay frame showed the character. This does not
  establish correctness across all frames. GPU window clipping is available
  behind the existing opt-in flag. Neither change establishes original-hardware
  equivalence.
- A traced frame exposed whole-target D3D11 clears where the title requested
  narrow black/depth letterbox strips. The GPU backend now honors clear
  rectangles and channel masks; the executor skips the roughly 5 MiB
  software depth clear when a registered backend owns drawing and clearing.
  One later depth/clip diagnostic showed the character in two views and
  had zero exact-black matches in 3,458 movement flips. Earlier diagnostics
  matched black in over half their flips. These individual, differently timed
  runs do not establish original-hardware rendering or a performance gain.
- A final instrumented run after depth-clear offload captured the character in
  one gameplay view and had zero known-black matches in 1,154 movement flips.
  Its census counted 3,197 GPU-owned depth clears and no software-owned
  clears across 1,191 gated flips. One capture interval still showed a tutorial
  panel; the census and captures perturb timing, so this is not a world-fps
  or general character-visibility guarantee.
- User captures show very bright, bloomed vegetation and sky when facing
  toward the sun, and a darker view after turning. The cause remains open;
  no global brightness or gamma adjustment was applied. The user reported
  that bloom returned during the latest comparison; captured baseline
  frames also show heavily bloomed vegetation and sky.
- A traced bright pass samples the prior GPU depth allocation as texture
  format `0x41`. The opt-in `RECOMP_GPU_STENCIL_ALIAS=1` path reconstructs
  its packed depth and stencil on the GPU; its relevance to bloom is a
  hypothesis, not a confirmed diagnosis. The CPU rasterizer ignores stencil.
- The opt-in `RECOMP_GPU_DIRECT_PRESENT=1` path presents a finished GPU
  snapshot through a one-slot keyed-mutex handoff, with CPU fallback for
  pinned frames and PVIDEO. It still performs per-flip guest readback and
  does not limit all GPU work in flight. WARP and hardware synthetic tests
  cover the alias, pixel transfer, queue ordering and fallback.
- In the latest alternating comparison, three baseline runs averaged 44.94
  stationary rendered fps and three runs with both new options averaged
  40.18, with the latter ranging from 23.56 to 57.69. Movement whole-frame
  pixel-change rates averaged 67.16 and 64.00 fps, respectively, but
  include HUD changes and do not establish distinct world FPS. Both paths
  had zero matches to the two known black hashes. These results do not
  establish a speedup.
- The new combined path made the stationary background markedly darker.
  Baseline/candidate captures differed in about 47.32% of pixels by more
  than 8/255 in all three pairs. The panel state is content-matched but not
  frame-locked, the options were enabled together, and no trusted reference
  established which brightness is correct. G4 remains unmet.
- A null-backend surface-guard run aborted on the GPU semaphore after a
  guarded range covered its page. A new explicitly reported page exclusion
  permitted a completed movement diagnostic, but leaves that page unobserved.
  The report recorded seven non-NV2A reads within an estimated depth range;
  offline symbolization provisionally points to a USB rumble-report read,
  but the original module base and actual resource ownership remain unknown.

## Work cadence

Implement substantial related work as a milestone, then build and run the
relevant synthetic fixtures together. Use a single alternating A/B/A campaign
of at least three runs per variant on the same generation to validate the
combined result. `--scene movement --capture` collects the stationary hold,
movement, profiles, and flip-time captures in those same runs. Do not run a
separate campaign after each small patch; use targeted diagnostics only to
resolve failures. Never trade away the CPU reference or guest-visible flush,
semaphore, report, and flip ordering.

## 1. Complete the GPU-authoritative pipeline

- Continue the guest surface-read investigation with the null backend and
  `BRUTEFORCE_SURFACE_GUARD`, covering stationary and movement in one scripted
  run. Account for excluded pages and the diagnostic's other blind spots;
  a partial run cannot prove that no guest readers exist. Keep output local.
- Extend the opt-in shared GPU presenter into a fully GPU-authoritative
  pipeline: establish guest surface readers, implement safe on-demand
  readback and bound all GPU work in flight. Retain readback for guest
  readers, screenshots, frame statistics, and other consumers until they
  have equivalent GPU-aware paths. Do not release fences before their inputs
  are consumed or skip a flush or flip hold.
- In the same milestone, address the largest observed image differences.
  Compare numbered flip captures from the CPU path, the D3D11 CPU-vertex
  path, and the GPU-vertex path at the same tutorial state. Isolate stencil,
  depth, texture/palette, and sampling differences rather than assuming a
  dark region is expected. Compare the bright sun-facing view, the turned
  view and the glow composite against a trusted reference; investigate any
  recurring black intervals or missing character without a global gamma hack.
- Isolate packed-stencil aliasing from shared presentation and explain the
  much darker stationary image. Inspect the slow candidate's high sampled
  GPU-wait share, the frequent busy shared slot, and repeated packed-depth
  conversions; only avoid conversion when depth and stencil writes are
  correctly tracked.
- Make the movement gate measure visible world changes rather than HUD or
  black-frame transitions. The harness now reports two known solid-black
  hashes, but other dark or black images and intentional fades still need
  classification; a zero match count is not proof of valid world frames.
- Validate the opt-in `LIN_D24S8` path with matched gameplay captures and
  original hardware or a trusted reference. Extend guest-write tracking,
  partially overlapping aliases, and CPU reference behavior before making it
  the default.

## 2. Remove remaining pipeline stalls

- Profile callers of `NtQueryVirtualMemory` and investigate texture hashing
  and visibility polling using the combined campaign's profiles and counters.
- Batch the event-driven flush acknowledgement, an idle NV2A thread, and
  segment-at-a-time walking where their ordering can be preserved. Keep
  timing changes behind separate switches, and compare the combined result
  once; isolate switches only to diagnose regressions.
- Check texture-cache invalidation, palette lengths other than 256 entries,
  device-loss handling, and more drivers before proposing the generic
  backend changes upstream. Mipmap and LOD-transition enhancements are
  deferred by user direction unless needed to preserve existing behavior.

## 3. Remaining renderer and compatibility work

- Exercise `RECOMP_GPU_VSH_VERIFY=50` on the title and add fixtures for SHORT,
  UBYTE, packed-normal and relative-constant addressing, plus GPU culling.
- Add the persistent shader-bytecode cache and measure compile stutter.
  Blood Wake's `bw_pscache.c` is prior art.
- Validate GPU window clipping in gameplay and investigate the intermittent
  loading hang, including the stalled candidate boot. Re-check upstream
  overlap and coordinate with Blood Wake's authors before preparing a
  contribution; do not file or comment without approval.
