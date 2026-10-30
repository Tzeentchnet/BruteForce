# Next steps

The performance plan is in [docs/PERFPLAN.md](docs/PERFPLAN.md), measured
results are in [docs/FINDINGS.md](docs/FINDINGS.md), and framework work is
tracked in [docs/UPSTREAM.md](docs/UPSTREAM.md). State as of 2026-10-29:
framework tree `ec3fa862999fa1f2bd58d5eaffbaee04d7baf68d`, generation
`20261029-175500-dfadbdd9`.

## Current boundary

- The previous stationary campaign measured about 3.2 rendered frames per
  second on the CPU and 30.4 on D3D11 with GPU vertex programs, three runs
  each. The hold is visually static, so these are **rendered**, not distinct,
  frames. Native 30 Hz remains a hypothesis.
- A new alternating three-run GPU comparison increased stationary rendered
  fps from 32.60 to 45.06 and movement **distinct** fps from 25.15 to 35.23
  with GPU texture decoding and async visibility together. The candidate's
  three movement rates ranged from 32.84 to 39.82; one boot stalled after
  12 flips and succeeded on retry. Distinctness hashes the whole frame,
  including HUD, so it does not prove every change is a new world frame.
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
- In the new movement profiles, the estimated game-thread GPU wait dropped
  from 33.5% to 24.7% of samples. `NtQueryVirtualMemory` remains a major
  game-thread hotspot, and texture hashing remains visible on the NV2A
  thread. The two GPU changes were measured together, not individually.
- Both variants of the new movement campaign logged unsupported texture
  format `0x2E` (`LIN_D24S8`). Its effect on the visible image is not yet
  established; the CPU reference also lacks a decoder for that format.

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

- Answer the guest surface-read question using the existing null backend and
  `BRUTEFORCE_SURFACE_GUARD`, covering stationary and movement in one scripted
  run. Keep diagnostic output local.
- Implement direct D3D11 presentation, bounded frames in flight, and
  on-demand readback as one coherent milestone. Retain readback for guest
  readers, screenshots, frame statistics, and other consumers until they
  have equivalent GPU-aware paths. Do not release fences before their inputs
  are consumed or skip a flush or flip hold.
- In the same milestone, address the largest observed image differences.
  Compare numbered flip captures from the CPU path, the D3D11 CPU-vertex
  path, and the GPU-vertex path at the same tutorial state. Isolate stencil,
  depth, texture/palette, and sampling differences rather than assuming a
  dark region is expected. Check the bright statue during movement.
- Investigate `LIN_D24S8` sampling and depth-surface aliasing using emulator
  and other recomp implementations as prior art. Blood Wake's optional
  ambient-occlusion path creates a typeless depth resource with D24S8 and
  R24 views; it does not by itself establish NV2A alias or sampling semantics.
  Determine those semantics and ordering before implementing a fallback.

## 2. Remove remaining pipeline stalls

- Profile callers of `NtQueryVirtualMemory` and investigate texture hashing
  and visibility polling using the combined campaign's profiles and counters.
- Batch the event-driven flush acknowledgement, an idle NV2A thread, and
  segment-at-a-time walking where their ordering can be preserved. Keep
  timing changes behind separate switches, and compare the combined result
  once; isolate switches only to diagnose regressions.
- Check texture-cache invalidation and palette lengths other than 256 entries,
  cube mip layouts, device-loss handling, and more drivers before proposing
  the generic backend changes upstream.

## 3. Remaining renderer and compatibility work

- Exercise `RECOMP_GPU_VSH_VERIFY=50` on the title and add fixtures for SHORT,
  UBYTE, packed-normal and relative-constant addressing, plus GPU culling.
- Add the persistent shader-bytecode cache and measure compile stutter.
  Blood Wake's `bw_pscache.c` is prior art.
- Implement GPU window clipping and investigate the intermittent loading
  hang, including the stalled candidate boot. Re-check upstream overlap and
  coordinate with Blood Wake's authors before preparing a contribution; do
  not file or comment without approval.
