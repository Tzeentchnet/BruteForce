# Next steps

The rendering architecture is in [RENDERING.md](RENDERING.md),
the performance plan is in [PERFPLAN.md](PERFPLAN.md), measured
results are in [FINDINGS.md](FINDINGS.md), and framework work is
tracked in [UPSTREAM.md](UPSTREAM.md). The current framework pin is xboxrecomp
v0.18.0 at `37b61d589168adee3680fabd51805ee81427bf07` with 43 local patches
and no separate selected upstream commits. Its reconstructed tree is
`8938ce1b635e25832539a0bc6500b2fe2fa14565`. The clean patch replay and
toolkit verification passed; focused v0.18 framework fixtures passed 32
CTests, the FCMOV/WBINVD fixtures passed nine tests, and the project DSP
memory and lifecycle fixtures passed. Retail regeneration, exact
discovery/translation comparison, and bounded one-core, audio-on CPU/D3D11
smokes also pass. Details, limits and the 128 MB host-mirror observation are
in [FINDINGS.md](FINDINGS.md).

Supported retail generation `20261120-163420-f9e3b4ed` is a Release build on
that tree. It translated all 22,295 identified functions with zero failures,
six unresolved direct-call targets and the same 11 untranslated instruction
sites as v0.17. Function starts and ends are unchanged; only
`0x002EA100`'s instruction count changed, from 262 to 265, after code/data
resynchronization. The 37,582,848-byte executable SHA-256 is
`a85d62f9558da67757559fc35119fb9d904fd6b0700da4ace86ccdf878902cc8`;
the 36,474,880-byte PDB SHA-256 is
`fab0485c13954988d3e9a1f95619b637275838a6d57d29432ebae40f0dd80420`.
One D3D11 and one CPU attempt reached the same reviewed Basic Controls state
without retry at 87.97 and 87.59 seconds, with audio on and guest threads on
one core. Their stable captures differ by more than 8/255 in 677 pixels
(0.2204%). This accepts the framework base, not performance, movement
correctness, listening quality or long-session reliability.

The last matched build of the same generation, with visibility and the
CPU-transformed Vulkan pixel pipeline, produced executable SHA-256
`17ef3c06b9e205ffcf93cc532b02f2fcf23b3638e07de81d9cfae27ed182887f`.
Precise queries first restored the expected stationary geometry volume:
1,607.6 draws and 132,454.7 vertices per frame versus D3D11's 1,608.4 and
132,641.7. Four texture stages, register combiners and cube maps then reduced
the matched Basic Controls difference from 149,590 pixels over 8/255
(48.6947%) to 134 (0.0436%); mean channel error is 0.374562 and maximum
channel error is 29. The validation-enabled NVIDIA capture pair is
byte-identical with SHA-256
`b81bb0546d28ba0fe53a34af0ad4cebcac6ee081d16399d625bb438a80070870`.
This closes the first stationary full-pixel-pipeline comparison. Native
presentation, matched gathered-GPU-VSH output, direct raw-stream fetch,
wider scene/stability parity and any accepted performance claim remain open.

Four-connected follow-up on that retained pair finds 102 components among
the 134 over-8/255 pixels, with a largest component of 26 pixels. Reviewed
mask locations are all exposed animated world content around the irregular
tutorial-panel boundary. Three opaque panel regions have zero pixels over
8/255 and maximum channel differences of four to six. The remaining
subthreshold cross-API numeric drift is not isolated, but the sparse
over-threshold tail is no longer an unexplained stationary UI difference.

The current default-off Vulkan mip build has executable SHA-256
`8ab947dec007dfd852db9ceed4c3248faa438838e8776e3d69567d56244feeee`.
NVIDIA and AMD fixtures select supplied lower levels for 2D and cube textures,
observe lower-level rewrites, and keep render-target aliases at level zero.
One validation-enabled retail probe uploaded a supplied six-level format
`0x0E` BC chain, then took the legacy script's wrong Join Deathmatch route
and was stopped after 3,435 flips. Its roughly 7 flips/s menu observation,
missing on-screen movies and capture are not correctness or performance
evidence: the script deliberately skipped the movies and never reached the
target scene.

The first 37,823,488-byte gathered-GPU-VSH diagnostic executable has SHA-256
`108c787713c52007fe67e7c90ed33db77e4e6443cca11c075db0be5baec314cc`.
Its static SPIR-V NV2A interpreter, non-finite triangle filter and compute
verification readback pass with zero Vulkan validation errors on the NVIDIA
and AMD adapters. One uninstrumented legacy-route collection reported 26.391
stationary and 38.862 movement flips/s; a caller-profiled repeat reported
26.683 and 38.503. This materially improves the preceding 8.960-11.240
gameplay intervals and removes `vsh_run_decoded`/`run_mac` from the sampled
NV2A hot list, but it is still unreviewed diagnostic evidence, not distinct
world frames or A/B/A. A sparse retail verifier logged its first three checks
with zero differences and no later mismatch through 1,745,525 accepted
program batches. The throughput blocker is cleared for continued correctness
work; matched scene review still precedes renderer comparisons.

The follow-up ownership build has SHA-256
`8446de9e7be7834dd465369320bc4baeaf2520a1a6748ae0fb6ef376d24bf9eb`.
It preserves CPU/PVIDEO writes made to clean Vulkan surfaces, uploads them
before later partial clears or draws, and reports dirty guest/GPU conflicts
while retaining the completed GPU frame. NVIDIA and AMD validation fixtures
cover clean preservation, partial-clear resynchronization and GPU-wins
conflicts. The operator confirmed that the formerly black manual route now
reaches the visible Basic Controls popup.

Matched-build continuous movement captures have also completed: all 918
classified Vulkan captures contain 917 reviewed world changes (23.021/s,
12.135 1% low), and all 1,274 classified D3D11 captures contain 1,273
(32.026/s, 15.307 1% low), with zero unknown frames. Fifteen contact sheets
cover every classified capture and show the same tutorial scene and fixed
camera route without panels, cinematics, fades or missing geometry. These are
reviewed guest-surface correctness/cadence results, not compositor proof or
performance evidence; continuous BMP capture perturbed both timing and input
delivery.

The remaining ownership diagnostic is bounded but unresolved. Surface
`0x80000000` is a retained 512x512, 1 MiB target whose changed guest span
advances through the full allocation immediately before the tutorial world,
while the active output resolves to `0x802B0000`. This matches the known
staged-pushbuffer overlap and the executor's ignored
`NV097_SET_CONTEXT_DMA_COLOR` base, but the exact writer was not intercepted.
Keep GPU-wins conflicts and mandatory readback. A same-generation no-capture
D3D11/Vulkan diagnostic is now complete with one manually confirmed run per
renderer, minimizing operator intervention. It must not reuse the
continuous-capture rates or be promoted to the post-presentation M5 result.

The final synchronized 37,826,048-byte executable has SHA-256
`1dd3bc9499d14a260a1650f086e6cf40ae71da70708b5cffd36eb67c73a2b939`.
Its no-retry scripted Vulkan smoke reached the tutorial world at 92.42 seconds
and completed one stationary interval at 14.100 flips/s. The run reproduced
the pre-world full-range conflict and is a validation smoke, not performance
evidence.

The first no-capture renderer campaign on that executable is invalid. Its
first D3D11 and Vulkan attempts completed, but attempt 3 reproduced the
pre-movie two-flip, zero-draw loader poll and broke the alternating sequence.
Do not analyze or combine its first pair with later runs. Patch 0047's
37,827,072-byte replacement has SHA-256
`f717c74980d81e506272f71f7c3135ebd68017d90753cb783d0e64060d9ffc7a`;
three independent no-retry D3D11 boots reached movie draws and one continued
through operator-confirmed Basic Controls. The replacement two-run diagnostic
used this exact executable, four raster threads, one guest CPU, the zero-delay
backoff, audio off, mips and asynchronous visibility on, manual readiness, no
profiling, no capture and no adaptive panel presses. Both attempts completed.
D3D11 reported 26.409 stationary and 48.193 movement flips/s; Vulkan reported
23.164 and 17.469. Input dispatch differed by at most 78 ms. These are
unreviewed single-run flip diagnostics, not world FPS or A/B/A evidence.

The preceding v0.17 toolkit used selected #314/#319/#320 commits and 43
local patches, with tree `3e3ae3600802d2bf4446d86fc546322d60849047`.
The framework tools on that earlier tree reported 995 passed, 2 skipped and 134
subtests passed; the project suite reports 972 passed with WARP and the
available hardware adapter; eleven focused framework groups report 47 CTests
passed; and the presenter suite reports 19 passed. The Phase 4 subset
comprises 22 of those CTests, while generated-store coverage reports 3 passed
and the title-side fence, pushbuffer, PGRAPH, interrupt and toolkit selection
reports 67 passed. The validation includes the runtime PGRAPH/PFB deadlock
fix and its segment-ordering fixture.

xboxrecomp v0.18.0 was released on 2026-11-20. The rebase preserves #286's
RAM-mirror names in the external DSP. Its cached translation fallback remains
for low image/stack names, which are not aliased, and for all low names when
aliases are disabled. Patch 0033's unmeasured fixed APU-frame priority is
retired rather than stacked on #341's new pacing. Patch 0041 preserves the
upstream split, address-order and reprotection rules while tracking allocation
lifetimes; 0042 retains both the upstream clear/present test and local
diagnostics. The provisional review tree
`c549a8ebd6ca5bca31072737153e811014cd1ec2` remains historical and is not the
expected toolkit tree.

The preceding v0.17 supported generation `20261113-214800-12924697` is a Release build on
runtime tree `3e3ae3600802d2bf4446d86fc546322d60849047`. It translated all
22,295 identified functions with zero failures, six unresolved direct-call
targets and 11 untranslated instruction sites. Its current 37,578,240-byte
executable SHA-256 is
`f07ab1b80f34331eb2484c4c75f198cb7fb7726174cdf3eb2fce83dd84eb7d69`;
the 36,474,880-byte PDB SHA-256 is
`9ce6039972b27a5a9c1e173a5f91a82300d10def0a096e8c688d05702c0bb48d`.
It predates the new pin and is not a v0.18 generation. This clean generation
retains explicit title-side caller layouts for three
PFB sites found incomplete by the texture-index campaign, adds the one-core
guest-thread title default, and rejects both alias options at active internal
scales. The scheduling/default selection reports 64 passed; the focused
alias/rejection selection reports 24 passed; the broader WARP/hardware
depth/stencil/glare selection reports 42 passed; and the framework tree
verifies. The current fail-closed executable has no retail run. The preceding
alias-capable build from the same generation, executable SHA-256
`57e63f097924d4c051d4d6565c8999e720c7c0e53edcdc76df1ae6659884859a`,
supplies the 4.5x missing-player evidence below. Its
`20261113-160606-ad4c0e30` predecessor's partial
retail repeat completed three clean attempts before the fourth locked during
Loading. All 64 reviewed, movement-contained PFB/PUT intervals have enclosing
callers, confirming the mapping on title execution. The only completed A/B
pair is not timing-valid, so no throughput result is accepted.

