# Next steps

The rendering architecture is in [RENDERING.md](RENDERING.md),
the performance plan is in [PERFPLAN.md](PERFPLAN.md), measured
results are in [FINDINGS.md](FINDINGS.md), and framework work is
tracked in [UPSTREAM.md](UPSTREAM.md). The current framework pin is xboxrecomp
v0.18.0 at `37b61d589168adee3680fabd51805ee81427bf07` with 44 local patches
and no separate selected upstream commits. Its reconstructed tree is
`23a92b43c0042297160e985b31d61308e0d15da6`. The clean patch replay and
exact reverse application, toolkit verification, and the new presentation-
provider fixture passed; focused v0.18 framework fixtures passed 32
CTests, the FCMOV/WBINVD fixtures passed nine tests, and the project DSP
memory and lifecycle fixtures passed. Retail regeneration, exact
discovery/translation comparison, and bounded one-core, audio-on CPU/D3D11
smokes passed on the preceding 43-patch runtime tree. Patch 0048 is default-
inactive; its generic seam has synthetic fixture coverage, while its
downstream Vulkan consumer later reached visible retail Basic Controls
and carried changing Bink frames in a bounded audio-on native trace. Details,
limits and the 128 MB host-mirror observation are in
[FINDINGS.md](FINDINGS.md).

Completed generations, campaigns, fixed defects, closed candidates and
finished milestone steps are recorded in [COMPLETED.md](COMPLETED.md); this
file keeps open work, ordering and settled decisions.

The latest full regeneration-validated generation is
`20261120-163420-f9e3b4ed` on runtime tree
`8938ce1b635e25832539a0bc6500b2fe2fa14565`; it predates patch 0048. The v0.18
base-acceptance and focused-bring-up completion gate is met. The accepted
default and visibility performance baselines still use old generation
`20261103-145850-21e3091e` on tree
`25a2a713738b7aabb1c21ef6720765ff93ccffea`; do not compare those measurements
numerically with current-generation campaigns. Do not restart the closed
texture-index campaign or reuse the interrupted no-capture renderer sequence.

Open from that work:

- Extend the exact native xemu evidence, matched so far only at Basic
  Controls against v0.17 CPU and D3D11 captures, through the sun-facing and
  end-of-opening-fade states, and verify E1-E2 ownership in gameplay (A2, E).
- Native Vulkan presentation parity, matched gathered-GPU-VSH output, direct
  raw-stream fetch, wider scene/stability parity and any accepted performance
  claim remain open (F). The retained `0x80000000` surface conflict is bounded
  but its exact writer is unresolved; keep GPU-wins conflicts and mandatory
  readback (F7). The subthreshold cross-API drift at Basic Controls is not
  isolated.
- Patch 0037's one-line Windows raster diagnostic correction is the strongest
  small upstream candidate, followed by the software-interrupt ABI fix,
  immediate-entry guard, firmware-exit half of patch 0032, patch 0013, and the
  current-main half of patch 0019. Patch 0007 is held until its accounting
  covers or deliberately excludes every exported allocator.
  sp00nznet/xboxrecomp#315 remains unselected; v0.18.0 reverted it before
  release. Do not file another PR, amend an existing PR or comment upstream
  without approval.
