# Next steps

The rendering architecture is in [RENDERING.md](RENDERING.md),
the performance plan is in [PERFPLAN.md](PERFPLAN.md), measured
results are in [FINDINGS.md](FINDINGS.md), and framework work is
tracked in [UPSTREAM.md](UPSTREAM.md). State as of 2026-11-10: the current
toolkit uses xboxrecomp v0.17.0 at
`343b6d8901723b37ce568b2418c5df4e1fe1d223`, selected post-release commits
from #314, #319 and #320, and 34 local patches. Its verified tree is
`131abd8d1f4559bb36dba4391e4c644925cff5df`.

Supported retail generation `20261111-002138-a9dbc18a` is a Release build on
that v0.17 tree. Its executable SHA-256 is
`f1bf8622df3b651af6747566b4628a1d8ca18d7a01a4c196df910124cb9f7844`;
it translated all 22,295 identified functions with zero failures, six
unresolved direct-call targets and 11 untranslated instruction sites. One
audio-off, four-thread CPU run and one D3D11 run each reached the scripted
tutorial world, stationary hold and movement window without a retry, reached
generated stub, untranslated-instruction hit, ICALL failure, renderer-device
error or crash.

The reviewed performance campaigns still use old generation
`20261103-145850-21e3091e` on tree
`25a2a713738b7aabb1c21ef6720765ff93ccffea`. The new bring-up runs have one
attempt per renderer and no reviewed world labels; their flip and whole-frame
change figures are not a performance comparison. Do not compare the old
campaign measurements with the v0.17 generation without a fresh A/B/A
campaign.

