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
at commit `e631edddfe56d22344e9f696923aeb6d78a9e7ba`. It pins the **same**
xboxrecomp base as this project (`f0f8a461`, v0.16.0) and reports 205–290 fps
in gameplay at 3440×1440 with a D3D11 renderer. Its own files are MIT
licensed, so they can be adapted here with attribution in
`THIRD_PARTY_NOTICES.md`. Its title-specific hooks, addresses, and Remastered
features do not apply and must not be copied.

## Status (2026-10-30)

Measurements are in [FINDINGS.md](./FINDINGS.md#performance-plan-phase-0-and-the-first-gpu-path-results);
next steps are in [NEXTSTEPS.md](../NEXTSTEPS.md).

| Phase | Status |
| --- | --- |
| 0 | Mostly done. Harness (`tools/bench.py`), frame statistics (patch 0014), census and ceiling probes (0015), profiler (`src/profile.c`), null back end and surface guard (`src/gpu/null_backend.c`) exist and were run. Request audit 0025 has a partial, unreviewed retail diagnostic; flip-ID capture mapping 0026 is synthetic-tested. Presenter capture 0027 verified one link per GDI and CPU-upload route in short, unmatched retail probes, but did not validate physical visibility. Matched tutorial inventory, real scene review and world-frame labels remain open. A guard diagnostic aborted on a protected semaphore page, so the surface-read answer remains open. Missing: the native-rate measurement and upstream PR measurements (#231, #272, #273). |
| 1 | Implemented, G4 not met. D3D11 back end (`src/gpu/`, `RECOMP_GPU=d3d11`) with the extended interface (0016). Synthetic WARP and hardware fixtures match apart from listed edge pixels. The new GPU-vertex tutorial capture differs from the CPU reference in 0.52% of pixels beyond 8/255; an older CPU-vertex capture differs substantially. |
| 2 | Implemented (0017, on by default with the D3D11 back end). Stationary hold 3.20 → 30.41 rendered fps (3 runs each, alternating). An unreviewed retail diagnostic sampled 23,400 GPU-program batches without reported vertex differences through its last report. WARP and hardware fixtures cover additional packed formats, relative constants and culling against the CPU interpreter; matched-scene and complete format/hardware verification remain open. |
| 3 | Partial. Async visibility reports (0018) and shared GPU presentation (0024) are opt-in. Shared presentation bounds one pending snapshot and retains synchronous per-flip guest readback; on-demand readback and a limit on all GPU frames in flight are not implemented. CPU-written Bink frames are now synchronized into the GPU surface before scaled submission, and bounded 4.5x opening captures are visible. The combined alias/presentation campaign did not establish a speedup. |
| 4 | Not started. Preserve flush, semaphore, and flip ordering. |
| 5 | In progress. GPU compute decodes common textures; exact D24 and packed D24S8/stencil surface aliases and GPU window clipping are experimental opt-ins. One alias-only hardware diagnostic reproduced the combined alias/presentation candidate's much darker stationary background; its correctness is unknown and G4 remains unmet. D3D11 honors partial clear rectangles, and patch 0022 removes redundant software depth clears when a backend owns drawing and clearing. Patch 0019 cached hot `getenv` calls. |

### Audio performance status

Simple stereo fold-down is now the default independently of the experimental
graphics path. The vendored xemu v0.8.139 GP/EP interpreter remains an
experimental option on a dedicated high-priority worker while the framework
APU frame worker is also raised to `THREAD_PRIORITY_HIGHEST` on Windows.
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
Campaign route reached the world and exercised 3D/HRTF voices. Patch 0035
fixes a generic mono-source channel-selection defect there. A clean scripted
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
Synthetic rewrite, failure, alias and P8 boundary/reprotection fixtures pass,
but the final binary has not been rebenchmarked, and the user-reported 6 fps
case, controlled snapshot cost, guest-write races and matched gameplay
correctness remain unresolved. Renderer device loss fails explicitly;
recovery and the separate presenter device remain open. See
[FINDINGS](./FINDINGS.md#default-d3d11-movement-regression-2026-10-30).

An opt-in persistent combiner pixel-shader cache now avoids recompiling
matching bytecode in synthetic warm runs and an alternating stationary-tutorial
comparison. Timing and output checks are in
[FINDINGS.md](./FINDINGS.md#launcher-and-pixel-shader-cache); gameplay stutter
and G4 remain unestablished, and the title captures have small unexplained
differences. Synthetic blocked-publication and concurrent-writer checks now
exercise recovery when an identical valid entry already exists; their
results are not gameplay performance evidence.

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
not establish G2 or playability. The harness reports known-black matches but
cannot classify all dark frames or distinguish an intentional fade.

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
correctness comparisons and remains the command-line default. Per user
preference, the launcher defaults to the experimental D3D11 path even though
G4 remains unmet.

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
   it does not replace the state census. Its synthetic fixtures pass, but it
   has not yet been run on the tutorial. Rank observed ignored methods and
   compare matched CPU/GPU/xemu captures before inferring visible impact.
4. **Guest surface reads.** Determine whether the title's CPU code ever reads
   a rendered colour or depth surface. Candidates: screenshots, the save
   image, occlusion or lens-flare readbacks. Use page protection on surface
   ranges in a diagnostic build, or a census of CPU reads in those ranges.
   Phase 3 depends on the answer.
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

6. **Cheap upstream candidates.** Measure the open upstream PRs listed in
   [UPSTREAM.md](./UPSTREAM.md#open-upstream-work-to-evaluate-for-selection)
   with the harness:
   - [#273](https://github.com/sp00nznet/xboxrecomp/pull/273), for walk
     cost on either renderer;
   - [#272](https://github.com/sp00nznet/xboxrecomp/pull/272), for the
     vertex-program CPU fallback;
   - [#231](https://github.com/sp00nznet/xboxrecomp/pull/231), for the CPU
     reference path.

   Select a PR only if it measurably helps and its tree verifies. None of
   them replaces Phase 1.

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

**Exit:** no `nv2a_vsh_run` or `fetch_attr` in the NV2A-thread profile during
gameplay, apart from constant-writing programs. G4 still holds.

## Phase 3: remove synchronous round trips

1. **Present directly from the GPU.** The opt-in shared-texture path copies
   the finished 32-bit target to the existing D3D11 window device, with one
   pending snapshot and CPU fallback for pinned frames or video overlays.
   The frame ID prevents older GPU snapshots replacing newer CPU snapshots.
   Guest readback and CPU snapshot creation remain; this is not yet a
   fully GPU-authoritative presentation pipeline.
2. **Readback only on demand.** Skip copying each flipped frame back into
   guest memory unless Phase 0 found a guest reader, or a dump or diagnostic
   requests it. Blood Wake measured about 4 ms per flip for this copy. If the
   title does read surfaces, read back only that surface, at that point.
3. **Bound the number of frames in flight.** Use an event query per flip and
   limit unfinished GPU frames, defaulting to 2, so GPU work cannot queue
   unboundedly behind a fast walk. Use the frame-latency waitable swap chain
   so presents do not block the NV2A thread.
4. **Fence semantics.** Semaphore releases are still written when the
   executor has *submitted* the preceding work to D3D, not when the GPU has
   finished it. This is safe only while no guest-visible memory depends on
   GPU output. With GPU-authoritative surfaces and no guest surface readers,
   that holds. Otherwise, the release for a batch that produces a read-back
   surface must wait for that readback. This is a deliberate rule, documented
   in FINDINGS. It is not the "mirror fences early" shortcut FINDINGS rejects:
   vertex and index data are consumed from guest memory into the ring buffer
   before the release is written.

**Exit:** no per-flip GPU wait in the NV2A-thread profile, and G1.

## Phase 4: let the game thread run ahead

Each step changes guest-visible timing, so each stays behind its own switch.
Implement related steps as a milestone and measure them in one alternating
campaign; isolate individual switches only when needed to explain a result.

1. **Event-driven flush acknowledgement.** Identify the KickOff routine
   containing or calling the PFB flush poll at `0x00372180`. Replace its
   polling with a wait on an event that the ack path signals when it clears
   bit 16 of `0x100410`, with a short high-resolution timer as a backstop so
   interrupt safe points still run. Registers and protocol are unchanged.
   This follows Blood Wake's `bw_flush_wait` and `bw_flush_acked`.
2. **Idle NV2A thread.** Replace the end-of-pass `Sleep(0)` with: spin while
   work is recent, then nap on a high-resolution timer or until the next
   KickOff signals.
3. **Walk one segment at a time.** Walk each published PUT segment on its own
   and advance GET after each, so a KickOff waiting on GET is released after
   one segment rather than the whole batch. This changes when a held flip
   takes effect relative to later segments. Validate it against flip pacing
   and the PGRAPH trap ordering in [test_pgraph.py](../tests/test_pgraph.py).
4. **Asynchronous KickOff (optional).** Allow up to N segments outstanding
   before KickOff waits on GET. Only consider this if G3 is not met after
   steps 1–3. It relies on D3D reusing ring space only behind GET and on
   resource locks waiting on the fences the walk writes. Verify both for this
   title before enabling it.

**Exit:** G3, and G2 at the movement scene.

## Phase 5: residual CPU cost

Use the latest profile to select this work; batch it with related earlier
phases when it is on the critical path:

- **Executor decode and batching:** per-method dispatch cost, redundant state
  rebuilds per batch, and texture hash cost (move to sparse hashing with a
  per-frame full check, as Blood Wake does).
- **Generated code:** compiler flags (`/O2`, `/GL` with LTCG, `/arch:AVX2`)
  measured by A/B. Also check guest memory-access macro overhead and x87
  emulation hot spots in the title's own code.
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

This is not evidence that the performance gates hold. The complete D3D11
matrix passes 190 hardware cases and 123 WARP cases with 67 hardware cases
skipped, including CPU-written frames before scaled presentation. Bounded
4.5x retail probes show the Microsoft, Digital Anvil and intro movies, but a
matched retail gameplay campaign, VRAM headroom, mip/LOD behavior and a native
4K display remain untested. G4 comparisons continue at scale 1. G5 requires
the controlled 1080p/4K A/B/A campaign in
[NEXTSTEPS.md](../NEXTSTEPS.md), and the reported default-settings movement
regression remains a separate blocker.

## Not in scope, or explicitly rejected

- Further SIMD or threading work on the CPU rasterizer beyond what it needs
  as a reference.
- Skipping or prematurely clearing the PFB flush handshake, or marking
  submitted fences complete before their inputs are consumed. FINDINGS
  explains why these are invalid.
- Direct3D 12, which is deferred.
- Mipmaps are wanted for the 4K target (NEXTSTEPS B10) and are no longer
  deferred for scaled modes; other LOD-transition work stays deferred.
  Preserve existing behavior at scale 1.
- MSAA, widescreen, and frame interpolation. The Phase 1–3 architecture is
  what makes these possible later, but none of them are performance work.
  Higher internal resolution is implemented synthetically as workstream B in
  [NEXTSTEPS.md](../NEXTSTEPS.md), but G5 remains its performance gate.

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
- **Synchronization.** PGRAPH software-method traps, flip holds, and the
  semaphore hook all run on the NV2A thread. The backend must not block that
  thread on the GPU while a trap is pending.
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
  | Phase 0 | 19, 20, 28; also evaluate open PRs #231, #272, #273, and #222 with the benchmark |
  | Phase 1 | 12, 14 |
  | Phase 2 | 13 |
  | Phase 3 | 15 |
  | Phase 4 | 16, 17, 18 |

  Title-specific pieces stay in `src/`: the KickOff hook, Brute Force
  addresses, and census conclusions. Coordinate the back-end interface with
  [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) and
  [#285](https://github.com/sp00nznet/xboxrecomp/pull/285) before preparing
  a PR.
- **README:** status, the renderer switch, and build requirements (the
  D3DCompiler runtime) once a phase changes user-facing behaviour.
- **THIRD_PARTY_NOTICES:** attribution for adapted Blood Wake files.