Campaign generation `20261111-084222-7bdb6e56` used the same runtime tree.
Before the runtime-only Phase 4 fix, a codegen-identical executable completed
one audio-off, four-thread CPU run and one D3D11 run through the scripted
tutorial world, stationary hold and movement window without retry. The
post-deadlock 37,575,168-byte executable at
`132bece507c1773c41eeaa6549687d2c493a121c2c102452a10465d5a584f073`
then completed the two fixed-tree Phase 4 smokes. Its later 37,578,240-byte
texture-index executable at
`d02f6dec418a19399626808567fd578c0e142aa531a2048e09a1191cab280e79`
completed the first six-attempt exact linear/indexed campaign. That campaign
is fully reviewed but remains strict-inconclusive because its historical PFB
records lack enclosing callers and one of three cadence pairs regressed. The
caller-corrected generation's retained `desktop9` repeat reviewed 3,574 raw
captures from three completed attempts and confirmed complete caller
attribution, but run 04 locked before readiness and pair 01/02's final
runtime input differed by 1,656 ms. The operator closed the repeat rather
than scheduling another six attempts, so the title-speedup question remains
inconclusive. The
same source line includes default-off `RECOMP_PB_INDEX_RUNS=1`; its
scalar/run executor state and both walker paths pass synthetic equivalence,
but it has no matched rendered-output or performance result. The earlier
`20261111-002138-a9dbc18a` generation remains the exact xemu-matched
reference because the reference comparison predates the integrated pipeline.

The Loading lock is traced to the title loader's zero-delay guest-worker
poll. The earlier `RECOMP_GUEST_CPUS=one` default passed four bounded boots
but did not eliminate the race: the third attempt of the first no-capture
D3D11/Vulkan campaign again stopped at two flips and zero draws while ordinal
99 dominated roughly one billion kernel calls. That campaign is invalid and
remains stopped.

Local framework patch 0047 now adds default-off
`RECOMP_ZERO_DELAY_SLEEP=1`; Brute Force selects it together with one-core
guest affinity. The focused producer/worker fixture passes with the option
and deliberately fails its backoff assertion without it. Rebuilt executable
`f717c74980d81e506272f71f7c3135ebd68017d90753cb783d0e64060d9ffc7a`
completed three no-retry D3D11 startup checks: first movie draws arrived at
6.787, 7.329 and 7.232 seconds, and the operator-confirmed run reached Basic
Controls and completed its stationary interval. This enabled the reduced
two-run renderer diagnostic recorded above; it is not long-session
reliability or performance evidence. Do not restart the closed texture-index
campaign or reuse the interrupted renderer sequence.

The accepted default and visibility performance baselines still use old
generation `20261103-145850-21e3091e` on tree
`25a2a713738b7aabb1c21ef6720765ff93ccffea`. Do not compare those measurements
numerically with either current-generation campaign. The texture-index
campaign is a separate same-generation alternation with its own listed
controls and limitations.

