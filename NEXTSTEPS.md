# Next steps

The rendering architecture is in [docs/RENDERING.md](docs/RENDERING.md),
the performance plan is in [docs/PERFPLAN.md](docs/PERFPLAN.md), measured
results are in [docs/FINDINGS.md](docs/FINDINGS.md), and framework work is
tracked in [docs/UPSTREAM.md](docs/UPSTREAM.md). State as of 2026-10-30:
current framework tree `9bec55267a19bf1fa54320274cb073d785a8d411`. The
documented retail generation `20261029-175500-dfadbdd9` was tested against
the preceding tree `a4489cd69600a1489edea98ac19cf20db242b6d2`; patches 0025
and 0026 have not been retested on that generation. Generation
`20261030-135733-86f4d023` used tree
`fe8216622a8195fd7a2531a7fc4f1f723f402aa3` for one unmatched, instrumented
retail diagnostic; flip-ID capture mapping was not exercised there.
Generation `20261030-142754-81f8c6e6` was rebuilt in Release against the
current tree. It still reports 12 unresolved call targets and 34 untranslated
instruction sites; the new build and presenter-source patch 0027 have not
been validated in reviewed retail gameplay.

## Current boundary

- A private xemu 0.8.139 attempt verified the retail XBE and read-only disc
  setup, but the target window did not acquire verified focus. No input was
  sent, no requested tutorial/fade scene was independently matched, and the
  window captures are not pixel-exact. An operator-assisted reference run
  remains needed; see [FINDINGS](docs/FINDINGS.md#private-xemu-reference-attempt-2026-10-30).
- Default-off presenter-source capture links Windows GDI and CPU-upload
  D3D11 submissions to verified flip records in synthetic tests, including
  PVIDEO composition. In two short, unmatched retail probes it verified one
  submission per route; remaining submissions were no-client or occluded.
  The successful submissions may predate minimization. It rejects direct
  shared-GPU capture and does not establish physical scanout, visibility or
  matching guest-snapshot pixels. Matched-scene validation and benchmark
  integration remain open; see
  [FINDINGS](docs/FINDINGS.md#presenter-source-capture-alignment).
- The previous stationary campaign measured about 3.2 rendered frames per
  second on the CPU and 30.4 on D3D11 with GPU vertex programs, three runs
  each. The hold is visually static, so these are **rendered**, not distinct,
  frames. Native 30 Hz remains a hypothesis.
- The user reports about 6 fps while moving with default D3D11 settings.
  Failed texture uploads previously accumulated empty cache entries; the
  renderer now discards retryable failures and negatively caches deterministic
  unsupported decodes after alias checks. A P8 query-reuse experiment had no
  observed hits and was removed; reprotection is tested between binds. A
  three-run candidate comparison suggests better flip and whole-frame change
  rates but used unmatched adaptive inputs and included the removed
  experiment. The final binary was not rebenchmarked. Unreviewed whole-frame
  changes do not establish world fps; the reported case and matched visual
  correctness remain open. See
  [FINDINGS](docs/FINDINGS.md#default-d3d11-movement-regression-2026-10-30).
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
  not met. The CPU remains the reference and command-line default; the
  launcher now defaults to experimental D3D11.
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
G2 and G4 gates in [PERFPLAN.md](docs/PERFPLAN.md) are not formally met; the
acceptance is a judgement, so keep them as regression guards. Do not trade
the baseline away while working on the items below.

The three workstreams are largely independent. Audio touches only the APU and
host output, so it can proceed alongside the renderer work. Upscaling should
follow the artifact inventory (A1), because a larger render target makes any
existing state-tracking error harder to separate from a scaling error.

The user selected xemu as the rendering reference, wants mipmapping at 4K,
and wants audio default-on once its quality and listening gates are met.
The xemu DSP is the planned direction, but its project-licence decision is
deferred (see "Decisions" at the end).

Blood Wake is the main source, as it was for the renderer. It pins the same
xboxrecomp base, and its checkout is in `.deps/bloodwake-recomp` at
`e631edddfe56d22344e9f696923aeb6d78a9e7ba`. Follow the AGENTS rules when
adapting it: copy only generic code, keep the MIT notice, add the files to
`THIRD_PARTY_NOTICES.md`, and name the source in the UPSTREAM entry. Its
title-specific hooks, addresses and Remastered features do not apply. Its
`xemu_dsp` directory is GPL-2.0-or-later; adopting it also means choosing
this project's licence (see C4).

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
   Visibility remains unknown for non-dropped routes. One isolated,
   minimized, automatically scripted diagnostic now has a partial retail
   audit, but its scenes were not independently reviewed. It recorded 174
   line-list requests dropped as unsupported and many cull-enable writes;
   their visible impact is unknown. A matched run still needs to rank
   unhandled and mirror-only methods and compare CPU/GPU/xemu captures rather
   than treating method counts as defects. See
   [FINDINGS](docs/FINDINGS.md#unmatched-retail-gpu-audit-and-vertex-probe)
   and [UPSTREAM item 28](docs/UPSTREAM.md#performance-and-gpu-back-end-candidates)
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
   [FINDINGS](docs/FINDINGS.md#static-gamma-ramp-investigation). Obtain matched
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
   establish whether the title draws any, then adapt Blood Wake's vertex
   program point-size output and sprite path only if it does.
8. **GPU window clipping.** Validate it in gameplay, then make it the default
   if no regression remains.

*Done when:* the A1 inventory lists every difference from the reference with
a cause or an accepted reason; no recurring black interval or missing
character appears across a movement campaign; and the bright/dark behaviour
is explained and settled against the reference. Keep G4 open until then.

## B. Internal upscaling for 4K (implemented; retail validation pending)

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
fixtures build, the complete D3D11 matrix passed 188 cases with hardware
coverage enabled, and launcher and shared-presenter fixtures passed. No
retail gameplay or 4K-display campaign has been run, so G4/G5 and the
reported default 6 fps remain open.

1. **Implemented: add the scale to the D3D11 back end.** Adapt the generic parts only:
   the single place that decides the scale, the surface sizing, the epoch
   that triggers resizing, and the final scale pass. Skip Blood Wake's
   widescreen, texture restoration, HUD-edge, AO and HDR features. Provide
   `RECOMP_GPU_SCALE=<f>` or an output size, default 1. At scale 1 the
   output must be unchanged from today, bit for bit.
2. **Implemented: keep readback at the guest's size.** The back end reads each presented
   surface back to guest memory at every flip for the CPU consumers. At 4K
   output, the centered 4:3 picture is 2880×2160; a four-byte-per-pixel read
   would be about 24.9 MB per flip. Average the scaled surface down on the
   GPU first and read that. Separately, present the full-size image directly.
   This extends the existing shared
   presentation path, which still performs the per-flip guest readback.
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
   Widescreen is out of scope and 480p remains the default.
7. **Partly implemented: memory and robustness.** Budget VRAM for scaled targets and reject
   unsupported scales or allocation failures with a clear error; do not
   silently reduce the scale. Numeric values and D3D11 dimensions are
   validated, allocation errors are explicit, and native window changes use
   the scale epoch without a restart. A VRAM budget and device recovery
   remain open in D; fixed launcher preset changes require a restart.
8. **WARP implemented; hardware and retail pending.** Synthetic WARP and hardware fixtures: scale 1 matches the
   current output exactly; scale 2 downsampled matches scale 1 within a
   stated tolerance on a fixture with draws, partial clears, window clips
   and render-to-texture; readback is at guest size; aliases and visibility
   counts behave as designed. The CPU rasterizer stays at scale 1 as the
   reference, so G4 comparisons are made at scale 1. WARP now covers scale
   two with draws, render-to-texture, partial clears, window clips,
   guest-sized readback and normalized sync/async visibility. Run the same
   scaled cases on hardware and add a controlled retail campaign.
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

## C. Audio

**Current state (observations from FINDINGS and README).** The toolkit's
standalone APU and XAudio2 output are opt-in (`--experimental-audio`,
`RECOMP_AC97_READY`) with the GP/EP DSP stubbed. Isolated runs produced
non-silent 48 kHz stereo PCM through the title, menus and cinematics. No one
has listened to it, no capture has reached the tutorial, and enabling audio
changed menu-script timing. Whether the sound is correct, complete or free of
gaps is unknown. A separate short, unclassified boot validated the benchmark's
console-control shutdown and WAV-finalization helpers with the game; its
2.01-second WAV was silent and host coverage was inconclusive.

**Blood Wake's audio work** (observed in its tree) is a stack of carried
toolkit patches plus a DSP:

| Item | What it addressed |
| --- | --- |
| TK-14, `apu_core.c` | The sequencer sample-limit tick (ISTS bit 3) fired every 2048 samples; DirectSound answered each with a hardware restart, leaving silent gaps about 20 times a second. |
| TK-15, `apu_core.c` | Resolve the image before the window when a physical address names both. |
| TK-16, `apu_dsp.c` | The stub DSP's mixdown: every mixbin at unity, even bins left and odd bins right. |
| TK-17, `kernel_memory.c`, `apu_vp.c` | One physical number could name a heap page and a window page; the APU read the heap copy of the music stream's buffer and produced noise. |
| `apu_xaudio2.c` | A 5.1 source voice when the device has six or more channels. |
| D-012, `bw_apu_xdsp.c` plus vendored `xemu_dsp` | The GP and EP DSPs on xemu's DSP56300 interpreter, giving the console's echo and 3D-sound programs; it falls back to the stub fold-down if a program cannot keep real time. |

Whether Brute Force triggers each of these is **unverified**. The symptoms
are specific, so test for them.

**Upstream overlap (checked 2026-10-30 from PR titles only; contents not
compared).** Open PRs on `sp00nznet/xboxrecomp` appear to cover several of
the same problems: #264 (hold the frame while the SSL routine restarts the
sequencer), #266, #270 (no silent frames while the title has the SE off),
#261 and #286 (heap pages named by their RAM mirror alias). Per AGENTS,
prefer selecting or improving these to a competing patch, and check whether
the merged #193 and #195 (device-paced output, pitch resampling, WAV tap) are
already in the pinned base. Record the overlap in UPSTREAM.

1. **Establish a listening baseline.** Capture a WAV through the menus and
   the tutorial with `RECOMP_AUDIO_WAV` and isolated roots. The streaming
   `tools/audio_analysis.py` checker reports silent 2048-sample periods,
   broadband indicators and host underrun evidence for marked intervals;
   apply it to reviewed real scenes. A listener still has to judge correctness;
   the check can only identify indicators. Keep captures local.
2. **Reach the tutorial with audio on, and understand the timing change.**
   Find why audio shifts menu-script timing and make the audio path
   independent of it, so the benchmark and the scripts behave the same with
   audio on or off.
3. **Fix what the baseline shows.** Evaluate #264/#270/#266 for the gap
   problem and #261/#286 for the noise problem against the WAV checks. Where
   no upstream PR covers it, add a numbered patch under `patches/xboxrecomp/`
   adapted from Blood Wake's carried version with credit, an environment
   switch off by default unless it is a plain bug fix, `RECOMP_*` names, and
   a synthetic fixture.
4. **Adopt the xemu DSP wholesale (blocked on licence).** Before vendoring
   files or distributing a combined build, obtain the user's deferred
   project-licence decision and review component provenance and terms.
   After that, port Blood Wake's
   `src/xemu_dsp` (xemu v0.8.139, GPL-2.0-or-later; `dsp_dma.*` is
   LGPL-2.0-or-later) and its glue `bw_apu_xdsp.c`, with the shims and no
   changes to xemu's files. Keep its fallback to the stub fold-down when a
   program cannot keep real time, and an environment switch that selects
   the stub for comparison and slow machines. Start after C1 shows what the
   stub gets wrong, so the DSP's benefit is measured rather than assumed,
   and find which DirectSound effects the title loads (reverb, 3D
   positioning, HRTF, echo). Measure its per-frame CPU cost against the
   frame-rate baseline; Blood Wake advises its simple mode on slow PCs.
   Licensing work for the same change: add the files and their notices to
   `THIRD_PARTY_NOTICES.md` and the `LICENSES` directory; choose and add a
   project licence compatible with GPL-2.0-or-later for the combined
   program (the project has no licence file yet); and say in the README
   which terms a distributed build is under. Game data and the disc stay
   out of the repository whatever the licence. This is an engineering note,
   not legal advice; check it before the first public release.
5. **Speaker configuration.** The kernel answers the title's audio-flags
   query with stereo. Keep it. Optional 5.1 output is a later item; confirm
   the title mixes for surround first.
6. **Make audio default-on (decided).** Audio affects timing and
   synchronisation (it already shifts menu-script timing), so it should be
   part of every run, and the benchmark must be measured with it on. Once
   the baseline is clean (the C1 checks pass and the user's listening pass
   is acceptable), enable it by default in the launcher and on the command
   line, drop the "experimental" label in README, and keep an explicit off
   switch for diagnostics. Until then it stays opt-in. Re-measure the
   frame-rate baseline with audio on, and make `tools/bench.py` default to
   it.
7. **Synchronisation.** Check audio under frame-rate dips and during loading
   (underruns, drift, the intermittent loading hang in D). Never gain
   frame rate by skipping a semaphore or flush to feed the audio.

*Done when:* a user-run listening pass through menus, cinematics and the
tutorial reports no gaps, noise or missing effects; audio is on by
default; the WAV checks pass; the
frame-rate baseline holds with audio on; and README, FINDINGS and UPSTREAM
are updated.

## D. Remaining pipeline and compatibility work

These items are unchanged in substance from earlier plans; they matter now
mainly where they gate 4K and audio.

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
- Profile callers of `NtQueryVirtualMemory` and investigate texture hashing
  and visibility polling using the combined campaign's profiles and counters.
- Batch the event-driven flush acknowledgement, an idle NV2A thread, and
  segment-at-a-time walking where their ordering can be preserved. Keep
  timing changes behind separate switches and compare the combined result
  once; isolate switches only to diagnose regressions.
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
  is prior art. Vertex and other shaders are not cached.
- Investigate the intermittent loading hang, including the stalled candidate
  boot. Re-check upstream overlap and coordinate with Blood Wake's authors
  before preparing a contribution; do not file or comment without approval.

## Decisions (settled 2026-10-30)

1. **Rendering reference:** xemu, run on the same disc at matched states (A2).
2. **DSP direction:** the xemu DSP is planned, but the exact project licence
   is deferred. Do not vendor or distribute the combined work until the
   user decides and provenance and terms are reviewed (C4).
3. **Mipmaps:** wanted at 4K; the earlier deferral is lifted for scaled
   modes (B10).
4. **Audio:** keep stereo, then enable audio by default once its quality,
   performance and user-listening gates are met (C6).
5. **4K target:** use the RTX 5070 for 3840×2160 output and captures, with
   a 2880×2160 4:3 picture (4.5× 640×480). The active display is 3440×1440,
   so this does not establish native 4K-display validation (B9).
6. **Scale failures:** reject unsupported scales and allocation failures
   explicitly; fixed preset changes require restarting, while Native follows
   output-size changes through the scale epoch (B7).

Still open: the exact DSP project licence and access to a native 4K display.
Optional 5.1 output is deferred (C5).

## Order

1. A1 (inventory) and C1–C2 (audio baseline) in parallel: neither changes
   shared code paths.
2. A2–A4 as the reference and the gamma-ramp question allow; then B1–B2
   behind a switch with scale 1 unchanged.
3. B3–B10 and C3–C4 with their fixtures, then one A/B/A campaign for each.
4. C5–C7 and the launcher and README updates last.

Update FINDINGS, UPSTREAM, PERFPLAN, ARCHITECTURE and the README in the same
change as each milestone, as AGENTS requires.
