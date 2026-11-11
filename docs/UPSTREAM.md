# Upstream contribution candidates

This is a worklist and status record for both filed and prospective upstream
work. The local framework changes are reproducible through
[toolkit.py](../tools/toolkit.py); they have not all been prepared or
validated as independent upstream contributions. Recheck the current upstream
tree and open work before filing, and use synthetic reproducers rather than
game data or generated game code.

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

## Current comparison snapshot

As of 2026-11-10, upstream `main` and release v0.17.0 are at
`343b6d8901723b37ce568b2418c5df4e1fe1d223`, now the base pinned by this
project. The refreshed comparison covered pull-request metadata through #321,
issue status through #306, the v0.17.0 release contents, and every local
framework patch. It was a targeted semantic review of overlapping and
potentially applicable work, not a line-by-line review of unrelated changes.

v0.17.0 includes all eight commits previously selected on top of v0.16.0.
It also includes this project's #297, #298, #299 and #300 submissions, so
local patches 0001, 0005 and 0035 have been retired. Issue #301 is closed:
v0.17.0 corrects the raw `XC_VIDEO` high-word encoding. Issue #306 remains
the open renderer-backend contract discussion, and #308 remains relevant to
kernel behavior without correcting the software-interrupt calling convention.

Three post-release changes are selected on top of v0.17.0: #314 fixes FCMOV
carry liveness; both commits from #319 provide a reusable backend contract
fixture; and #320 adds exact-value `RECOMP_VIDEO_WIDESCREEN=1` and
`RECOMP_VIDEO_480P=1` opt-ins while retaining NTSC 4:3 480i/60 defaults.
#315 is deliberately not selected. Its available 32-bit MSVC differential
run compared 24,192 vectors and found 2,643 mismatches, concentrated in signed
16-bit register, memory and alias `IDIV` cases. Its macOS validation had
skipped those native MSVC cases.

The remaining recent PRs were reviewed but are not needed by this Windows
title integration: #313 broadens POSIX test coverage, #316 and #317 change
POSIX/path behavior, and #321 addresses non-default utility-drive cluster
sizes for other title headers. They remain upstream review candidates rather
than speculative local selections.

