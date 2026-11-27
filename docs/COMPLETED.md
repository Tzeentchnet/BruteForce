# Completed work

This file holds completed and closed work moved out of
[NEXTSTEPS.md](NEXTSTEPS.md) on 2026-11-23: finished generations and
campaigns, fixed defects, closed or superseded candidates, and completed
milestone steps. The text is kept as it was recorded; "current", "latest" and
"above/below" refer to the state when each entry was written. Open work,
ordering and settled decisions remain in NEXTSTEPS. Measurements and their
limits are in [FINDINGS.md](FINDINGS.md), and framework work is in
[UPSTREAM.md](UPSTREAM.md).

## Framework base, generations and campaigns

The latest full regeneration-validated generation
`20261120-163420-f9e3b4ed` predates patch 0048 and uses the preceding runtime tree
`8938ce1b635e25832539a0bc6500b2fe2fa14565`. It translated all 22,295
identified functions with zero failures,
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

## Former boundary records

Completed, superseded or closed entries moved from the NEXTSTEPS "Current
boundary" list. Entries with open follow-up remain there.

- Local framework patch 0048 adds a default-inactive, host-owned framebuffer
  presentation-provider seam. Registration is frozen before window
  initialization; provider lifecycle and presentation run on the framebuffer
  thread, output-size queries may run concurrently, shutdown waits for
  in-flight callbacks, and provider failures stop instead of silently falling
  back. Its provider and presenter-capture fixtures pass, including paced and
  unpaced results, forced CPU input, failed initialization and concurrent
  shutdown. The default-off downstream WSI consumer now adds a real Win32
  Vulkan surface and swap chain, a three-slot completed-image queue, exact
  frame IDs, resize, fitted blit, VSync and CPU fallback. Its NVIDIA and AMD
  hardware fixtures pass with validation. One validation-enabled retail run
  reached operator-confirmed visible Basic Controls through a 640x480 FIFO
  swap chain. Both movie observations disabled audio; an otherwise matched,
  unattended audio-on trace opened the Bink files and produced changing
  CPU-owned native-route frames. None of these runs is performance evidence.
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

## B. Internal upscaling

Completed steps from workstream B; numbering matches NEXTSTEPS.

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
4. **Implemented fail-closed boundary: reject scaled depth/stencil
   aliases.** Scaled `LIN_D24S8` sampling and guest-depth seeding remain
   undefined. An exact packed `0x41` experiment passed scale-two glare and
   dimension fixtures, but the 4.5x retail spawn comparison omitted the
   player with the alias on and showed it with the alias off. The renderer
   and launcher therefore reject both switches at non-1 scales. Do not
   restore the packed exception until a fuller render-sequence fixture
   reproduces and prevents that regression.
6. **Implemented controls and handoff.** The existing resizable window and
   swap chain fit a 4:3 picture. The renderer queries that live picture for
   Native mode; fixed diagnostic output dimensions are also fitted to the
   4:3 picture, rather than treated as the picture itself. Fixed 720p,
   1080p, 1440p and 4K presets are in the launcher.
   Workstream E owns true widescreen; 480p 4:3 remains the default here.

## C. Game audio

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

## D. Pipeline and compatibility