- From the Blood Wake v0.3.3 review, actual shader precompilation and native
  fixed-function work still need title evidence and parity fixtures; audio,
  latency and HDR work remain deferred. See
  [UPSTREAM candidates](UPSTREAM.md#performance-and-gpu-back-end-candidates).

## Current boundary

Completed, superseded and closed boundary entries are in
[COMPLETED](COMPLETED.md#former-boundary-records).

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
  behind the existing opt-in flag on D3D11 and Vulkan. Vulkan's base,
  CPU-transformed extended and gathered GPU-VSH paths pass the focused
  validation-enabled NVIDIA and AMD fixture. Neither change establishes
  original-hardware equivalence.
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

**Known so far** (observations are in the boundary above and in
[COMPLETED](COMPLETED.md#former-boundary-records); causes are open): bloom and
over-bright vegetation and sky that begin as the scripted opening ends, a much
darker stationary background with the packed depth/stencil alias,
black-frame intervals and a missing character in some runs, an unsupported
`LIN_D24S8` (`0x2E`) texture format, and 149 distinct unhandled GPU methods in
the CPU-path census.

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
   is unmodeled, so that side effect is checked on GPU separately. Vulkan now
   advertises the capability and passes inclusive/exclusive, incomplete-axis,
   AA-scaled, clear, depth-write, base, extended and GPU-VSH cases with
   validation on both available vendors. Keep the switch off by default until
   matched gameplay and reference evidence settle reset semantics and
   regressions.

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
in `src/gpu/render_scale.c`, with its D3D11 passes in
`src/gpu/render_scale_d3d11.c`. Colour surfaces retain logical and physical
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

1. **Complete: scale in the D3D11 back end.** See [COMPLETED](COMPLETED.md#b-internal-upscaling).
2. **Complete: readback at the guest's size.** See [COMPLETED](COMPLETED.md#b-internal-upscaling).
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
4. **Complete fail-closed boundary: scaled depth/stencil aliases are
   rejected.** Do not restore the packed exception until a fuller
   render-sequence fixture reproduces and prevents the 4.5x missing-player
   regression. See [COMPLETED](COMPLETED.md#b-internal-upscaling).
5. **Partly implemented: quantities measured in pixels.** Z-pass visibility counts scale with the
   pixel count, so normalise them or the title's thresholds will misfire
   and are now normalized per query segment using its starting
   physical/logical pixel-area ratio in both sync and async paths, including
   target and scale changes. Confirm the title's thresholds.
   Check point size and line width, which the title may specify in pixels,
   and the half-texel offsets in screen-space draws.
6. **Complete: controls and handoff.** Workstream E owns true widescreen;
   480p 4:3 remains the default here. See [COMPLETED](COMPLETED.md#b-internal-upscaling).
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

**Implemented and verified:** audio is default-on, with Simple stereo
fold-down as the default and Accurate (the vendored xemu DSP) as an
experimental option. Lifecycle, licensing, channel-routing and
host-continuity evidence is in [COMPLETED](COMPLETED.md#c-game-audio). The
v0.18 default Simple path passed bounded CPU and D3D11 Basic Controls smokes
without an underrun or starvation diagnostic; Accurate, listening and
long-session acceptance remain open. The user's gameplay listening pass found
the fixed Simple path improved but not fully correct; the remaining symptom
was not characterized before the user deferred further audio work.

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

- RecompLauncher support is implemented but unpublished; see
  [COMPLETED](COMPLETED.md#d-pipeline-and-compatibility). Remaining: push a
  first `v*` tag and publish its draft release, install that release through
  RecompLauncher's developer tools ("Add a build") and confirm setup, the
  settings page and Play, then submit the project to the catalog and point the
  README badge at its game page. Add Redump sizes and CRC32s to the manifest's
  `dumps` only after verifying them against Redump. Larger gaps against the
  Recomp Standards: accept an extracted game folder besides disc images, keep
  saves, settings, logs and the shader cache under one data folder
  (`RECOMPLAUNCHER_DATA`), and start through a native executable so the
  launcher's ReShade looks can be enabled.
- The current-generation guest surface-read diagnostic is complete; see
  [COMPLETED](COMPLETED.md#d-pipeline-and-compatibility). Treat it as a requirement to retain readback, not proof
  of a live title surface read, and do not repeat it unless its live
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
- The exact texture-descriptor index is implemented; both matched campaigns
  are closed as inconclusive (Decision 11 and [COMPLETED](COMPLETED.md#d-pipeline-and-compatibility)). Do not claim
  a speedup or use their unbalanced diagnostics to select another cache
  policy.
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

## F. Vulkan-default renderer migration (presentation provider seam implemented)

The 2026-11-20 design in
[RENDERING](RENDERING.md#vulkan-first-backend-architecture-and-bring-up-status) replaces
the earlier Vulkan/D3D12 deferral. The explicit Vulkan 1.3 base, precise visibility,
CPU-transformed full-pixel-pipeline and gathered-input GPU-VSH milestones are
implemented. The backend reaches and
near-matches the retail Basic Controls state through mandatory readback and
CPU upload. This is not a default change or broad retail parity result.
D3D11 remains the current launcher and graphical `build.py` default, and the
CPU renderer remains the correctness reference. Complete native Vulkan
presentation parity, direct raw-stream vertex fetch, matched wider-scene/
stability validation, D3D12 and production `auto` remain open.

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

1. **Complete: adopt the upstream contract baseline.** See
   [COMPLETED](COMPLETED.md#f-vulkan-default-renderer-migration). Production `auto` remains gated on F8-F9.
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
   constructor is also shared, and the render-scale configuration and size
   authority is now separated from the D3D11 resolve passes and consumed by
   both backends. Focused texture-layout coverage and the
   unchanged D3D11 regression suite guard the extractions. Cache resources
   and synchronization remain backend-specific.
4. **Complete for the current Vulkan shaders: pin shared shader and API
   dependencies.** See [COMPLETED](COMPLETED.md#f-vulkan-default-renderer-migration). Cross-backend generated-shader cache
   identity (backend, compiler, target, flags and binding ABI) remains F7/F8
   work.
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
   camera route on D3D11 and Vulkan. Fixed numeric `RECOMP_GPU_SCALE`
   now renders physical color/depth images with box-resolved guest readback
   and bilinear guest seeding. `native` follows the presentation provider's
   fitted output through a scale epoch that resamples color surfaces on the
   GPU and recreates depth/stencil cleared (synthetic, both vendors). Both
   modes now have validation-clean retail runs, including live window
   resizes. Direct
   raw-stream fetch, complete native-presentation parity and wider parity
   remain F7 scope.
6. **Host-owned Vulkan presentation (retail bring-up in progress).** Patch
   0048's default-inactive `RecompPresentProvider` seam and the default-off
   Brute Force Vulkan WSI consumer are implemented and pass the
   validation-enabled NVIDIA and AMD fixtures, including borderless-fullscreen
   enter/restore, fail-stop surface loss and test-only final-image readback.
   A native retail run reached visible Basic Controls. See
   [COMPLETED](COMPLETED.md#f-vulkan-default-renderer-migration). Open:
   - physical fullscreen review, physical compositor scanout and real device
     loss;
   - physical movie review, folded into a later required audio-on run (the
     benchmark rejects audio-off movie readiness);
   - do not require D3D11 keyed-mutex interop, and do not start the deferred
     M5 comparison campaign;
   - coordinate any upstream proposal with the unanswered presentation part
     of [sp00nznet/xboxrecomp#312](https://github.com/sp00nznet/xboxrecomp/issues/312).
7. **Reach Vulkan parity and PR readiness (in progress).** The stationary
   Basic Controls comparison (134 pixels over 8/255), the gathered GPU-VSH
   interpreter and verifier, the first reviewed movement pair, the reduced
   no-capture diagnostic, the Vulkan counter run, deferred visibility polls,
   fixed numeric render scaling, synthetic native-mode scaling and the first
   fixed and native scaled retail runs are complete; see
   [COMPLETED](COMPLETED.md#f-vulkan-default-renderer-migration). Open:
   - Investigate the remaining `0x80000000` staged-memory conflict through
     the generic color-DMA/lifetime gap; do not change the conservative
     GPU-wins policy. In the 2026-11-23 native runs it grows by about one
     per flip once movement starts, with or without resizes.
   - Attribute the movement-phase rise in WSI CPU presentations with no
     exact native source (from about one third to over half), seen in every
     native run with or without resizes. Use the bounded counters, not
     continuous capture.
   - Make native mode follow physical pixels on scaled displays. The
     executable is not DPI-aware, so at 150% the provider sizes the swap
     chain and render scale in logical pixels (UPSTREAM item 51). Decide
     between a per-title manifest and a framework window change, and measure
     D3D11 native mode on the same display first.
   - Compare a scaled Vulkan capture with a same-scale D3D11 capture at a
     reviewed matched state. The scale-2 run has a reviewed hold capture but
     no D3D11 scale-2 counterpart.
   - Close the remaining automatable renderer-state gaps next; reserve a
     multi-run A/B/A campaign for M5, and only when its acceptance evidence is
     actionable. Do not reuse the invalidated first no-capture set or schedule
     six manual runs for the renderer diagnostic.
   - Continue covering menus, movies, fades, PVIDEO and CPU fallback without
     weakening synchronization.
   - Repeat the mip retail comparison only with reviewed target-scene
     readiness; the first legacy attempt entered Join Deathmatch.
   - Exact texture-snapshot elision is deferred until an exact
     write/invalidation mechanism exists. Defer direct raw-stream fetch unless
     the unattributed framework gather cost is independently shown to justify
     it; the measured backend vertex-copy share is 0.56%.
   - Extract only approved contract, helper, fixture and documentation pieces
     into small upstream candidates. When the contract and host presentation
     boundary are accepted, request explicit approval to propose the optional
     Vulkan backend itself. Until then the renderer remains an
     upstream-quality downstream exemplar.
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

## G. Asset formats and Blender importer (descriptor formats decoded)

Goal: a Blender importer for characters, props and levels from the
user's own disc. The evidence, open questions and milestones M0-M5 are in
[ASSETFORMATS.md](ASSETFORMATS.md#importer-architecture-and-milestones).

- G1 (done 2026-11-23): BXML and BXSD decoding; see
  [COMPLETED](COMPLETED.md#g-asset-formats).
- G2 (partly done): the stdlib-only `bundle`, `names`, `bxsd` and `bxml`
  layers in `tools/bfassets`, a JSON `dump`, and a local candidate
  name-map builder exist, with synthetic malformed-input fixtures in
  `tests/test_assets.py`. Remaining: curate the candidate names (858 of
  2,286 hashes have uncurated candidates) into a reviewed local map.
- G3 (next): textures (M1). The descriptor fields are located, and every
  texture's size matches a mip chain in an Xbox `D3DFORMAT`. Decode P8 and
  DXT pixels, including swizzle and palettes, and compare them with the
  runtime's guest texture data in two bundles.
- G4-G7: static meshes, multi-part props, levels and characters in the
  ASSETFORMATS milestone order, each validated against runtime or
  reference captures that stay local.

This work is independent of the renderer and audio workstreams and does not
change the runtime. Commit no decoded game content or name dictionaries
derived from the disc.

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
   completed parity, presentation and diagnostic steps are in
   [COMPLETED](COMPLETED.md#f-vulkan-default-renderer-migration). Preserve the unresolved color-DMA conflict policy,
   continue synthetic renderer-state parity, the DPI-awareness decision and
   a matched scaled comparison without scheduling the deferred M5 campaign, and do not prioritize direct
   raw-stream fetch from the measured 0.56% backend vertex-copy cost alone.
   Submit no upstream work without approval. Implement D3D12 in F8, promote
   `auto` only after F9, and retain D3D11 and CPU as F10 requires.
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
8. Advance G's asset-format milestones independently of the runtime work,
   starting with G3's texture decode.

Update FINDINGS, UPSTREAM, PERFPLAN, ARCHITECTURE, ASSETFORMATS, COMPLETED and
the README in the same change as each milestone, as AGENTS requires.