A separate prior-art refresh on 2026-11-10 compared Blood Wake v0.2.4 at
[`e631edd`](https://github.com/minimalice92/bloodwake-recomp/tree/e631edddfe56d22344e9f696923aeb6d78a9e7ba)
with v0.3.3 at
[`6292340`](https://github.com/minimalice92/bloodwake-recomp/tree/6292340705d9b863351a45ba160174a346a81688),
ten public commits later. Current Blood Wake still fingerprints xboxrecomp
v0.16.0; it is prior art rather than a framework update to select. No code
was copied from the newer revision. Candidate-specific overlap and validation
requirements are recorded below.

The surviving local stack was rebased onto v0.17.0 plus those selections.
Thirty-three existing patches applied in order with ordinary
`git apply --index`; patch 0037 corrects stale Windows serial-raster worker
telemetry. The earlier 34-patch tree was
`131abd8d1f4559bb36dba4391e4c644925cff5df` and passed clean reconstruction.
The previously recorded 35-patch tree was
`86dcf92c99a87940b3346b3cd3b55b8f6145a881`; a clean local replay of that
patch list instead produced `3978863d9bcbe6f26536315c65daa9313932c8b5`.
The former tree object was unavailable locally, so the discrepancy has not
been attributed to a specific change. The five additional patches 0039-0043
produced the independently replayed and verified 40-patch tree
`e9c4349117b38482bd8ec3a64556996f549682b1`. Patch 0044 now produces the
independently replayed, verified 41-patch tree
`153571c77e95ccb7e337d40aa03a5633233ccb44`. The isolated index and working
files pass `python tools/toolkit.py --verify --checkout build\artifact-toolkit`;
shared setup was not rerun. The earlier integrated framework reported
992 Python tests passed, 2 skipped and 134 subtests passed; the Release
backend-contract, PFB-flush, GPU-audit, raster-batch and EEPROM fixtures also
pass.

Patch [0045](../patches/xboxrecomp/0045-bounded-nv2a-segments-and-waits.patch)
adds a 42nd local patch; its separately reconstructed tree is
`64ce266664a875084be63cb32148b3ae78c55185`. Both the private implementation
checkout and a second independent local replay verify against that tree. The
shared local toolkit now uses this verified tree; generated title output has
not yet been regenerated. Integrated validation also corrected patch 0039's
presenter script so each direct `fb_present.c` consumer links
`display_awake.c`. The framework tools report 995 passed, 2 skipped and 134
subtests passed; 41 focused CTests and 19 presenter cases also pass.

### Bounded acknowledgement, idle waiting and guarded PUT segments

- **Problem:** a PFB event wait lacked a total deadline; an NV2A worker
  busy-polls when idle; sampling only the latest PUT can collapse publication
  boundaries and place a later segment on the wrong side of a flip hold.
  Complete interception is not guaranteed by a small static list of guest
  functions.
- **Evidence:** earlier Brute Force profiles and the dated PFB smoke are
  recorded in item 17 below; neither establishes a new speedup. In the
  supported `20261111-002138-a9dbc18a` generated code, the device setup writes
  a physical-zero bootstrap JUMP before calling KickOff. The two observed PUT
  stores occur in `sub_00371FA0` and `sub_00372050`; the former also stores
  to a shadow RAM register, not always the USER aperture. This is static
  evidence, not proof that every runtime write is covered.
- **Change:** patch 0045 bounds real PFB acknowledgement waits, implements
  independent default-off `RECOMP_NV2A_IDLE_WAIT=1`, and adds default-off
  `RECOMP_NV2A_SEGMENTS=1`. The latter requires the staged executor, IRQs,
  producer integration and a Windows read-only USER view with writable host
  alias. A bounded FIFO preserves PUT order, GET advances only after each
  completed walk, pending work/flip holds defer PFB acknowledgement, and
  untracked guest writes, resets, invalid PUTs and timeouts abort. A generic
  function-scoped 32-bit MOV-store callback is opt-in at generation time;
  Brute Force addresses and callbacks stay in `src/recomp_manual.c`.
  Enabled diagnostics report bounded caller-attributed counters. The guard
  intentionally refuses unmodeled USER writes rather than claiming support.
- **Tests:** 7 PFB, 6 idle and 9 segmented native fixture cases passed; the
  latter covers guarded aliases, untracked writes/reset, bootstrap JUMP,
  queue backpressure/timeout, split packets, control flow and held-flip
  dispatch using synthetic executor markers. One case combines the real PFB
  acknowledgement primitive with the queue and held-flip boundary. Three
  translator tests, including compiled generated stores, and 64 title
  fence/pushbuffer/interrupt/PGRAPH cases passed, including the new
  store-hook and PFB-attribution cases. These do not
  execute the full production worker against a title or prove presentation.
- **Overlap:** the existing #184/open-and-merged comparison was last checked
  2026-11-03 for PFB and 2026-11-10 for the broader snapshot. Blood Wake's
  idle and event concepts are prior art; no code was copied. This task did
  not contact upstream, so overlap must be rechecked before a submission.
- **Before filing:** independently split the generic pieces, test the full
  worker/guard integration and another title, exercise unsupported hosts,
  review native/CPU matching and alternating title measurements, and check
  current upstream with approval before any upstream interaction.
- **Status:** local patch, default off; no upstream submission or new
  gameplay/performance measurement.

### Submission queue

The four submissions from the previous queue merged in v0.17.0. These are the
strongest remaining non-overlapping fixes to turn into focused PRs; no
upstream action should be taken without approval.

| Priority | Candidate | Current evidence | Next action |
| --- | --- | --- | --- |
| 1 | [0037: report serial raster work accurately](../patches/xboxrecomp/0037-report-serial-raster-worker.patch) | v0.17.0's Windows serial branch leaves the previous parallel worker count visible. The existing Release raster fixture rendered 2,944 covered pixels with no pixel errors but reported four workers for a below-threshold serial batch; the one-line correction makes the fixture pass. | Extract the fix, run the existing fixture on Windows and POSIX, recheck current upstream, and request approval before filing. |
| 2 | Correct `HalRequestSoftwareInterrupt` and `HalClearSoftwareInterrupt` fastcall ABI | v0.17.0 declares both exports `__stdcall`, marshals the IRQL from `STACK_ARG(0)`, and reports four callee-popped bytes. Independent nxdk and Cxbx-Reloaded declarations plus JSRF-PC prior art identify the Xbox ABI as fastcall. Exact PR/issue searches were empty. #308 changes DPC exclusion in the same kernel area but not these declarations, bridges, or pop counts. | Independently implement the declaration, `g_ecx`, and zero-pop corrections. Add a bridge fixture with different ECX/stack sentinels, cover canonical and remapped ordinals, credit JSRF-PC commit `a3938ee`, and run the bridge/thunk suites. |
| 3 | Reject immediate-derived entries that consume incoming arithmetic flags | Current `_pass_imm_ref_targets` can accept a `jcc` immediately after its producer because it calls the callback-body probe without the entry-frame guard. Merged #42 rejects weak immediates inside decoded instructions but does not cover this instruction-boundary case. JSRF-PC's eight-case fixture passed locally; no exact follow-up PR or issue was found. | Credit and extend #42 rather than presenting an unrelated discovery. Port the focused cases, add more legal/illegal entry shapes, run all disassembler tests, regenerate Brute Force both ways, and report every function-boundary delta before filing. |
| 4 | Firmware-exit half of [0032](../patches/xboxrecomp/0032-run-hooks-before-firmware-exit.patch) | Current `HalReturnToFirmware` and `HalInitiateShutdown` call `ExitProcess` without running the already-merged primary exit hook. The full local patch passes its registry fixture but also adds an independent multi-hook API. | Extract the two call-site changes from the registry API and add a child-process or mocked-exit test that proves the existing hook runs before process termination. Review the multi-hook registry separately. |
| 5 | [0013: software texture-sampling fast paths](../patches/xboxrecomp/0013-optimize-texture-sampling.patch) | No matching arithmetic change exists in #231 or merged #253. The Release fixture checks edge and random coordinates plus one million DXT cache-index cases. | Extract it from the local stack, include the fixture and report an isolated software-rasterizer benchmark. |
| 6 | Upstream-relevant half of [0019](../patches/xboxrecomp/0019-cache-hot-getenv.patch) | v0.17.0 caches `RECOMP_PB_EXEC_VERBOSE` in one method path but still calls `getenv` in every draw. Closed #128 contains the shared cache-helper prior art. The `RECOMP_PB_TRACE_CALLS` half depends on local patch 0008 and is not a current-main fix. | Extract one shared `RECOMP_PB_EXEC_VERBOSE` helper, credit #128, document startup-only semantics and re-profile that isolated current-main change. |

0007 is no longer in the immediate queue. Its local fixture passes retail,
devkit, extended and retained modes, but it exercises internal heap/contiguous
helpers rather than the exported kernel allocation APIs. Replacing host-memory
statistics remains a valid framework goal; the accounting model must cover or
deliberately exclude every exported allocator before it is proposed.

### Potential upstream issues

No new issue should be filed without approval. The current issue and PR search
found no exact report for these topics; open issue #127 is title bring-up
guidance and not a duplicate of the ownership or configuration contracts below.

| Readiness | Candidate | Why an issue rather than a PR | Status or next action |
| --- | --- | --- | --- |
| Resolved | Item 38, `XC_VIDEO` layout and policy | v0.17.0 corrects the high-word encoding identified by retail XAPI wrappers and independent EEPROM documentation. The compatibility default and configuration surface were separate policy decisions. | [#301](https://github.com/sp00nznet/xboxrecomp/issues/301) closed on 2026-11-06. Selected #320 adds exact-value widescreen and 480p opt-ins while preserving the 4:3 480i/60 default. |
| Filed | Renderer backend ownership and contract | Items 12, 15, 24-28, 31, 36, 39 and 40 intersect one versioning, capability, ownership, failure, synchronization and presentation boundary. Separate issues would fragment that decision. | Filed as [#306](https://github.com/sp00nznet/xboxrecomp/issues/306). Add backend-neutral reproducers or requirements there only with approval; do not file competing surface-ownership or output-sizing issues. |
| Ready with approval | Item 6a, `MmQueryStatistics` uses host free memory | Current `main` caps host `ullAvailPhys` to half of the Xbox's fixed 64 MiB total, so a modern host normally reports 8,192 available pages regardless of guest allocations. The defect is concrete, but the correct accounting must cover image, stack, heap, contiguous, extended and exported allocator paths and devkit memory. No exact issue or PR exists; #261/#269/#286 concern physical-page identity rather than this API contract. | File a requirements-first issue with the current-code reproducer and local fixture limitations. Keep patch 0007 out of the issue as a proposed solution until exported allocators and reset/concurrency are covered. |
| Coordinate in #306 | Item 36, CPU writes to GPU-owned colour surfaces | The failure and project workaround are concrete, but a generic ownership/write-notification contract crosses #184's renderer work and #285's readback seam. | Distill the existing CPU-write/GPU-overlay fixture into a backend-neutral reproducer, then add its required ordering cases to #306 with approval. |
| Coordinate in #306 | Item 39, aspect-aware output sizing | #184 carries title-facing aspect and Hor+ heuristics, but not the presenter's requested-picture-size contract. A standalone issue would split the same API discussion. | With approval, ask in #306 whether the backend should request a ratio or validated dimensions; file separately only if maintainers request it. |
| Hold | Item 40, ordered application metadata | Blood Wake supplies useful prior art, but Brute Force has no implementation and the NOP registration/collision contract is unresolved. | Ask the prior authors whether they intend to upstream it, then prove ordering on every walker path before opening a design issue. |

### Complete local-patch disposition

The read-only overlap search was refreshed on 2026-11-11 against all PR
titles through #321 and the #184 tracking index. Relevant existing work
includes merged #89, #97, #145, #247, #253, #285 and #300, closed #219, open
#231, #319 and #320, and design issue #306. No upstream interaction was
performed.

| Patch or range | Current-main isolation | Upstream overlap and disposition |
| --- | --- | --- |
| 0001 | Retired | Merged as #297 and included in v0.17.0. It intentionally does not solve in-range ordinal-shaped data at the lookbehind boundary. |
| 0002 | Clean | Merged #309 treats `WBINVD` as a cache no-op. The local patch deliberately retains a stronger C11 sequentially consistent fence, preserves flags, and now enables C11 atomics in every direct MSVC differential harness. Hold for a documented cross-host memory/device-ordering contract; the fence remains a partial model. |
| 0003 | Clean | Coordinate with #184's executor/backend work. Add a real cross-thread executor test and define reset/release behavior before filing. |
| 0004 | Clean | Non-overlapping small diagnostic candidate, behind the stronger queue above. Correct its comment, port the real-bridge test and coordinate environment documentation with #251. |
| 0005 | Retired | Merged as #300 and included in v0.17.0. Merged #285 is related ordering work, not a duplicate. |
| 0006 | Clean | Merged #252 supplies Linux x86-64 MMIO/OHCI decoding but is not a lifecycle duplicate. The v0.17 rebase preserves that decoder. Split API migration from title-side guards and add cross-platform lifecycle tests. |
| 0007 | Clean | The framework defect is ready for a requirements issue, but the patch is not ready for a PR: merged #158 provides allocation substrate while the fixture and implementation omit exported system/contiguous allocation paths. Replace the accounting model, then reconcile #261/#269/#286 alias ownership. |
| 0008 | Clean, but monolithic | Do not submit as one patch. Split staged walking, PGRAPH trap delivery, event shadows and IRQ/timer changes, then coordinate with #209, #222, #231 and #184. |
| 0009-0010 | Stack-dependent | Fold tests and any demonstrated union/strip corrections into #222; #249 is also relevant to strip winding. Do not file competing clipping/culling implementations. |
| 0011 | Stack-dependent | No exact PR found. Keep experimental until original-hardware depth/viewport behavior is established. |
| 0012 | Clean | #258 establishes the upstream SIMD-helper and cross-architecture pattern but does not vectorize this combiner path. Benchmark first and add compiler/architecture coverage. |
| 0013 | Stack-dependent | #231 and #253 are complementary, not duplicates. Extract the arithmetic fast paths, include the existing million-case fixture and benchmark them as a focused PR. |
| 0014-0015 | Stack-dependent | No exact frame-statistics or GPU-census PR found. Decide runtime versus tooling ownership, port standalone tests and quantify disabled-path cost before filing. |
| 0016-0018 | Stack-dependent | Backend-seam, vertex-program and visibility work belongs in coordination with #184, #272 and #285. Amend one agreed interface rather than opening a parallel backend series. |
| 0019 | Stack-dependent | Split it. Only the per-draw `RECOMP_PB_EXEC_VERBOSE` cache applies to current `main`; reuse one helper and credit closed #128. `RECOMP_PB_TRACE_CALLS` belongs to local 0008. Re-profile the isolated upstream-relevant change. |
| 0020-0022 | Stack-dependent | Texture metadata, window-clip state and depth-clear callbacks overlap the #184 interface and #222 state model. Contribute tests or fields to those designs instead of filing the local extension unchanged. |
| 0023 | Clean | Merged #156 partially overlaps capture suppression. Reconcile the capture UX, port the fixture and coordinate documentation with #251 before proposing another switch. |
| 0024 | Stack-dependent | Shared presentation is part of the broader #184/#285 ownership contract. Split the generic presenter seam from the project backend before further review. |
| 0025-0027 | Stack-dependent | Audit and capture tooling has no exact duplicate, but it crosses #184 and merged #285 executor/presenter seams. Selected #319 supplies a reusable backend contract fixture; 0027 adds the local trap and tagged-present callbacks to that shared fixture. Extract standalone framework behavior and coordinate before proposing it. |
| 0028 | Stack-dependent | Separate arbitrary source dimensions from Brute Force output policy; coordinate the generic contract with #184 and #285. |
| 0029 | Stack-dependent | Brute Force blend heuristic, not established NV2A behavior. Keep local; do not submit as a generic fix. |
| 0030-0031 | 0030 clean; 0031 depends on it | Draft #286 supersedes 0030's cached physical-intent tags with mirror-alias naming across APU/NV2A/OHCI. Select that model if accepted; separate any still-needed external-DSP hook/lifecycle API from GPL project code. |
| 0032 | Clean | Split the existing-hook firmware call sites from the independent registry. Prepare and test the small call-site fix first; keep startup registration, concurrency and repeated-exit policy with the registry proposal. |
| 0033-0034 | Clean | Scheduling and queue-depth policy overlaps the symptoms in issue #259, not an exact PR. Measure more titles/devices and prefer validated opt-in settings or MMCSS over changing global defaults. |
| 0035 | Retired | Merged as #298 and included in v0.17.0. Coordinate any equivalent xemu correction separately. |
| 0036 | Stack-dependent | Event-driven waiting retains the real PFB acknowledgement and does not skip synchronization. Keep the synthetic five-case fixture and coordinate any generic wait primitive with the backend contract in #306. |
| 0037 | Clean | v0.17.0's Windows serial raster path leaves stale parallel-worker telemetry. The existing raster fixture is a failing-before/passing-after reproducer; extract the one-line correction and run it cross-platform before filing with approval. |
| 0038 | Stack-dependent | Presenter device-loss handling belongs with the lifecycle and failure contract in #306; see item 45. |
| 0039 | Clean | Foreground-only display-awake policy is independent and default off; see item 42 before proposing it. |
| 0040 | Stack-dependent | Bounded P8 palette metadata extends the renderer seam; coordinate with #184 and item 24. |
| 0041, 0043 | Stack-dependent | Checked snapshots and allocation lifetime identities inform the shared surface-ownership contract; see item 46. |
| 0042 | Stack-dependent | Bounded verification and stalled-work diagnostics cross executor and wait interfaces; see item 47. |
| 0044 | Stack-dependent | Extends 0025's default-off audit and fixes stale frame-trace context; coordinate with #184 and extract the generic fixtures before proposing it. See item 28. |
| 0045 | Stack-dependent | Bounded acknowledgement, idle waiting and guarded PUT segments overlap merged #97's GET/segment ordering, closed #219's wrapped reads, merged #247's wait diagnostics and #306's backend contract. Keep the switches independent and default off; coordinate the executor contract before proposing any part. |

### Already filed from this project's account

These are existing upstream contributions and must not be duplicated by a new
local proposal.

| PR | Purpose | Status checked 2026-11-10 | Follow-up |
| --- | --- | --- | --- |
| [#283](https://github.com/sp00nznet/xboxrecomp/pull/283) | Build the MMX integer test on ARM64 and report skips. | Merged in v0.17.0 | No local selection remains. |
| [#284](https://github.com/sp00nznet/xboxrecomp/pull/284) | Define scalar `PMADDWD` wraparound. | Merged in v0.17.0 | No local selection remains. |
| [#285](https://github.com/sp00nznet/xboxrecomp/pull/285) | Allow backend readback before frame snapshots. | Merged in v0.17.0 | Coordinate items 12 and 15 with the merged seam rather than opening a competing hook. |
| [#297](https://github.com/sp00nznet/xboxrecomp/pull/297) | Validate extra-thunk lookbehind ordinals before rewriting them. | Merged in v0.17.0 | Local patch 0001 was removed. |
| [#298](https://github.com/sp00nznet/xboxrecomp/pull/298) | Preserve both HRTF output channels for a mono source. | Merged in v0.17.0 | Local patch 0035 was removed; coordinate an equivalent xemu correction separately. |
| [#299](https://github.com/sp00nznet/xboxrecomp/pull/299) | Describe generated `RECOMP_UNIMPL` runtime calls accurately. | Merged in v0.17.0 | No local patch was needed. |
| [#300](https://github.com/sp00nznet/xboxrecomp/pull/300) | Synchronize Windows framebuffer publication and snapshots. | Merged in v0.17.0 | Local patch 0005 was removed. |

## Already selected upstream work

These are existing upstream pull requests, not new proposals. Their pinned
commits are applied on top of xboxrecomp v0.17.0 at
`343b6d8901723b37ce568b2418c5df4e1fe1d223`; the resulting selected-commit and
local-patch tree is verified as `131abd8d1f4559bb36dba4391e4c644925cff5df`.
The exact commit list and application order are in
[toolkit.py](../tools/toolkit.py).

| PR | Purpose | Pinned commit(s) | Status checked 2026-11-10 |
| --- | --- | --- | --- |
| [#314](https://github.com/sp00nznet/xboxrecomp/pull/314) | Keep carry live for FCMOV conditions that consume it. | `f4e42bece1bfa7cafabc5112b0d59790634a0f63` | Open; selected. Focused FCMOV/integer/WBINVD/unimplemented tests passed. |
| [#319](https://github.com/sp00nznet/xboxrecomp/pull/319) | Add a reusable test-only NV2A backend contract fixture. | `7aba7bcb73b439671848e32829b2ca1953108ed2`, `fc95701c1a1994991e06c41d56fdeb234d708930` | Open; selected. All four backend, contract, present-sync and strip-winding cases passed after local fixture callbacks were shared. |
| [#320](https://github.com/sp00nznet/xboxrecomp/pull/320) | Add exact-value widescreen and 480p EEPROM opt-ins. | `cf5a4db8272945d256d472ff46278470552113ae` | Open; selected. All five default, widescreen, 480p, combined and zero-is-off EEPROM cases passed. |

The former selected PRs #262, #264, #277, #278 and #280 merged into v0.17.0
and are no longer cherry-picked separately.

Reviewed changes intentionally not selected:

| PR | Decision |
| --- | --- |
| [#315](https://github.com/sp00nznet/xboxrecomp/pull/315) | Rejected from the pin. The 32-bit MSVC differential run found 2,643 mismatches in 24,192 vectors, concentrated in signed 16-bit `IDIV`; the submitted validation skipped those native cases on macOS. |
| [#313](https://github.com/sp00nznet/xboxrecomp/pull/313) | Test portability only; useful upstream coverage but no runtime change needed by this Windows integration. |
| [#316](https://github.com/sp00nznet/xboxrecomp/pull/316), [#317](https://github.com/sp00nznet/xboxrecomp/pull/317) | POSIX and cross-host path behavior not required for the current Brute Force flow; do not broaden the pin without title evidence. |
| [#321](https://github.com/sp00nznet/xboxrecomp/pull/321) | Utility-drive cluster sizes for titles whose XBE requests 32 or 64 KiB. Brute Force has no observed need, so leave upstream rather than selecting speculatively. |

These statuses can change. They do not imply that any candidate below has been
filed or accepted.

## Local patches to evaluate

1. **Bound the kernel thunk lookbehind scan.**
   Merged [#297](https://github.com/sp00nznet/xboxrecomp/pull/297) rejects
   zero and out-of-range ordinals rather than rewriting arbitrary high-bit-set
   data. Its synthetic kernel-bridge regression covers valid and invalid
   candidates. The contribution remains intentionally scoped to ordinal
   validation: in-range ordinary data can still look like an import, and this
   change does not claim to solve that separate boundary problem.
   *Status:* merged in v0.17.0; local patch 0001 was removed.

2. **Model `WBINVD` ordering for the lifted guest.**
   [0002-model-wbinvd.patch](../patches/xboxrecomp/0002-model-wbinvd.patch)
   emits a C11 sequentially consistent fence and keeps EFLAGS tracking intact.
   Its lifter tests check emission, a subsequent conditional branch, and that
   `INVD` remains unimplemented. This is a partial model: guest caches are not
   modeled, and a C11 fence alone is not proof of all device, DMA,
   non-temporal-store, or host memory-ordering semantics. Before filing,
   establish the intended contract across supported compilers and hosts and
   cover their build requirements. The patch now enables `/std:c11` and
   `/experimental:c11atomics` in the shared direct-MSVC harness helper; the
   complete tools suite exercises those consumers. Merged #309 added a
   framework `WBINVD` no-op, but that does not provide the ordering retained
   by this stronger local model.

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
   Merged [#300](https://github.com/sp00nznet/xboxrecomp/pull/300) locks both
   publication and copying: two flips can otherwise reuse a buffer while the
   window thread is reading it. The
   [native fixture](../tests/present_test.c) tests format conversion,
   reader/writer exclusion, and concurrent whole-frame snapshots; a later
   local capture also matched a completed flip in the checked text region. The
   local Release fixture passed again on 2026-10-31. The upstream branch's
   Windows CTest covers the initial state, 32-bit and RGB565 conversion,
   pinned/running gates, 2x AA sampling, deterministic exclusion and
   concurrent whole-frame consistency; it passed 1/1 in Release. #285 is a
   separate ordering hook for backend readback before a snapshot; it does not
   protect either of these two CPU presentation buffers.

   A one-thread x64 Release synthetic check on an AMD Ryzen 7 9800X3D ran the
   same 640x480x32 publication and snapshot workload for 300 iterations in
   each of three alternating baseline/locked pairs. Baseline operations took
   0.035446, 0.035630 and 0.037276 ms; locked operations took 0.035480,
   0.035403 and 0.036693 ms. The overlapping ranges show no measurable
   uncontended regression. This is not a contention test, game-generation
   measurement or world-frame rate.

   The change merged in v0.17.0 and local patch 0005 was removed. The limited
   local capture remains supporting evidence rather than proof of general
   rendering correctness.

6. **Give the experimental OHCI controller a safe lifecycle.**
   [0006-harden-usb-lifecycle.patch](../patches/xboxrecomp/0006-harden-usb-lifecycle.patch)
   reports initialization failures, rolls back MMIO protection, retains and
   joins the worker, retires the IRQ callback under the strict gate, and
   reports failed shutdown. [test_usb.py](../tests/test_usb.py) covers
   enumeration, rollback, late startup, and shutdown failure paths using the
   Windows native fixture; the patch also updates the upstream POSIX fixture.
   The API change and the requirement to stop guest threads before unmapping
   need explicit caller migration, cross-platform tests, and lifecycle review.
   Merged [#252](https://github.com/sp00nznet/xboxrecomp/pull/252) supplies
   the Linux x86-64 MMIO/OHCI decoder retained by the v0.17 rebase, but it is
   not a lifecycle duplicate. Do not generalize this project's title-specific
   XPP instruction guard into a USB lifecycle fix.

6a. **Report guest memory pressure rather than host memory pressure.**
    *Problem:* `MmQueryStatistics` derives the available Xbox pages from host
    physical memory, so its result does not reflect the title's 64 MB or
    128 MB guest allocation space. The bridge also translated a null output
    pointer before the kernel implementation could reject it.
    *Evidence:* two retail x87 entries needed the corrected result during
    tutorial-resource bring-up. This establishes title dependence on the
    value, not that the current accounting exactly models every Xbox
    allocator.
    *Change:*
    [0007-account-guest-memory.patch](../patches/xboxrecomp/0007-account-guest-memory.patch)
    conservatively counts the image, declared initial stack commitment,
    scratch reservation, heap-page union, contiguous allocations and extended
    virtual-memory commitments, then reports the remainder of configured
    guest RAM. It also preserves null-pointer validation.
    *Tests:* the [memory-statistics fixture](../tests/memory_stats_test.c) and
    [Python wrapper](../tests/test_memory_stats.py) cover retail, devkit,
    extended-map and retained-memory configurations; all four Release modes
    passed again on 2026-10-31. The fixture allocates through the internal
    heap, contiguous and extended-VMA helpers. It does not call the exported
    `MmAllocateSystemMemory` or `MmAllocateContiguousMemory[Ex]` APIs, whose
    current host allocation paths are not counted by this patch. It also does
    not establish allocator fragmentation, every alias combination, or
    concurrent reset/reinitialization behavior.
    *Overlap:* checked 2026-10-31. Merged #158 supplies the reclaim and
    extended-VMA allocation substrate but leaves `MmQueryStatistics` tied to
    host RAM. Open #261, #269 and draft #286 change physical-page naming and
    alias ownership but do not implement guest memory statistics.
    *Before filing:* first replace the incomplete accounting model so every
    exported allocator is covered or explicitly out of scope. Then port the
    fixture upstream, reconcile the result with those alias changes, add
    reset/concurrency cases and validate a second title without treating the
    conservative estimate as hardware proof.
    *Status:* local patch; applies independently to current `main`; not
    prepared or filed.

6b. **Split staged pushbuffer walking from PGRAPH software-trap delivery.**
    *Problem:* the default walker does not preserve all JUMP/CALL/RETURN
    control flow needed by the observed stream, while a nonzero subchannel-0
    NOP can require an ordered PGRAPH software-method interrupt and guest
    acknowledgement. Advancing GET or clearing the flush early would violate
    the guest-visible protocol.
    *Evidence:* the staged path uses PFIFO
    `CACHE1_DMA_SUBROUTINE` as authoritative call state, stops invalid streams
    with diagnostics, and holds the GPU until the registered guest handler
    acknowledges a trap. The observed retail vector, miniport and event
    relationship is title evidence; the framework mechanism is generic.
    *Change:*
    [0008-experimental-pgraph-and-staged-walker.patch](../patches/xboxrecomp/0008-experimental-pgraph-and-staged-walker.patch)
    currently bundles an opt-in staged walker, PGRAPH trap delivery,
    event-shadow lifecycle and supporting IRQ/timer changes. The exact event
    registration remains in title code.
    *Tests:* [test_pushbuffer.py](../tests/test_pushbuffer.py) covers
    control flow and invalid streams; [test_pgraph.py](../tests/test_pgraph.py)
    and [test_fence.py](../tests/test_fence.py) cover masking, trap ordering,
    event registration, acknowledgement and failure paths. Cross-platform
    scheduling and broader hardware trap semantics remain unverified.
    *Overlap:* checked 2026-10-31. #209 changes timer/interrupt behavior,
    #222 changes PGRAPH state, #231 pipelines the walker/rasterizer boundary,
    and #184 indexes the pending backend series.
    *Before filing:* do not propose the monolith. Extract independently
    reviewable walker and trap/event changes, preserve the flush, semaphore
    and flip handshakes, then rebase each onto the accepted upstream pieces.
    *Status:* local opt-in framework experiment; applies as a monolith to
    current `main`; not prepared or filed.

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
   2026-10-31) records the eight rectangles but folds only slot 0 into a
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
   the cause of the user's live-window observation. Patch 0044 prevents
   stale CPU-transformed vertices in GPU-program traces and labels GPU
   triangle counts as pre-clip submissions with visibility unknown; it does
   not measure GPU post-clip output.
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
    and blocks rewritten at the same address. The Release math fixture passed
    again on 2026-10-31.
    *Overlap:* checked 2026-10-31. #231 pipelines raster work and #253 changes
    texture filtering; neither implements these coordinate or cache-index
    arithmetic fast paths.
    *Before filing:* extract the patch from the local walker-dependent stack,
    move the math fixture into upstream CTest and report an isolated software
    rasterizer benchmark.
    *Status:* strong preparation candidate; local patch, not yet prepared or
    filed.

## Performance and GPU back-end candidates

These come from the [performance plan](./PERFPLAN.md), the recorded Blood
Wake adaptation baseline at
[`e631edd`](https://github.com/minimalice92/bloodwake-recomp/tree/e631edddfe56d22344e9f696923aeb6d78a9e7ba),
and the v0.3.3 source review at
[`6292340`](https://github.com/minimalice92/bloodwake-recomp/tree/6292340705d9b863351a45ba160174a346a81688).
Both Blood Wake revisions pin the older xboxrecomp v0.16.0 base
(`f0f8a461`), and carry many changes as patched toolkit files under
`src/toolkit_patches/`. Unless an item says otherwise, its status is
*planned*: no local patch exists yet. The broad overlap snapshot was refreshed
on 2026-11-10 through upstream #321; individual older entries retain the date
of their more focused search. This was not an exhaustive line-by-line review
of unrelated PRs. Adapted Blood Wake code is listed in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md); no code from `6292340`
has been adapted. Before preparing any of these, ask its authors whether they
are upstreaming the same parts.

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
    (open, this project's account, checked 2026-10-31) adds
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
    A final-generation six-leg title campaign on 2026-11-03 measured 96 cold
    compile/create events at 12.914/25.543/30.623 ms p50/p99/max and 96 warm
    lookup/create events at 0.143/0.517/0.535 ms. Each cold run published 32
    entries, each warm run loaded all 32, and no warning or temporary file
    remained. Movement flip throughput and p99 frame time were effectively
    unchanged. The stationary captures were visually matched but not
    byte-identical or frame-locked, so strict title output identity remains
    open and the cache remains default-off.
    *Overlap:* #184's D3D11 back end, and
    [#274](https://github.com/sp00nznet/xboxrecomp/pull/274) (compile once
    per HLSL text), which applies the same keying idea to the D3D8 layer;
    overlap checked 2026-10-31.
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

    Blood Wake v0.3.1 adds another relevant but title-coupled layer:
    `bw_vshpre.c` observes every vertex shader created by known boot-time
    title wrappers, generates the same raw, gathered, point and fixed-function
    HLSL variants as first use, and compiles them into the persistent cache
    on workers without touching the D3D device. Brute Force currently caches
    only combiner pixel shaders and falls back to CPU vertex transformation
    while a new vertex shader compiles. A future adaptation needs a
    Brute Force-owned discovery route or generic preseed API, exact compiler
    and source identity, explicit worker failures, and cold/warm output
    checks; Blood Wake's wrapper addresses and shader groups are not generic.

14a. **Upload and sample declared guest mip chains in GPU back ends.**
    *Problem:* a backend exposing only level zero cannot reproduce guest
    minification; independently, the existing texture metadata does not
    carry CONTROL0 LOD clamps as explicit fields. Whole-chain source bounds,
    cube spacing, P8 ranges and cache invalidation need to agree.
    *Evidence:* a synthetic D3D11 scene selects distinct levels and catches
    same-frame lower-level rewrites; the off path remains level zero. This
    does not establish that the Brute Force retail revision supplies a
    particular chain or that its enabled image matches xemu/NV2A.
    *Change:* generic project-side layout, uploads, SRV exposure and
    descriptor-based sampler identity use the existing raw register mirror.
    `RECOMP_GPU_MIPS` and `RECOMP_GPU_ANISO` are independently default-off;
    no framework patch or pinned-tree change was made. Explicit fallbacks
    retain the CPU reference, and no title addresses or content are involved.
    The level-zero path also rejects malformed non-power-of-two swizzles whose
    Morton indices exceed their source span instead of reading out of bounds.
    Blood Wake's mip upload and `VFX_ANISO` are prior art; no code was copied
    from it for this implementation.
    *Tests:* WARP and RTX 5070 synthetic 2D/cube upload and sampled-image
    fixtures cover swizzled, linear, BC and P8 formats, all exposed levels,
    filters, fractional clamps, positive/negative bias, declared-count and
    palette/last-face rewrites, guarded and tracked-short chains including
    fallback rewrites under approximate caching, and a final-level invalid P8
    index. Render-target, depth and stencil alias regressions exercise their
    level-zero/rejection behavior. They do not prove NV2A semantics, title
    compatibility, race freedom for concurrent guest writes, or performance.
    *Overlap:* checked read-only 2026-11-11: [#184](https://github.com/sp00nznet/xboxrecomp/pull/184)
    tracks GPU backend work; merged [#253](https://github.com/sp00nznet/xboxrecomp/pull/253)
    concerns CPU filtering without this D3D11 mip path; merged
    [#145](https://github.com/sp00nznet/xboxrecomp/pull/145) covers CPU P8
    sampling. [NV30 sampler code](https://github.com/Kenji-NX/mesa/blob/main/src/gallium/drivers/nouveau/nv30/nv30_texture.c)
    supports the 4.8 clamp interpretation used here, while
    [xemu v0.8.139](https://github.com/xemu-project/xemu/blob/aa54c34cae1c09410d74c69a44210a9867fe2d16/hw/xbox/nv2a/pgraph/texture.c)
    uses raw integer clamp values. These are reference observations, not
    proof of NV2A hardware behavior.
    *Before filing:* reconcile clamp units and declared cube stride with
    xemu and hardware evidence; define generic backend metadata and
    ownership/race contracts with #184; verify additional titles, formats,
    aliases and performance. Recheck upstream before any approved filing.
    *Status:* project implementation and synthetic fixture; framework
    contribution candidate, not prepared or filed.

15. **Present from the back end, with readback on demand.**
    *Problem:* the presenter copies or converts the flipped guest frame on
    the CPU, and a GPU back end must read its surface back into guest memory
    every flip to feed it. Blood Wake measured about 4 ms per flip for that
    wait. The framework contract also does not define a transaction for
    changing the producer's scaled resources together with a shared output
    while a prior frame can still be owned by the presenter.
    *Evidence:* source inspection found that the previous local native-scale
    path could commit a new output and epoch before all replacement
    allocations had succeeded. Synthetic failures and a held keyed mutex
    reproduce the relevant failure and busy boundaries; this is not an
    observed retail allocation failure.
    *Change:* the opt-in project back end publishes a 32-bit D3D11 shared
    texture to the existing window device through
    [0024-gpu-shared-presentation.patch](../patches/xboxrecomp/0024-gpu-shared-presentation.patch).
    A keyed mutex and one pending slot bound snapshots; frame IDs prevent a
    stale GPU image from replacing a newer CPU snapshot. Pinned framebuffers
    and PVIDEO overlays drain the slot and use the CPU frame. The producer
    retains guest readback by default. A separate default-off project option
    elides copies only for already-coherent native 32-bit surfaces; every
    tracked-surface flip still waits. A two-event renderer-work bound now
    provides backpressure without defining general on-demand readback.
    [0028-scaled-gpu-presentation.patch](../patches/xboxrecomp/0028-scaled-gpu-presentation.patch)
    extends that local protocol for the Blood Wake-derived internal-resolution
    milestone: the shared source may have dimensions independent of the guest
    framebuffer, those dimensions are verified against the opened texture,
    and the renderer can query the current 4:3 client picture. The project
    backend still owns the exact/box/Catmull-Rom final resolve; this patch
    does not import Blood Wake's direct swap-chain ownership.
    The project backend now prepares replacements before committing native
    output changes, migrates colour, and retains prior resources on failure.
    It defers a busy handoff rather than skipping the keyed-mutex protocol.
    Changed-size depth/stencil is not migrated. This hardening changes no
    framework patch; a generic lifecycle contract remains to be designed.
    Patch 0038 separately makes unrecoverable Windows presenter/device
    failures explicit; it does not implement recovery.
    *Tests:* WARP and hardware fixtures check exact shared pixels, busy,
    future and stale slots, arbitrary valid source dimensions, dimension
    propagation, pinned and overlay fallback, and snapshot consistency.
    Project fixtures inject failures across scaled initialization, target,
    migration, shared-output and readback allocations; they check rollback,
    retry, an actual hidden-window resize and its flip-only fallback with a
    different target bound. The focused cases passed on
    WARP and the default NVIDIA GeForce RTX 5070 hardware adapter as
    applicable. They do not prove retail resize, depth preservation, device
    recovery or multi-driver behavior.
    *Overlap:* #285 covers ordering the readback against snapshots;
    [#215](https://github.com/sp00nznet/xboxrecomp/pull/215) (merged) added
    the D3D11 presenter, #220 covers PVIDEO work, and #306 discusses the
    backend contract (checked with the #184 tracking index on 2026-11-11).
    *Before filing:* separate the generic arbitrary-source contract from the
    project-specific output-size policy; define transactional resource and
    depth-content behavior across backends; investigate the variable combined
    title performance and frequent busy slots; test real device loss, other
    drivers and window lifecycles; establish guest readers before eliminating
    readback; and reconcile the presentation interface with #184 and #285.
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
    *Evidence:* the reviewed Brute Force baseline attributed a mean 69.1% of
    game-thread samples to `sub_00372180`. The title sets the real bit and
    `nv2a_ack_busy_bits` clears it; a flip hold deliberately refuses that
    acknowledgement. Blood Wake measured its game thread at 49% in this wait
    before signalling it. The corrected Brute Force candidate completed one
    enabled scripted movement smoke. Its cumulative diagnostics exercised
    both event and timer wakes without a reported spurious wake, but the run
    had no caller profile or reviewed capture and is not performance evidence.
    *Change:*
    [0036-event-driven-pfb-flush-wait.patch](../patches/xboxrecomp/0036-event-driven-pfb-flush-wait.patch)
    adds the generic, default-off `RECOMP_PFB_FLUSH_WAIT=1` event and wait
    helper. It wakes at least every 200 us for IRQ safe points, rechecks the
    bit after every wake, signals only after the real atomic clear, and does
    not acknowledge while a flip is held. The title-specific address,
    structure checks, kickoff write and generated-function wrapper remain in
    `src/recomp_manual.c`.
    [0042-bounded-nv2a-diagnostics.patch](../patches/xboxrecomp/0042-bounded-nv2a-diagnostics.patch)
    adds default-off, at-most-eight prolonged-wait reports without clearing
    the bit or altering the acknowledgement.
    *Tests:* five native helper cases and six project wrapper cases cover
    disabled, normal, spurious, lost-event/timer recovery, reuse, flip-held
    and invalid-value behavior. Fence/semaphore, interrupt, PGRAPH trap,
    pushbuffer-ring, flip-hold and D3D11 device-failure selections passed
    before the title smoke. Two title-hook fixture modes now require the
    disabled and enabled paths to consume exactly one guest return. The first
    title build stalled because that title wrapper omitted the generated
    routine's four-byte `ret` effect; this was corrected without changing the
    framework helper. One corrected enabled movement smoke passed. The
    diagnostic fixture exercises off/on, bounded reports and flip-held
    acknowledgement. A reviewed alternating title comparison remains
    outstanding.
    *Overlap:* checked open and merged work plus the tracking index
    [sp00nznet/xboxrecomp#184](https://github.com/sp00nznet/xboxrecomp/pull/184)
    on 2026-11-03. No current change implements this handshake.
    [sp00nznet/xboxrecomp#285](https://github.com/sp00nznet/xboxrecomp/pull/285)
    concerns pre-snapshot readback and does not overlap.
    *Before filing:* run and review poll/event/poll/event/poll/event, explain
    the timer/recheck counts, retain the CPU reference, test another title or
    a framework fixture at the integration boundary, and split any unrelated
    documentation or title hook from the generic patch.
    *Status:* local default-off framework patch; applies in the pinned toolkit
    tree; not prepared or filed.

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
    stop explicitly rather than writing fabricated successful results. The
    project backend also snapshots the physical/logical area divisor at each
    query start and splits queries around target changes and internal blits;
    this avoids normalizing earlier samples with a later scale or AA layout.
    *Evidence:* the old fixture let the framework's ack thread poll the
    single-threaded D3D device while the fixture's main thread drew. That is
    an API violation and a plausible contributor to the WARP device loss;
    the separate effects of the fixes have not been isolated. A reviewed
    six-leg title campaign on 2026-11-03 isolated async reports from every
    other option. Mean guest-surface world cadence increased from 27.401 to
    30.606 changes/s (11.7%) and G3 fell from 69.367% to 65.467%. The selected
    async intervals wrote 487,725 reports; no visibility-query or device error
    was logged. All three async attempts completed, while one sync boot
    stalled after flip 12 and passed on retry. The async 1% low was only
    15.697/s and G3 still failed, so this is evidence for the mechanism, not a
    default or correctness claim.
    *Tests:* the [D3D11 fixture](../tests/d3d11_test.c) checks ordered
    reports, active counting, clears, empty intervals, and more than 64
    counting intervals on WARP and hardware. New sync and async synthetic
    cases span failed and successful native resizes and mixed-AA targets.
    CPU and D3D counts differ on the shared triangle edge, as expected from
    their rasterization rules.
    *Overlap:* #184's GPU series and #306's backend contract discussion; no
    matching visibility-report PR was found in the PR listing checked
    2026-11-11.
    *Before filing:* add broader title-scene and driver coverage, verify
    report lifetime and actual device reset, and obtain matched CPU-reference
    evidence. Keep async default-off while the 1%-low and G3 gates fail.
    *Status:* local patch and opt-in project implementation.

23. **Avoid `getenv` in per-draw and per-CALL code.**
    *Problem:* `draw_primitive` called `getenv("RECOMP_PB_EXEC_VERBOSE")` for
    every draw, and the pushbuffer walker evaluated
    `getenv("RECOMP_PB_TRACE_CALLS")` before a cheap counter test on every
    CALL. `strchr` (from `getenv`) was 4.5% of the NV2A thread on the D3D11
    path.
    *Change:*
    [0019-cache-hot-getenv.patch](../patches/xboxrecomp/0019-cache-hot-getenv.patch)
    reads both once. For an upstream change, use one file-scope helper for
    `RECOMP_PB_EXEC_VERBOSE`, which current `main` already caches in one
    method path but still reads in every draw. Keep the
    `RECOMP_PB_TRACE_CALLS` cache with local staged-walker patch 0008; that
    switch does not exist on upstream `main`. Plain efficiency fix; no
    behaviour change after startup.
    *Tests:* existing pushbuffer and executor fixtures; not yet re-profiled.
    *Overlap:* checked 2026-10-31. No current open or merged PR fixes the
    remaining per-draw lookup. Closed
    [#128](https://github.com/sp00nznet/xboxrecomp/pull/128) contains the
    shared `env_once`/`pb_verbose` prior art from the original monolithic
    bring-up; it was closed after other pieces were split and must be credited.
    *Before filing:* extract only the current-main
    `RECOMP_PB_EXEC_VERBOSE` change, state that the switch is startup
    configuration, reuse or improve #128's helper rather than duplicating
    caches, and re-profile the isolated patch.
    *Status:* small current-main extraction candidate; the full local patch
    remains stack-dependent and is not prepared or filed.

24. **Expose palette and cube-layout metadata to render back ends.**
    *Problem:* `Nv2aTexture` omitted the P8 palette, and a stale linear
    pitch incorrectly made a swizzled or compressed texture report one mip
    level. A back end could not decode P8 or locate cube faces correctly.
    Before the current change, the palette address was passed but the executor
    discarded its programmed length bits; the CPU P8 sampler therefore could
    not check indices against that range.
    *Evidence:* the Brute Force tutorial census recorded P8 and cube use;
    the CPU decoder already samples P8 through the guest palette. Synthetic
    guarded allocations exercise all four palette lengths in the D3D11 path.
    Synthetic CPU decoder cases now cover short ranges and invalid indices;
    out-of-range hardware behavior is not established.
    *Change:* [0020-texture-metadata.patch](../patches/xboxrecomp/0020-texture-metadata.patch)
    passes the palette through both backend paths and the reference decoder,
    and preserves the programmed level count for format-sized textures.
    The D3D11 back end ignores stale pitches for swizzled and DXT data, as
    Blood Wake's `drop_stale_pitch` does, and includes mip padding in cube
    face offsets. The project renderer separately reads the raw stage
    register, snapshots the programmed palette length, and rejects invalid
    indices and known-short or unreadable ranges.
    [0040-checked-p8-palette.patch](../patches/xboxrecomp/0040-checked-p8-palette.patch)
    carries the programmed length through generic state and both backend
    interfaces, bounds CPU samples and adds a checked snapshot decoder.
    An invalid index fails without modifying whole-decoder output; per-pixel
    software sampling retains its failed-sample fallback. Zero length on
    caller-constructed metadata keeps the former 256-entry default. Follow-up
    [0043](../patches/xboxrecomp/0043-checked-snapshot-lifetimes.patch)
    rejects input/output overlap before validating and decoding snapshots, so
    output cannot change an index after its bounds check.
    *Tests:* CPU/GPU P8 decoding and same-frame palette rewrite, plus cube
    faces with multiple levels on WARP and hardware. Project D3D11 fixtures
    also cover guarded 32/64/128/256-entry palettes, invalid indices and
    known-short allocations. The framework fixture checks all programmed
    lengths, malformed indices, guard/readability, overlapping buffers and
    unchanged output on failure. These do not establish original-hardware
    behavior or serialize concurrent guest writes.
    *Overlap:* [#145](https://github.com/sp00nznet/xboxrecomp/pull/145)
    added CPU P8 sampling; #184 tracks the GPU interface. Checked
    2026-10-31; no new upstream check was made for this work.
    [xemu's texture code](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/texture.c)
    derives a 32/64/128/256-entry palette length from the register; its
    OpenGL backend hashes and bounds-checks that selected range. Its
    conversion directly indexes the palette, so it does not establish
    hardware behavior for out-of-range indices.
    *Before filing:* coordinate the backend interface with #184, establish
    hardware behavior for out-of-range indices, and test shorter allocations,
    cube layouts and guest-write synchronization on other titles.
    *Status:* local patches 0020, 0040 and 0043; synthetic-tested, not filed.

25. **Support depth textures and depth-surface aliases.**
    *Problem:* the CPU reference decoder and this project's D3D11 backend
    did not sample `LIN_D24S8` (`0x2E`); GPU depth surfaces were not exposed
    as shader resources. The framework/backend contract also lacks ordered
    guest-write notification and GPU-depth readback/ownership semantics.
    *Evidence:* both variants of the 2026-10-29 movement campaign repeatedly
    logged failed `0x2E` decodes. Whether those reads explain a visible
    rendering difference has not been determined. A bright pass also binds
    a tracked GPU depth allocation as a `0x41` texture. A native 640x480 xemu
    Basic Controls match now establishes the visible effect in that state:
    with all other controls fixed, the project frame differs from xemu by
    more than 8/255 in 43.4320% of pixels without packed-stencil
    reconstruction and 3.7119% with it.
    *Change:* the opt-in project renderer can sample an exact matching
    GPU-created linear D24S8 surface through an R24 view. New eligible surfaces
    are initialized from guest memory, converting the Z24S8 field order;
    full-surface GPU depth clears also initialize them. Checked guest spans,
    including padding and allocation identities, detect changes on eligible
    unscaled surfaces. A clean owner is transactionally reseeded; changes
    after potential GPU writes, invalid spans, incompatible dirty owner
    transitions and conflicting color/depth or partial overlaps reject work.
    A stable full depth-and-stencil clear can rebaseline a live conflicted
    allocation. This project-side workaround is not ordered write notification
    or synchronized guest readback. When bound for depth writes, a separate
    snapshot avoids simultaneous DSV/SRV binding. An independent opt-in reconstructs the three packed
    depth bytes and stencil alpha through a GPU compute shader for exact
    `0x41` overlaps, requiring both planes to be initialized. Ordinary
    non-overlapping `0x41` textures are unchanged. With depth aliasing on but
    packed-stencil aliasing off, `0x41` over tracked depth is rejected rather
    than falling back to possibly stale guest bytes.
    PROJECT2D and PROJECT3D shadow comparisons follow
    [xemu's shader translation](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/glsl/psh.c).
    Same-value and unchecked concurrent writes, guest reads, unknown
    suballocations, partial aliases, scaled aliases, other formats and CPU
    `0x2E` decoding remain unsupported. The CPU samples `0x41` guest bytes
    with a separate float depth buffer and no stencil.
    *Tests:* WARP and hardware fixtures cover every comparison above, below
    and at the reference depth, a bound depth target that is written between
    draws, an unbound target, base draws, and rejected pitch, size, offset,
    mip, cube, format, swizzled layout, recreated depth and stage-mode cases,
    plus guest depth and stencil initialization without a GPU clear. Packed
    alias fixtures cover exact stencil keys, filtering, GPU stencil writes,
    initialization, invalid overlaps and base/vertex-program paths. A
    synthetic replay of the traced bright-pass combiner demonstrates the
    effect of stale versus GPU-cleared stencil on its threshold and an
    additive composite, without replaying gameplay or its blur. New WARP and
    hardware ownership fixtures cover clean/dirty guest changes, clear
    rebaselining, guarded and short ranges, padding, exact/partial color and
    depth overlaps, allocation release/reuse, layout changes, injected
    snapshot races and transactional rollback. Both switches explicitly
    reject scaled aliases.
    *Overlap:* Blood Wake's optional ambient-occlusion path in
    `bw_d3d11.c` creates an `R24G8_TYPELESS` resource with D24S8 and R24
    views. It is not a guest-depth-alias implementation. The
    [brute-force-2003-reloaded recreation](https://github.com/kduong-dev/brute-force-2003-reloaded)
    describes a stencil-aware glare pipeline but is not an xboxrecomp
    renderer or an original-hardware oracle. A refreshed read-only search on
    2026-11-11 found no exact depth/stencil-alias implementation. Design issue
    #306 and open backend-contract fixture #319 remain the relevant ownership
    coordination points; no upstream interaction was performed.
    *Before filing:* the darker Basic Controls image is now validated against
    a native xemu reference. Coordinate a generic ownership contract in #306,
    refresh upstream overlap with approval before action, preserve GPU
    contents across surface recreation, define partial and scaled aliases,
    and validate
    matched movement, sun-facing, fade and split-screen scenes. The focused
    same-generation pair is one run per variant and is correctness evidence,
    not a performance result.
    *Status:* experimental project-only implementation.

26. **Pass window clips to capable GPU back ends.**
    *Problem:* GPU backends need the same explicit clip state as CPU draws;
    clear and stencil side effects must not silently escape the clipping rule.
    *Evidence:* the existing project executor forwards opt-in NV2A rectangle
    state; synthetic scenes exercise it, not original-hardware reset behavior.
    *Change:* [0021-gpu-window-clip.patch](../patches/xboxrecomp/0021-gpu-window-clip.patch)
    carries the existing rectangle state and an explicit backend capability.
    Back ends without the capability still reject the opt-in. The D3D11 base,
    transformed and GPU-vertex draws apply the same inclusive/exclusive union
    and AA coordinate shifts as the software helper; patch 0021 does not
    change clears. The project D3D11 renderer separately honors clear
    rectangles and color masks.
    *Tests:* CPU, WARP and hardware fixtures check all eight rectangles,
    empty and incomplete state, inclusive/exclusive masks, state changes,
    both AA shifts, scale-two mapping, clear behavior, base/transformed/GPU
    vertex paths, and depth/stencil discard side effects. GPU stencil is
    checked separately because the CPU renderer does not model it.
    *Overlap:* #222 and the #184 index were last checked in the recorded
    2026-11-10/11 review; no new upstream check or contact was made.
    *Before filing:* reconcile #222 state/reset semantics, test other
    backends and obtain matched gameplay/original-hardware evidence.
    *Status:* local patch and project fixtures; keep the switch default off.

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
    also models clear rectangles (open, checked 2026-10-31).
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
    [0044-geometry-depth-audit.patch](../patches/xboxrecomp/0044-geometry-depth-audit.patch)
    distinguishes CPU post-clip output from GPU pre-clip submissions,
    reports CPU `0x2E`/`0x41` current-zeta bindings, and attributes dropped
    primitive/input counts to at most eight recent unhandled/mirror-only
    method events, expired after 256 method events and reset at flips. These
    correlations and bindings do not prove causation, sampling or visibility.
    It also fixes frame traces that could reuse stale CPU vertices for GPU
    submissions and labels CPU-transformed trace vertices pre-clip.
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
    Additional standalone and real-executor cases cover the history bound,
    expiry/flip reset, clipping scopes, depth-binding diagnostics, and
    audit-off/on guest bytes and callback ordering on CPU, WARP and available
    hardware. Coverage and limitations are in
    [FINDINGS](./FINDINGS.md#gpu-audit-instrumentation).
    *Overlap:* public PR-title and tracking-index metadata checked 2026-10-31:
    merged [#152](https://github.com/sp00nznet/xboxrecomp/pull/152) and
    [#162](https://github.com/sp00nznet/xboxrecomp/pull/162) touch related
    executor/backend work; open tracking index
    [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) holds a GPU
    series. No matching audit/census PR was found by title; this was not an
    exhaustive content review. Generic accounting draws on Blood Wake's
    `src/gpu/bw_gpu_audit.{c,h}` at `e631edddfe56d22344e9f696923aeb6d78a9e7ba`;
    the MIT notice is retained and credited in
    [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
    A refreshed read-only search on 2026-11-11 found no exact audit duplicate.
    Open #231 changes raster scheduling and open #319 provides a reusable
    backend-contract fixture; both are coordination points rather than
    substitutes. No upstream contact was performed.
    *Before filing:* recheck upstream and coordinate with prior authors; move
    the executor integration cases into a standalone framework test, test
    Linux/pthread and other backends, assess disabled-path overhead and
    snapshot durability, and review the bounded-table loss semantics. No
    title-specific data or captured game content belongs in a contribution.
    *Status:* local patches 0025 and 0044; synthetic-tested, not prepared or filed.

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
    *Overlap:* checked 2026-10-31. Merged
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
    the ramp at the opening fade or its visible impact. The native xemu Basic
    Controls difference is reproduced by packed-stencil reconstruction
    without a gamma adjustment, so the DAC hypothesis is not its cause. See
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
    *Overlap:* checked 2026-10-31. Merged
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
    *Status:* planned investigation; deprioritized by the matched
    packed-stencil result; no patch or pull request.

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
    *Tests:* The stale fixture call to `take_shared` was corrected to pass
    its width and height outputs. The original 17 synthetic capture cases
    passed on MSVC, including WARP and
    hardware shared rejection, CPU-upload readback, concurrent snapshots,
    repeat/skip alignment, overlays and error paths. Capture-off project
    presentation fixtures, four framework CTests and 174 integrated project
    GPU/statistics/audit/clip/presentation cases passed. Two bounded,
    unclassified retail probes verified one flip link each for GDI and D3D11
    CPU-upload; other submissions were no-client or occluded. The successful
    submissions may predate minimization. Matched scenes, physical visibility,
    shared-GPU capture and SDL validation remain open.
    *Overlap:* checked 2026-10-31. Merged
    [#156](https://github.com/sp00nznet/xboxrecomp/pull/156) covers flip
    dumps, [#220](https://github.com/sp00nznet/xboxrecomp/pull/220) PVIDEO,
    and [#215](https://github.com/sp00nznet/xboxrecomp/pull/215) the Windows
    presenter. Merged [#285](https://github.com/sp00nznet/xboxrecomp/pull/285)
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
    *Overlap:* checked 2026-10-31. Upstream #264 is selected separately for
    the SSL restart gap. Draft #286's mirror-alias page naming supersedes the
    cached physical-translation intent in 0030 and covers APU, NV2A and OHCI;
    #261 and #269 are related allocation steps. #266/#270 concern
    sequencer-off frames and are not substitutes for an external DSP.
    *Before filing:* select #286's physical-identity model if it is accepted
    and remove the superseded local tags. Separate any still-needed generic
    hook and lifecycle API from the GPL project DSP, add non-Windows
    concurrency coverage, and isolate the accurate-mix defect before
    proposing that DSP as a quality path.
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
    currently bundles two separable changes: it runs the already-merged
    primary hook before firmware and shutdown exits, and it adds bounded
    independent hook registration while preserving that replaceable primary
    report hook. The call-site fix does not require the registry API.
    *Tests:* a native fixture covers registration, deduplication, primary
    replacement, ordering scope, and reason propagation; the retail exit path
    finalized and shut down cleanly. The Release fixture passed again on
    2026-10-31, but it calls `recomp_exit_hook_run` directly and does not
    exercise either `Hal*` exit function before `ExitProcess`.
    *Overlap:* checked 2026-10-31. Merged #161 and #215 provide the primary
    exit-hook/window foundations but do not add independent hooks or run them
    before firmware exits.
    *Before filing:* prepare the two existing-hook call sites first, with a
    child-process or mocked-exit fixture proving the hook runs before
    termination. Review the independent registry separately: document
    registration as startup-only, define concurrent/repeated-run behavior,
    and decide whether bugcheck paths need an additional-only runner without
    duplicating the missing summary.
    *Status:* split candidate. The existing-hook firmware call sites are the
    stronger first PR; the independent registry remains a local framework
    patch, not yet prepared or filed.

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
    whether MMCSS is preferable to a fixed Win32 priority. Blood Wake v0.3.0
    is now direct prior art for joining its audio workers to an MMCSS task;
    compare that policy rather than stacking it blindly on the fixed priority.
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
    Checked snapshots now reconcile exact texture aliases as well; currently
    bound targets use an ordered GPU copy, and ambiguous or conflicting
    GPU-dirty overlaps reject the draw. This remains a project-side policy,
    not a generic write-notification or synchronization contract.
    *Tests:* CPU-written colour frames survive scale 1 and scale 2, both alone
    and with a later GPU overlay. The complete D3D11 matrix passes 190 cases
    on hardware and 123 with 67 hardware cases skipped under WARP. Bounded
    scale-1 and 4.5x retail opening probes retained visible movie content.
    Additional synthetic WARP/hardware cases cover exact aliases, guest
    changes, partial overlap rejection and sampling a bound target. No new
    retail run exercised the current alias logic.
    *Overlap:* checked 2026-10-31. Open
    [#285](https://github.com/sp00nznet/xboxrecomp/pull/285) establishes
    backend readback before snapshots but does not define guest writes after a
    GPU clear. Tracking index
    [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) covers the GPU
    series. Its
    [`d08e117`](https://github.com/sp00nznet/xboxrecomp/commit/d08e117289352b4544bf1c8d607d1de51373e62d)
    head commit centralizes when a GPU render target may be sampled as a
    texture; it does not detect or order CPU writes into guest backing memory.
    Merged
    [#220](https://github.com/sp00nznet/xboxrecomp/pull/220) covers PVIDEO,
    which this Bink path did not use.
    *Before filing:* design a generic ownership/write-notification contract
    instead of requiring a backend to poll guest bytes. Cover partial CPU
    writes, writes between multiple GPU batches, clear masks and rectangles,
    render-target aliases, races, non-Windows backends and page-protection
    interactions. Preserve the CPU reference path.
    *Status:* framework gap with a tested project-side workaround; not
    prepared or filed.

37. **Preserve both HRTF output channels for a mono source.**
    *Problem:* the VP records a voice's source width before filtering. HRTF
    accepts a duplicated mono input and produces distinct left and right
    samples, but the following bin mixer still selected with
    `bin_index % source_channels`. For a one-channel source that always chose
    HRTF output zero and copied its left result into every route.
    *Evidence:* a synthetic mono multipass voice with HRTF coefficients that
    emit `+1` left and `-1` right routed `+1/+1` before the fix. Corrected
    Brute Force retail generation `20261030-142754-81f8c6e6` reached a
    rendered Quick Campaign world and used 3D-list voices 25-31 with HRTF
    routes in bins 6-10. In a clean scripted firing interval, matched
    fixed/baseline/fixed captures had left/right correlation
    `0.415/0.542/0.422` and side-to-mid RMS ratios
    `0.641/0.544/0.636`. The two fixed legs and baseline reported no
    starvation, refused submissions, underruns, clipping, or bounded 32/256
    frame holes. These are signal measurements, not a listening verdict.
    *Change:* merged
    [#298](https://github.com/sp00nznet/xboxrecomp/pull/298) tracks the
    post-filter output width and uses two channels after HRTF succeeds. It is
    a generic bug fix with no title addresses or opt-in switch.
    *Tests:* the standalone `apu_vp` fixture now requires the synthetic
    `+1/-1` result; the unpatched negative control fails as `+1/+1`.
    On 2026-10-31 the isolated patch applied to current upstream `main` and
    its Release `apu_vp` CTest passed on MSVC; the same test-only change on
    unmodified `main` failed with the expected `+1/+1` diagnostic. The
    project fixture also passed after a final Release rebuild. The corrected
    retail A/B/A above exercises the real 3D/HRTF route. Focused
    audio/build/launcher tests passed 144 cases; the user's gameplay listening
    pass found the fixed Simple path improved but not fully correct, and the
    residual issue was not isolated before audio work was deferred. The v0.17
    integration verifies framework tree
    `131abd8d1f4559bb36dba4391e4c644925cff5df`; these results do not replace
    cross-title testing.
    *Overlap:* checked 2026-11-10. Merged
    [#67](https://github.com/sp00nznet/xboxrecomp/pull/67) supplies the
    all-mixbin fallback, and merged
    [#217](https://github.com/sp00nznet/xboxrecomp/pull/217) enables HRTF and
    other voice fixes; neither updates the channel count after HRTF.
    The same source-channel selection remains in current xemu and xemu
    v0.8.139. No matching HRTF correction was found in current open PR
    metadata or affected code; tracking index
    [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) still selects
    bins with the pre-HRTF source width.
    *Follow-up:* coordinate a separate xemu correction so the shared defect is
    not fixed incompatibly. Additional HRTF titles and stereo-source coverage
    remain desirable follow-up, not a reason to bundle unrelated audio
    changes.
    *Status:* merged in v0.17.0; local patch 0035 was removed.

38. **Correct and configure the `XC_VIDEO` EEPROM setting layout.**
    *Problem:* the framework defines `XC_VIDEO` flags as low-bit values and
    returns them directly. Xbox title wrappers commonly read the video-user
    settings from the high word; a framework comment currently describes the
    value as widescreen and HDTV enabled even though such a wrapper observes
    neither.
    *Evidence:* Brute Force retail generation
    `20261030-142754-81f8c6e6` queries setting index 8 at guest
    `0x002F2AF6`, shifts the result right by 16 and masks it with `0x5F`.
    The selected framework returns `0x00000003`, so the wrapper reads zero.
    Its checked direct readers do not use the widescreen bit, so this defect
    does not itself explain or enable Brute Force widescreen. Independent
    [EEPROM documentation at
    `426736c`](https://github.com/feudalnate/Original-Xbox-Data-Structures/blob/426736c0fd957ba509eb98a7106a0ee16e338fad/EEPROM/README.md#video-flags)
    places widescreen, 720p, 1080i, 480p, letterbox and 60 Hz at raw bits
    `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`,
    `0x00100000` and `0x00400000`. The
    [DirectXbox analysis at
    `530e8d1`](https://github.com/bechapm/DirectXbox-Recomp-D3D9/blob/530e8d1f2f2d7108e3744323a073935fadfb392d/docs/technical/widescreen-and-resolution.md)
    independently records the same `>> 16 & 0x5F` wrapper in Burnout 2 and
    recommends a 4:3 compatibility default.
    *Change:* v0.17.0 corrects the EEPROM/user-preference flags to their raw
    high-word positions. Selected
    [#320](https://github.com/sp00nznet/xboxrecomp/pull/320) adds exact-value
    `RECOMP_VIDEO_WIDESCREEN=1` and `RECOMP_VIDEO_480P=1` opt-ins rather than
    silently forcing every title into widescreen or progressive scan. The
    default remains NTSC 4:3 480i/60. Brute Force camera hooks remain
    title-specific.
    *Tests:* the Release EEPROM fixture passed its default, widescreen, 480p,
    combined and zero-is-off cases. It queries `XC_VIDEO` through the real
    kernel path and preserves unrelated setting behavior.
    *Overlap:* checked 2026-11-10. Merged
    [#216](https://github.com/sp00nznet/xboxrecomp/pull/216) adds other EEPROM
    settings but does not correct or configure this `XC_VIDEO` layout. No
    open issue or PR implementing that correction was found.
    [#137](https://github.com/sp00nznet/xboxrecomp/pull/137) is a closed
    launcher option to hide a widescreen setting, not an EEPROM-layout fix.
    The two external documents above are evidence, not code copied into this
    project.
    *Follow-up:* validate both opt-ins through a retail wrapper before making
    either a launcher default. Keep host presentation size, guest camera
    behavior and EEPROM preferences as separate controls.
    *Status:*
    [#301](https://github.com/sp00nznet/xboxrecomp/issues/301) closed on
    2026-11-06 after the encoding fix entered v0.17.0. #320 remains open and
    is selected at `cf5a4db8272945d256d472ff46278470552113ae`.

39. **Make GPU output-picture sizing aspect-aware.**
    *Problem:* the shared D3D11 presenter can fit an independently sized
    source by its own dimensions, but `gpu_present_output_size()` always
    reports the largest 4:3 rectangle. Native render sizing therefore has no
    generic way to request the physical dimensions for a 16:9 or ultrawide
    picture.
    *Evidence:* patch 0028 added independent shared-source dimensions and the
    4:3 query for Brute Force internal scaling. The proposed widescreen path
    can submit a wide source, but using that query to size it would retain
    the 4:3 bound. Blood Wake computes one requested picture aspect and uses
    it consistently for target sizing and presentation. The local fixed
    diagnostic-output path also previously treated the whole client as the
    4:3 picture, producing an incorrect native scale for letterboxed output.
    A local fixture now confirms that 1280x960 and 1600x960 clients both
    produce the same 1280x960 query result, although their fitted 16:9 and
    64:27 pictures differ. A later shared-frame rejection can also choose the
    CPU picture after projection, so sizing alone is not a frame/epoch and
    fallback contract.
    *Change:* the project backend now fits fixed diagnostic dimensions to
    4:3, matching the existing live 4:3 query; no framework patch changed.
    For widescreen, add an aspect-aware output-size query or
    presenter contract, keeping 4:3 as the default. The backend supplies its
    requested picture aspect; window fitting, resize and dimension limits are
    framework-owned. Guest readback dimensions remain a separate backend
    contract.
    *Tests:* synthetic client rectangles at 4:3, 16:9, 64:27, portrait and
    zero/minimized sizes; resize transitions; invalid aspects; submitted
    source dimensions; exact regression coverage for the existing 4:3
    output and CPU fallback. Local synthetic tests now cover 3840x2160 and
    portrait fixed bounds, a hidden 600x800 live presenter resize, the 4:3
    size collision, and shared-frame fallback on WARP and the default
    hardware adapter; they do not implement or validate widescreen.
    *Overlap:* checked 2026-11-11.
    [#215](https://github.com/sp00nznet/xboxrecomp/pull/215) supplies the
    merged scalable D3D11 presenter, while tracking
    [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) now includes
    title-facing aspect heuristics such as
    [`7b38f1c`](https://github.com/sp00nznet/xboxrecomp/commit/7b38f1c4250fa50eb7997d5f4ce78673de7ce086)
    and
    [`d45e030`](https://github.com/sp00nznet/xboxrecomp/commit/d45e030d31137cff10c5bb9db4c7b4d720ace48b),
    plus D3D11/Metal backend seams. Those classify and stretch selected
    draws inside padded targets; they do not provide a general
    requested-picture aspect to the presenter. Closed
    [sp00nznet/xboxrecomp#137](https://github.com/sp00nznet/xboxrecomp/pull/137)
    is not a presentation implementation.
    *Before filing:* design this with #184's multi-backend ownership, define
    whether the API uses a ratio or validated dimensions, and retain GDI,
    PVIDEO and pinned-framebuffer fallbacks.
    *Status:* planned framework extension; not patched or filed.

40. **Deliver ordered application metadata through the pushbuffer executor.**
    *Problem:* a backend cannot reliably infer title-defined 2D, movie and
    special-copy boundaries from raster state alone. The executor has no
    generic opt-in path for inert, title-generated metadata that stays
    ordered with surrounding draws.
    *Evidence:* Blood Wake wraps its verified overlay flush and movie quad
    with tagged `NV097_NO_OPERATION` parameters. Its carried executor sends
    those values to a singleton callback, allowing the backend to pillarbox
    only protected 2D while transparent 3D in the same high-level layer stays
    wide. Brute Force currently has no equivalent path.
    *Change:* no patch yet. Generalize the idea into a namespaced,
    registered metadata/filter hook or an optional backend entry. The
    framework owns ordering and dispatch; each title owns its tag, emission
    sites and meaning. With no registration, ordinary behavior remains
    unchanged. The backend bounds a lost end marker by resetting state at
    flip.
    *Tests:* synthetic streams with metadata before, between and after draws;
    JUMP, CALL/RETURN and ring-wrap order; absent and rejected registrations;
    ordinary NOP values; multiple frames and reset behavior. The fixture
    must not contain game addresses or data.
    *Overlap:* checked 2026-10-31. #222 records NOP state but does not expose
    an ordered title-metadata callback, and #184's backend seam does not
    document one. Closed
    [#219](https://github.com/sp00nznet/xboxrecomp/pull/219) is related
    pushbuffer scan/wrap work, not the classification protocol. No public PR
    implementing Blood Wake's exact Hor+ chain was found.
    *Before filing:* choose a collision-resistant registration contract,
    confirm how the walker treats NOPs on every path, coordinate with #184,
    and keep the Blood Wake-specific `0xB1D3` tag out of the framework.
    *Status:* planned framework extension; not patched or filed.

41. **Report serial raster batches as one worker.**
    *Problem:* on Windows, `xf_rows_parallel` returns through its serial
    below-threshold path without updating `s_raster_last_workers`. A small
    batch after a parallel batch is rendered serially but reports the stale
    parallel worker count. The POSIX branch already records one worker.
    *Evidence:* on the v0.17.0 integration, the existing Release
    `nv2a_raster_batch` fixture rendered 2,944 covered pixels with zero wrong
    pixels, then failed because the below-threshold batch reported four
    workers instead of one.
    *Change:*
    [0037-report-serial-raster-worker.patch](../patches/xboxrecomp/0037-report-serial-raster-worker.patch)
    sets the diagnostic to one immediately before the Windows serial loop.
    This is a generic reporting bug fix and does not change raster output,
    scheduling thresholds, or synchronization.
    *Tests:* the existing fixture first exercises and reuses the four-worker
    pool, then draws two 40x40 quads below
    `NV_RASTER_MT_MIN_PIXELS`; it now passes its pixel and worker-count checks
    in Release. The integrated framework's complete Python tool suite and the
    other focused native fixtures also pass.
    *Overlap:* checked 2026-11-10 through #321. No exact PR or issue was
    found. Open #231 changes raster scheduling more broadly and is not a
    substitute for correct telemetry on the current serial path.
    *Before filing:* extract the one-line fix from the local series, rerun the
    existing fixture on Windows and POSIX, and request approval before opening
    a PR.
    *Status:* local patch; not filed.

42. **Keep the display awake only while a game window is active.**
    *Problem:* controller input does not reset Windows' keyboard/mouse display
    idle timer. A long controller-only session can therefore let the monitor
    power down even though the game is active; a process-wide unconditional
    hold would instead keep the display awake while the game is minimized or
    in the background.
    *Evidence:* source inspection of Blood Wake v0.3.3 at `6292340` shows its
    framebuffer-window thread holding
    `ES_CONTINUOUS | ES_DISPLAY_REQUIRED` only while the top-level play window
    is foreground and not minimized, then releasing to `ES_CONTINUOUS` on
    background, minimize, and close. This is prior-art evidence of a host
    policy, not a Brute Force runtime report.
    *Change:* [0039-foreground-display-awake.patch](../patches/xboxrecomp/0039-foreground-display-awake.patch)
    independently implements generic Windows behavior behind exact-value,
    default-off `RECOMP_DISPLAY_AWAKE=1`. It resolves child and foreground
    windows to top-level hosts, updates only on state changes, releases on
    close, and reports and retries failed API calls. Non-Windows behavior is
    unchanged. The idea is credited to Blood Wake; no code or title-specific
    `BW_*` policy was copied.
    *Tests:* a Windows fixture with substituted window and execution-state
    calls covers disabled, invalid, foreground, background, minimized,
    restored, repeated-tick, child/host, close and API-failure cases, including
    failed acquire/release retry and shutdown.
    *Overlap:* exact PR/issue searches for `SetThreadExecutionState`,
    `ES_DISPLAY_REQUIRED`, display-awake, and screensaver behavior found no
    xboxrecomp equivalent through 2026-11-10. The full PR-title and #184 index
    refresh on 2026-11-11 still found no equivalent. Blood Wake's
    implementation is the identified prior art; no upstream interaction was
    made for this work.
    *Before filing:* ask Blood Wake's authors whether they intend to upstream
    it, test the framework's hosted and standalone Windows topologies, decide
    whether SDL owns a separate cross-platform policy, and request approval
    before filing.
    *Status:* local default-off framework patch; synthetic-tested, not filed.

43. **Offer fixed-function and point batches to capable GPU back ends.**
    *Problem:* the extended interface can hand vertex-program batches to a
    backend before CPU transformation, but fixed-function batches still run
    their transform and lighting on the NV2A thread. Primitive types below
    triangles are rejected before a backend can handle them.
    *Evidence:* Brute Force's interface and D3D11 backend have no optional
    fixed-function or point-batch entries. A partial retail audit recorded
    174 line-list requests with 696 vertices and no point requests; it did
    not measure fixed-function cost and does not prove points are absent in
    other scenes. Blood Wake v0.3.1 extends its corresponding interface with
    matrices, lighting, material and fog state for fixed-function batches,
    plus point size, sprite coordinates, winding, optional acceptance, and
    verification readback for point sprites.
    *Change:* no local patch. If evidence justifies it, adapt only the generic
    optional interface and executor semantics behind separate default-off
    `RECOMP_*` controls. Keep the CPU transform and CPU point expansion as
    fallbacks and correctness references. Brute Force's backend would
    implement the D3D11 shaders within its existing surface, readback,
    scaling, and synchronization ownership. Line primitives are a separate
    gap; Blood Wake's point path does not solve the observed line requests.
    *Tests:* compare CPU and fake-backend fixed-function outputs across
    matrices, lighting classes, material colors, fog modes, raw/gathered
    attributes, rejection, and constant state. Point fixtures need fixed and
    shader-written sizes, clamping, behind-eye rejection, winding/culling,
    stage-3 sprite coordinates, anti-alias scaling, readback, and disabled or
    unsupported fallback. D3D11 WARP and hardware frames must match the CPU
    reference within documented precision.
    *Overlap:* #184 remains the coordination point for GPU-backend interface
    work. Current Blood Wake is direct prior art; the broad upstream snapshot
    through #321 did not establish that its newer contract has been proposed
    independently.
    *Before filing:* census fixed-function batches and cost on the fresh
    v0.17 retail generation, establish whether point or line pixels are
    visible, coordinate the versioned structure with items 12-14 and #184,
    ask Blood Wake's authors about upstream plans, and retain every CPU
    reference route.
    *Status:* planned investigation; no patch or upstream action.

44. **Recover a lost or changed Windows audio output off the frame worker.**
    *Problem:* the framework XAudio2 output has no general device-rebuild
    worker. Reopening a lost/default-changed endpoint synchronously on the
    real-time APU path can stall generation, while silently accepting a dead
    device produces success-shaped audio loss.
    *Evidence:* source inspection finds no Brute Force rebuild lifecycle.
    Blood Wake v0.2.5 moves stalled-device reconstruction to a normal-priority
    COM worker, and v0.3.0 adds MMCSS sound-thread policy and a shared-mode
    WASAPI output. Brute Force has not reproduced a host-device-loss failure;
    its unresolved narrow/crackly listening result is not evidence of one.
    *Change:* deferred. Preserve XAudio2 as a reference while defining an
    explicit output state machine, one off-thread rebuild job, cancellation
    and join behavior, and surfaced failure. Treat WASAPI as a separately
    selectable backend rather than coupling recovery to a latency change.
    *Tests:* substitute endpoint discovery and output APIs to cover default
    changes, removal, rebuild failure/success, repeated notifications,
    concurrent submission, shutdown during rebuild, COM lifetime, bounded
    queue behavior, and unchanged PCM ordering. Hardware tests should record
    endpoint format, latency, discontinuities, underruns, and recovery time;
    they cannot replace the human listening gate.
    *Overlap:* audio PRs #266 and #270 concern sequencer-active behavior, not
    host output recovery. Blood Wake is the identified implementation prior
    art; its patched APU and DSP components carry LGPL/GPL obligations that
    must be reviewed per file.
    *Before filing:* leave deferred until audio work resumes, recheck current
    upstream then, ask Blood Wake's authors about contribution plans, split
    generic recovery from title/DSP policy, and validate on multiple Windows
    versions and endpoint types.
    *Status:* deferred framework candidate; no patch or upstream action.

45. **Stop safely when the Windows presenter loses its device or resources.**
    *Problem:* the separate presenter device could encounter removal/reset or
    fail a resize, present, or keyed-mutex ownership transition without a
    deterministic boundary for outstanding GPU work and shared frames.
    *Evidence:* source review identified those independent producer and
    presenter boundaries. Fault-injected synthetic Windows tests exercise
    them; no retail device-loss incident or automatic recovery is claimed.
    *Change:* [0038-presenter-device-loss.patch](../patches/xboxrecomp/0038-presenter-device-loss.patch)
    is a generic, default-on correctness fix: it reports the operation,
    HRESULT and device-removed reason and fails closed on device loss or
    unrecoverable runtime presenter/ownership errors. Non-device initial
    setup and shared-open errors retain GDI/CPU fallback. Fault injection is
    compiled into the fixture only. Project-side producer checks and its
    bounded work queue remain in `src/`; neither patch skips synchronization.
    *Tests:* 19 presenter-capture fixture cases passed with WARP and the
    available hardware adapter, including injected initialization, shared
    open/acquire/release, resize, back-buffer, render-target and Present
    failures. Pending/completed markers verify fatal paths do not resume.
    Existing capture-off presentation paths also run. These simulations do
    not test driver removal races, other adapters or device recreation.
    *Overlap:* the refreshed read-only check on 2026-11-11 identified
    merged #215's Windows presenter, #220's PVIDEO work, #285's snapshot
    ordering and #306's backend contract under tracking index #184.
    No new upstream contact was made for this work.
    *Before filing:* recheck current upstream and prior art, split and review
    fail-stop policy from any future recovery design, and test actual loss,
    resizing, shutdown and pending guest-visible work on multiple drivers.
    Request approval before filing or commenting.
    *Status:* local framework patch; synthetic-tested, not prepared or filed.

46. **Expose allocation lifetimes and checked texture snapshots.**
    *Problem:* a backend caching a guest texture cannot distinguish a reused
    tracked allocation from its previous occupant, infer a guest-managed
    suballocation from parent pages, or safely rely on a page query followed
    by an unchecked copy. Colour-surface output writes also need a checked
    failure path. A CPU decoder that rereads guest memory can consume bytes
    different from those validated by the backend.
    *Evidence:* source inspection and synthetic guard, reprotection,
    allocator-reuse, reset, byte-rewrite and colour-alias cases demonstrate
    these boundaries. They are not a retail write trace or evidence that
    concurrent snapshots are atomic.
    *Change:* [0041-texture-snapshots-and-bounds.patch](../patches/xboxrecomp/0041-texture-snapshots-and-bounds.patch)
    adds a checked non-P8 immutable-buffer CPU decoder, tracked heap and
    contiguous allocation identities with remaining extents, mapping
    generations and an explicit logical-range registry. Follow-up
    [0043](../patches/xboxrecomp/0043-checked-snapshot-lifetimes.patch)
    rechecks parent identity and generation during registration, recognizes
    mirrored RAM and tiled contiguous parents, and rejects decoder
    input/output overlap. These generic APIs are inert until used; checked
    decoder behavior is a safety fix. The project D3D11 backend uses checked
    snapshots and compares exact bytes by default, while
    `RECOMP_GPU_TEXTURE_BOUNDS=1` separately opts
    into requiring registered logical extents. Owners must unregister a
    logical suballocation before reuse. Exact colour aliases reconcile guest
    changes or use a bound-target GPU copy; unsafe GPU-dirty overlaps reject.
    The Windows project backend preflights colour output and uses
    exception-guarded writes rather than the debugger-oriented
    `WriteProcessMemory`, which can override executable read-only protection.
    Failed or partial output invalidates the alias; failure or an observed
    post-copy race stops a tracked flip. Concurrent reprotection remains
    non-atomic and may consume a newly set guard or leave a partial write.
    *Tests:* Release framework memory-regression and decoder fixtures check
    tracked and mirrored/tiled parent reuse, unknown parents, logical range
    replacement, reset, input extents and overlapping decoder buffers.
    Project WARP and hardware fixtures cover checked source and palette
    spans, guards, reprotection, rewrites, freed and short-pitch aliases,
    raced colour-shadow boundaries, read-only/guarded and partial colour
    writes with retry, padded rows, and CPU/GPU decoding. They do not prove
    race-free guest-write ordering,
    register every title allocation, cover other platforms/drivers or measure
    title throughput.
    *Overlap:* the last recorded checks in this worklist cover #184 and
    #306's backend contract on 2026-11-11 and #261/#269/#286's related
    physical-page identity on 2026-11-10. No fresh search or upstream contact
    was made. These references do not establish that the current APIs are
    non-overlapping today.
    *Before filing:* recheck current upstream, agree on cross-backend
    ownership/write-notification and explicit suballocation lifetime, test
    reset and concurrent writes on other platforms and titles, and run a
    same-generation A/B/A cost and matched-image campaign. Request approval
    before coordination or filing.
    *Status:* local framework patches plus project-side use; synthetic-tested,
    not prepared or filed.

47. **Bound verification and stalled-work diagnostics.**
    *Problem:* GPU vertex verification can silently skip unavailable or
    failed readback, and a long event-driven flush wait gives little evidence
    about its last hold state. Shader preparation may also remain pending
    without identifying whether work was queued, compiled or failed.
    *Evidence:* synthetic missing/failed-readback and held-flush cases
    exercise the diagnostic paths. The existing intermittent title loading
    hang has no established root cause.
    *Change:* [0042-bounded-nv2a-diagnostics.patch](../patches/xboxrecomp/0042-bounded-nv2a-diagnostics.patch)
    reports at most 16 verification skips when verification is enabled, and
    offers default-off `RECOMP_NV2A_WAIT_DIAG=1` with at most eight
    prolonged PFB wait reports. The project backend independently offers
    default-off `RECOMP_GPU_SHADER_DIAG=1` with at most 32 vertex-preparation
    events. None completes a wait, acknowledges a flush, or forces shader
    synchronization.
    *Tests:* the framework fixture forces 23 verification skips and verifies
    the 16-report cap; the event-wait fixture checks off/on reporting, cap,
    flip hold and real acknowledgement. Project fixtures check off, invalid,
    queued, compiled, ready and injected event-creation failure diagnostics.
    No title hang, actual device removal or prolonged real shader compile was
    reproduced.
    *Overlap:* the read-only search through #321 and tracking index #184 was
    refreshed on 2026-11-11. Merged #247 supplies generic long-wait logging
    and open #319 supplies a backend-contract fixture, but neither duplicates
    the bounded verification, PFB-hold or shader-preparation diagnostics. No
    upstream contact was made for this work.
    *Before filing:* refresh the overlap search, separate verification,
    wait and project-backend diagnostics into independently reviewable
    changes, validate prolonged cases and log volume on real workloads, and
    request approval for upstream interaction.
    *Status:* local framework patch and project diagnostics; synthetic-tested,
    not filed.

### Open upstream work to select or coordinate with

These existing PRs may help Brute Force directly or own an interface touched
by a local candidate. The snapshot and statuses were checked on 2026-11-10.
Measure performance changes with the
[benchmark protocol](./PERFPLAN.md#phase-0-measurement-and-ceiling-probes)
before adding them to the selected list. Coordination-only rows should not be
selected as if they were interchangeable implementations.

| PR | Purpose | Relevance | Status |
| --- | --- | --- | --- |
| [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) | Index the portability, kernel and GPU-backend series | Coordination point for 0008 and 0014-0029; explicitly not intended to merge | Open draft index |
| [#222](https://github.com/sp00nznet/xboxrecomp/pull/222) | Re-sent NV2A state, window clip, clear rect and face culling | Fold demonstrated 0009/0010 corrections and fixtures here instead of competing | Open |
| [#231](https://github.com/sp00nznet/xboxrecomp/pull/231) | Pipeline the CPU rasterizer behind the executor | CPU reference path; overlaps 0008 and 0013 | Open, stacked on merged #228; not measured |
| [#266](https://github.com/sp00nznet/xboxrecomp/pull/266) | Treat a short sequencer-off interval as active | Audio alternative adjacent to local 0030-0034 | Open; do not select without title A/B/A |
| [#286](https://github.com/sp00nznet/xboxrecomp/pull/286) | Name APU heap pages by RAM mirror alias | Must be reconciled with 0007 and 0030 | Open |
| [#308](https://github.com/sp00nznet/xboxrecomp/pull/308) | Kernel crypto, DPC exclusion and shared-read behavior | Adjacent to the software-interrupt ABI candidate but does not fix its calling convention | Open; review independently |

v0.17.0 now includes #249, #251, #253, #258, #261, #272, #273, #274 and
#285. They are base behavior rather than selections. The local rebase keeps
#249's strip winding, combines #273's transform-constant fast path with the
staged walker's CALL-state tracking and GPU audit, and registers the #285
present-sync callback without dropping local backend restrictions.

- **#231:** The upstream [review](https://github.com/sp00nznet/xboxrecomp/pull/231#issuecomment-6270034277)
  flags counter wrap, incomplete worker-state snapshots, and publishing GET
  before workers drain. Cover queue saturation, render-to-texture dependencies,
  and GET, semaphore, flip and flush ordering before evaluating it. Patch 0012
  is complementary rather than overlapping.

The separate GPU series in [#184](https://github.com/sp00nznet/xboxrecomp/pull/184)
is related prior art, not an interchangeable implementation: equivalence of
its constant handling for non-increasing packets is unverified. No gameplay
benchmark has been run for these candidates; compare at least three matched,
alternating A/B/A runs per variant and report distinct world frames and visual
fidelity, not flips alone.

## JSRF-PC prior-art review

On 2026-10-31, the review covered
[spriteice/JSRF-PC](https://github.com/spriteice/JSRF-PC) at
[`36e08c18dd56d03458ab43d79631c43740c867c3`](https://github.com/spriteice/JSRF-PC/commit/36e08c18dd56d03458ab43d79631c43740c867c3):
146 fork commits after its `4d31983557fb15631d31824b08fe80da6f0650b4`
merge base with JSRF Unleashed. The fork says its additions are AI-written;
that is not a correctness finding, but it makes independent fixtures and
Brute Force measurements mandatory rather than treating its changelog as
validation.

The fork's project-owned code is MIT-licensed, while its xemu-derived APU
files remain LGPL-2.1-or-later. No code was copied in this review, so
`THIRD_PARTY_NOTICES.md` did not change. Any later adaptation must credit the
fork and exact commit, retain the applicable notice, and keep title-specific
JSRF hooks and addresses out of generic patches.

The 2026-11-10 refresh confirms xboxrecomp `main` is v0.17.0 at
`343b6d8901723b37ce568b2418c5df4e1fe1d223`. Targeted title/body searches
through pull requests up to #321 and issues up to #306 found no exact upstream
submission for the flag-entry guard, software-interrupt ABI, index-run
batching, or same-bit vblank race. Merged #42 is related prior art for the
flag-entry guard: it rejects weak immediate references inside an already
decoded instruction, not a branch at a valid instruction boundary that
consumes incoming flags. Open #308 changes DPC exclusion but not the
software-interrupt declarations, bridge argument source, or stack-pop table.
Merged #217 is related to voice locking, but it only adds the per-voice lock;
it does not defer a frame until the title releases `VOICE_LOCK`.

### J1. Reject immediate-derived function starts that consume incoming flags

- **Problem:** `_pass_imm_ref_targets` can split a real function between a
  flag-producing instruction and its conditional branch when an unrelated
  immediate happens to equal the branch address. The existing
  `require_entry_frame` callback-table path already rejects that shape, but
  the immediate-reference pass does not.
- **Evidence:** JSRF-PC commit
  [`36e08c1`](https://github.com/spriteice/JSRF-PC/commit/36e08c18dd56d03458ab43d79631c43740c867c3)
  reports a title IOCTL value that named the `jne` after a `cmp`, truncating a
  result-banner update. Its focused eight-case test passed locally. A
  source-and-test-only `git apply --check` succeeded against Brute Force's
  selected-plus-local toolkit tree with only line offsets. This is portability
  evidence, not proof that the supported Brute Force executable changes.
- **Change:** extract the existing arithmetic-flag entry check into
  `DisasmEngine.reads_flags_on_entry` and call it before accepting an
  immediate-reference target. This is generic recompiler logic.
- **Tests:** carry the positive and negative immediate-boundary cases, then
  run all `tools/disasm` tests. Regenerate the supported Brute Force revision
  both ways and review every changed function boundary and translation, rather
  than accepting a lower function count as success.
- **Overlap:** refreshed 2026-11-10. Merged #42 rejects weak immediate
  references that land inside decoded instructions, while this case lands on a
  valid instruction boundary and consumes incoming flags. No exact follow-up
  PR or issue was found. It also complements the missed-callback investigation:
  stronger discovery needs stronger false-positive rejection.
- **Before filing:** test more titles and compiler shapes, especially entries
  that legitimately begin with string operations or partially define flags;
  report the full corpus delta without game bytes.
- **Status:** planned; strongest JSRF-PC recompiler adaptation candidate.

### J2. Correct the software-interrupt exports' fastcall ABI

- **Problem:** the selected kernel declares `HalRequestSoftwareInterrupt` and
  `HalClearSoftwareInterrupt` as `__stdcall`; their bridges read a stack
  argument and report four callee-popped bytes. The Xbox ABI passes the IRQL
  in `cl`/`ecx` and does not pop a stack argument.
- **Evidence:** JSRF-PC commit
  [`a3938ee`](https://github.com/spriteice/JSRF-PC/commit/a3938eeee4d18a1bdb9d69d5555b9284a24479d5)
  changes the declarations, bridge reads, and stack-pop table. The patch
  applies cleanly to the selected-plus-local toolkit tree. Independent
  [nxdk](https://github.com/XboxDev/nxdk/blob/f8c52e4be65bab7d6e10455db8d86d69d232bbe3/lib/xboxkrnl/xboxkrnl.h)
  and
  [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/EmuKrnlHal.cpp)
  declarations also identify `HalRequestSoftwareInterrupt` as fastcall.
- **Change:** use `__fastcall`, marshal the bridge argument from `g_ecx`, and
  return zero stack-pop bytes for ordinals 38 and 48. This is a plain generic
  ABI fix; it does not need an environment switch.
- **Tests:** add a kernel-bridge fixture that places different sentinels in
  `ecx` and the first stack slot, verifies the IRQL received by each export,
  and verifies zero stack cleanup. Compile-time declarations alone are not
  sufficient.
- **Overlap:** refreshed 2026-11-10. No matching PR or issue was found. Open
  #308 changes DPC exclusion in `kernel_hal.c` but not these declarations,
  bridge reads, or stack-pop entries. The selected tree already bridges
  `NtProtectVirtualMemory` and `KeSetDisableBoostThread`, so those earlier
  JSRF-PC kernel additions need no second adaptation.
- **Before filing:** add the fixture, check both canonical and remapped kernel
  ordinal tables, and run the existing bridge and thunk suites.
- **Status:** planned; strong standalone kernel bug-fix candidate.

### J3. Do not mix a voice while the title holds `VOICE_LOCK`

- **Problem:** merged #217 prevents `VOICE_LOCK` from waiting for an entire
  frame by giving each voice a small spin lock. The current frame walk still
  processes an active voice while its `voice_locked` flag is set, so it can
  observe a partially applied title update and write back position or envelope
  state over that update.
- **Evidence:** JSRF-PC commit
  [`ba86819`](https://github.com/spriteice/JSRF-PC/commit/ba8681947bae653d763d942a0cd61fcdb26523f8)
  gathers active voices, waits on the existing APU condition until all are
  unlocked, and then mixes them. Its fixture changes a locked voice's right
  route while a frame is pending and requires the new route to be used. The
  source patch does not apply directly because the selected APU and local
  tracing have diverged; the behavioral gap remains visible in the current
  frame loop. No Brute Force title trace has established a hit.
- **Change:** manually port the gather/wait/mix separation to the selected APU,
  preserving the existing per-voice spin lock, trapped-idle handling, pause
  behavior, accurate-DSP hooks, and frame statistics.
- **Tests:** extend the existing `apu_vp` fixture with the locked-route case,
  multiple locked voices, pause during the wait, and a missed-wakeup stress
  case. Add default-off wait counters before doing matched title captures.
- **Overlap:** #217 supplies the complementary per-voice lock and remains the
  foundation. Open #264 and #266/#270 concern sequencer timing, not this
  title-owned voice-update interval.
- **Before filing:** prove the wait cannot deadlock with `d->lock`, shutdown,
  or pause; run fold and external-DSP lifecycle tests; then compare title audio
  and game-thread waits without lowering the host queue.
- **Status:** planned generic correctness adaptation; manual port required.

### J4. Pace voice-processor frames individually

- **Problem:** the selected pacer admits work in 256-sample encode periods, so
  the 32-sample voice-processor frames inside a period can run as a burst.
  A title that waits for the next voice frame to retire an idle voice can
  therefore wait several milliseconds.
- **Evidence:** JSRF-PC commit
  [`4f6012c`](https://github.com/spriteice/JSRF-PC/commit/4f6012c76d2e077d6a82d244b5a87a22898db949)
  reports an isolated mean wait reduction from 5.3 ms to 0.6 ms with ring
  pacing and fewer late JSRF frames. Its source patch conflicts with the
  selected queue pacer and SSL hold, although its new fixture files apply.
  Brute Force has no measurement attributing game-thread cost to this wait.
- **Change:** if profiling justifies it, teach `mcpx_apu_pace_step` about each
  32-sample VP frame while retaining the current 256-sample output period,
  queue high/low water marks, SSL hold, and external-DSP worker.
- **Tests:** adapt the synthetic idle-retirement latency test; also cover
  pause/resume, trapped and held frames, exact sample counts, queue bounds,
  underrun counters, and both fold and external-DSP modes.
- **Overlap:** selected #264 and open #266/#270 are adjacent APU scheduling
  work but do not pace individual VP frames.
- **Before filing:** profile the supported title first, then run A/B/A with
  unchanged host buffering and record both game-thread waits and audio queue
  health. JSRF's title measurements are not transferable performance results.
- **Status:** planned investigation, below J3 until Brute Force evidence shows
  the wait is material.

### J5. Batch non-incrementing index-word runs

- **Problem:** both selected pushbuffer walkers still dispatch each
  `ARRAY_ELEMENT16` or `ARRAY_ELEMENT32` parameter word separately, even when
  one non-incrementing packet contains thousands of index words.
- **Evidence:** JSRF-PC commit
  [`e81e29c`](https://github.com/spriteice/JSRF-PC/commit/e81e29ce594e34d5a89b57b69a605f8f6ce0a4ad)
  reports about seven million index words per second in one JSRF scene and an
  isolated renderer reduction from roughly 14 ns to under 2 ns per word. Its
  executor hunk applies but its walker and OpenGL renderer integration do not;
  Brute Force uses different walker, audit, CPU-rasterizer, and D3D11 paths.
- **Change:** add a generic run-level executor entry only after the method has
  been classified as a subchannel-0, non-incrementing index packet. Preserve a
  word-at-a-time reference path and make the run update audit, limits, overflow
  and backend state exactly as repeated calls do.
- **Tests:** compare run and scalar execution byte-for-byte across 16/32-bit
  indices, packet splits, CALLs, wraps, other subchannels, full index buffers,
  audit enabled/disabled, CPU reference output and D3D11 batch output.
- **Overlap:** #273 accelerates constant uploads and #231 pipelines the CPU
  rasterizer; neither is the same dispatch reduction. No exact upstream PR or
  issue was found in the targeted 2026-10-31 search.
- **Before filing:** use the Brute Force audit/census to establish index-word
  volume, then run alternating A/B/A. Report distinct world frames and
  per-thread time, not JSRF's title numbers.
- **Status:** planned Phase 0/5 performance candidate; concept to adapt, not a
  patch to cherry-pick.

### J6. Stress the local vblank acknowledgement against a concurrent raise

- **Problem:** a status-bit clear based on an earlier observation can erase a
  newly raised interrupt that uses the same bit.
- **Evidence:** JSRF-PC commit
  [`07e20c7`](https://github.com/spriteice/JSRF-PC/commit/07e20c704e90794eb9610bad6d83c86b04292832)
  replaces whole-register completion with compare/exchange and a second PCRTC
  check; its stress fixture reports no losses where the old code lost 0.4-0.9%
  of 200,000 vblanks. Brute Force's selected tree has a different, more
  elaborate ack owner and `tick_busy` protocol, so the patch does not apply.
  That divergence is not proof that every same-bit interleaving is covered.
- **Change:** adapt the race fixture, not the implementation. Force a raise
  between the local watcher's observation and clear, and change the handshake
  only if the current test can lose the new vblank.
- **Tests:** count every raised, claimed, acknowledged and coalesced vblank;
  exercise masked delivery, a late DPC, the nv2a-ack backstop, PGRAPH bits and
  shutdown. Keep the existing flush, semaphore and flip-hold protocols intact.
- **Overlap:** no exact xboxrecomp PR or issue was found in the targeted
  search. This is adjacent to the local vblank-ack and safe-point patches, not
  a clean replacement for them.
- **Before filing:** produce a deterministic local reproducer and isolate the
  smallest generic fix. Do not infer a defect from JSRF's different state
  machine alone.
- **Status:** planned synchronization audit.

### Disposition of the other reviewed improvements

- JSRF-PC's direct DXT upload
  ([`39f73fa`](https://github.com/spriteice/JSRF-PC/commit/39f73fa2e04a4353255d42972124c4e5f9c87cf1))
  and combiner constants as uniforms
  ([`adf4574`](https://github.com/spriteice/JSRF-PC/commit/adf457458a829c98ec5ae1be034dc0e9c46079b1))
  are already present in Brute Force's D3D11 architecture: eligible DXT data
  goes directly to BC1-3 textures, and `GpuRcKey` excludes C0/C1/final
  constants while a persistent constant buffer carries them. No adaptation is
  needed.
- The wider OpenGL vertex-cache hash, GL error polling, reversed depth,
  Linux display pacing and P-core pinning do not map to the current Windows
  D3D11 target or need independent Brute Force evidence before they become
  work items.
- The loading-frame fix
  ([`19630f4`](https://github.com/spriteice/JSRF-PC/commit/19630f4e807bc9dddfa39e554c22f167dfd313fc))
  is specific to JSRF's OpenGL display-buffer bookkeeping. Brute Force's
  prolonged black-frame observation still had scene batches and program
  vertices, so it is not evidence of the same condition.
- JSRF-PC's queued-GPU experiment
  ([`f62e007`](https://github.com/spriteice/JSRF-PC/commit/f62e0072cd6fc2e7572b93cd7bed376241f48576))
  completes the PFB flush at the register write so the title can run ahead.
  That shortcut is **dropped for Brute Force**: it conflicts with the measured
  flush handshake and the explicit rule not to skip or prematurely satisfy
  flushes for speed. Only an event-driven wait on the real acknowledgement,
  plus separately proved GET/fence batching, remains eligible.

## Further investigation and standalone follow-up

- **Recover missed indirect callback entries safely.** The observed XPP
  callback at `0x0037EE7C` required a
  [title seed](../fingerprints/retail-seeds.json); the indirect call and
  function prologue were observed locally. Investigate whether a general
  function-discovery improvement can recover analogous code without
  misclassifying mixed code/data. A synthetic reproducer and negative cases
  are needed before proposing an upstream change.
- **Correct the unimplemented-instruction report.** Merged
  [#299](https://github.com/sp00nznet/xboxrecomp/pull/299) now describes the
  generated `RECOMP_UNIMPL(text, va)` runtime calls accurately.
  All seven focused unittest cases passed. The change affects reporting and
  tests only, not generated runtime behavior.

Merged #285 remains separate from the Windows snapshot race fixed by merged
#300: it orders backend readback before consumers, while #300 protects CPU
presentation buffers from reuse during the window thread's copy. Items 12
and 15 must extend the established #285 callback contract if they need a
richer seam; they should not open a competing readback hook.

For reproducing the project's configured native fixtures and tests, see the
[development test instructions](./DEVELOPMENT.md#tests). Keep any disc-derived
evidence, logs, and captures local.