- RecompLauncher support was added on 2026-11-26: `recomplauncher.json`, a
  windowless `--prepare` setup step and headless `--play` mode behind
  `Brute Force.cmd` arguments, and a tag-triggered draft-release workflow.
  The manifest validates against the published schema with zero errors. On
  the retail generation `20261121-005939-bc768cc6`, one setup run through the
  batch file with the disc image exited 0 in 117 seconds (disc verification
  and an up-to-date incremental build), one run without the disc reused
  `game\disc` and exited 0, and one `--play` run under a 25-second watchdog
  with isolated saves booted the game and wrote its output to the requested
  log. Without arguments, the batch file still opens the launcher window. The
  focused tests are listed in [DEVELOPMENT](DEVELOPMENT.md#tests). The first
  release, `v0.1.0` (tag commit `600e091`), was published on 2026-11-26 from
  the release workflow; its 247-file zip was checked to contain the manifest
  at its top, a CRLF batch file, and no game data, binaries or logs.
  RecompLauncher itself has not installed or run it yet; open follow-ups are
  in [NEXTSTEPS](NEXTSTEPS.md#d-remaining-pipeline-and-compatibility-work).
- Settings, saves, caches and the headless play log were moved into one
  [data folder](USER_GUIDE.md#data-folder) on 2026-11-26:
  `%LOCALAPPDATA%\BruteForce`, `BRUTEFORCE_DATA`, or RecompLauncher's
  `RECOMPLAUNCHER_DATA`. Launches copy saves from the earlier
  `%LOCALAPPDATA%\xboxrecomp` once and leave the originals. On the retail
  generation `20261121-005939-bc768cc6`, one `--play --data` run under a
  25-second watchdog placed the partition images under `cache\console`,
  resolved UDATA and TDATA under `saves`, wrote its log to `logs`, and logged
  the same four failed optional-file opens as a run with the previous layout.
  Synthetic tests cover precedence, the one-time copy, explicit overrides and
  the non-ASCII fallback.
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

## F. Vulkan-default renderer migration

Completed steps, plus the F6 and F7 progress records as they stood when
moved. Open items for F6 and F7 are listed in NEXTSTEPS.

1. **Adopt the upstream contract baseline (complete for this milestone).**
   Reuse #319 for callback order,
   exactly-once delivery, borrowed-data copies, callback threads and backend
   registration/replacement/removal. Add focused project fixtures for the
   uncovered selector lifecycle: Vulkan success, Vulkan-to-D3D12 fallback,
   both-backends-fail, explicit no-fallback choices, no partial registration
   and teardown. The project lifecycle fixture and Vulkan failure injection
   now cover this scope; production `auto` remains gated on F8-F9.
4. **Pin shared shader and API dependencies (complete for the current Vulkan shaders).** Use one pinned DXC version to
   produce SPIR-V for Vulkan and DXIL for D3D12; retain D3DCompiler for the
   transition D3D11 path. Pin and verify Vulkan headers and distributable DXC
   artifacts, record licences, and require no end-user Vulkan SDK. Include
   backend, compiler, target, flags and binding ABI in shader-cache identity.
   Vulkan-Headers 1.4.363.0 and DXC v1.9.2609 are pinned and verified; nine
   base/clear/extended/VSH shaders build as Vulkan 1.3 SPIR-V. Cross-backend
   generated-shader cache identity remains future F7/F8 work.
6. **Prototype host-owned Vulkan presentation (retail bring-up in
   progress).** Local framework patch 0048 adds a versioned, copied
   `RecompPresentProvider` registration that is default-inactive and separate
   from `Nv2aBackend`. Its `initialize`, `present` and `shutdown` callbacks
   run on the framebuffer-window thread; `output_size` may run concurrently.
   The provider receives stable CPU pixels, dimensions, a CPU-required/source-
   allowed flag and publication frame ID, and returns paced, unpaced or fatal.
   Shutdown rejects new calls, waits for in-flight callbacks and runs exactly
   once. Pinned frames, PVIDEO and presenter-source capture continue to force
   CPU input. Existing GDI/D3D11 behavior is unchanged without a registered
   provider, while provider failures stop instead of silently falling back.
   The off-by-default Brute Force Vulkan consumer now owns the Win32 surface,
   swap chain, acquire/resize/VSync/final fitted blit and CPU upload fallback.
   The renderer supplies exact-frame completed images through a three-slot
   FREE/WRITING/READY/PRESENTING queue, coalesces only READY images, shares
   one externally synchronized graphics queue, and retains mandatory guest
   readback. Validation-enabled NVIDIA and AMD fixtures cover native routing,
   coalescing, forced CPU input, resize and explicit provider-before-device
   shutdown. The first integrated retail run reached visible Basic Controls
   through the native FIFO swap chain with no validation error. Its opening
   movies were not observed in two audio-off attempts. Bounded 300-frame
   counters now distinguish native,
   forced-CPU and no-exact-source CPU routes, source-drop reasons and backend
   publication suppression. Matched unattended traces showed that audio-off
   skipped the Bink files, while audio-on opened them and emitted changing
   CPU-owned frames with no WSI or validation error. The benchmark now rejects
   audio-off movie readiness. Do not require D3D11 keyed-mutex interop. Fold
   physical movie review into a later required run. The cross-vendor fixture
   now enters and restores the exact host borderless-fullscreen bounds,
   recreates the swap chain, and requires injected surface loss to return
   fatal. Test-only readback now verifies the final composed swap-chain image
   for native, forced-CPU, source-allowed CPU and fullscreen routes, including
   exact center colours and opaque-black fitted bars, on both vendors. This
   proves Vulkan image composition, not physical compositor scanout.
   Physical fullscreen review and real device loss remain open. Do not
   start the deferred M5 comparison campaign. Coordinate any upstream
   proposal with the unanswered
   presentation part of
   [sp00nznet/xboxrecomp#312](https://github.com/sp00nznet/xboxrecomp/issues/312).
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
   proof. The manual Vulkan menu route now reaches Basic Controls after clean
   guest-surface writes stopped being overwritten. The two direct movie
   observations were audio-off; a bounded audio-on trace opened both Bink
   files and remained on changing CPU-owned movie frames.
   Investigate the remaining `0x80000000` staged-memory conflict through the
   generic color-DMA/lifetime gap; do not change the conservative GPU-wins
   policy. The first no-capture same-build A/B/A attempt set was invalidated
   by the pre-movie loader poll on run 3 and must not be reused. The
   default-off framework zero-delay backoff now passes its focused fixture
   and three no-retry D3D11 startup checks. The replacement one-run-per-
   renderer diagnostic completed with matched controls and input dispatch,
   but Vulkan movement flips were 63.75% below D3D11 while stationary flips
   were 12.29% below. Do not schedule six manual runs for this diagnostic.
   The one approved Vulkan-only counter run is complete and line-order
   aligned without another game route. Its three wholly contained movement
   intervals cover 900 flips at 32.845 diagnostic flips/s. Inclusive draw
   callbacks consumed 30.57% of wall time, exact texture snapshots 17.59%
   inside them, mandatory readback 13.54%, and asynchronous report polling
   11.97%; backend gathered-vertex copies consumed only 0.56%. Detailed
   counters add overhead and the run's 32.558 movement rate rebounded from
   the pair's 17.469, so neither single run is a renderer decision.
   The first follow-up now defers visibility polls that cannot yet complete
   while retaining the 0.25 ms flush deadline, ordered completion,
   backpressure and synchronous waits. A deterministic pre-deadline/aged-
   report fixture passes with validation on both available GPU vendors; no
   extra retail route was used. Exact snapshot elision was then rejected as
   unsafe without an exact write/invalidation mechanism: checked
   `ReadProcessMemory` snapshots and post-copy layout/allocation/bounds
   identities preserve guard and lifetime behavior, while Windows write-watch
   cannot cover the framework's aliased file mappings. Defer
   direct raw-stream fetch unless the unattributed framework gather cost is
   independently shown to justify it. Patch 0048's provider seam and the
   downstream WSI/source-ownership module now pass the cross-vendor synthetic
   gate, and a native retail route reaches visible Basic Controls. The
   audio-on/off no-script trace resolves the missing-movie observation without
   another manual run. Synthetic borderless-fullscreen enter/restore and
   fail-stop surface loss now pass on both vendors. Test-only final
   swap-chain-image readback now proves native, both CPU and fullscreen
   compositions on both vendors without claiming physical scanout. Fixed
   numeric render scaling now passes a validation-enabled scale-2 child
   fixture on both vendors, including physical native-presentation sources.
   Native-mode scaling (2026-11-23) now reads the provider's fitted output at
   each flip, commits a scale epoch transactionally, resamples color
   surfaces from their old physical images, recreates depth/stencil cleared
   and passes a validation-enabled native child fixture on both vendors,
   including injected allocation failure and live provider-driven epochs.
   The first scaled retail runs (2026-11-23) then completed. Fixed scale 2
   reached a reviewed Basic Controls hold, and native mode committed four
   resize epochs during the hold and movement, all without validation
   errors. No depth artifact appeared in 52 post-resize window images; see
   [FINDINGS](FINDINGS.md#vulkan-scaled-and-native-mode-retail-runs-2026-11-23).
   Close the
   remaining automatable renderer-state gaps next; reserve a multi-run A/B/A campaign for M5,
   and only when its acceptance evidence is actionable.
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

## G. Asset formats

- G1 (done 2026-11-23): traced the game's BXML reader (`0x254860`,
  `0x255330`, `0x254C60`) and BXSD loader (`0x281FE0`). The token stream
  is word-oriented LZSS in blocks interleaved with the values, and value
  encodings come from the schema types. Gate met: `tools\assets.py verify`
  decodes all 7 schemas and all 759 nonempty descriptors exactly, with
  every block emitting its declared word count.