sp00nznet/xboxrecomp#297, sp00nznet/xboxrecomp#298,
sp00nznet/xboxrecomp#299 and sp00nznet/xboxrecomp#300 merged in v0.17.0;
local patches 0001, 0005 and 0035 were removed. Issue
[sp00nznet/xboxrecomp#301](https://github.com/sp00nznet/xboxrecomp/issues/301)
closed after v0.17.0 corrected the raw `XC_VIDEO` encoding. Previously selected
sp00nznet/xboxrecomp#314, sp00nznet/xboxrecomp#319 and
sp00nznet/xboxrecomp#320 were validated on v0.17 and are now included in the
v0.18.0 base; their separate cherry-picks have been removed.
sp00nznet/xboxrecomp#315 remains unselected. Although GitHub marks it merged,
v0.18.0 reverted that merge before release because its native Windows 16-bit
`IDIV` test failed, consistent with the earlier 2,643 signed 16-bit mismatch
result.

The v0.18 base-acceptance and focused-bring-up completion gate is met. The
exact native xemu Basic Controls match remains against v0.17 CPU and D3D11
captures; that same-generation controlled comparison identifies the packed
stencil alias as the mechanism that removes the large brightness difference.
The next gate is to extend that evidence through the sun-facing and
end-of-opening-fade states and verify E1-E2 ownership in gameplay; the old
generation remains only the reviewed measurement baseline. Patch 0037's
one-line Windows raster diagnostic correction is the strongest small upstream
candidate, followed by the software-interrupt ABI fix, immediate-entry guard,
firmware-exit half of patch 0032, patch 0013, and the current-main half of
patch 0019. Patch 0007 is held until its accounting covers or deliberately
excludes every exported allocator. Do not file another PR, amend an existing
PR or comment upstream without approval.

A 2026-11-20 Blood Wake recheck confirmed the earlier refresh from the
recorded adaptation baseline
v0.2.4 at `e631edddfe56d22344e9f696923aeb6d78a9e7ba` with public v0.3.3 at
`6292340705d9b863351a45ba160174a346a81688`, ten commits later. The recorded
checkout remains on `e631eddd`; the newer revision was reviewed only as prior
art and still fingerprints xboxrecomp v0.16.0. Public `main` and v0.3.3 have
no newer commit; closed issue #6 added no implementation or verified cache
cause. No code was imported. The
independent foreground-only display-awake policy is now implemented and
synthetic-tested behind a default-off switch. Shader-preparation diagnostics
are bounded and opt-in; actual precompilation and native fixed-function work
still need title evidence and parity fixtures. Audio, latency, and HDR work
remain deferred. Vulkan and Direct3D 12 are now an accepted, gated renderer
migration; the explicit Vulkan base, visibility, CPU-transformed pixel
pipeline and first near-matched CPU-presented retail capture milestones are
implemented, but this is not a broad parity or performance claim. See
[the Vulkan review and bring-up](FINDINGS.md#vulkan-feasibility-review-and-integrated-bring-up-2026-11-20),
[the Blood Wake review](FINDINGS.md#blood-wake-v025-v033-refresh), and the new
[UPSTREAM candidates](UPSTREAM.md#performance-and-gpu-back-end-candidates).

## Current boundary

- Two private xemu 0.8.139 sessions acquired and verified the exact target
  window, completed the retail Campaign route, and produced native 640x480
  screenshots through xemu's own Screenshot action. Basic Controls is
  content-matched to fresh CPU and D3D11 captures. The packed-stencil path
  reduces the fresh D3D11/xemu over-8/255 difference from 43.4320% to
  3.7119%; the single-run pair is correctness evidence, not a performance
  comparison. Stationary, turned and upward references are retained locally;
  a confirmed sun-facing view and end-of-opening fade remain an operator
  checkpoint. Split-screen reference capture and layout work are explicitly
  deferred and are not an active completion gate. Later unattended retries
  were stopped after focus failures and an undismissed Basic Controls overlay
  invalidated their camera-sweep labels; do not spend additional full boots
  on the remaining active states without an operator confirming each capture. See
  [FINDINGS](FINDINGS.md#native-xemu-reference-and-packed-stencil-match-2026-11-11).
- Default-off presenter-source capture links Windows GDI and CPU-upload
  D3D11 submissions to verified flip records in synthetic tests, including
  PVIDEO composition. In two short, unmatched retail probes it verified one
  submission per route; remaining submissions were no-client or occluded.
  The successful submissions may predate minimization. It rejects direct
  shared-GPU capture and does not establish physical scanout, visibility or
  matching guest-snapshot pixels. Matched-scene validation and benchmark
  integration remain open; see
  [FINDINGS](FINDINGS.md#presenter-source-capture-alignment).
- The previous stationary campaign measured about 3.2 rendered frames per
  second on the CPU and 30.4 on D3D11 with GPU vertex programs, three runs
  each. The hold is visually static, so these are **rendered**, not distinct,
  frames. Native 30 Hz remains a hypothesis.
- The user reported about 6 fps while moving with default D3D11 settings.
  The previous reviewed Release baseline was rebenchmarked with three
  reviewed camera-pan runs, fixed input, no retries and complete
  flip-ID-aligned labels. It averaged 27.349 guest-surface world changes/s,
  even with detailed counters, caller profiling and continuous captures.
  This explicitly fails to reproduce a current 6 fps default result; it does
  not identify the historical cause. Matched CPU-reference correctness remains
  open. See
  [FINDINGS](FINDINGS.md#default-d3d11-movement-regression-2026-10-30).
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
  not met. The CPU remains the reference; the launcher and graphical
  `build.py` runs now default to D3D11.
- Common swizzled, linear, and P8 textures now decode on the GPU; compressed
  textures with stale pitch state can upload as BC blocks. The compute
  decoder matches the CPU decoder on synthetic WARP and hardware cases.
  `RECOMP_GPU_TEX_DECODE=0` restores the CPU decoder for comparison.
  The D3D11 cache now compares source and palette snapshots on every use;
  the former approximate per-frame/sparse checks require
  `RECOMP_GPU_TEX_CACHE_APPROX=1` and can miss rewrites. Synthetic WARP and
  hardware fixtures cover same-frame and sparse-blind-spot changes. The
  memory and runtime cost of the new default has not been isolated.
- P8 metadata and the framework CPU decoder now preserve programmed
  32/64/128/256-entry lengths; invalid indices fail deterministically rather
  than reading beyond the palette. Renderer inputs use checked snapshots,
  allocation/mapping identities and, optionally, explicit suballocation
  bounds. Compatible colour aliases reconcile guest changes, and a bound
  render target is sampled from an ordered GPU copy; unsafe GPU-dirty overlaps
  are rejected. Synthetic guard, reuse, reset and alias cases exercise these
  paths; actual NV2A out-of-range behavior, unregistered suballocations and
  concurrent-write atomicity remain unresolved. These results do not establish
  retail or hardware equivalence.
- Renderer-device loss is reported and stops execution instead of silently
  completing outstanding GPU work. Mocked removed/reset/hung/driver and
  pending-query cases pass. The separate presenter's loss and resource
  boundaries are also fault-injection tested; actual device removal,
  recovery and semaphore races remain unverified.
- Ordered asynchronous visibility reports pass synthetic WARP and hardware
  fixtures. A new six-leg campaign isolated only
  `RECOMP_GPU_ZPASS=async`, reviewed all 6,903 selected captures, and found
  no visible visibility artifact or asynchronous query/device error. Async
  improved mean reviewed world cadence from 27.401 to 30.606 changes/s and
  the mean 1% low from 13.757 to 15.697/s. G3 fell from 69.367% to 65.467%.
  The 1% low remains below 24/s and G3 remains far above 10%, so async stays
  opt-in. One synchronous boot stalled after flip 12 and succeeded on retry;
  all three asynchronous attempts completed. These are matched guest-surface
  scene reviews, not CPU-reference or physical-presentation equivalence.
- The first exact linear/indexed texture-cache campaign completed six clean
  attempts with 15 dispatched and runtime-acknowledged inputs each. Pair
  dispatch/runtime timing stayed within 94/140 ms, below its 250 ms review
  tolerance. Review covered all 7,412 raw captures on 50 contact sheets and
  selected 7,379 movement-bounded frames; every exact HUD-free ROI transition
  changed. Indexed lookup reduced 82.495 linear probes/lookup to 1.230 and
  measured lookup time from 0.0486 to 0.0336 ms/flip while retaining full
  source/palette comparison. Mean cadence increased from 30.284 to 31.562
  world changes/s and the 1% low from 16.418 to 17.751/s, but pair deltas
  were +5.431, -3.459 and +1.862/s. Sampled G3 was unchanged at 0.003% and
  NV2A busy increased 0.660 points. All six historical runs contain
  zero-caller PFB intervals, so the strict result and G3 gate remain
  inconclusive; G2/G4/G5 are also open. Current generation
  `20261113-160606-ad4c0e30` fixes the three observed stack layouts. Repeat
  the campaign on that generation before claiming a speedup or starting the
  separate index-word comparison.
- Reviewed movement manifests now report world-change cadence percentiles and
  a 1% low only after complete real-image labels, consecutive flip alignment,
  HUD-region validation and presentation-source review. The profiler summary
  no longer treats every game-thread kernel wait as GPU time:
  `--profile-callers` adds waits whose first executable caller is one of the
  two known guest GPU polls, while unrelated and unattributed waits remain
  separate. The final rebuilt default campaign reviewed 3,258 gameplay
  captures and measured 26.847-27.713 world changes/s with
  13.571-14.360/s 1% lows. The captures are aligned guest-surface evidence,
  not proof of physical presentation. G3 was 68.8-69.5%, dominated by
  `sub_00372180`; both G2 and G3 fail.
- Nine periodic intervals wholly inside those reviewed ranges measured
  0.787 ms/flip readback, 4.327 ms/flip visibility waiting,
  9.336 ms/flip `guest_word` validation, 9.639 ms/flip renderer
  readable-range validation and 0.832 ms/flip exact texture comparison.
  `VirtualQuery` dominated both validation totals. The counters add QPC and
  atomic overhead and run on different threads, so these figures are not
  additive frame-time components.
- The post-index `MEM_WRITE_WATCH`/guarded-page candidate is closed without
  implementation. Windows write watching rejects the file-mapped RAM and
  contiguous/tiled alias views that the framework requires, while per-view
  protection cannot prove physical-page cleanliness across those aliases.
  Keep exact source and palette comparison; reconsider only with a generic
  framework write-notification and lifetime contract, not another retail run.
- The final-generation cold/warm pixel-shader-cache campaign measured
  12.914/25.543/30.623 ms p50/p99/max across 96 cold compile/create events
  and 0.143/0.517/0.535 ms across 96 warm lookup/create events. Movement
  averaged 35.743 cold and 35.990 warm flips/s, with effectively unchanged
  p99 frame time, so the win is startup hitch removal rather than steady-state
  throughput. Publication passed in all three pairs and existing failure
  fixtures. Title captures were visually matched but not byte-identical or
  frame-locked, so strict output identity remains open and the cache stays
  opt-in.
- Phase 4's three separately selectable mechanisms are implemented behind
  `RECOMP_PFB_FLUSH_WAIT`, `RECOMP_NV2A_IDLE_WAIT`, and
  `RECOMP_NV2A_SEGMENTS`. The first combined smoke exposed a real deadlock:
  the segment queue kept PFB busy while the executor synchronously waited for
  a PGRAPH software-trap acknowledgement, but the guest could not service the
  interrupt until its PFB poll returned. A segment-only run reproduced the
  same boundary as a 60-second stall after flip 12,611. Patch 0045 now permits
  PFB acknowledgement while a PGRAPH trap is pending without advancing GET,
  completing the segment or clearing the interrupt. The fixed tree passed
  exactly one no-retry segment-only smoke and one no-retry all-three smoke;
  both reached reviewed tutorial movement with the character and HUD visible
  and no Basic Controls panel, cinematic, solid-black sample, queue wait or
  overflow. These are bring-up checks, not distinct-world FPS or an A/B/A
  result. The previously planned 24-run Phase 4 matrix—four separate
  baseline/candidate comparisons for flush, idle, segment and all-three, with
  three runs per side—is explicitly deferred and is not active work or a
  completion gate. The switches remain default-off, and no G2–G5 or
  performance claim follows from the smokes. Guard/reset failures remain
  evidence to investigate, not permission to bypass the handshake.
- Both variants of the new movement campaign logged unsupported texture
  format `0x2E` (`LIN_D24S8`). Its effect on the visible image is not yet
  established; the CPU reference also lacks a decoder for that format.
- Experimental GPU depth aliases cover exact matching D24S8 surfaces,
  with checked guest-memory initialization, clean-owner reseeding and
  conservative rejection of conflicting writes and overlaps. This is
  synthetic coverage, not a complete guest-write or readback contract.
  An initial combined
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
  frames also show heavily bloomed vegetation and sky. The user also reports
  that the bloom starts as the scripted opening ends and gameplay begins,
  while the vignette/letterbox bars fade; that timing does not establish
  whether the fade causes it.
- The user later supplied live-window screenshots of nearly the same view
  with a bright HUD-visible image and a darker HUD-absent image, and reports
  that mouse movement with the window active can make the HUD and glow
  disappear together. The images differ in size and focus and are not
  frame-locked. Report-time dump timing alone does not explain the live
  observation.
- On hardware D3D11, a synthetic zero-depth glow and HUD can disappear
  together when the legacy programmable near plane changes; the existing
  screen-zero experiment keeps both visible and still clips a negative-depth
  control. Patch 0029 adds a HUD-safe mode that keeps ordinary and
  alpha-blended zero-depth UI visible while clipping zero-depth additive
  draws; positive-depth additive geometry remains visible. CPU and WARP
  fixtures pass, and the launcher now selects it by default. This is a
  targeted response to the user's 1440p UI/bloom report, not yet a reviewed
  retail result or proof of original-hardware behavior.
- A traced bright pass samples the prior GPU depth allocation as texture
  format `0x41`. The opt-in `RECOMP_GPU_STENCIL_ALIAS=1` path reconstructs
  its packed depth and stencil on the GPU. A synthetic replay of the traced
  combiner shows that stale zero stencil can brighten its output, while a
  higher GPU stencil threshold suppresses it; actual gameplay stencil values
  and original-hardware fidelity remain unverified. The CPU rasterizer
  ignores stencil.
- The opt-in `RECOMP_GPU_DIRECT_PRESENT=1` path presents a finished GPU
  snapshot through a one-slot keyed-mutex handoff, with CPU fallback for
  pinned frames and PVIDEO. WARP and hardware synthetic tests cover the
  alias, pixel transfer, queue ordering and fallback. Submitted renderer work
  is now limited to two event-query batches, with checkpoints every 128
  operations or at flips. Default-off clean-surface readback elision avoids
  copies only when native-size 32-bit guest bytes are already coherent;
  tracked-surface flips still wait for GPU completion, and initial seeds,
  guest uploads, dirty, scaled, 16-bit and diagnostic frames still read back.
  This is not general on-demand readback.
- State-aware depth/stencil revision tracking avoids reconversion for
  read-only draws and rejects ambiguous, partial, scaled and recognized
  mirrored-RAM overlaps rather than sampling stale guest bytes. Potentially
  writing draws may invalidate even if no fragment writes; pixel-level
  write detection is not established. Synthetic device-loss injection
  covers producer and Windows presenter fail-stop boundaries; automatic
  recovery and real-driver loss races remain open.
- The reported blank 4K Microsoft, Digital Anvil and intro movies were
  CPU-decoded Bink frames, not PVIDEO. D3D11 readback had overwritten those
  guest writes with a GPU-only clear. The backend now shadows guest colour
  surfaces, mirrors full clears and uploads CPU changes before a GPU overlay
  or flip. Scale-1 and 4.5x captures show movie content; the 4.5x runs
  reported zero guest/GPU conflicts while submitting scaled shared frames.
  An independent unobscured swap-chain capture and matched gameplay remain
  open.
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
- One subsequent hardware D3D11 run enabled the packed alias alone and left
  shared presentation off. Its stationary image differed from the earlier
  combined candidates in only 20–473 pixels beyond 8/255, versus roughly
  145,000 pixels from each alias-off baseline. This supports the alias as
  the main source of that stationary brightness change, but does not prove
  which image is correct or isolate the opening fade.
- A null-backend surface-guard run aborted on the GPU semaphore after a
  guarded range covered its page. A new explicitly reported page exclusion
  permitted a completed movement diagnostic, but leaves that page unobserved.
  The report recorded seven non-NV2A reads within an estimated depth range;
  offline symbolization provisionally points to a USB rumble-report read,
  but the original module base and actual resource ownership remain unknown.

## Direction (2026-10-30)

By the user's direction the current rendering and frame rate are accepted as
a working baseline. The next goal is playable quality: fewer render
artifacts, internal upscaling so the game can run at 4K, and audio. The G1,
G2, G3 and G4 gates in [PERFPLAN.md](PERFPLAN.md) are not formally met; the
acceptance is a judgement, so keep them as regression guards. Do not trade
the baseline away while working on the items below.

The four workstreams are largely independent. Audio touches only the APU and
host output, so it can proceed alongside the renderer work. Upscaling should
follow the artifact inventory (A1), because a larger render target makes any
existing state-tracking error harder to separate from a scaling error.
True widescreen is a separate correctness feature: it can reuse the physical
surface and shared-presentation foundation, but it must not be folded into
the internal-resolution scale or treated as a framebuffer stretch.

The user selected xemu as the rendering reference and wants mipmapping at 4K.
Audio is default-on. The xemu DSP path was implemented, but the user's
crackly, mono-sounding dialogue/gunfire report failed its listening gate;
Simple stereo fold-down is now the recommended default. The licence decision
is recorded under "Decisions" at the end.

Blood Wake remains the main prior-art source, as it was for the renderer. The
adapted-code baseline is the checkout in `.deps/bloodwake-recomp` at
`e631edddfe56d22344e9f696923aeb6d78a9e7ba`; the latest reviewed source is
v0.3.3 at `6292340705d9b863351a45ba160174a346a81688`. Both use the older
xboxrecomp v0.16.0 base, not Brute Force's current v0.18.0 tree. Follow the
AGENTS rules when adapting it: copy only generic code, preserve the applicable
notice, add new adaptations to `THIRD_PARTY_NOTICES.md`, and name the exact
source revision in the UPSTREAM entry. Its title-specific hooks, addresses
and Remastered features do not apply. Its `xemu_dsp` directory is
GPL-2.0-or-later; adopting it also means choosing this project's licence
(see C4).

## Work cadence

Implement substantial related work as a milestone, then build and run the
relevant synthetic fixtures together. Use a single alternating A/B/A campaign
of at least three runs per variant on the same generation to validate the
combined result. `--scene movement --capture` collects the stationary hold,
movement, profiles, and flip-time captures in those same runs. Do not run a
separate campaign after each small patch; use targeted diagnostics only to
resolve failures. Never trade away the CPU reference or guest-visible flush,
semaphore, report, and flip ordering.

## A. Remaining render artifacts

**Known so far** (observations are in the boundary above; causes are open):
bloom and over-bright vegetation and sky that begin as the scripted opening
ends, a much darker stationary background with the packed depth/stencil
alias, black-frame intervals and a missing character in some runs, an
unsupported `LIN_D24S8` (`0x2E`) texture format, and 149 distinct unhandled
GPU methods in the CPU-path census.

1. **Inventory before fixing.** Instrumentation is implemented and
   synthetic-tested in patch 0025: `RECOMP_GPU_AUDIT=1` observes CPU and GPU
   routes without a dummy backend, including early drops, submissions,
   rejections, software culls, clears, flips and methods by subchannel.
   The benchmark now owns a unique `gpu_audit.csv` in every enabled attempt,
   records the exact audit controls in that attempt's metadata, and rejects a
   missing or incomplete post-window snapshot instead of accepting a
   success-shaped run. Synthetic retry, disabled, malformed and native-fixture
   cases pass.
   Patch 0044 adds CPU post-clip triangle counts, separately identifies GPU
   pre-clip submissions, preserves unknown visibility, and correlates dropped
   primitive types with a bounded recent-method history. It also reports
   CPU-path `0x2E`/`0x41` bindings without claiming a texture was sampled.
   Frame traces no longer reuse stale CPU vertices for GPU programs.
   A fresh v0.17 scripted
   run produced a complete post-window report with 4,486,537 requests,
   including 1,106 unsupported line requests with 4,424 vertices, 161
   distinct unhandled methods and 22 mirror-only methods. It also produced
   CPU/D3D11 stationary and movement captures, but its scenes were not
   independently reviewed and no xemu frame was matched. The older minimized
   run remains partial. A matched operator-reviewed run still needs to rank
   unhandled and mirror-only methods and compare CPU/GPU/xemu captures rather
   than treating method counts as defects. See
   [FINDINGS](FINDINGS.md#unmatched-retail-gpu-audit-and-vertex-probe)
   and [UPSTREAM item 28](UPSTREAM.md#performance-and-gpu-back-end-candidates)
   for report semantics and limitations.
2. **Partly met: use xemu as the reference (decided).** The CPU path models no
   stencil, so it cannot settle the brightness, stencil and bloom questions.
   Run the same disc in xemu and capture matched states: the tutorial
   stationary hold, the sun-facing and turned views, and the end of the
   opening fade. The disc image and every capture stay local. Matching is
   not automatic: xemu's resolution, filtering and frame timing differ, so
   run it at the native 640x480 without upscaling or extra filtering, match
   by content rather than wall-clock time, and record the xemu version, its
   settings and the capture points in FINDINGS. Where our output and xemu's
   differ, list and classify the difference; do not assume xemu is perfect
   either. `tools/compare_frames.py` now reports neutral equal-size
   comparisons. Native stationary, turned and upward captures exist, and
   Basic Controls is matched without resizing, alignment or color correction.
   The remaining reference work is a confirmed sun-facing frame, the
   end-of-opening fade. Treat these as an operator checkpoint: unattended
   retries that cannot verify the visible state are not evidence and should
   not be repeated. Native split-screen references and layout classification
   are deferred and do not block A2.
3. **Mechanism established at Basic Controls; broader brightness, bloom and
   HUD validation remains.** Use hardware D3D11 and the existing user
   captures first to investigate why the HUD and glow can disappear together.
   Match the view and capture actual programmable vertex/clip state before
   attributing this to the near plane; GPU-program traces now label submissions
   as pre-clip with post-clip output and visibility unknown. Isolate the
   screen-zero plane and packed alias
   separately, and check the stencil threshold and packed-depth conversion
   against real gameplay values. A fresh same-generation pair with every
   other control fixed reduced xemu differences above 8/255 from 43.4320%
   with packed aliases off to 3.7119% with them on. This identifies stale
   packed stencil as the large matched-panel brightness cause, but unsupported
   untracked/concurrent guest writes, partial overlaps, scaled depth-texture
   aliases and other scenes still block a default. The attempted exact scaled
   packed path passed a scale-two synthetic glare replay but failed the fresh
   4.5x retail check: two alias-on boots omitted the player at the untouched
   spawn, while the otherwise matched alias-off control showed the player.
   Current builds reject both alias options at active internal scales. Keep
   native-resolution brightness work separate; do not reopen the scaled path
   without a fixture that covers the affected full render sequence. Explain
   and minimize any further gameplay run. Do not add a global gamma or
   brightness adjustment.
4. **Display gamma ramp (hypothesis not supported by the matched panel).** Blood Wake found that
   the toolkit's NV2A registers are plain memory, so the title's write of a
   gamma ramp to the video DAC was lost and every frame was shown as if the
   ramp were the identity. Blood Wake's missions fade in from black through
   that ramp (`bw_gamma.c`). Brute Force's opening fades its letterbox bars
   and the bloom begins at that moment, so a ramp the title sets and the port
   drops is one candidate. A static scan found code that builds three
   256-byte tables and conditionally calls a D3D-section DAC-port writer.
   One isolated, unreviewed startup-to-heuristic-hold run logged one entry,
   before the first recorded flip, with the NV2A base and an identity ramp;
   no later entry was logged by that probe. This does not rule out other
   writers or establish the ramp at the matched opening fade. The opt-in
   probe's snapshots are not atomic and do not prove completed writes or
   displayed pixels. The native xemu match is reproduced by packed-stencil
   reconstruction without a gamma adjustment, so this hypothesis is not the
   current cause. See
   [FINDINGS](FINDINGS.md#static-gamma-ramp-investigation). Obtain matched
   runtime evidence before changing presentation. Only if the title writes a
   relevant ramp, apply its own ramp at present behind a switch, as Blood Wake
   does; that differs from a global adjustment.
5. **Black frames and the missing character.** Synthetic-tested classification
   distinguishes reviewer-labelled intentional fades/solid frames from
   unexplained black, dark, solid and unchanged intervals. Movement collection
   is explicitly inconclusive and exits 2; a separate complete review needs
   a real world change and presented-frame validation.
   A zero black-hash count is not proof of visible world or character.
   Review a matched movement campaign; do not infer fades from darkness.
6. **`LIN_D24S8` (`0x2E`).** The default-off, unscaled path now snapshots
   eligible guest spans and allocation identities, reseeds clean owners and
   rejects conflicts, unsupported layouts and partial overlaps rather than
   sampling stale bytes. Synthetic fixtures cover changes, guards, lifetime
   reuse, clears, rollback and scaled rejection. Same-value and unchecked
   concurrent writes, guest reads, unknown suballocations and general alias
   semantics remain open. The CPU reference still cannot decode `0x2E` and
   does not model stencil. Validate matched gameplay/reference states and
   establish whether `0x2E` affects visible pixels before considering a
   default; never treat this conservative detection as synchronization.
7. **Points, lines and other dropped primitives.** Synthetic audit fixtures
   now preserve primitive/input counts and bounded, temporal method context;
   they do not prove a dropped request was visible or that a method caused it.
   No generic line or point rasterization was added without independently
   verified NV2A semantics. Rank the recorded line-list requests and method
   routes in matched scenes; adapt Blood Wake's optional point contract only
   after visible use and semantics are established.
8. **GPU window clipping.** Expanded CPU, WARP and hardware fixtures cover
   multiple rectangles, incomplete state, AA and render scaling, base and
   vertex paths, clears, and depth/stencil discard side effects. CPU stencil
   is unmodeled, so that side effect is checked on GPU separately. Keep the
   switch off by default until matched gameplay and reference evidence settle
   reset semantics and regressions.

*Done when:* the A1 inventory lists every difference from the reference with
a cause or an accepted reason; no recurring black interval or missing
character appears across a movement campaign; and the bright/dark behaviour
is explained and settled against the reference. Keep G4 open until then.

## B. Internal upscaling for 4K (opening movies validated; gameplay pending)

**Design source (observed in `.deps/bloodwake-recomp`).** Blood Wake's
Remastered mode chooses one render scale `S`, in physical pixels per guest
screen pixel, from the output picture height divided by 480, optionally times
a supersampling factor (`bw_vfx.c`). Its presets are 720p = 1.5, 1080p =
2.25, 1440p = 3 and 4K = 4.5 (`docs/SETTINGS.md`). Guest surfaces are
allocated at the scaled physical size and are recreated when the scale
changes. Scissor, clear rectangles and window clips map from logical to
physical pixels. A finished frame goes through `bw_vfx_post.c`'s final pass
into the output (an exact copy when the scale matches, a box average when it
is larger, Catmull-Rom when smaller). Anything the guest reads is averaged
back down to its own size. MSAA, anisotropic filtering and the other effects
are separate switches and are not part of this item.

**Current milestone.** The Blood Wake-derived generic scale system now lives
in `src/gpu/render_scale.c`. Colour surfaces retain logical and physical
dimensions; depth follows the physical target; scale epochs resize native
output; GPU passes seed resized targets, resolve guest-sized readback and
produce exact/box/Catmull-Rom shared output. Patch 0028 lets the existing
keyed-mutex presenter accept source dimensions independent of the guest
framebuffer while preserving flip IDs, pinned/PVIDEO fallback and capture
refusal. Scale 1 remains the launcher default. The main executable and
fixtures build. The synthetic D3D11 matrix passed 234 cases with hardware
coverage enabled, and 22 launcher and three presenter cases passed. Bounded
scale-1 and 4.5x runs retained CPU-decoded opening movies, including the intro
montage. One integrated 4.5x D3D11 attempt now reached the scripted movement
window without retry, logged a 2880x2160 4:3 render size, and produced reviewed
sample gameplay frames without a panel, cinematic, missing character or
solid-black image. Mipmaps and 8x anisotropy were enabled, but the bounded
runtime diagnostic observed only a tutorial texture with no additional levels
declared, so the run does not establish lower-level sampling. It was not a
matched or A/B/A campaign, and the 3440x1440 display cannot validate native
4K output; G4/G5 remain open.

1. **Implemented: add the scale to the D3D11 back end.** Adapt the generic parts only:
   the single place that decides the scale, the surface sizing, the epoch
   that triggers resizing, and the final scale pass. Skip Blood Wake's
   texture restoration, HUD-edge, AO and HDR features. Keep widescreen
   isolated in workstream E. Provide `RECOMP_GPU_SCALE=<f>` or an output
   size, default 1. At scale 1 the output must be unchanged from today, bit
   for bit.
2. **Implemented: keep readback at the guest's size.** The back end normally
   reads each presented surface back to guest memory at every flip for the
   CPU consumers; scaled frames always do so. At 4K
   output, the centered 4:3 picture is 2880×2160; a four-byte-per-pixel read
   would be about 24.9 MB per flip. Average the scaled surface down on the
   GPU first and read that. Separately, present the full-size image directly.
   This extends the existing shared presentation path. The default-off
   clean-surface experiment above is a limited unscaled exception.
   CPU-written colour surfaces are detected against a logical shadow and
   uploaded before that readback/presentation boundary.
3. **Current choice: one global Blood Wake-style scale; validate it per
   surface.** The traced frame renders
   into 640×480, 320×240 and 160×120 colour surfaces that are then sampled,
   plus a 512×512 depth-only pass. Screen-sized targets scale. Whether the
   downsample chain, the glow composite and the 512×512 pass should scale
   is **unverified**. The current implementation scales all tracked colour
   and depth targets, folding out guest AA factors. Hypothesis: any effect
   that uses texel offsets or
   kernel sizes in guest units must stay consistent with the surface it
   samples. Verify on the title with the A1 inventory before choosing.
4. **Implemented fail-closed boundary: reject scaled depth/stencil
   aliases.** Scaled `LIN_D24S8` sampling and guest-depth seeding remain
   undefined. An exact packed `0x41` experiment passed scale-two glare and
   dimension fixtures, but the 4.5x retail spawn comparison omitted the
   player with the alias on and showed it with the alias off. The renderer
   and launcher therefore reject both switches at non-1 scales. Do not
   restore the packed exception until a fuller render-sequence fixture
   reproduces and prevents that regression.
5. **Partly implemented: quantities measured in pixels.** Z-pass visibility counts scale with the
   pixel count, so normalise them or the title's thresholds will misfire
   and are now normalized per query segment using its starting
   physical/logical pixel-area ratio in both sync and async paths, including
   target and scale changes. Confirm the title's thresholds.
   Check point size and line width, which the title may specify in pixels,
   and the half-texel offsets in screen-space draws.
6. **Implemented controls and handoff.** The existing resizable window and
   swap chain fit a 4:3 picture. The renderer queries that live picture for
   Native mode; fixed diagnostic output dimensions are also fitted to the
   4:3 picture, rather than treated as the picture itself. Fixed 720p,
   1080p, 1440p and 4K presets are in the launcher.
   Workstream E owns true widescreen; 480p 4:3 remains the default here.
7. **Partly implemented: memory and robustness.** Budget VRAM for scaled targets and reject
   unsupported scales or allocation failures with a clear error; do not
   silently reduce the scale. Initialization and live native changes now
   prepare required resources before committing the configuration or epoch;
   ordinary allocation failures retain prior resources and can be retried.
   Busy shared frames defer replacement without skipping the mutex handshake;
   non-timeout mutex ownership errors stop execution. Colour
   contents migrate, but changed-size depth/stencil contents do not; guest
   reinitialization after resize is not yet verified. A VRAM budget and device
   recovery remain open in D; fixed launcher preset changes require a restart.
8. **Synthetic hardware and one retail bring-up implemented; matched gameplay pending.** Synthetic WARP and hardware fixtures: scale 1 matches the
   current output exactly; scale 2 downsampled matches scale 1 within a
   stated tolerance on a fixture with draws, partial clears, window clips
   and render-to-texture; readback is at guest size; aliases and visibility
   counts behave as designed. The CPU rasterizer stays at scale 1 as the
   reference, so G4 comparisons are made at scale 1. WARP now covers scale
   two with draws, render-to-texture, partial clears, window clips,
   guest-sized readback, CPU-written movie frames and normalized sync/async
   visibility. Focused transactional fixtures cover injected initialization,
   colour, depth, shared-frame, readback and staging failures; retries;
   output-only changes; and mixed-AA query segments on WARP and hardware as
   applicable. Hardware also exercises a hidden live presenter resize and its
   flip-only fallback. These are synthetic, not a title run. Bounded earlier
   4.5x hardware captures show all three opening movie stages. Add a
   controlled matched gameplay and live-resize campaign, including evidence
   that depth/stencil is reinitialized. The integrated 4.5x movement attempt
   establishes only successful bring-up and representative visible gameplay.
   A focused scale-two packed-stencil glare experiment and exact/mismatch
   fixtures passed, but the completed fresh 4.5x correctness check overruled
   them: the alias-on spawn omitted the player while the alias-off control
   showed it. The current build rejects the combination. Continue ordinary
   scaled gameplay and live-resize validation with aliases off; scaled alias
   work is blocked on a reproducing full-sequence fixture, not another retail
   retry.
9. **Recast G5.** "G1 holds at 2× internal resolution" becomes a measurement
   at 1080p and 4K presets, A/B/A against scale 1, reporting distinct world
   frames and the GPU, driver and scale used. Record results in FINDINGS.

10. **Mipmaps and filtering for scaled modes: implemented experimentally;
    reference and retail gates open.** `RECOMP_GPU_MIPS=1` uploads declared
    swizzled, P8 and BC chains for 2D and cube textures and exposes their
    valid levels to D3D11 sampling. `RECOMP_GPU_ANISO=1` separately opts
    eligible mipped samplers into 8x anisotropy. Both default off; the CPU
    reference and the disabled D3D11 path remain at level zero. Linear
    textures and render-target aliases remain level-zero-only; depth/stencil
    aliases reject multi-level declarations. These paths report diagnostics;
    no levels are generated. Synthetic WARP and hardware
    fixtures cover subresources, filtering, fractional clamps, bias,
    rewrites, guarded ranges and cube edges. See
    [FINDINGS](./FINDINGS.md#experimental-mipmap-sampling-synthetic-evidence-2026-11-11)
    for the evidence and the unresolved xemu/NV2A clamp interpretation.

    Next establish the retail title's supplied chains and compare identical
    scale-1 frames against xemu and the CPU reference with the switches off
    and on, including mip transitions and aliases. Resolve the 4.8 clamp
    interpretation and declared cube-face spacing against stronger reference
    evidence before considering a default change. Generate levels only if
    evidence shows the console samples levels the title did not supply.
    Measure texture-cache cost, VRAM and distinct-world-frame performance
    with controlled A/B/A runs at 1080p and 4K; native 4K-display and
    matched retail gameplay remain external gates.

*Done when:* the 4K preset runs a scripted movement campaign with the output
matching the scale-1 picture on average once downsampled, no scale-specific
artifacts that the fixtures do not cover, and G5 is recorded.

## C. Resolve game-audio quality

**Implemented and verified**

The detailed retail observations in this section use the earlier framework.
The v0.18 default Simple path passed bounded CPU and D3D11 Basic Controls
smokes without an underrun or starvation diagnostic; Accurate, listening and
long-session acceptance remain open.

- Audio is default-on; `--no-audio` and `RECOMP_AUDIO=0` are explicit off
  paths. The launcher exposes On/Off and Accurate/Simple choices.
- The default Simple path preserves the stereo all-mixbin fold-down. Accurate
  remains an experimental option that runs the title's GP and EP programs
  through the vendored xemu v0.8.139 DSP56300 C interpreter.
- Merged framework #298, formerly local patch 0035, preserves the two
  channels produced when HRTF filters a mono source. Its synthetic negative
  control produced `+1/+1`; the fixed path produces `+1/-1`.
- RAM-mirror physical names distinguish heap and contiguous-window pages by
  default; the legacy no-mirror fallback remains ambiguous before an entry is
  observed. The dedicated DSP worker is signaled and joined before
  interpreters, shadows, queues, and caches are released.
- Firmware-return and shutdown routes run full APU cleanup before
  `ExitProcess`; bounded WAVs now receive an exact RIFF header.
- Xboxrecomp v0.17.0 includes former selected PR #264 commit
  `eb7b1f5833205a24fcc0eda6eb4ed2df4f2bc2e3` for the observed 2,048-sample
  sequencer restart hold, and merged #261 is current-base behavior. Open #266
  remains unselected; #286 is merged in v0.18, and closed #270 was not
  adopted because its distinct failure mode was not observed.
- Six repeated native init/shutdown cycles passed without leaked process
  handles. Accurate and fold-down retail exit captures finalized correctly.
- The v0.17 host policy used eight 512-sample buffers, a three-quarter refill
  threshold, and a high-priority APU frame worker. Eight buffers and the
  refill threshold remain, but the fixed frame-worker priority is retired on
  v0.18 pending measurements. The last long then-default Accurate probe ran
  164.47 seconds from startup through
  tutorial/gameplay; it, an earlier 115-second transition, and a 260-second
  tutorial-world run reported zero underruns, starvation events, and refused
  submissions.
- A private 155-second accurate capture covered startup, cinematics/menu,
  tutorial loading, and scripted gameplay. Every marked interval was
  non-silent, no samples clipped, and no 32-frame, 256-frame, or near-2,048
  paired-hole candidates were found.
- The 164.47-second Accurate capture repeated those interval checks, reached
  the 421-draw tutorial-world signature, accepted fixed movement inputs, and
  remained non-silent and hole-free in every marked phase.
- The user reports crackly, mono-sounding dialogue and gunfire in Accurate
  mode while music sounds acceptable. Matched opening intervals have
  left/right correlation near 0.88-0.89 under Accurate versus 0.16-0.21 under
  Simple, without corresponding host underruns or 32/256-sample boundary
  discontinuities. This fails the Accurate listening gate and motivates the
  default change; it does not identify the faulty DSP/voice operation.
- The user then reported that the prior Simple build still sounded
  mono/centred for dialogue and effects. Initial long traces were still on
  the main menu, not in gameplay. The corrected Quick Campaign route reaches
  the rendered world and uses 3D-list voices 25-31 with HRTF bins 6-10.
- Clean fixed/baseline/fixed gameplay captures measured firing-window
  correlation `0.415/0.542/0.422` and side-to-mid RMS ratios
  `0.641/0.544/0.636`. All three selected runs had zero starvation,
  underruns, refused submissions, clipping, and bounded 32/256-frame holes.
  This validates the channel-routing change but cannot replace listening.
- The user's gameplay listening pass found the fixed Simple path improved but
  not fully correct. The remaining symptom was not characterized before the
  user deferred further audio work.
- The earlier Release executable rebuilt with the then-local patch 0035. The standalone
  `apu_vp` fixture and toolkit-tree verification passed; focused
  audio/build/launcher tests passed 144 cases, and the integrated selection
  passed 274 with 365 hardware/platform cases skipped.
- The final Release executable was relinked after the default change. A
  five-second retail boot with `RECOMP_APU_DSP` unset selected stereo
  fold-down; the six-cycle DSP lifecycle fixture and the 639-test integrated
  hardware-enabled selection passed.
- Licensing is recorded in `LICENSE`, `THIRD_PARTY_NOTICES.md`,
  `src/xemu_dsp/NOTICE.md`, and `LICENSES/`. Project glue derives from Blood
  Wake's MIT code; the vendored interpreter is GPL-2.0-or-later except its
  LGPL-2.0-or-later DMA files. A linked executable is distributed, if at all,
  under GPL-2.0-or-later as a combined work.

**Remaining validation**

1. **Deferred:** resume with a focused listening report for the fixed Simple
   build (dialogue, gunfire, panning strength/direction, crackling, missing
   cues, balance, and ambience) and isolate the remaining symptom.
2. If Accurate is pursued further, isolate its independent narrow/crackly
   output in GP mixbins, EP processing, or FIFO routing; compare matched
   captures rather than host counters alone.
3. Record distortion, missing or duplicated cues, dialogue/music balance,
   stereo reversal, spatial/reverb behavior, and cutscene synchronization.
4. Repeat host-continuity probes only if the listening route reaches a
   materially different audio-heavy scene or exposes a symptom.
5. The mono-HRTF correction merged as
   [sp00nznet/xboxrecomp#298](https://github.com/sp00nznet/xboxrecomp/pull/298)
   and local patch 0035 was removed; do not bundle follow-up changes with the
   external-DSP series. The remaining 0030-0034 changes still require their
   generic pieces and policy changes to be split before upstream review.
   Compare #341 pacing without the retired 0033 policy against a controlled
   priority/MMCSS alternative before considering its restoration.
6. Do not import Blood Wake v0.3.0's native DSP, WASAPI path, or bit-exact hot
   loops while audio is deferred. If work resumes, first assess its
   off-thread device-rebuild and MMCSS policy against the existing XAudio2
   reference, then consider native DSP only with interpreter lockstep and the
   existing GPL obligations.
7. Do not cherry-pick draft sp00nznet/xboxrecomp#344. Brute Force explicitly
   selects eight buffers, so its proposed six-buffer Windows default would not
   alter this title's path. Repeat host-continuity and listening checks on
   v0.18's merged #341 before changing queue or worker policy.

*Done when:* after audio work resumes, a human listener signs off the fixed
Simple scene list above, the Accurate defect is either fixed or remains
explicitly experimental, and at least one additional audio-heavy gameplay
scene stays free of host underruns if it differs materially from the validated
Quick Campaign route.

Automated captures establish valid transport and bounded signal properties;
they cannot establish that dialogue, effects, music, or reverb sound correct.
Stereo remains the acceptance target.

## D. Remaining pipeline and compatibility work

The directly implementable texture, P8, checked-copy, diagnostic and
display-awake candidates below are synthetic-tested; their external
correctness and measurement gates remain open. The complete D3D11 matrix
passed 394 cases with WARP and the RTX 5070 hardware adapter; the framework
memory, decoder, PFB and display-awake fixtures passed 17 cases, and the
separate synchronization, presenter, pushbuffer and toolkit suite passed 61.
These are synthetic results, not a title comparison.

- The current-generation guest surface-read diagnostic is complete. One
  no-retry null-backend attempt reached 104 stationary flips, completed the
  40.01-second movement phase, resolved two accesses inside an accumulated
  colour range to generated title code, and recorded zero dropped sites. The
  stationary timing gate was incomplete but did not truncate the cumulative
  access report. The same range overlaps staged pushbuffer memory, page
  `0x80001000` remained excluded, ranges are never retired and no depth read
  recurred. The static framework-consumer inventory remains: flip
  publication, CPU/GDI/SDL upload, pinned/PVIDEO composition, dumps,
  statistics and presenter capture consume the CPU path, while
  render-to-texture and visibility queries do not require whole-surface
  readback. Treat the result as a requirement to retain readback, not proof of
  a live title surface read. Do not repeat the same diagnostic unless its live
  lifetime/access model changes.
- Extend the opt-in shared GPU presenter only after establishing guest
  surface lifetimes, intercepting arbitrary guest accesses and defining a safe
  on-demand-readback contract. The bounded
  renderer queue and clean-surface-only copy elision are synthetic-tested;
  neither makes dirty surfaces generally GPU-authoritative. Retain readback
  for unproven readers and diagnostic consumers. Do not release fences before
  their inputs are consumed or skip a flush or flip hold.
- Inspect the slow combined candidate's high sampled GPU-wait share, the
  frequent busy shared slot and remaining packed-depth conversions. The
  current state-aware guard is conservative for potentially writing draws;
  prove fragment-level changes before attempting to reuse those conversions.
- Checked texture and colour-shadow reads replace their repeated
  page-query-then-dereference validation; colour-clear mirrors and readbacks
  use checked, exception-guarded writes. Guard, reprotection, tracked
  allocation reuse, logical registration, mapping reset and observed
  colour-shadow races have synthetic coverage. GPU-owned short-pitch colour
  aliases and freed tracked surfaces are rejected; a layout replacement with
  uncertain ownership or a failed/partial write cannot be sampled until a
  successful full clear or readback. A failed tracked readback stops the process
  before the flip completes.
  Concurrent reprotection can still consume a newly installed guard or cause a
  partial write after the preflight.
  `guest_word` and
  depth-seed validation still use their existing mechanisms. Rebenchmark the
  current implementation on one generation with matched scene/input and
  alternating A/B/A runs; retain an uninstrumented baseline because detailed
  counters add overhead.
- Ordinary texture descriptors now use a 4,096-bucket exact index instead of
  scanning as many as 2,048 entries. A synthetic invariant fixture covers
  exact fields, collisions, rewrite/rekey, failed-upload discard relocation
  and full-cache LRU replacement. Exact source and palette snapshots remain
  mandatory. The first matched campaign attributes a reduction from 82.495 to
  1.230 probes/lookup and from 0.0486 to 0.0336 ms/flip lookup time, but does
  not establish repeatable cadence improvement: one pair regressed, its
  historical PFB interval callers are incomplete, and G2/G4/G5 are absent.
  The caller-corrected partial repeat confirms 64/64 attributed PFB/PUT
  intervals and again shows scan removal (80.476 versus 1.239 probes/lookup),
  but its only A/B pair fails runtime-input matching and the fourth attempt
  locked before readiness. The operator closed the repeat as inconclusive.
  Do not claim a speedup or use its unbalanced diagnostics to select another
  cache policy.
- Retained complete audit generation `20261111-002138-a9dbc18a` counted
  73,787,237 `ARRAY_ELEMENT16` words and 115,904 `ARRAY_ELEMENT32` words.
  Their 147,690,378 expanded indices exactly match `ARRAY_REQUESTED` and
  `ARRAY_BUFFERED`; there was no `DRAW_ARRAYS` method row. This establishes
  material dispatch volume, not performance or reviewed scene identity.
  Patch 0046 adds default-off `RECOMP_PB_INDEX_RUNS=1` for eligible
  non-incrementing subchannel-zero runs in both selected walkers. It preserves
  packet, PUT, segment and physical-wrap boundaries, final method state and
  exact index capacity; GPU audit and all ineligible paths remain scalar.
  Scalar/run executor state, CALL, wrap, packet/segment split, staged-walk,
  audit and invalid-value tests pass. Before promotion, compare matched
  CPU/D3D11 rendered frames and run a separate reviewed six-attempt A/B/A
  campaign with `RECOMP_GPU_TEX_INDEX=0` held in both variants. Do not combine
  this measurement with the texture-index candidate or infer speedup from the
  census.
- The supported compiler profile remains the ordinary Release build. Separate
  `ltcg` and `avx2` Release profiles now build in isolated directories, apply
  only to title-executable sources, cannot be combined, and carry exact
  metadata. The multi-binary benchmark alternates default/candidate
  executables and rejects runtime-switch changes or unstable hashes. Both
  candidates compile, but neither has been started or matched against retail,
  CPU/D3D11 output or performance. Run default/LTCG and default/AVX2 as
  separate reviewed campaigns only after the higher-priority texture-index
  and index-word gates; keep AVX2 optional because it narrows compatibility.
- The fixed segment-only and all-three bring-up smokes reached reviewed
  movement once each without retry. The 24-run isolation/combined campaign is
  deferred; do not schedule the three poll/event pairs or the idle, segment
  and all-three pairs as current work. Retain the documented caller-profile,
  reviewed-world and matched-control requirements if the campaign is resumed.
  Keep all three mechanisms default-off because G2, G3 and correctness gates
  remain unmet.
- Validate the checked cache, explicit logical-bound registrations, alias and
  layout-replacement behavior and framework P8 decoder on the title and
  additional drivers;
  determine NV2A out-of-range palette behavior and an ordered guest-write
  contract. Unknown suballocations cannot be inferred, and snapshots alone
  do not serialize concurrent writers. Compare cache cost with an A/B/A
  campaign before upstreaming. Implement and test device/resource recreation
  for both devices without
  violating pending flush, semaphore, report or flip ordering. The synthetic
  fail-stop paths are not recovery or real-driver fault coverage. Mipmaps are
  now wanted for 4K (B10); LOD-transition enhancements beyond that remain
  deferred.
- Exercise `RECOMP_GPU_VSH_VERIFY=50` on matched title scenes. WARP and
  hardware synthetic fixtures now cover raw and gathered SHORT3/4, UBYTE3/4
  and packed-normal inputs, relative constants, and culling modes and strip
  winding against the CPU interpreter and renderer. A fully culled GPU batch
  now invalidates the previous verification result and logs its skipped
  readback. An unreviewed title diagnostic sampled every hundredth batch
  without reported vertex differences through its last report; neither that
  probe nor the synthetic fixtures establish title-scene or NV2A culling
  fidelity.
- Investigate the small differences between content-matched cold/warm
  stationary captures and measure gameplay compile stutter. Synthetic and
  title cold/warm timing is recorded in FINDINGS; Blood Wake's `bw_pscache.c`
  is prior art. Vertex and other shaders are not cached. Blood Wake v0.3.1
  now observes every vertex program through a title-specific creation wrapper
  and precompiles raw, gathered, point, and fixed-function variants during
  loading. Reuse the cache/preparation concept only through Brute Force's own
  routes and identities; do not copy its wrapper addresses or shader groups.
- The reproduced Phase 4 loading hang was a segment/PGRAPH/PFB dependency
  cycle and is fixed on the current tree. Retain the bounded PFB-wait,
  shader-preparation and verification-skip diagnostics for any unrelated
  loading hang; they provide evidence without satisfying a wait or changing
  shader-worker ordering.
- The independent `RECOMP_DISPLAY_AWAKE=1` candidate requests a display hold
  only for a visible foreground, non-minimized host and releases on background,
  minimize or close. Synthetic API-substitution covers failures and window
  transitions; hosted/standalone real-window validation and launcher policy
  remain open. Recheck upstream overlap and coordinate before preparing any
  contribution; do not file or comment without approval.

## E. True Hor+ widescreen (camera diagnostic implemented; mode pending)

**Static evidence.** The complete source review is in
[FINDINGS](FINDINGS.md#blood-wake-feature-review-and-true-widescreen-investigation)
and the proposed ownership boundary is in
[RENDERING](RENDERING.md#proposed-true-widescreen-boundary). Blood Wake
uses coordinated Hor+ projection, culling, world visibility, projected-marker
correction, ordered 2D/movie markers, selective wide targets, centred guest
readback and wide presentation. Brute Force has a conventional perspective
builder and camera aspect/tangent state that appear suitable, but their
runtime roles and all culling paths are not yet classified.

**Safety gate:** no Hor+ mode is enabled or exposed. The existing trace does
not identify a gameplay-only caller or a safe full-screen view. It also does
not classify split screen; split-screen widening is explicitly deferred, so
an initial mode must remain off or use the existing 4:3 path whenever it
cannot establish one supported full-screen view. A matrix-only change cannot
provide the requested 4:3 fallbacks or preserve title culling. The presenter
reports only a fitted 4:3 size, and its shared frame can subsequently be
rejected in favour of CPU pixels.
Implementing a wide mode requires a coherent per-frame picture/resize epoch
and fallback contract as well as operator-classified title evidence; do not
treat a requested aspect or a candidate viewport as proof of the displayed
picture. The new synthetic fixtures cover trace state preservation for
perspective-shaped and unchanged matrices and demonstrate the presenter's
4:3 size collision; they do not implement or validate Hor+ or a fail-closed
single-view eligibility guard.

1. **Implemented; retail coverage pending: diagnostic-only camera trace.**
   `BRUTEFORCE_CAMERA_TRACE=1` wraps the central perspective builder and
   records bounded JSON lines containing its caller, entry arguments,
   input/output matrix, and explicitly labelled candidate aspect, tangent,
   viewport and camera-owner fields. The wrapper always calls the generated
   body and leaves its resulting guest state unchanged. Limits and sampling
   are validated, and synthetic direct/indirect dispatch and
   state-equivalence cases pass. The current supported generation contains
   this manual-source state; regenerate after any future trace change because
   the manual-source fingerprint participates in generation provenance.
   One fresh-generation scripted run sampled 774 records through call 7,731.
   It observed only caller `0x0006BE90`, four parent return classes and six
   candidate camera states; all matrices were readable and changed across the
   generated call. Caller `0x0006D240` did not appear. Runtime coverage still
   needs operator-classified gameplay, menus, movies and in-engine cutscenes.
   Split-screen caller and layout classification are deferred and are not an
   E1 gate. The trace does not alter the matrix or establish ownership by
   itself.
2. **Establish title ownership before choosing hooks.** Confirm where each
   gameplay camera's aspect and horizontal/vertical tangent are finalized,
   which routine rebuilds dependent frustum planes, and whether another
   camera class bypasses that path. Identify object, static-world/grid and
   effect culling that can reject newly visible side geometry. A projection
   patch is not complete without this.
3. **Add one aspect authority.** Plan an opt-in `RECOMP_*` control for 4:3,
   16:9, 64:27/21:9 and display matching, with 4:3 the default and invalid
   input rejected. Keep `K` independent of `RECOMP_GPU_SCALE`; CPU rendering
   remains 4:3. Bind it to the picture and resize epoch actually eligible
   for presentation, and define how a later shared-frame rejection remains
   consistent with projection and the 4:3 fallback. Decide an ultrawide cap
   only after side-region validation.
4. **Implement title Hor+.** Preserve vertical FOV, widen horizontal
   projection and matching culling by `K`, and correct world-to-screen
   markers. Keep addresses and structure knowledge in `src/`, not a framework
   patch. Validate every camera class found by E1.
5. **Classify 2D in stream order.** Locate verified HUD/text flush and movie
   submission functions. Emit title-tagged inert methods around only those
   draws. Add a generic executor/backend marker callback with a synthetic
   order fixture; reset backend classification at every flip. Explicitly
   classify full-view fades, flashes and scene-copy backdrops.
6. **Widen only proven screen targets.** Census the title's colour and depth
   surfaces across the same scenes. Apply `K` only to device-sized targets
   whose role is established; leave glow/downsample, shadow, reflection and
   other off-screen targets on the existing `S` rule unless evidence says
   otherwise. Use a per-view x remap to pillarbox protected 2D.
7. **Preserve guest and presenter semantics.** Resolve the centred 4:3 region
   into guest memory and existing CPU-facing captures. Publish the full wide
   source through shared presentation. Generalize the framework's current
   4:3 output-size query without changing its default and retain the
   CPU/PVIDEO/pinned fallbacks.
8. **Validate in gates, not by appearance alone.** First run 4:3 A/B/A and
   require unchanged output and behavior. Then run at least three alternating
   16:9 and 21:9 runs on the same generation, scene and scripted input.
   Compare the 4:3 centre against the reference and inspect the added side
   regions for missing objects, pop-in, effects, shadows and invalid
   geometry. Exercise menus, all movie paths, fades, HUD, projected markers,
   and cutscenes. Split-screen is outside the current acceptance matrix; do
   not claim support, and keep the wide mode off or on the existing 4:3 path
   for unsupported multi-view scenes. Report distinct world frames separately
   from flips and do not weaken flush, semaphore, report or flip ordering.
9. **Defer HUD-edge relocation.** A centred 4:3 HUD is the first correctness
   target. Moving selected panels to outer corners is optional title layout
   work after the base mode passes.

*Done when:* for the supported single-view scenes, 4:3 remains unchanged, the
16:9 and 21:9 centre matches the reference within the existing renderer's
listed differences, newly visible side geometry survives matching title
culling, HUD and movies retain their shape, and guest readback remains 4:3.
Split-screen support is a deferred follow-up, not part of this gate.

## F. Vulkan-default renderer migration (gathered GPU-VSH implemented)

The 2026-11-20 design in
[RENDERING](RENDERING.md#vulkan-first-backend-architecture-and-bring-up-status) replaces
the earlier Vulkan/D3D12 deferral. The explicit Vulkan 1.3 base, precise visibility,
CPU-transformed full-pixel-pipeline and gathered-input GPU-VSH milestones are
implemented. The backend reaches and
near-matches the retail Basic Controls state through mandatory readback and
CPU upload. This is not a default change or broad retail parity result.
D3D11 remains the current launcher and graphical `build.py` default, and the
CPU renderer remains the correctness reference. Native Vulkan presentation,
direct raw-stream vertex fetch, matched wider-scene/stability validation,
D3D12 and production `auto` remain open.

The upstream shape follows
[sp00nznet/xboxrecomp#306](https://github.com/sp00nznet/xboxrecomp/issues/306):
grow official `Nv2aBackend` incrementally, keep full renderers downstream for
now, forbid per-draw GPU round trips, document callback threading, and keep
presentation as a host service rather than backend window ownership. Merged
[sp00nznet/xboxrecomp#319](https://github.com/sp00nznet/xboxrecomp/pull/319)
already supplies the reusable fake-backend contract fixture. Vulkan is the
downstream exemplar used to prove each generic extension and presentation
requirement. The long-term goal is an optional, default-off upstream Vulkan
backend PR after the contract is accepted, parity passes, maintainers agree
to take a full renderer, and the user explicitly approves opening it.

The target fresh graphical policy is `auto`: complete Vulkan initialization
before guest execution, then try D3D12 only when Vulkan is unavailable or
fails startup initialization. Explicit `vulkan`, `d3d12`, and `d3d11`
selections fail closed without falling through. If both automatic candidates
fail, stop rather than silently changing to D3D11, CPU, or `null`. Device
loss after guest work begins is fatal; reconstructing GPU-authoritative state
for live backend switching is outside this workstream.

1. **Adopt the upstream contract baseline (complete for this milestone).**
   Reuse #319 for callback order,
   exactly-once delivery, borrowed-data copies, callback threads and backend
   registration/replacement/removal. Add focused project fixtures for the
   uncovered selector lifecycle: Vulkan success, Vulkan-to-D3D12 fallback,
   both-backends-fail, explicit no-fallback choices, no partial registration
   and teardown. The project lifecycle fixture and Vulkan failure injection
   now cover this scope; production `auto` remains gated on F8-F9.
2. **Shape extensions for official `Nv2aBackend` (pending).** Do not upstream local
   patch 0016 as one wholesale seam. Separate version/capabilities, durable
   failure, texture stages and combiners, vertex programs, visibility and
   other required state into optional, synthetic-test changes. Keep a narrow
   adapter for the current D3D11 implementation and rebase it as accepted
   pieces land.
3. **Extract only proven renderer-neutral code (in progress).** Move immutable texture and
   surface layouts, checked snapshots, cache keys, scale calculations, frame
   identities, and shader inputs out of D3D11-specific ownership one piece at
   a time. Existing D3D11 fixtures must remain unchanged after each move.
   Devices, resources, descriptors, pipelines, commands, queries and render
   fences stay backend-specific; window and swap-chain ownership stays in
   the host presentation service. Checked surface layouts, immutable texture
   storage/span calculations and allocation-stable guest snapshots are now
   shared by D3D11/Vulkan. The default stage-zero-times-diffuse combiner
   constructor is also shared. Focused texture-layout coverage and the
   unchanged D3D11 regression suite guard the extractions. Cache resources
   and synchronization remain backend-specific.
4. **Pin shared shader and API dependencies (complete for the current Vulkan shaders).** Use one pinned DXC version to
   produce SPIR-V for Vulkan and DXIL for D3D12; retain D3DCompiler for the
   transition D3D11 path. Pin and verify Vulkan headers and distributable DXC
   artifacts, record licences, and require no end-user Vulkan SDK. Include
   backend, compiler, target, flags and binding ABI in shader-cache identity.
   Vulkan-Headers 1.4.363.0 and DXC v1.9.2609 are pinned and verified; nine
   base/clear/extended/VSH shaders build as Vulkan 1.3 SPIR-V. Cross-backend
   generated-shader cache identity remains future F7/F8 work.
5. **Bring up Vulkan explicitly downstream (base, visibility, transformed pixel pipeline, gathered GPU-VSH and opt-in mips complete).** The
   loader/device/queue, validation diagnostics, BGRA8 surfaces, clears, base
   draws, exactly snapshot-checked 2D/cube texture cache, depth/stencil,
   RGB565 conversion, compatible full/self/subregion color-target aliases,
   eight bounded command slots and mandatory guest readback are implemented
   behind `RECOMP_GPU=vulkan`. CPU-transformed extended batches add four
   2D/cube texture stages, up to eight general combiners, the final combiner,
   alpha test and precise visibility reports. Eligible `draw_vsh` batches
   execute a static NV2A interpreter from gathered inputs; compute readback
   verifies its executor-format outputs. `RECOMP_GPU_MIPS=1` additionally
   exposes supplied swizzled, P8 and BC 2D/cube chains with programmed
   filtering, clamps and bias; aliases remain level zero. NVIDIA and AMD
   synthetic validation passes. Validation-enabled NVIDIA retail runs reach
   Basic Controls and capture a near-matched frame without validation or
   renderer errors. Clean CPU/PVIDEO surface writes now survive readback, and
   reviewed movement captures retain the same tutorial scene and scripted
   camera route on D3D11 and Vulkan. Direct raw-stream fetch, scaling, native
   presentation and wider parity remain F7 scope.
6. **Prototype host-owned Vulkan presentation (pending).** `Nv2aBackend` produces and
   reads back surfaces; it never owns or pumps the window. The host service
   owns the platform Vulkan surface, swap chain, resize, VSync, final present,
   CPU/PVIDEO fallback and frame identity. Define a separate tested handoff
   for the completed image and synchronization without making D3D11/Vulkan
   interop a requirement. Coordinate with the unanswered presentation part
   of [sp00nznet/xboxrecomp#312](https://github.com/sp00nznet/xboxrecomp/issues/312).
7. **Reach Vulkan parity and PR readiness (in progress).** Precise visibility
   restored D3D11-like draw/vertex volume but left 149,590 of 307,200 pixels
   over 8/255. The CPU-transformed four-stage register-combiner path reduces
   that to 134 (0.0436%); only eight tutorial-crop pixels and 126 pixels
   outside it exceed 8/255. Component review places that sparse over-threshold tail in exposed
   animated world content and finds none in three opaque panel regions; the
   remaining one-to-six-LSB panel differences have no isolated cross-API
   cause. Wider-scene profiling now shows that CPU interpretation consumed
   about 58.7% of NV2A-thread samples, satisfying the evidence gate. The
   gathered Vulkan interpreter and compute verifier are implemented and
   cross-vendor-clean; one unreviewed route improves gameplay cadence to
   26-39 flips/s and removes the interpreter from the sampled hot list.
   The first matched movement pair is now fully reviewed: 2,192 classified
   captures contain continuous world changes and the same scene structure,
   with no unknown frames, panel, cinematic, fade or missing geometry. The
   guest-surface source and continuous-capture perturbation make this
   correctness evidence rather than performance or physical-presentation
   proof. The manual Vulkan route now shows the opening and reaches Basic
   Controls after clean guest-surface writes stopped being overwritten.
   Investigate the remaining `0x80000000` staged-memory conflict through the
   generic color-DMA/lifetime gap; do not change the conservative GPU-wins
   policy. The first no-capture same-build A/B/A attempt set was invalidated
   by the pre-movie loader poll on run 3 and must not be reused. The
   default-off framework zero-delay backoff now passes its focused fixture
   and three no-retry D3D11 startup checks. The replacement one-run-per-
   renderer diagnostic completed with matched controls and input dispatch,
   but Vulkan movement flips were 63.75% below D3D11 while stationary flips
   were 12.29% below. Do not schedule six manual runs for this diagnostic.
   Parse the existing `[PERF-VULKAN]` counters and use one Vulkan-only
   counter run to attribute draw setup, vertex copies, texture work,
   command recording, readback, waits and reports before deciding whether
   direct raw-stream fetch is needed. Reserve a multi-run A/B/A campaign for
   M5 after native presentation, and only when its acceptance evidence is
   actionable.
   Continue covering menus,
   movies, fades, PVIDEO and CPU fallback without weakening synchronization.
   The mip fixture is complete; repeat its retail comparison only with
   reviewed target-scene readiness because the first legacy attempt entered
   Join Deathmatch.
   Extract only
   approved contract, helper, fixture and documentation pieces into small
   upstream candidates. When the contract and host presentation boundary are
   accepted, request explicit approval to propose the optional Vulkan backend
   itself. Until then the renderer remains an upstream-quality downstream
   exemplar.
8. **Implement D3D12 against the same contract (pending).** Reach the same state,
   readback, ordering, scaling and presentation coverage, with WARP fixtures
   where applicable. D3D12 remains explicit until its parity gate passes.
9. **Promote `auto`, not an incomplete backend (pending).** Compare matched CPU,
   D3D11, Vulkan and D3D12 frames for the opening, tutorial stationary and
   movement states, menus, movies, HUD, fades, render-to-texture, PVIDEO and
   CPU fallback. Exercise available NVIDIA, AMD and Intel Vulkan drivers,
   force every startup-fallback branch, migrate launcher settings without
   overwriting saved explicit choices, and log the selected backend and any
   rejected candidate. Only then make `auto` the fresh graphical default.
10. **Retain D3D11 through both parity gates (active constraint).** Keep it selectable for
   regressions and field comparison after the default switches. Retirement
   requires a later explicit decision and is not part of F9.

*Done when:* Vulkan and D3D12 both meet the shared synthetic, matched-frame,
native-presentation, synchronization and failure gates; automatic startup
fallback is deterministic and tested; current saved choices survive
migration; the CPU reference and D3D11 comparison path still work; and any
performance result follows the same-generation A/B/A protocol with distinct
world frames reported separately from flips.

## Decisions (settled 2026-10-30; updated 2026-11-20)

1. **Rendering reference:** xemu, run on the same disc at matched states (A2).
2. **DSP direction:** the xemu v0.8.139 DSP is implemented. Project-owned
   code is MIT; the linked combined executable is GPL-2.0-or-later, with the
   component exceptions and notices recorded in the repository (C).
3. **Mipmaps:** wanted at 4K; the earlier deferral is lifted for scaled
   modes (B10).
4. **Audio:** keep stereo and audio default-on. Simple is the recommended
   default after Accurate failed the dialogue/effects listening gate; retain
   explicit Off and experimental Accurate modes. Simple listening remains a
   quality gate (C).
5. **4K target:** use the RTX 5070 for 3840×2160 output and captures, with
   a 2880×2160 4:3 picture (4.5× 640×480). The active display is 3440×1440,
   so this does not establish native 4K-display validation (B9).
6. **Scale failures:** reject unsupported scales and allocation failures
   explicitly; fixed preset changes require restarting, while Native follows
   output-size changes through the scale epoch (B7).
7. **Widescreen:** pursue genuine Hor+ with matching culling and protected
   2D, not stretched output. Keep it default-off, D3D11-only and separate
   from internal resolution until E's gates pass.
8. **Split screen:** defer native references, camera/layout classification,
   Hor+ behavior and performance validation. It is not an active A2, E or
   performance completion gate, and no split-screen support is claimed.
9. **Phase 4 campaign:** defer the 24-run flush/idle/segment/all-three A/B/A
   matrix. The two fixed-tree smokes remain bring-up evidence only; all
   switches stay default-off and no performance gate is waived.
10. **Renderer API direction (updated 2026-11-20):** target Vulkan as the
    fresh graphical default with D3D12 as an automatic startup-only backup.
    The title still submits NV2A pushbuffers, so both implementations sit
    behind official `Nv2aBackend` and separately tested optional extensions,
    following the incremental contract chosen in sp00nznet/xboxrecomp#306
    rather than upstreaming the local extended seam wholesale. Presentation
    remains a host service: the backend produces and reads back surfaces but
    does not own the window or swap chain. Vulkan is the downstream exemplar
    and eventual optional upstream-backend PR target after maintainer and
    user approval. Both backends must reproduce current surface ownership,
    mandatory guest readback, render scaling, native and CPU-fallback
    presentation, submitted-work bound, visibility, flip, PFB and semaphore
    ordering. Explicit selections never fall through; runtime device-loss
    switching is excluded. Keep D3D11 selectable through both parity gates
    and keep CPU as the correctness reference. No graphics-API performance
    gain is assumed; the dominant measured PFB flush wait is API-independent.
11. **Texture-index campaign:** retain the first six-attempt collection and
    the caller-corrected three-run partial collection as inconclusive
    evidence. Together they prove descriptor-scan removal and confirm the
    missing caller mappings on retail execution, not repeatable
    world-cadence improvement. The operator declined another full restart;
    no repeat is scheduled. Keep independent index-word or compiler
    campaigns separate and do not weaken the input, native, CPU-reference or
    scaling gates to reuse the partial result.

Still open: access to a native 4K display. Optional 5.1 output is deferred.

## Order

1. Treat completed F1/F4 and the initial F3 extraction/F5 base backend as the
   renderer foundation. Continue shaping incremental official extensions and
   extracting only helpers proved by a second backend. Do not change the
   fresh default.
2. Continue Vulkan parity and native presentation through F5-F7. The
   integrated retail draw/flip capture, exact 2D/cube texture cache,
   compatible color-target aliases, precise visibility and CPU-transformed
   register-combiner path are complete. The retained stationary
   over-threshold differences are reviewed as exposed world animation. The
   gathered GPU-VSH path now clears the immediate gameplay-throughput blocker,
   the manual movie/menu route is visible, and the first matched movement pair
   has complete guest-surface review. Preserve the unresolved color-DMA
   conflict policy, collect a no-capture D3D11/Vulkan A/B/A diagnostic, decide
   whether direct raw-stream fetch is needed, then implement the host-owned
   native presentation handoff. Submit no
   upstream work without approval. Implement D3D12 in F8, promote `auto` only
   after F9, and retain D3D11 and CPU as F10 requires.
3. Complete A1 and the single-view E1-E2 scope, then A2-A4 as the reference
   and gamma-ramp question allow. The widescreen trace is diagnostic and must
   not change output.
4. Implement the single-view E3-E8 scope behind one default-off mode, with a
   4:3 regression gate before wide output. Keep HUD relocation and split
   screen deferred.
5. Continue B1-B10 behind switches with default scale-1 behavior unchanged.
6. Run C's human listening pass against the stable scene from A; repeat
   automated audio probes only for a new symptom or materially different
   audio-heavy scene.
7. Continue D's compatibility work without weakening audio or GPU ordering.

Update FINDINGS, UPSTREAM, PERFPLAN, ARCHITECTURE and the README in the same
change as each milestone, as AGENTS requires.