sp00nznet/xboxrecomp#297, sp00nznet/xboxrecomp#298,
sp00nznet/xboxrecomp#299 and sp00nznet/xboxrecomp#300 merged in v0.17.0;
local patches 0001, 0005 and 0035 were removed. Issue
[sp00nznet/xboxrecomp#301](https://github.com/sp00nznet/xboxrecomp/issues/301)
closed after v0.17.0 corrected the raw `XC_VIDEO` encoding. Selected
sp00nznet/xboxrecomp#320 supplies exact-value widescreen and 480p opt-ins.
sp00nznet/xboxrecomp#314 and sp00nznet/xboxrecomp#319 are also selected and
validated. sp00nznet/xboxrecomp#315 remains unselected because the 32-bit
MSVC differential test found 2,643 signed 16-bit `IDIV` mismatches.

The fresh-generation and focused-bring-up completion gate is met. The next
gate is the matched A1/E1-E2 retail and xemu reference evidence; the old
generation remains only the reviewed measurement baseline. Patch 0037's
one-line Windows raster diagnostic correction is the strongest small upstream
candidate, followed by the software-interrupt ABI fix, immediate-entry guard,
firmware-exit half of patch 0032, patch 0013, and the current-main half of
patch 0019. Patch 0007 is held until its accounting covers or deliberately
excludes every exported allocator. Do not file another PR, amend an existing
PR or comment upstream without approval.

A 2026-11-10 Blood Wake refresh compared the recorded adaptation baseline
v0.2.4 at `e631edddfe56d22344e9f696923aeb6d78a9e7ba` with public v0.3.3 at
`6292340705d9b863351a45ba160174a346a81688`, ten commits later. The recorded
checkout remains on `e631eddd`; the newer revision was reviewed only as prior
art and still fingerprints xboxrecomp v0.16.0. No code was imported. The
strongest small generic candidate is its foreground-only display-awake
policy; shader preparation and native fixed-function work need title evidence
and parity fixtures, while audio, latency, HDR, and Direct3D 12 work remain
deferred. See
[FINDINGS](FINDINGS.md#blood-wake-v025-v033-refresh) and the new
[UPSTREAM candidates](UPSTREAM.md#performance-and-gpu-back-end-candidates).

## Current boundary

- A private xemu 0.8.139 attempt verified the retail XBE and read-only disc
  setup, but the target window did not acquire verified focus. No input was
  sent, no requested tutorial/fade scene was independently matched, and the
  window captures are not pixel-exact. An operator-assisted reference run
  remains needed; see [FINDINGS](FINDINGS.md#private-xemu-reference-attempt-2026-10-30).
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
- Renderer-local P8 uploads honor the programmed 32/64/128/256-entry lengths,
  validate sampled indices, and reject known short or unreadable allocations.
  Guard-page and cube fixtures pass on WARP and hardware, but actual NV2A
  out-of-range behavior, untracked suballocations, GPU render-target aliases,
  and concurrent guest writes remain unresolved. The CPU reference path is
  unchanged; these synthetic results do not establish hardware equivalence.
- Renderer-device loss is reported and stops execution instead of silently
  completing outstanding GPU work. Mocked removed/reset/hung/driver and
  pending-query cases pass; actual device removal, recovery, the separate
  presenter device and semaphore races are not verified.
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
- The final-generation cold/warm pixel-shader-cache campaign measured
  12.914/25.543/30.623 ms p50/p99/max across 96 cold compile/create events
  and 0.143/0.517/0.535 ms across 96 warm lookup/create events. Movement
  averaged 35.743 cold and 35.990 warm flips/s, with effectively unchanged
  p99 frame time, so the win is startup hitch removal rather than steady-state
  throughput. Publication passed in all three pairs and existing failure
  fixtures. Title captures were visually matched but not byte-identical or
  frame-locked, so strict output identity remains open and the cache stays
  opt-in.
- Phase 4's first mechanism now exists behind
  `RECOMP_PFB_FLUSH_WAIT=1`. Synthetic coverage and one corrected movement
  smoke show that the real acknowledgement can drive the title through the
  scripted route. The initial title wrapper omitted the generated `ret` stack
  effect; a four-byte guest-stack correction and explicit enabled/disabled
  fixture modes fixed it. The smoke did not include capture review or caller
  profiling, so the next task is a six-leg poll/event alternating campaign,
  not the idle-thread or segmented-walk mechanism. Do not satisfy the
  acknowledgement early or weaken semaphore, report, flip, or CPU-reference
  ordering. The shader-cache output gate remains independent.
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
  pinned frames and PVIDEO. It still performs per-flip guest readback and
  does not limit all GPU work in flight. WARP and hardware synthetic tests
  cover the alias, pixel transfer, queue ordering and fallback.
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
xboxrecomp v0.16.0 base, not Brute Force's current v0.17.0 tree. Follow the
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
   Visibility remains unknown for non-dropped routes. A fresh v0.17 scripted
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
2. **Use xemu as the reference (decided).** The CPU path models no
   stencil, so it cannot settle the brightness, stencil and bloom questions.
   Run the same disc in xemu and capture matched states: the tutorial
   stationary hold, the sun-facing and turned views, and the end of the
   opening fade. The disc image and every capture stay local. Matching is
   not automatic: xemu's resolution, filtering and frame timing differ, so
   run it at the native 640x480 without upscaling or extra filtering, match
   by content rather than wall-clock time, and record the xemu version, its
   settings and the capture points in FINDINGS. Where our output and xemu's
   differ, list and classify the difference; do not assume xemu is perfect
   either. Extend `tools/compare_frames.py` to compare against xemu captures
   and report differences without treating either side as correct.
3. **Brightness, bloom and HUD.** Use hardware D3D11 and the existing user
   captures first to investigate why the HUD and glow can disappear together.
   Match the view and capture actual programmable vertex/clip state before
   attributing this to the near plane; current GPU-program frame traces may
   print stale CPU-transformed vertices and count submitted rather than
   post-clip triangles. Isolate the screen-zero plane and packed alias
   separately, and check the stencil threshold and packed-depth conversion
   against real gameplay values. Explain and minimize any further gameplay
   run. Do not add a global gamma or brightness adjustment.
4. **Display gamma ramp (hypothesis, unverified).** Blood Wake found that
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
   displayed pixels. See
   [FINDINGS](FINDINGS.md#static-gamma-ramp-investigation). Obtain matched
   runtime evidence before changing presentation. Only if the title writes a
   relevant ramp, apply its own ramp at present behind a switch, as Blood Wake
   does; that differs from a global adjustment.
5. **Black frames and the missing character.** Make the movement gate
   measure visible world change. The harness reports two known solid-black
   hashes, but other dark images and intentional fades still need
   classification. A zero match count is not proof of valid world frames.
6. **`LIN_D24S8` (`0x2E`).** Validate the opt-in path with matched gameplay
   captures and a reference. Extend guest-write tracking, partially
   overlapping aliases and the CPU reference behaviour before making it the
   default. Establish first whether the unsupported format affects any
   visible pixels.
7. **Points, lines and other dropped primitives.** Use the A1 inventory to
   establish whether the title draws any. Blood Wake v0.3.1 now keeps
   eligible point batches on the GPU and expands point sprites there, but it
   does not solve Brute Force's observed line-list drops. Adapt its optional
   point-size/sprite contract only if a visible point route is established;
   investigate the 174 recorded line-list requests separately.
8. **GPU window clipping.** Validate it in gameplay, then make it the default
   if no regression remains.

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
fixtures build, the complete D3D11 matrix passed 190 cases with hardware
coverage enabled, and launcher and shared-presenter fixtures passed. Bounded
scale-1 and 4.5x runs retained CPU-decoded opening movies, including the intro
montage. No matched retail gameplay or native-4K-display campaign has been
run, so G4/G5 and the reported default 6 fps remain open.

1. **Implemented: add the scale to the D3D11 back end.** Adapt the generic parts only:
   the single place that decides the scale, the surface sizing, the epoch
   that triggers resizing, and the final scale pass. Skip Blood Wake's
   texture restoration, HUD-edge, AO and HDR features. Keep widescreen
   isolated in workstream E. Provide `RECOMP_GPU_SCALE=<f>` or an output
   size, default 1. At scale 1 the output must be unchanged from today, bit
   for bit.
2. **Implemented: keep readback at the guest's size.** The back end reads each presented
   surface back to guest memory at every flip for the CPU consumers. At 4K
   output, the centered 4:3 picture is 2880×2160; a four-byte-per-pixel read
   would be about 24.9 MB per flip. Average the scaled surface down on the
   GPU first and read that. Separately, present the full-size image directly.
   This extends the existing shared
   presentation path, which still performs the per-flip guest readback.
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
4. **Implemented boundary: reject scaled depth and stencil aliases.** The exact-size D24S8 alias, the guest-depth
   seeding and the snapshots assume the guest's surface dimensions. Define
   what each does for a scaled target (for example, seed by upsampling or
   resolve by sampling) remains undefined. The backend now refuses either
   alias option with an active scale instead of silently stacking them. The
   packed alias is still an unvalidated experimental path (A3).
5. **Partly implemented: quantities measured in pixels.** Z-pass visibility counts scale with the
   pixel count, so normalise them or the title's thresholds will misfire
   and are now normalized per query by the physical/logical pixel-area
   ratio in both sync and async paths. Confirm the title's thresholds.
   Check point size and line width, which the title may specify in pixels,
   and the half-texel offsets in screen-space draws.
6. **Implemented controls and handoff.** The existing resizable window and
   swap chain fit a 4:3 picture. The renderer queries that live picture for
   Native mode; fixed 720p, 1080p, 1440p and 4K presets are in the launcher.
   Workstream E owns true widescreen; 480p 4:3 remains the default here.
7. **Partly implemented: memory and robustness.** Budget VRAM for scaled targets and reject
   unsupported scales or allocation failures with a clear error; do not
   silently reduce the scale. Numeric values and D3D11 dimensions are
   validated, allocation errors are explicit, and native window changes use
   the scale epoch without a restart. A VRAM budget and device recovery
   remain open in D; fixed launcher preset changes require a restart.
8. **Synthetic hardware implemented; retail gameplay pending.** Synthetic WARP and hardware fixtures: scale 1 matches the
   current output exactly; scale 2 downsampled matches scale 1 within a
   stated tolerance on a fixture with draws, partial clears, window clips
   and render-to-texture; readback is at guest size; aliases and visibility
   counts behave as designed. The CPU rasterizer stays at scale 1 as the
   reference, so G4 comparisons are made at scale 1. WARP now covers scale
   two with draws, render-to-texture, partial clears, window clips,
   guest-sized readback, CPU-written movie frames and normalized sync/async
   visibility. The complete matrix passes 190 hardware cases and 123 WARP
   cases with 67 hardware skips. Bounded 4.5x hardware captures show all
   three opening movie stages. Add a controlled retail gameplay campaign.
9. **Recast G5.** "G1 holds at 2× internal resolution" becomes a measurement
   at 1080p and 4K presets, A/B/A against scale 1, reporting distinct world
   frames and the GPU, driver and scale used. Record results in FINDINGS.

10. **Mipmaps and filtering for scaled modes (wanted; was deferred).** The
    deferral is lifted for 4K. Today both paths sample level 0 only, so
    minification will shimmer at high resolution. Plan: (a) establish what
    the title supplies: whether its textures carry mip chains in guest
    memory, in which formats, and how many levels (unverified; the existing
    native BC mip fixtures show the decoder can read levels); (b) make the
    GPU decoder upload the supplied chain and let the hardware select
    levels, honouring the NV2A level range and LOD bias from the texture
    registers, and generate levels only where the title supplies none and
    xemu shows the console sampled them; (c) add anisotropic filtering as a
    separate switch (Blood Wake's `VFX_ANISO` is prior art); (d) put it
    behind `RECOMP_GPU_MIPS`, off by default until compared with xemu at
    scale 1, since it changes the image relative to the CPU reference.
    Record the CPU/GPU difference in FINDINGS as a known difference, and
    make G4 comparisons at scale 1 with mips off. Cube, render-target,
    palette and linear textures need separate checks, and so does the
    texture-cache and VRAM cost.

*Done when:* the 4K preset runs a scripted movement campaign with the output
matching the scale-1 picture on average once downsampled, no scale-specific
artifacts that the fixtures do not cover, and G5 is recorded.

## C. Resolve game-audio quality

**Implemented and verified**

- Audio is default-on; `--no-audio` and `RECOMP_AUDIO=0` are explicit off
  paths. The launcher exposes On/Off and Accurate/Simple choices.
- The default Simple path preserves the stereo all-mixbin fold-down. Accurate
  remains an experimental option that runs the title's GP and EP programs
  through the vendored xemu v0.8.139 DSP56300 C interpreter.
- Framework patch 0035 preserves the two channels produced when HRTF filters
  a mono source. Its synthetic negative control produced `+1/+1`; the fixed
  path produces `+1/-1`.
- Physical-translation intent distinguishes aliased heap and contiguous-window
  pages. The dedicated DSP worker is signaled and joined before interpreters,
  shadows, queues, and caches are released.
- Firmware-return and shutdown routes run full APU cleanup before
  `ExitProcess`; bounded WAVs now receive an exact RIFF header.
- Upstream PR #264 commit
  `eb7b1f5833205a24fcc0eda6eb4ed2df4f2bc2e3` is selected for the observed
  2,048-sample sequencer restart hold. PRs #261/#286 and #266/#270 remain
  unselected because their distinct failure modes were not observed.
- Six repeated native init/shutdown cycles passed without leaked process
  handles. Accurate and fold-down retail exit captures finalized correctly.
- The host policy uses eight 512-sample buffers, a three-quarter refill
  threshold, and a high-priority APU frame worker. The last long
  then-default Accurate probe ran 164.47 seconds from startup through
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
- The final Release executable rebuilt with patch 0035. The standalone
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
   external-DSP series. Patches 0030-0034 still require the generic pieces and
   policy changes to be split before upstream review.
6. Do not import Blood Wake v0.3.0's native DSP, WASAPI path, or bit-exact hot
   loops while audio is deferred. If work resumes, first assess its
   off-thread device-rebuild and MMCSS policy against the existing XAudio2
   reference, then consider native DSP only with interpreter lockstep and the
   existing GPL obligations.

*Done when:* after audio work resumes, a human listener signs off the fixed
Simple scene list above, the Accurate defect is either fixed or remains
explicitly experimental, and at least one additional audio-heavy gameplay
scene stays free of host underruns if it differs materially from the validated
Quick Campaign route.

Automated captures establish valid transport and bounded signal properties;
they cannot establish that dialogue, effects, music, or reverb sound correct.
Stereo remains the acceptance target.

## D. Remaining pipeline and compatibility work

These items are unchanged in substance from earlier plans; they matter now
mainly where they gate 4K, widescreen and audio.

- Continue the guest surface-read investigation with the null backend and
  `BRUTEFORCE_SURFACE_GUARD`, covering stationary and movement in one
  scripted run. Account for excluded pages and the diagnostic's other blind
  spots; a partial run cannot prove that no guest readers exist. Keep output
  local. This determines how much readback B2 can remove.
- Extend the opt-in shared GPU presenter into a fully GPU-authoritative
  pipeline: establish guest surface readers, implement safe on-demand
  readback and bound all GPU work in flight. Retain readback for guest
  readers, screenshots, frame statistics and other consumers until they have
  equivalent GPU-aware paths. Do not release fences before their inputs are
  consumed or skip a flush or flip hold.
- Inspect the slow combined candidate's high sampled GPU-wait share, the
  frequent busy shared slot and repeated packed-depth conversions; only avoid
  conversion when depth and stencil writes are correctly tracked.
- The final rebuilt default renderer now has reviewed world captures,
  caller-attributed profiles and detailed validation/cache counters. First
  isolate synchronous versus ordered asynchronous visibility in an
  alternating campaign with all other options and instrumentation fixed.
  Then address repeated page-query validation with checked accesses that
  retain guard-page, reprotection, allocation and reset detection. Retain a
  normal uninstrumented baseline because the counter switch adds timing and
  atomic-accounting overhead.
- First measure the event-driven flush acknowledgement alone with three
  poll/event pairs, caller profiles, fixed counters and reviewed world
  captures. Explain its high smoke-run timer/recheck counts and keep it
  default-off until G2, G3 and correctness gates pass. Only then add the idle
  NV2A-thread wait and segment-at-a-time walking behind separate switches;
  compare the combined milestone and isolate switches to diagnose regressions.
- Extend the synthetic cache and P8 fixes in the current boundary to tracked
  guest writes, render-target aliases and untracked suballocations; establish
  NV2A out-of-range palette behavior and preserve palette length through the
  framework's CPU decoder. Validate on the title and additional drivers, and
  compare the new cache's cost with an A/B/A campaign before upstreaming.
  Implement and test device/resource recreation and loss handling for the
  separate presenter without violating pending flush, semaphore, report or
  flip ordering. The current fail-stop behavior is not recovery. Mipmaps are
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
- Investigate the intermittent loading hang, including the stalled candidate
  boot. Re-check upstream overlap and coordinate with Blood Wake's authors
  before preparing a contribution; do not file or comment without approval.
- After the fresh v0.17 retail bring-up, optionally prepare the independent
  foreground-only display-awake candidate. Keep it behind a default-off
  `RECOMP_*` switch, release the hold when backgrounded, minimized, or closed,
  surface API failures, and cover the Windows window-state transitions in a
  synthetic fixture before changing launcher policy.

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

1. **Implemented; retail coverage pending: diagnostic-only camera trace.**
   `BRUTEFORCE_CAMERA_TRACE=1` wraps the central perspective builder and
   records bounded JSON lines containing its caller, entry arguments,
   input/output matrix, and explicitly labelled candidate aspect, tangent,
   viewport and camera-owner fields. The wrapper always calls the generated
   body and leaves its resulting guest state unchanged. Limits and sampling
   are validated, and synthetic direct/indirect dispatch and state-equivalence
   cases pass. Regenerate because the manual-source fingerprint changed.
   One fresh-generation scripted run sampled 774 records through call 7,731.
   It observed only caller `0x0006BE90`, four parent return classes and six
   candidate camera states; all matrices were readable and changed across the
   generated call. Caller `0x0006D240` did not appear. Runtime coverage still
   needs operator-classified gameplay, menus, movies, in-engine cutscenes and
   every available split-screen layout. The trace does not alter the matrix
   or establish ownership by itself.
2. **Establish title ownership before choosing hooks.** Confirm where each
   gameplay camera's aspect and horizontal/vertical tangent are finalized,
   which routine rebuilds dependent frustum planes, and whether another
   camera class bypasses that path. Identify object, static-world/grid and
   effect culling that can reject newly visible side geometry. A projection
   patch is not complete without this.
3. **Add one aspect authority.** Plan an opt-in `RECOMP_*` control for 4:3,
   16:9, 64:27/21:9 and display matching, with 4:3 the default and invalid
   input rejected. Keep `K` independent of `RECOMP_GPU_SCALE`; CPU rendering
   remains 4:3. Decide an ultrawide cap only after side-region validation.
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
   cutscenes and split screen. Report distinct world frames separately from
   flips and do not weaken flush, semaphore, report or flip ordering.
9. **Defer HUD-edge relocation.** A centred 4:3 HUD is the first correctness
   target. Moving selected panels to outer corners is optional title layout
   work after the base mode passes.

*Done when:* 4:3 remains unchanged, the 16:9 and 21:9 centre matches the
reference within the existing renderer's listed differences, newly visible
side geometry survives matching title culling, HUD and movies retain their
shape, guest readback remains 4:3, and split-screen behavior is documented.

## Decisions (settled 2026-10-30)

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

Still open: access to a native 4K display. Optional 5.1 output is deferred.

## Order

1. Complete A1 and E1-E2, then A2-A4 as the reference and gamma-ramp question
   allow. The widescreen trace is diagnostic and must not change output.
2. Implement E3-E8 behind one default-off mode, with a 4:3 regression gate
   before wide output. Keep HUD relocation deferred.
3. Continue B1-B10 behind switches with scale 1 unchanged.
4. Run C's human listening pass against the stable scene from A; repeat
   automated audio probes only for a new symptom or materially different
   audio-heavy scene.
5. Continue D's compatibility work without weakening audio or GPU ordering.

Update FINDINGS, UPSTREAM, PERFPLAN, ARCHITECTURE and the README in the same
change as each milestone, as AGENTS requires.
