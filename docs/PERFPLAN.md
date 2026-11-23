# In-game performance plan

The CPU reference path previously presented roughly 3.5–4 tutorial flips
per second on the reference machine. Experimental GPU runs are much faster
but do not yet establish distinct world FPS or rendering correctness.
Getting the title to its native frame rate is the gate for
most other work: input, audio, HUD, and rendering investigations all slow
down when each experiment runs at a tenth of real time. This plan replaces
the CPU rendering path rather than tuning it further. Brute Force-specific
evidence is in [FINDINGS.md](./FINDINGS.md); framework contribution candidates
that come out of this work belong in [UPSTREAM.md](./UPSTREAM.md). The current
pipeline and system boundaries are summarized in [RENDERING.md](./RENDERING.md).

The worked example is the
[Blood Wake recompilation](https://github.com/minimalice92/bloodwake-recomp)
at commit `e631edddfe56d22344e9f696923aeb6d78a9e7ba`. That recorded checkout
pins xboxrecomp `f0f8a461` (v0.16.0), while this project now pins v0.18.0.
Blood Wake reports 205–290 fps in gameplay at
3440×1440 with a D3D11 renderer and remains implementation prior art, not a
shared current framework baseline. Its own files are MIT licensed, so they
can be adapted here with attribution in `THIRD_PARTY_NOTICES.md`. Its
title-specific hooks, addresses, and Remastered features do not apply and
must not be copied.

## Status

Measurements are in [FINDINGS.md](./FINDINGS.md#performance-plan-phase-0-and-the-first-gpu-path-results);
next steps are in [NEXTSTEPS.md](./NEXTSTEPS.md).

The v0.18 framework rebase is the supported base after clean replay, focused
fixtures, exact retail regeneration review, and one bounded audio-on CPU and
D3D11 Basic Controls smoke each. Those single stationary runs used legacy
readiness and are not an A/B/A comparison or distinct-world measurement.
No prior v0.17 frame-rate or audio-scheduling result becomes a v0.18 result;
all performance phases and their existing reference, ordering and A/B/A gates
remain unchanged.

The 2026-11-20 renderer direction now targets Vulkan as the future fresh
graphical default and D3D12 as its startup-only fallback. The explicit Vulkan
1.3 base, precise visibility, CPU-transformed register-combiner path and
gathered-input GPU-VSH interpreter are implemented, and default-off supplied
mip chains have cross-vendor synthetic coverage. Earlier correctness builds
reported 0.9-1.261 flips/s, the wrong-scene mip probe about 7 flips/s, and a
later uninstrumented exact-index route 8.960-11.240 gameplay flips/s. Profiling
then attributed about 58.7% of NV2A-thread samples to CPU vertex-program
interpretation. The gathered Vulkan interpreter removes those functions from
the sampled hot list; one unreviewed route reported 26.391 stationary and
38.862 movement flips/s, with a caller-profiled repeat at 26.683 and 38.503.
These are diagnostics, not an alternating A/B/A result or distinct-world FPS.
D3D11 remains the measured GPU baseline until matched correctness and
presentation gates pass, and no graphics API is presumed to improve the
guest-side PFB flush wait.

The ownership build subsequently restored the visible Vulkan manual-menu
route and completed one same-executable movement capture per renderer. Strict
review classifies 917 Vulkan and 1,273 D3D11 world changes with no unknown
frames and confirms the same tutorial scene and fixed camera route. Their
23.021 and 32.026 changes/s, and 12.135 and 15.307 1% lows, are synchronous
guest-surface capture results. They are excluded from performance comparison:
continuous BMP writes changed timing, the capture counts differ, there is only
one run per renderer, and neither source proves compositor presentation.
The next throughput diagnostic disabled continuous capture but deliberately
used only one D3D11 and one Vulkan run to minimize operator intervention.
Both used the same generation, executable, scene, script, guest-CPU setting,
thread count and options. This can guide the next attribution step but cannot
satisfy M5 before native presentation and the remaining parity gates.

The first such sequence is invalid and stopped: D3D11 run 1 and Vulkan run 1
completed, but D3D11 run 2 reproduced the pre-movie loader starvation at two
flips and zero draws. Its first pair must not be combined with later runs.
Local framework patch 0047 adds the default-off
`RECOMP_ZERO_DELAY_SLEEP=1` backoff selected by this title. Its focused
producer/worker fixture passes, and rebuilt executable
`f717c74980d81e506272f71f7c3135ebd68017d90753cb783d0e64060d9ffc7a`
reached movie draws in three no-retry D3D11 checks; one continued through
operator-confirmed Basic Controls. The replacement same-build pair completed
without retries: D3D11 reported 26.409 stationary and 48.193 movement
flips/s; Vulkan reported 23.164 and 17.469. All 15 runtime input dispatches
matched within 78 ms. There was no continuous capture or profiling, and all
movement frames remain unreviewed, so these are mechanism diagnostics rather
than world FPS or A/B/A evidence. Do not spend six more manual runs on this
pre-presentation comparison. A single Vulkan-only
`BRUTEFORCE_PERF_COUNTERS=1` run is complete; reserve multi-run A/B/A
collection for M5 when native presentation makes it actionable. Its three
line-order-aligned intervals wholly inside movement cover 900 flips at 32.845
diagnostic flips/s. Complete draw callbacks consumed 30.57% of interval wall
time, exact texture snapshots 17.59% inside those callbacks, mandatory
readback 13.54%, asynchronous report polling 11.97%, and backend
gathered-vertex copies 0.56%. Report-completion, fence-poll and submission-wait
times are nested and must not be added as independent frame costs. The run's
overall movement rate rebounded to 32.558 flips/s, versus 17.469 in the
reduced pair, so single-run rates are explicitly unstable. Counters also add
per-call timing overhead. Use this result only to order implementation:
remove unactionable visibility polls without weakening synchronization,
then investigate exact texture-snapshot invalidation/copy safety before
starting native presentation.
The first change is implemented: a pre-deadline report that has not been
assigned to a submission is deferred until it can act, while assigned reports
and all blocking paths remain unchanged. Its forced pre-deadline/aged-report
fixture passes with validation on both available GPU vendors. This is
synthetic correctness evidence, not a retail performance result. Do not
prioritize direct raw-stream fetch from backend copy time alone;
framework-side gathering remains in the unattributed interval remainder.
The snapshot review found no exact elision mechanism compatible with guarded,
aliased file mappings and allocation-lifetime checks, so that optimization is
deferred rather than approximated. Default-inactive framework patch 0048 now
supplies the host presentation-provider lifecycle/CPU-fallback seam and its
synthetic fixtures. It is M2 infrastructure only; no Vulkan swap chain or
performance result exists.

Before the runtime-only Phase 4 fix, integrated v0.17 Release generation
`20261111-084222-7bdb6e56`'s codegen-identical executable completed one CPU
and one D3D11 scripted movement bring-up without retries. The post-deadlock
executable later completed the fixed-tree Phase 4 smokes described below.
That generation's later texture-index executable completed a six-attempt
linear/indexed campaign with timing-matched input and complete guest-surface
review. It reduced exact descriptor probes from 82.495 to 1.230 per lookup
and measured lookup cost from 0.0486 to 0.0336 ms/flip, but one of three
cadence pairs regressed and historical PFB intervals lack complete caller
attribution. The strict result is inconclusive, not a promotion result.
Generation `20261113-160606-ad4c0e30` fixes the three observed title caller
layouts. Its partial retail repeat reviewed three completed attempts and
confirmed all 64 contained PFB/PUT intervals have callers, but run 04 locked
before readiness and the only A/B pair fails runtime-input matching by
1,656 ms at the final command. The operator closed the repeat as
inconclusive. The separate default-off index-word-run candidate remains
synthetic-only. Representative
captures from the preceding executables show gameplay
without the controls panel or a cinematic, but the 116 CPU and 1,929 D3D11
movement flips do not have continuous reviewed world labels. The reported
2.919 and 48.231 whole-picture changes/s are therefore not distinct-world
rates or a performance comparison. All reviewed phase measurements below
remain tied to their explicitly named generation.

New title runs default to `RECOMP_GUEST_CPUS=one`. This pins guest title
threads, not GPU/APU host workers, to one allowed core and avoids the
pre-movie loader's pathological zero-delay polling observed in the partial
repeat. Older unpinned measurements remain valid only for their named
generations and controls; do not combine them numerically with new pinned
runs. Every future campaign must retain and report this control or name an
explicit override.
Current supported generation `20261120-163420-f9e3b4ed` contains that default
on the v0.18 framework tree and restores the fail-closed scaled
depth/stencil-alias boundary. Its bounded D3D11 and CPU attempts reported
25.409 and 3.681 stationary flips/s, respectively, but one run per renderer
without alternation is not a performance comparison and promotes no phase or
gate. The preceding v0.17 generation's 4.5x correctness check found a missing
player model with packed aliases enabled and remains historical evidence for
the boundary, not a performance run.

A native 640x480 xemu Basic Controls frame is now matched by content to the
preceding `20261111-002138-a9dbc18a` reference generation. A focused
one-run-per-variant correctness diagnostic held
all other controls fixed and reduced D3D11 pixels over 8/255 against xemu
from 43.4320% to 3.7119% when the packed-stencil alias was enabled. This
identifies one rendering mechanism but is not an A/B/A throughput result and
does not close G4 for movement or other scenes.

Synthetic-only follow-up advances Phase 0 diagnostics and Phase 5 correctness:
patch 0044 separates CPU post-clip counts from GPU pre-clip submissions and
adds bounded dropped-route context; movement collection now fails closed as
inconclusive without reviewed content. The opt-in unscaled packed-depth path
detects changed clean/dirty guest snapshots and rejects conflicting ownership,
and window-clip fixtures cover additional depth/stencil and scaled cases.
These are not new gameplay, throughput, A/B/A or original-hardware evidence.
G4 and the existing Phase 5 exit remain open.

Retained complete A1 evidence now establishes material index-word dispatch
volume: 73,787,237 `ARRAY_ELEMENT16` words plus 115,904
`ARRAY_ELEMENT32` words expand to 147,690,378 indices, exactly matching the
requested and buffered totals. Patch 0046 adds default-off
`RECOMP_PB_INDEX_RUNS=1` across both selected walkers. Exact executor state,
CALL, physical-wrap, packet/segment split and audit-scalar cases pass
synthetically. This is not a rendered-output comparison or Brute Force timing
result, so promotion remains open and separate from the texture-index
campaign.

Patch 0045 now supplies bounded event acknowledgement, an independent idle
wait, and guarded segment-at-a-time PUT consumption behind separate
default-off switches. Initial retail isolation exposed a PGRAPH/PFB deadlock:
queued work held PFB busy while the executor waited synchronously for a guest
trap acknowledgement that could not run until the PFB poll returned. The
fixed tree releases only that PFB dependency while preserving GET, the queued
segment and PGRAPH interrupt state. One no-retry segment-only smoke and one
no-retry all-three smoke then reached reviewed movement. They do not establish
a speedup, distinct-world FPS or any of G2–G5. The benchmark checks reviewed,
matched performance pairs and reports missing gate evidence as inconclusive.
The 24-run Phase 4 matrix is explicitly deferred; it is not an active phase
exit or prerequisite for other work. The switches remain default-off and the
unmeasured gates remain open.
Phase 4 generation `20261111-084222-7bdb6e56` contains the required store
hooks and its runtime-only rebuild pins the fixed historical tree.
Opt-in interval timing also now covers pixel-shader compile/create failures
and elapsed time when the persistent shader cache is off; existing
cache-enabled hit/compile diagnostics remain separate.

| Phase | Status |
| --- | --- |
| 0 | Mostly done. Harness (`tools/bench.py`), frame statistics (patch 0014), census and ceiling probes (0015), profiler (`src/profile.c`), null back end and surface guard (`src/gpu/null_backend.c`) exist and were run. Reviewed movement manifests now report baseline-to-change and change-to-change cadence percentiles and 1% lows. The profile summary separates known guest GPU polls and caller-attributed kernel waits from unrelated kernel waits; `--profile-callers` enables the latter at additional diagnostic cost. `BRUTEFORCE_PERF_COUNTERS=1` adds atomic interval counts and opt-in timing for title pointer validation, `VirtualQuery`, exact texture-cache lookup/comparison, snapshot copies, rewrites and eviction; concurrent validation and exact P8 rewrite fixtures cover the counters. A three-run reviewed camera-pan campaign records G2, G3, readback, visibility, validation and texture-cache work on the final Release generation. A six-leg reviewed sync/async visibility campaign then isolated the existing visibility option: async improved reviewed throughput by 11.7%, but its 15.697/s mean 1% low and 65.467% G3 still fail. Guest-surface captures do not establish displayed-present cadence. Request audit 0025 now has a complete fresh-generation scripted report as well as the older partial diagnostic; neither has operator-reviewed scene identity or a matched xemu frame. The harness gives every enabled attempt and retry an isolated audit report, records its controls and rejects missing/incomplete output. Flip-ID capture mapping 0026 is synthetic-tested. Presenter capture 0027 verified one link per GDI and CPU-upload route in short, unmatched retail probes, but did not validate physical visibility. Matched CPU-reference output and native-rate confirmation remain open. The current-generation surface guard completed in one attempt and resolved title-code access inside an accumulated colour range, but staged-pushbuffer overlap, one excluded page and missing live lifetime tracking prevent classification as a rendered-surface read. The current answer is to retain readback; do not repeat the same diagnostic without improving its model. Missing: measurements for open #231 and current-base behavior from merged #272/#273. |
| 1 | Implemented, G4 not met. D3D11 back end (`src/gpu/`, `RECOMP_GPU=d3d11`) with the extended interface (0016). Synthetic WARP and hardware fixtures match apart from listed edge pixels. The new GPU-vertex tutorial capture differs from the CPU reference in 0.52% of pixels beyond 8/255; an older CPU-vertex capture differs substantially. |
| 2 | Implemented for D3D11 and for Vulkan gathered inputs. D3D11 stationary hold improved 3.20 → 30.41 rendered fps in its historical alternating campaign. Vulkan now runs one static SPIR-V NV2A interpreter, rejects non-finite primitives, and supports compute verification readback; NVIDIA and AMD fixtures pass. A sparse unreviewed retail verifier reported no mismatch through 1,745,525 accepted batches. Vulkan direct raw-stream fetch remains open, `fetch_attr` still accounts for 2.09% of sampled NV2A time, and matched-scene G4 coverage is incomplete. |
| 3 | Partial. Async visibility reports (0018) and shared GPU presentation (0024) are opt-in. Three reviewed alternating runs per visibility mode measured 27.401 versus 30.606 world changes/s, but async still failed the 1%-low and G3 gates and remains opt-in. Submitted renderer work now has a two-event bound, and default-off elision skips copies only for demonstrably coherent, unscaled 32-bit guest surfaces. Tracked-surface flips still wait; dirty surfaces, diagnostics and scaled/16-bit frames still read back. General on-demand guest-reader interception is not implemented. CPU-written Bink frames synchronize before scaled submission. No new retail performance comparison was run. |
| 4 | Steps 1–3 implemented behind independent default-off switches with synthetic coverage. One fixed-tree segment-only smoke and one all-three smoke reached reviewed movement after correcting the PGRAPH/PFB dependency cycle. The 24-run alternating isolation/combined campaign is deferred; no performance gate is claimed. The last measured G3 remains 65.467% with async visibility. PFB, GET, semaphore, report, interrupt and flip ordering remain mandatory. Step 4 is deferred pending G3. |
| 5 | In progress. GPU compute decodes common textures; exact D24 and packed D24S8/stencil surface aliases and GPU window clipping are experimental opt-ins. A native xemu Basic Controls match now establishes that packed-stencil reconstruction, rather than a global gamma change, removes the large brightness error in that state: 43.4320% of pixels exceed 8/255 without it and 3.7119% with it. Unsupported ownership cases and other scenes keep the option experimental and G4 unmet. D3D11 honors partial clear rectangles, and patch 0022 removes redundant software depth clears when a backend owns drawing and clearing. Patch 0019 cached hot `getenv` calls. Checked texture/colour snapshots and tracked lifetimes replace repeated page queries on those paths. The first reviewed texture-index campaign proves the index removes descriptor-scan work but has historical caller gaps and one regressing cadence pair. The caller-corrected partial repeat closes the observed attribution gap, but its only A/B pair fails input timing and run 04 locks before readiness. The operator closed the campaign as inconclusive; G2/G4/G5 remain open. |

### Audio performance status

Simple stereo fold-down is now the default independently of the experimental
graphics path. The vendored xemu v0.8.139 GP/EP interpreter remains an
experimental option on a dedicated high-priority worker. The old v0.17
framework APU frame worker was also raised to `THREAD_PRIORITY_HIGHEST` on
Windows; that local policy is retired on v0.18 pending an A/B/A comparison
with merged per-frame pacing and any MMCSS alternative.
Observed DSP time was roughly 200-250 ms per second of game time on the
validation host, so raw compute was not the source of the remaining queue
gaps.

Four 512-sample device buffers were insufficient and six could still expose a
dry queue during an observed 44.9 ms scheduling/logging pause. The project now
defaults to eight buffers, with pacing at 3,072-4,096 queued samples and an
85.3 ms maximum. Raising the refill threshold from one half to three quarters
adds about 10.7 ms of average lead without changing that maximum. A final
164.47-second rebuilt-default startup-to-tutorial/gameplay probe, an earlier
115-second transition, and a 260-second tutorial-world run reported zero
XAudio2 underruns, APU starvation events, and refused submissions. The final
run retained audio across an observed 38.5 ms submit gap, reached the
421-draw tutorial-world signature, and accepted fixed stick inputs. The extra
two buffers add about 21.3 ms of maximum queue latency relative to six;
`RECOMP_AUDIO_BUF_COUNT` remains an expert override for controlled comparisons.

Do not infer perceptual correctness from these counters. The private
155-second waveform capture had nonzero marked intervals, no clipping, and no
32/256-frame zero-hole signatures, but the user reports crackly,
mono-sounding dialogue and gunfire in Accurate mode while music sounds
acceptable. Matched intervals are substantially more left/right correlated
under Accurate than Simple. The host transport gate passed; the Accurate
quality gate failed. The user subsequently reported centred dialogue/effects
in the then-current Simple build as well.

The first Simple traces were mistakenly classified as gameplay; framebuffer
evidence showed that they were still on the main menu. A corrected Quick
Campaign route reached the world and exercised 3D/HRTF voices. Former local
patch 0035, now merged as #298, fixed a generic mono-source channel-selection
defect there. A clean scripted
fixed/baseline/fixed comparison measured firing-window correlation
`0.415/0.542/0.422` and side-to-mid RMS ratios
`0.641/0.544/0.636`, with zero underruns, starvation, refusals, clipping, or
32/256-frame holes in all three selected captures. This closes the objective
channel-routing check, not the listening gate. Simple remains the recommended
default. A subsequent gameplay listening pass rated the fixed Simple path
improved but not fully correct; the residual symptom was not characterized
before audio work was deferred. The Accurate DSP defect remains separate and
open.

The first disc-inserted xemu reference attempt verified the retail XBE and
private read-only media setup, but foreground focus and controller response
could not be verified. No requested tutorial or fade scene was matched, and
the available client-window captures are not pixel-exact. G4 remains open;
see [FINDINGS](./FINDINGS.md#private-xemu-reference-attempt-2026-10-30).

The D3D11 texture cache now checks retained source and palette snapshots on
each ordinary use; the old approximate checks are a diagnostic opt-in.
Failed uploads no longer accumulate unkeyed cache slots, and deterministic
unsupported decodes are negatively cached after alias checks. A P8 page-query
reuse experiment recorded no hits in three candidate runs and was removed.
The former linear descriptor scan is now a 4,096-bucket exact index for the
2,048-entry cache. Synthetic coverage exercises collisions, all key fields,
rewrite/rekey, failed-upload discard relocation, full-cache LRU replacement,
cube levels and mip levels. Exact source and palette comparison is unchanged.
`RECOMP_GPU_TEX_INDEX=0` disables index maintenance and selects the former
exact linear lookup for measurement; unset or `1` selects the index. This
same-executable reference keeps generated code, framework identity and exact
byte validation fixed across a comparison. The first reviewed six-attempt
comparison measured 82.495 linear versus 1.230 indexed probes/lookup and
0.0486 versus 0.0336 ms/flip lookup time in nine contained intervals per
variant. Mean reviewed cadence moved from 30.284 to 31.562 world changes/s
and the 1% low from 16.418 to 17.751/s, but the three pair deltas were
+5.431, -3.459 and +1.862 changes/s. Sampled G3 was unchanged at 0.003%;
NV2A busy increased from 81.353% to 82.013%. Historical zero-caller PFB
records make the strict G3 gate inconclusive, and G2/G4/G5 evidence is absent.
The three missing title caller layouts are fixed in the next generation.
Its partial repeat confirms complete attribution in all 64 reviewed
intervals and reproduces scan removal, but the only completed pair fails
runtime-input matching and the fourth run locks before readiness. No further
repeat is scheduled; the throughput result remains inconclusive.
The current texture and colour-shadow paths instead use checked copies,
rechecking mapping and allocation identities, with optional explicit
suballocation bounds. Guard, reprotection, lifecycle, colour-alias and
layout-replacement cases are synthetic-tested, but snapshots do not serialize
concurrent writers. Checked colour-surface writes reject read-only and
already-guarded pages; failed or raced readback stops the tracked flip.
Concurrent reprotection remains non-atomic. A layout replacement with
uncertain GPU ownership cannot be sampled until a successful full clear or
readback; its title frequency and the cost of the new checks remain unknown.
No new retail binary or same-generation alternating comparison was run.
Windows write watching was evaluated after the index campaign and rejected
for the current memory model: `GetWriteWatch` only accepts regions reserved
with `VirtualAlloc(..., MEM_WRITE_WATCH)`, while framework RAM and
contiguous/tiled storage are file-mapped so multiple guest address views share
the same bytes. A local Windows NT 10.0.26200.0 probe returned error 87 for a
pagefile-backed mapped view, and page protection is per view rather than per
physical backing page. Guarding cannot close the alias, reprotection, DMA and
concurrent-baseline gaps. No switch was added; exact source and palette
comparison remains required pending a framework write-notification contract.
The user-reported 6 fps case, controlled snapshot cost, guest-write races and
matched gameplay correctness remain unresolved. Renderer device loss fails explicitly;
recovery and the separate presenter device remain open. See
[FINDINGS](./FINDINGS.md#default-d3d11-movement-regression-2026-10-30).
Detailed validation and cache-work counters are now available behind
`BRUTEFORCE_PERF_COUNTERS=1`. They report approximately every ten seconds
with the D3D11 statistics, but their QPC timing and atomic aggregation add
diagnostic overhead. In nine intervals wholly contained by the reviewed
camera-pan manifests, `guest_word` and `readable_guest_range` validation took
9.336 and 9.639 ms/flip respectively, almost entirely in `VirtualQuery`.
Exact texture comparison took 0.832 ms/flip, with 75.699 linear probes per
lookup. These are instrumented workload ratios on different threads, not
additive frame-time components. The historical `readable_guest_range`
counter now also includes checked texture/colour copies with no page-query
calls, so it is not directly comparable across these toolkit trees.

An opt-in persistent combiner pixel-shader cache now avoids recompiling
matching bytecode in synthetic warm runs and an alternating stationary-tutorial
comparison. Timing and output checks are in
[FINDINGS.md](./FINDINGS.md#launcher-and-pixel-shader-cache). A final-generation
six-leg title campaign measured 96 cold compile/create events at
12.914/25.543/30.623 ms p50/p99/max and 96 warm lookup/create events at
0.143/0.517/0.535 ms. Movement throughput and p99 frame time were effectively
unchanged. Publication passed, but content-matched title captures remain
non-identical and not frame-locked, so the cache stays opt-in and G4 remains
unestablished. Synthetic blocked-publication and concurrent-writer checks
exercise recovery when an identical valid entry already exists.

Scenes are defined by frames and content, not wall-clock windows, because the
tutorial's stationary hold appears to be a frame count. The stationary
scene's picture does not change, so it is measured in rendered frames. The
movement scene currently supplies whole-frame pixel changes, not a validated
count of distinct visible world frames.

## Targets

Micro-optimizations of the CPU rasterizer are out of scope: 0012 and 0013
together gained about 13% (see below), and similar steps cannot close a 7–10×
gap.

| Gate | Measure | Target |
| --- | --- | --- |
| G1 | Rendered frames per second in the stationary tutorial hold | The title's native rate (establish it first; 30 Hz is the expectation, not yet an observation). The identical hold images do not establish distinct FPS. |
| G2 | Distinct world frames per second in the scripted-movement tutorial benchmark | Native rate, with 1% low frames no worse than 80% of it |
| G3 | Game-thread time spent waiting on the NV2A thread | Under 10% of samples during gameplay |
| G4 | Rendering equivalence to the CPU reference path at matched frames | No unexplained regions in the image diff; every known difference listed |
| G5 | Headroom | G1 still holds at 2× internal resolution |

"Distinct world frames" are presented frames whose content differs from the
previous one, or a guest frame counter once one is identified. Flip counts
from the periodic report are not used as a target because a flip can repeat a
frame. The current whole-frame pixel hash also sees HUD changes, so measured
distinct movement fps is not yet proof that every counted change is a new
world frame. In the previous six-run GPU campaign, 58.9–60.2% of movement
flips in each run matched a solid-black image; those throughput figures do
not establish G2 or playability. The harness does not infer a fade from a
hash or darkness: reviewed intentional fades/solid frames are separate from
unknown black, dark, solid and unchanged intervals, and unreviewed movement
collection exits 2 with an inconclusive status. For a complete real-image
manifest with at least one reviewed world change, the offline analyzer
reports reviewer-asserted world-frame
cadence from each reviewed baseline to the next `world_changed` frame and
between successive `world_changed` frames. Static-world and HUD-only flips
remain inside those intervals; fades, invalid frames and unreviewed gaps break
the cadence. Its 1% low is the reciprocal of the 99th-percentile completed
interval. A trailing incomplete interval is not a completed frame and is
reported outside that cadence.

G3 includes samples in the two known guest GPU polls and kernel-wait samples
whose first executable caller is one of those polls, the event-driven PFB
wait, or the dedicated segment queue-space/lock wait helper. Ordinary PUT
publication is not counted wholesale as waiting. Other game-thread kernel
waits are reported separately and are not assumed to be GPU time. A profile
without complete, symbolized `--profile-callers` attribution provides only
the direct-poll lower bound and is not a complete G3 measurement.

The 2026-11-03 reviewed default baseline used three 40-second camera-pan runs
on generation `20261103-145850-21e3091e`, 12 raster threads and the RTX 5070.
Reviewed guest-surface world cadence averaged 27.349 changes/s
(26.847-27.713), with a 13.942/s mean 1% low
(13.571-14.360). G3 averaged 69.1%, entirely in the direct PFB flush poll;
NV2A busy averaged 73.8%. Both gates fail. Fully contained periodic intervals
measured 0.787 ms/flip synchronous readback and 4.327 ms/flip visibility
waiting. Readback is below 10% of the expected 33.33 ms native frame budget,
while the flush poll overwhelmingly dominates G3. Therefore Phase 3 is not the
first new mechanism; after isolating the existing ordered-async visibility
option, Phase 4 is the selected path. See
[FINDINGS.md](./FINDINGS.md#reviewed-default-d3d11-camera-pan-baseline-2026-11-03).

## Execution cadence

Work in substantial, related milestones rather than benchmarking each small
edit. Build the relevant fixtures together and run one targeted validation
batch after the milestone. Use one alternating A/B/A campaign, at least three
runs per variant on the same generation and scene, to evaluate the combined
change; movement runs can also collect the stationary hold, profiles, and
matched flip captures. Census reports can run alongside flip-only captures
without inserting report-time frames into the numbered sequence. Isolated
diagnostic runs are for investigating failures, not repeated acceptance campaigns. Keep separate switches for timing changes
so interactions can be isolated if the combined result regresses. The CPU
reference, image-difference accounting, and synchronization requirements are
not optional. Reuse existing captures and synthetic fixtures first, and
state the specific question each gameplay run will answer before starting it.
The phases below group work by mechanism; a milestone may span several phases
without waiting for a separate benchmark between them.

## Why it is slow

**Observed** (generation `20261029-170900-76fb2a5f` and
`20261029-175500-dfadbdd9`, see FINDINGS):

- On the original CPU path, all pixel work runs on the CPU. The instruction-pointer sample is dominated
  by `sample_tex`, `nv2a_rc_eval`, `read_rgb`, `nv2a_rc_unpack`, and
  `xf_rows`, which handle texture sampling, register combiners, and span
  rasterization.
- Vertex programs are interpreted per vertex on the CPU (`nv2a_vsh_run`).
- 2,266 of 3,600 samples were waits, and the guest PFB flush poll at
  `0x00372180` appeared 210 times. The game thread spends much of its time
  waiting for the NV2A thread.
- Going from 4 to 12 raster threads raised the flip rate from about 2.2 to
  about 3.5, not 3×. The serial parts of the pipeline dominate.

**From reading the pinned framework** (`xbox_memory_layout.c`,
`nv2a_pb_exec.c`):

- One NV2A thread walks the pushbuffer, decodes it, transforms vertices, and
  hands spans to the raster pool. It advances `DMA_GET` and writes semaphore
  releases only after rendering the commands before them. That ordering is
  correct, but it means the game's pushbuffer ring space, fence waits, and
  KickOff acknowledgements are all paced by CPU rendering time.
- The NV2A thread loop ends each pass with `Sleep(0)`, so it holds a core even
  when idle.
- The default presenter converts guest memory to a CPU frame before
  `gpu_present.c` uploads it. The optional shared path can avoid that upload,
  but still maintains the CPU snapshot and synchronous guest readback.

**Hypothesis:** the frame time is roughly the sum of CPU vertex work and CPU
rasterization for the frame, and the game thread mostly idles behind it. A
null-backend probe (Phase 0) tests this directly.

**Earlier GPU milestone:** the combined texture and visibility changes reduced
estimated game-thread GPU wait from 33.5% to 24.7% of movement samples.
`NtQueryVirtualMemory` remains prominent on the game thread, and texture
hashing remains visible on the NV2A thread. Those profiles include the
black-frame intervals and do not establish the cost during valid world
rendering. The later packed-stencil/shared-presentation comparison was highly
variable and did not establish a speedup. See FINDINGS for the run ranges and
limitations.

**Supporting evidence from Blood Wake:** its D3D11 backend replaced the same
CPU rasterizer. Its source notes that the vertex-program interpreter alone
was 42% of the NV2A thread in a mission before vertex programs moved to the
GPU. It also found KickOff waits costing about 500 ms per second of game
thread time before the handshake was made event-driven and asynchronous.

## Strategy

Make the GPU authoritative for rendering and decouple the game thread from it:

1. Render on D3D11, keeping surfaces, depth, render-to-texture, and
   presentation on the GPU.
2. Run vertex programs and vertex fetch on the GPU.
3. Remove per-flip GPU-to-CPU readback where nothing in the title reads the
   result.
4. Let the game thread run ahead of the NV2A thread by a bounded amount, as
   the console's CPU and GPU overlap.

The CPU rasterizer stays as the reference path (`RECOMP_GPU=cpu`) for A/B
correctness comparisons. The launcher and graphical `build.py` runs default
to D3D11 even though G4 remains unmet.

## Phase 0: measurement and ceiling probes

Done before writing renderer code. All evidence stays local.

1. **Benchmark harness.** Add `tools/bench.py`, or a `build.py bench`
   subcommand, that runs the tutorial with an isolated save root, a fixed
   `RECOMP_PAD_SCRIPT`, and a fixed watchdog. It produces:
   - frame-change diagnostics from each flipped guest surface, with separately
     reviewed world-frame changes rather than treating a whole-image hash as
     presented world FPS;
   - frame-time percentiles;
   - per-thread busy versus waiting time.

   Define two scenes: the stationary view used in FINDINGS, and a scripted
   movement route. Run comparisons as alternating A/B/A sequences of at least
   three runs, on the same generation. Opt-in patch 0026 tags each captured
   movement BMP with its frame-statistics flip ID, including across capture
   gate changes. This mapping is synthetic-tested; it does not prove that the
   guest surface matches the actual window output, especially with overlays,
   pinned surfaces or direct presentation. Capture writes occur synchronously
   before the statistics timestamp and perturb the measured run. Real scene
   labels and presented-image validation remain unverified. The
   console-control and WAV-finalization helpers passed a firmware-exit retail
   capture, and a bounded default-on campaign reached scripted gameplay. The
   matched scripted Simple audio A/B/A is complete; the subjective listening
   pass and manual scene review remain open.
2. **Sampling profiler with symbols.** Port the approach of Blood Wake's
   `bw_profile.c`: suspend and sample threads about every millisecond, then
   resolve addresses with DbgHelp against `BRUTEFORCE_LINK_SYMBOLS=ON`
   builds. Report per-thread function tables so that "busy" and "waiting"
   read directly, and restrict sampling to a time window. This replaces the
   ad-hoc instruction-pointer sample.
3. **GPU feature census.** Port the idea of `bw_gpu_audit.c` as an opt-in
   executor counter dump covering:
   - draws by path: fixed-function, vertex program, or screen-space;
   - texture stages in use and stage program modes, especially bump-env and
     dot-product modes;
   - combiner stage counts, texture formats, cube maps, and swizzled versus
     linear textures;
   - surface sizes and AA factors;
   - render-to-texture (a texture whose address is a recent colour surface);
   - vertex programs that write constants;
   - stencil use;
   - the 149 distinct unhandled methods.

   The census decides which renderer features are mandatory for Brute Force.
   For example, the 320×240 and 160×120 passes seen in FINDINGS look like
   downsample or post-processing render-to-texture. That is a hypothesis to
   confirm. Patch 0025 adds a separate opt-in request audit for early drops,
   backend outcomes, method/subchannel accounting and unknown visibility;
   it does not replace the state census. Patch 0044 adds post-clip CPU versus
   pre-clip GPU scopes and bounded temporal method context. Fresh tutorial
   counts exist but their scene identity remains unreviewed. Rank ignored methods and
   compare matched CPU/GPU/xemu captures before inferring visible impact.
4. **Guest surface reads.** The current-generation page-protection diagnostic
   reached 104 stationary flips and completed the 40.01-second movement phase
   in one attempt. The stationary timing gate itself was incomplete, but the
   cumulative access report covered both phases. It resolved two reads inside
   an accumulated colour range to generated title code, but that same range
   overlaps staged pushbuffer memory and the guard does not retire ranges.
   One page remained excluded and no depth read recurred. This completes the
   planned probe with an inconclusive ownership result: retain readback until
   live lifetimes and arbitrary guest accesses can be intercepted. Do not
   repeat the same run without changing that model.
5. **Ceiling probes.** These are throwaway measurements, not results.
   - **Null backend:** register an `Nv2aBackend` whose `draw` and `clear` do
     nothing and whose `flip` only counts. Run it with window clipping,
     culling, and the near-plane experiment off, because those reject a
     registered backend. The frame rate it reaches is the ceiling with CPU
     rasterization removed but CPU vertex work, the walk, and the game
     remaining.
   - **Null backend, no vertex programs:** the same probe with transformed
     batches dropped before `transform_vertex`. Its frame rate is the ceiling
     of the walk and the game alone.

   If the second probe does not clear the native rate, Phase 4 or 5 work
   moves ahead of the renderer.

6. **Relevant upstream work.** Measure the upstream work listed in
   [UPSTREAM.md](./UPSTREAM.md#open-upstream-work-to-select-or-coordinate-with)
   with the harness. The current base includes merged #272 and #273, while
   #231 remains open:
   - [#273](https://github.com/sp00nznet/xboxrecomp/pull/273), for current-base
     walk cost on either renderer;
   - [#272](https://github.com/sp00nznet/xboxrecomp/pull/272), for current-base
     vertex-program CPU fallback cost;
   - [#231](https://github.com/sp00nznet/xboxrecomp/pull/231), for the CPU
     reference path.

   Select open #231 only if it measurably helps and its tree verifies. None
   of this work replaces Phase 1.
   The 2026-10-31
   [JSRF-PC review](./UPSTREAM.md#jsrf-pc-prior-art-review) adds one
   non-PR candidate to the same measurement gate: batch non-incrementing
   `ARRAY_ELEMENT16/32` words only if the Brute Force census shows material
   volume. Its JSRF timing is prior-art evidence, not a transferable
   benchmark; preserve a scalar reference path and the audit counters.

**Exit:** a baseline table in FINDINGS covering distinct fps, per-thread
busy/wait, the census, the surface-read answer, and both ceilings, with each
generation identified.

## Phase 1: D3D11 rendering backend

This is the main lever, adapted from Blood Wake's `src/gpu/` and its carried
executor patch. New project code goes in `src/gpu/`. Executor changes become
local patches `0014+` applied by [toolkit.py](../tools/toolkit.py), with the
expected tree updated.

1. **Extended backend interface.** The toolkit's `Nv2aBackend` passes only
   texture stage 0 and diffuse colour, which is not enough for this title:
   register combiners already appear in the profile. Adopt the design of
   Blood Wake's `bw_backend_ext.h`, renamed for this project:
   - per vertex: both colours, the fog factor, and four texture-coordinate
     sets;
   - per batch: four texture stages, the stage program, combiner registers,
     fog colour, clip-plane mode, dot-RGB mapping, other-stage input, border
     colours;
   - `draw_vsh` for untransformed batches (Phase 2);
   - `clear_rect` and a register mirror accessor.

   Screen-space batches keep the plain `draw`. Rebase Blood Wake's
   `nv2a_pb_exec.c` diff onto this project's patches 0008–0013. It is about
   2,100 lines and shares our base commit.
2. **Renderer core.** Adapt the generic parts of `bw_d3d11.c`:
   - one render target per guest colour surface and one depth-stencil buffer
     per zeta surface, seeded once from guest memory and authoritative on the
     GPU after that;
   - textures whose address is a rendered surface sample that render target
     directly;
   - a snapshot-validated texture cache with direct BC and linear A8R8G8B8
     uploads, GPU compute decoding for common formats, and a CPU decoder
     fallback;
   - cached blend, depth-stencil, rasterizer, and sampler state objects;
   - one dynamic ring vertex buffer mapped with `NO_OVERWRITE`;
   - persistent constant buffers updated only when changed.

   Leave out everything tied to editions, VFX, widescreen, texture
   restoration, gamma, ocean, and Blood Wake addresses.
3. **Pixel shaders from combiner state.** Adapt `bw_rc_hlsl.c`, which mirrors
   the framework's `nv2a_combiner.c` rule for rule, and `bw_pscache.c`, a
   persistent cache of compiled bytecode keyed by HLSL text and compiler.
   Close any combiner or stage-mode gaps the Phase 0 census shows Brute Force
   using. Blood Wake leaves bump-env and dot-product stage modes at zero, as
   the CPU path does.
4. **Port the experiments.** The software-only options must work on the GPU
   path, otherwise their guards reject the backend:
   - window clipping (0009, 0021): pixel-shader rectangle-union and
     inclusive/exclusive masks on the D3D11 path, still opt-in;
   - face culling (0010): rasterizer state, keeping the strip-winding fix
     upstream of the backend;
   - the programmable near plane (0011): a clip distance in the generated
     vertex shader.

   Their defaults stay as decided by the existing investigations.
   Vulkan now consumes the same window-clip state through a dedicated
   fragment uniform and passes base, extended and GPU-VSH coverage on both
   available vendors; this closes backend support, not the opt-in parity gate.
5. **Tests.** Native fixtures run on the D3D11 WARP device so they need no
   GPU and no game data. They compare:
   - generated combiner shaders against `nv2a_combiner.c` over randomized
     combiner states and inputs, extending the synthetic parity checks
     already recorded in FINDINGS;
   - state translation for blend, depth, stencil, and cull;
   - texture-cache invalidation when a texture is rewritten at the same
     address;
   - render-to-texture without readback.

**Exit:** G4 is met at the stationary scene, compared against the CPU path at
matched frames using `RECOMP_FB_DUMP`-style captures on both paths. The
distinct-fps and profile deltas are recorded. Expected result, a hypothesis
from the ceiling probe: most of the null-backend ceiling.

## Phase 2: vertex programs and vertex fetch on the GPU

1. **GPU vertex programs.** Adapt `bw_vsh_hlsl.c`, which generates HLSL from
   NV2A vertex programs with the interpreter's semantics, through
   `draw_vsh`. After the program it applies the executor's own steps:
   `w = +0` taken as 1, AA scale, clipping at `w = 1e-3` and the near plane,
   and fog. Programs that write constants stay on the CPU.
2. **GPU vertex fetch.** Pass each stream's index range as the title's raw
   bytes, and let the input layout and shader prologue convert formats the
   way `fetch_attr` does: float, D3DCOLOR, UBYTE, SHORT, and packed CMP
   normals. Track a constant-file generation number so unchanged constants
   are not re-uploaded.
3. **Verification mode.** Add a switch that re-runs a batch through the GPU
   shader and reads back the results, comparing them with the CPU
   interpreter (Blood Wake's `vsh_readback`). Use it in fixtures on WARP and
   in occasional live diagnostics.

Vulkan implements step 1 with one static interpreter shader rather than
per-program compilation. It currently implements the gathered-input branch
of step 2 only and intentionally leaves `vsh_raw_ok` unset. Its compute
readback implements step 3 without adding output allocation or a wait to
ordinary draws.

**Exit:** no `nv2a_vsh_run` or `fetch_attr` in the NV2A-thread profile during
gameplay, apart from constant-writing programs. The Vulkan interpreter clears
the `nv2a_vsh_run` part of this gate, but gathered `fetch_attr` remains at
2.09% of the sampled NV2A thread. G4 still holds.

## Phase 3: remove synchronous round trips

1. **Present directly from the GPU.** The opt-in shared-texture path copies
   the finished 32-bit target to the existing D3D11 window device, with one
   pending snapshot and CPU fallback for pinned frames or video overlays.
   The frame ID prevents older GPU snapshots replacing newer CPU snapshots.
   Guest readback and CPU snapshot creation remain even on an eligible shared
   flip: the executor still publishes a guest-backed CPU snapshot. The static
   consumer inventory is complete and records CPU/GDI/SDL upload, pinned
   frames, PVIDEO, dumps, statistics and presenter capture. This is not yet a
   fully GPU-authoritative presentation pipeline.
2. **Readback only on demand.** Not implemented for GPU-dirty surfaces:
   arbitrary title readers have not been comprehensively intercepted. The
   current null-backend guard resolved generated title-code access inside an
   accumulated colour range, but staged-pushbuffer overlap means it cannot
   establish live render-target ownership. One page remains excluded. A
   metadata-only shared flip and an explicit CPU-snapshot request contract
   would address known framework consumers, but not those lifetime/access
   blind spots.
   The separate default-off `RECOMP_GPU_SKIP_CLEAN_READBACK=1` experiment
   removes a copy only when native 32-bit guest bytes are already coherent;
   diagnostics and unsupported formats still copy, and the flip still waits.
   Blood Wake measured about 4 ms per flip for its own copy; that is not a
   measurement of this experiment.
3. **Bound submitted work.** Implemented locally with two event-query batches
   and backpressure, checkpointed after 128 renderer operations or at a flip.
   The existing shared presenter separately bounds one pending snapshot.
   The conservative flip-completion wait remains, and a frame-latency
   waitable swap chain has not been implemented.
4. **Fence semantics.** Semaphore releases are still written when the
   executor has *submitted* the preceding work to D3D, not when the GPU has
   finished it. This is safe only while no guest-visible memory depends on
   GPU output. With GPU-authoritative surfaces and no guest surface readers,
   that holds. Otherwise, the release for a batch that produces a read-back
   surface must wait for that readback. This is a deliberate rule, documented
   in FINDINGS. It is not the "mirror fences early" shortcut FINDINGS rejects:
   vertex and index data are consumed from guest memory into the ring buffer
   before the release is written.

The reviewed six-leg visibility comparison isolated the existing asynchronous
report path. It increased mean reviewed world changes from 27.401/s to
30.606/s, but the mean 1% low reached only 15.697/s and G3 remained 65.467%.
The selected async intervals wrote 487,725 reports without a logged visibility
query or device error. Ordered retirement remains synthetic-tested rather than
independently proven by the title captures. Keep the path opt-in.

**Exit:** no per-flip GPU wait in the NV2A-thread profile, and G1. The
current conservative flip wait means this gate is not met.

## Phase 4: let the game thread run ahead

Each step changes guest-visible timing, so each stays behind its own switch.
The full controlled matrix, if explicitly resumed, measures flush-only,
idle-only, segment-only and all-three separately against the all-off
baseline, each in alternating A/B/A order with at least three runs per
variant and otherwise identical controls. Four six-run comparisons produce
the deferred 24-run campaign.

**Status:** steps 1–3 have focused synthetic coverage on tree
`3164795a5ed52658abb41f2630ed8153e6b2cce0`. An earlier corrected PFB-only
title smoke was not reviewed or profiled and predates patch 0045. The first
all-three attempt exposed a PGRAPH/PFB deadlock, and a segment-only attempt
reproduced it at flip 12,611. After the narrow dependency fix, one no-retry
segment-only and one no-retry all-three attempt reached reviewed movement.
Their flip and whole-picture-change rates are bring-up data only. If the
campaign is explicitly resumed, run the manual, matched A/B/A matrix with
reviewed world and presentation evidence, event/timer/queue and caller
counters, and the CPU reference before applying any gate. The matrix is
currently deferred: do not schedule it as active work, and leave all three
switches default-off with G2–G5 unclaimed.

1. **Event-driven flush acknowledgement.** Identify the KickOff routine
   containing or calling the PFB flush poll at `0x00372180`. Replace its
   polling with a wait on an event that the ack path signals when it clears
   bit 16 of `0x100410`, with a short high-resolution timer as a backstop so
   interrupt safe points still run. Registers and protocol are unchanged.
   This follows Blood Wake's `bw_flush_wait` and `bw_flush_acked`. JSRF-PC's
   different experiment completes the flush at the register write; do not
   adapt that shortcut. The wait must wake only from the real acknowledgement.
   The local implementation is framework patch 0036 plus the title-specific
   hook and the bounded extension in 0045; it remains non-default.
2. **Idle NV2A thread.** Replace the end-of-pass `Sleep(0)` with: spin while
   work is recent, then nap on a high-resolution timer or until the next
   KickOff signals. Implemented as independent
   `RECOMP_NV2A_IDLE_WAIT=1`, including a timer backstop.
3. **Walk one segment at a time.** Walk each published PUT segment on its own
   and advance GET after each, so a KickOff waiting on GET is released after
   one segment rather than the whole batch. This changes when a held flip
   takes effect relative to later segments. Validate it against flip pacing
   and the PGRAPH trap ordering in [test_pgraph.py](../tests/test_pgraph.py).
   `RECOMP_NV2A_SEGMENTS=1` queues each intercepted PUT and protects the
   USER aperture; unsupported writes/resets stop rather than skip commands.
   Production-scene behavior and ordering still require the campaign.
4. **Asynchronous KickOff (optional).** Allow up to N segments outstanding
   before KickOff waits on GET. Only consider this if G3 is not met after
   steps 1–3. It relies on D3D reusing ring space only behind GET and on
   resource locks waiting on the fences the walk writes. Verify both for this
   title before enabling it.

**Exit:** G3, and G2 at the movement scene.

## Phase 5: residual CPU cost

Use the latest profile to select this work; batch it with related earlier
phases when it is on the critical path:

- **Executor decode and batching:** per-method dispatch cost, including the
  JSRF-PC prior art for handing non-incrementing index-word runs to the
  executor whole; redundant state rebuilds per batch; and exact texture-cache
  lookup and comparison cost. Index exact keys first and retain full source
  and palette comparisons until tracked guest writes establish safe
  invalidation. Existing rewrite fixtures show that sparse checks can miss
  same-frame and unsampled changes, so sparse hashing remains diagnostic.
  Use the opt-in interval counters and the same-binary
  `RECOMP_GPU_TEX_INDEX=0/1` comparison to separate descriptor probes, exact
  source and palette comparison spans, snapshot copies, rewrites and evictions
  before selecting the next cache change.
  The first reviewed comparison attributes a 30.8% reduction in measured
  descriptor lookup time and preserves every exact comparison, but does not
  establish repeatable world-cadence improvement. Its incomplete historical
  PFB caller records keep G3 inconclusive. The caller-corrected partial repeat
  confirms all 64 contained PFB/PUT intervals have callers and measures
  80.476 linear versus 1.239 indexed probes/lookup, but its only A/B pair
  fails runtime-input matching and the fourth attempt locks before readiness.
  The operator closed the repeat as inconclusive. Retain the exact index and
  do not infer a title speedup or select another cache policy from the
  unbalanced diagnostics.
  The retained Brute Force census now establishes material
  `ARRAY_ELEMENT16/32` volume, and patch 0046 implements the default-off run
  API for both selected walkers. Scalar/run executor state, audit counts,
  overflow behavior, packet/PUT/segment/wrap boundaries, CALL/RETURN and
  staged walking pass synthetic tests. Matched CPU/D3D11 rendered output and
  an independent reviewed six-attempt A/B/A comparison remain required.
  Hold `RECOMP_GPU_TEX_INDEX=0` in both variants so the texture-index
  candidate is not combined with this measurement.
- **Generated code:** `/O2` is already active. Isolated, mutually exclusive
  Release profiles now build title-executable `/GL` plus LTCG and optional
  `/arch:AVX2` candidates without replacing the supported default. The
  benchmark can alternate default/candidate binaries, requires stable
  per-profile hashes and identical runtime switches, and applies correctness
  gates to the candidate profile. Both candidates compile; neither has retail
  or matched-render evidence. Measure them in separate reviewed campaigns
  after the higher-priority Phase 5 gates. AVX2 narrows host compatibility and
  is not an assumed default. Also check guest memory-access macro overhead and
  x87 emulation hot spots in the title's own code.
- **Hot title functions:** a native rewrite is allowed only with the same
  operations in the same order and a bit-exact comparison against the
  generated code over a large recorded corpus. Blood Wake rewrote its water
  surface this way, with 0 differences over 2.6 million batches, and took
  four-player split screen from 66 to 113 fps. Treat this as a last resort,
  not a plan item.

## Internal-resolution milestone

The generic Blood Wake physical-resolution model is now implemented in the
project D3D11 backend: logical/physical surfaces and scale epochs, physical
clear/clip mapping, guest-sized GPU readback resolves, normalized visibility
counts, and exact/box/Catmull-Rom output through an independently sized shared
texture. The launcher exposes Native, 2160p, 1440p, 1080p, 720p and the
unchanged 480p default. Scaled experimental depth/stencil aliases are
explicitly rejected.

This is not evidence that the performance gates hold. The synthetic D3D11
matrix passes 293 cases with WARP and hardware enabled, including CPU-written
frames before scaled presentation and the mipmap fixtures below. Bounded
4.5x retail probes show the Microsoft, Digital Anvil and intro movies. One
integrated 4.5x attempt also reached movement without retry and produced
reviewed representative gameplay frames, but it was not matched or
continuously labeled. Its mip diagnostic found no additional levels declared
for the observed tutorial texture. VRAM headroom, actual lower-level mip/LOD
behavior and a native 4K display remain untested. G4 comparisons continue at
scale 1. G5 requires
the controlled 1080p/4K A/B/A campaign in
[NEXTSTEPS.md](./NEXTSTEPS.md), and the reported default-settings movement
regression remains a separate blocker.

NEXTSTEPS B10 now has a default-off mip/anisotropy implementation with
synthetic WARP and hardware coverage, not a performance result. The CPU
reference remains level zero; keep both options off for existing G4 baselines
and list their intentional sampling differences in separate enabled-mode
comparisons. The title's broader mip supply, xemu-matched frames, 4K VRAM cost and
G5 remain open. Measure any performance change with at least A/B/A runs on
the same generation, scene, scripted input, guest-CPU setting, thread count
and options, and
report distinct world frames separately from flips.

## True-widescreen correctness workstream

Widescreen is now an active investigation in
[NEXTSTEPS E](./NEXTSTEPS.md#e-true-hor-widescreen-camera-diagnostic-implemented-mode-pending),
but it is not a performance phase. Success means genuine Hor+ geometry with
matching title culling, protected 2D and movies, centred guest readback and
aspect-aware presentation. A wider framebuffer by itself fails that gate.

The first implementation remains default-off and D3D11-only. The CPU path
stays at 4:3 as the correctness reference. Validate the new path in this
order:

1. Alternating 4:3 A/B/A on one generation, scene and scripted input, with no
   intended output or timing change.
2. Alternating 16:9 and 21:9 runs that compare the centre 4:3 region and
   inspect newly visible side geometry, culling, effects, HUD and movies.
3. Performance reporting only after correctness: record the aspect, render
   scale, GPU, thread count and options, and report distinct world frames
   separately from flips. Wider output naturally increases pixel work and
   must not be compared with 4:3 as if the workload were equal.

Split-screen widescreen behavior and performance validation are explicitly
deferred. They are outside the current true-widescreen exit and G2-G5
campaigns; do not report or imply split-screen support from single-view data.

The mode does not relax G4, does not justify skipping guest readback before
its consumers are replaced, and must not skip or prematurely satisfy flush,
semaphore, report or flip synchronization.

## Vulkan and D3D12 backend migration

This is a correctness and maintainability workstream, not a claim that a
newer host API is faster. The architecture and startup policy are defined in
[RENDERING.md](./RENDERING.md#vulkan-first-backend-architecture-and-bring-up-status);
the ordered implementation tasks are
[NEXTSTEPS F](./NEXTSTEPS.md#f-vulkan-default-renderer-migration-presentation-provider-seam-implemented).
Current D3D11 measurements remain the baseline until Vulkan reaches matched
retail rendering and native-presentation parity.

Current status is partial M1 with an integrated but incomplete M2 prototype. M0 has transactional startup/fake-fallback
coverage, the accepted #319 baseline, and renderer-neutral surface/texture
layouts, checked snapshots and filter/LOD decoding, but official optional
extensions are not complete. M1 has NVIDIA/AMD validation-clean base and
CPU-transformed extended draws, precise visibility, register combiners, exact
2D/cube/mip texture rewrites, compatible full/self/subregion color aliases,
one state-matched NVIDIA retail tutorial capture, gathered-input GPU vertex
programs, clean CPU/PVIDEO surface preservation and one completely reviewed
D3D11/Vulkan movement pair. It does not have direct raw-stream vertex fetch,
scale, complete retail native-presentation coverage, broad menu/movie/fade
coverage or the complete ordering matrix. M2 now has a default-inactive, versioned
host presentation-provider seam with explicit framebuffer-thread lifecycle,
concurrent output-size queries, CPU-required routing, frame identity, pacing
results, fail-stop errors and shutdown waiting. Its downstream Vulkan
consumer adds a real Win32 surface and swap chain, three-slot completed-image
handoff, queue synchronization, resize, fitted blit, VSync and CPU fallback.
Validation-enabled NVIDIA and AMD fixtures cover the synthetic route, but
one validation-enabled retail run also reached operator-confirmed visible
Basic Controls through its 640x480 FIFO swap chain. The opening movies were
not observed in that run or its focused retry because both disabled audio.
An otherwise matched bounded audio-on trace opened the Bink files and
produced changing CPU-owned frames through the native provider. Physical
movie/fullscreen review, PVIDEO/capture integration and real device loss
remain open. Synthetic borderless-fullscreen enter/restore and fail-stop
surface loss pass on both available vendors.
No M1-M5 gate is met.

The pre-presentation counter attribution is complete and promotes no gate.
The backend now avoids asynchronous visibility polls when the oldest report
has no submitted work that can complete and the existing 0.25 ms flush
deadline has not elapsed. Queue backpressure, ordered guest writes,
synchronous query drains and submission waits remain mandatory. Synthetic
ordering and bounded-submission coverage pass on both available GPU vendors.
Exact texture-snapshot elision was reviewed and deferred because checked
`ReadProcessMemory` plus post-copy layout/allocation/bounds identities
preserve guard and lifetime behavior, while Windows write-watch cannot track
the aliased file mappings. No approximate cache or stale snapshot replaces
that contract. The missing-movie observation is now attributed to the
audio-off title path with matched bounded traces, not a multi-run comparison.
Test-only readback now verifies the final composed Vulkan swap-chain image
for native, forced-CPU, source-allowed CPU and fullscreen routes, including
the fitted opaque-black bars, on both available vendors. This is final-image
composition evidence, not physical compositor scanout. Vulkan window clipping
now also passes focused validation-enabled NVIDIA and AMD fixtures for base,
extended and GPU-VSH draws without a retail run. Fixed numeric Vulkan
render scaling likewise passes a validation-enabled scale-2 synthetic child
on both vendors without a retail run; it is not a performance result.
Continue with the remaining synthetic renderer-state gaps; no additional manual route is scheduled merely
to repeat the baseline.

The retail captures are correctness bring-up evidence only. The final
combiner attempts ended at their 300-second watchdogs before the stationary
interval completed; the same-generation D3D11 reference completed one
interval at 13.813 flips/s. The separate visibility-only interval and stopped
wrong-menu mip probe are not comparable replacements. The runs were not an
alternating A/B/A campaign, so no Vulkan/D3D11 performance comparison is
recorded. The native-present retail validation reported 8.451 stationary
flips/s with validation enabled, and its focused movie retry was stopped
without a readiness token. Neither is distinct-world-frame or M5 evidence.

| Gate | Required evidence |
| --- | --- |
| M0: contract and extraction | Reuse merged #319 for callback order, exactly-once delivery, borrowed data, callback threads and registration lifecycle. Focused additions cover complete initialization before registration, structured failure, Vulkan-to-D3D12 startup fallback, explicit no-fallback choices, teardown and both-backends-fail behavior. Every required `Nv2aBackend` extension is optional and independently synthetic-tested; each renderer-neutral extraction leaves focused D3D11 fixtures unchanged. |
| M1: Vulkan rendering parity | Shared synthetic clear, state, texture, shader, visibility, readback, alias, scale and ordering fixtures pass. Matched CPU/D3D11/Vulkan captures cover the opening, stationary tutorial and movement, menus, movies, HUD, fades and render-to-texture. Unsupported state or resource failure stops explicitly. |
| M2: Vulkan native presentation | The host presentation service owns the window, Vulkan surface and swap chain, resize/fullscreen/VSync controls, final present, frame identity, PVIDEO and pinned-frame CPU fallback. The backend supplies a completed image, synchronization and mandatory guest readback through a separately tested handoff. CPU upload is bring-up evidence only. |
| M3: D3D12 parity and backup | D3D12 passes the same semantic and presentation matrix, including WARP where applicable. Forced Vulkan loader, feature, device, compiler and presenter failures each select D3D12 only from `auto`; explicit Vulkan still fails closed. |
| M4: default promotion | Available NVIDIA, AMD and Intel Vulkan paths are exercised, current saved renderer choices survive launcher migration, active-backend diagnostics are unambiguous, and both-backends-fail stops rather than selecting D3D11, CPU or `null`. Only then may fresh graphical settings select `auto`. |
| M5: performance characterization | After M1-M4, run same-generation, same-scene A/B/A comparisons with at least three runs per backend. Record guest-CPU mode, thread count, GPU, driver, API, scale and options; report distinct world frames separately from flips. First-run DXC compilation and warm-cache results are separate. |

The contract reference is official `Nv2aBackend`, grown according to
sp00nznet/xboxrecomp#306 rather than replaced with the local extended seam.
Vulkan remains downstream while it proves the needed optional extensions and
host-service presentation handoff. Generic pieces can be proposed upstream
one at a time; an optional Vulkan backend PR is a later goal that requires
parity, maintainer direction and explicit user approval.

The synchronization reference is semantic rather than API-specific. Vulkan
command buffers and fences or timeline primitives, D3D12 command lists and
fences, and D3D11 immediate-context queries must all enforce the same
submitted-work bound, ordered visibility reports, readback-before-consumer
rule, render-to-texture dependencies, and guest-visible PFB, semaphore,
software-trap and flip holds. No backend may pass a gate by acknowledging
work earlier than the current contract permits.

Device loss after guest work starts is not a fallback case. A runtime switch
would require a separately tested reconstruction of GPU-owned colour and
depth surfaces, texture and pipeline caches, pending queries, presentation
epochs and guest-visible ordering. Until such a design exists, every backend
must report the failed operation and stop without claiming outstanding work
completed.

D3D11 remains selectable through M4 and subsequent field validation. Its
eventual retirement is a separate decision after Vulkan and D3D12 both meet
the gates; the migration must not remove the only currently validated GPU
comparison path early.

## Not in scope, or explicitly rejected

- Further SIMD or threading work on the CPU rasterizer beyond what it needs
  as a reference.
- Skipping or prematurely clearing the PFB flush handshake, or marking
  submitted fences complete before their inputs are consumed. FINDINGS
  explains why these are invalid.
- Runtime Vulkan-to-D3D12 switching after device loss. The accepted fallback
  is startup-only, before backend registration and guest execution.
- Treating Vulkan or D3D12 as a performance shortcut around guest PFB,
  semaphore, report or flip synchronization.
- Mipmaps are an opt-in correctness experiment for the 4K target (NEXTSTEPS
  B10), not an established performance win; other LOD-transition
  enhancements stay deferred. Preserve the default scale-1 reference.
- MSAA and frame interpolation remain deferred. True widescreen is now a
  separate correctness workstream, not a performance optimization. The
  Phase 1–3 architecture makes all three possible, but none may be justified
  by frame-rate figures alone. Higher internal resolution is implemented
  synthetically as workstream B in [NEXTSTEPS.md](./NEXTSTEPS.md), but G5
  remains its performance gate.

## Risks

- **Fidelity.** The combiner generator inherits the CPU path's gaps, such as
  bump-env and dot-product modes. Brute Force may use features Blood Wake
  never exercised: stencil shadows, point sprites, cube or projective
  texturing, different surface formats. The census bounds this risk; G4
  guards it.
- **Existing visual issues.** The user reports that the HUD and excessive
  glow can disappear together in the live window; a synthetic hardware-D3D11
  near-plane case reproduces a possible shared clipping mechanism, not the
  actual gameplay cause. The dark frames, missing HUD, and black
  captures in FINDINGS are unexplained. The GPU path may change or hide them.
  Keep the CPU reference path working so a symptom can be assigned to the
  renderer or to the title.
- **Shader compile hitches.** Compile on first use; the opt-in persistent
  cache can avoid repeated combiner pixel-shader compiles on later runs.
  Vertex and other shaders are not cached. Measure title first-run stutter
  separately from the steady state.
- **Cross-API drift.** Shared HLSL and renderer-neutral descriptors reduce
  duplication but do not prove Vulkan, D3D12 and D3D11 use identical clip,
  depth, interpolation, format, query or sampling rules. Keep per-backend
  output fixtures and CPU-reference comparisons; do not normalize a mismatch
  merely to make two host APIs agree. In the retained Basic Controls pair,
  all 134 over-8/255 pixels form 102 sparse components in exposed animated
  world content, while reviewed opaque UI regions have none. The remaining
  one-to-six-LSB UI differences are correctness evidence to retain, not a
  performance result or justification for changing synchronization.
- **Presentation compatibility.** A Vulkan default cannot depend on optional
  D3D11/Vulkan external-memory interop. Native same-API presentation must
  remain a host service, with the backend supplying completed images and
  synchronization while the host retains its window, swap chain,
  CPU/PVIDEO fallbacks, frame identity, resize epochs and captures.
- **Guest/GPU ownership.** Vulkan now preserves clean CPU/PVIDEO writes and
  reports dirty conflicts, but a retained 1 MiB surface at `0x80000000`
  overlaps the known staged-pushbuffer range. The executor does not resolve
  `NV097_SET_CONTEXT_DMA_COLOR`, and the exact writer was not intercepted.
  Keep mandatory readback and GPU-wins conflict handling until generic
  DMA-object resolution and live surface lifetimes distinguish command memory
  from a real CPU-written target.
- **Synchronization.** PGRAPH software-method traps, flip holds, and the
  semaphore hook all run on the NV2A thread. The backend must not block that
  thread on the GPU while a trap is pending. A queued segment also cannot
  hold the guest in its PFB poll while the executor waits for that guest to
  service the trap. The current exception releases PFB only; GET, the queue,
  interrupt state and segment completion remain unchanged until the ISR
  acknowledges the trap.
- **Patch stack.** The Blood Wake executor diff overlaps 0008–0013. Rebase it
  once onto a pinned tree and record the result in `toolkit.py`; do not
  maintain two variants.

## Documentation obligations

- **FINDINGS:** the Phase 0 baseline and each phase's measured result, with
  generation, run count, scene, and whether figures are distinct frames or
  flips.
- **UPSTREAM:** every phase changes the framework, so each one updates its
  [UPSTREAM.md](./UPSTREAM.md#performance-and-gpu-back-end-candidates)
  entries in the same change, moving them from *planned* to *local patch*
  with tests and overlap:

  | Phase | UPSTREAM items |
  | --- | --- |
  | Phase 0 | 19, 20, 28; also evaluate open #222/#231 and benchmark current-base behavior from merged #272/#273 |
  | Phase 1 | 12, 14 |
  | Phase 2 | 13 |
  | Phase 3 | 15 |
  | Phase 4 | 16, 17, 18 |
  | Vulkan/D3D12 migration | 12-15 and 50; build on #306 and merged #319, coordinate the unanswered host-presentation question in #312, and treat #184 as prior art rather than the target ABI |

  Title-specific pieces stay in `src/`: the KickOff hook, Brute Force
  addresses, and census conclusions. Build on the incremental contract in
  [#306](https://github.com/sp00nznet/xboxrecomp/issues/306) and merged
  [#319](https://github.com/sp00nznet/xboxrecomp/pull/319); coordinate the
  readback seam with [#285](https://github.com/sp00nznet/xboxrecomp/pull/285)
  and the unresolved host-presentation question with
  [#312](https://github.com/sp00nznet/xboxrecomp/issues/312). Do not revive
  #184's decoded-state seam wholesale or prepare an upstream backend PR
  without explicit approval.
- **README:** status, the renderer switch, and build requirements (the
  D3DCompiler runtime) once a phase changes user-facing behaviour.
- **THIRD_PARTY_NOTICES:** attribution for adapted Blood Wake files.
