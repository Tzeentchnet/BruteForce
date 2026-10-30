# Brute Force bring-up findings

These observations apply to the one locally supplied North American retail
executable identified by [retail.json](../fingerprints/retail.json). Guest
addresses and layouts must not be assumed valid for other revisions. Asset
organization and importer research are tracked in
[ARCHITECTURE.md](./ARCHITECTURE.md); general framework contributions are in
[UPSTREAM.md](./UPSTREAM.md).

## Executable and provenance

- The supported `default.xbe` has title ID `0x4D53001E`, entry point
  `0x002F5D1F`, size 4,366,336 bytes, and SHA-256
  `25b1389b9593f280f1ab0382a776a050977ccb1e4b90611c59e2938286292e16`.
  This identifies that executable, not the whole disc or its assets.
- The XBE parser reports XDK 5233 and named `DSOUND`, `XONLINE`, `XNET`,
  `D3D`, `XPP`, and `BINK` sections. These names show linked code sections,
  not that their host-side equivalents already work.
- The disc contains six additional XBEs. The parser reports, for example,
  `fx\fx.xbe` as **Tao Feng Demo v0018** and `Mech\Mech.xbe` as
  **MechAssault Demo**. Both report the same title ID as the main executable,
  so title ID alone does not distinguish these binaries.
- Generation `20261029-175500-dfadbdd9` translated 22,295 functions with
  zero failed translations, 12 unresolved direct-call targets, and 34
  untranslated instruction sites. The manifest and generated sources are
  ignored local artifacts, not distributed inputs.
- [build.py](../tools/build.py) records and checks the XBE, fingerprint, seed,
  manual-source, and framework provenance. The pinned base, selected PR
  commits, local patches, and expected tree are in
  [toolkit.py](../tools/toolkit.py).

The recompiler's pinned summary calls untranslated sites "no-op comments",
but the generated sites actually call `RECOMP_UNIMPL`. This project's
[handler](../src/recomp_manual.c) aborts except for the narrowly guarded XPP
pair described below. Unresolved indirect calls also abort; generated
unresolved direct-call stubs retain the framework's logged-and-continue
behavior, so a stub hit is a correctness problem, not a successful emulation.

## GPU fence and presentation

**Observed:** `sub_00371CE0` waited for GPU progress during early bring-up.
`sub_00371AB0` submits a `BACK_END_WRITE_SEMAPHORE_RELEASE` method (`0x1D70`).
The graphics device global is at `0x0037DD88`; the device's `+0x2C` field was
observed as a submitted fence sequence and `+0x30` as a pointer to the
semaphore word.

**Mitigation:** The graphics-only
[entry hook](../src/recomp_manual.c) reads the device and semaphore
dynamically, validates the guest pointer, and registers the target before the
release is submitted. The target observed in one run is not hardcoded. A
changed target aborts because pending releases cannot safely be attributed to
an old versus new target. Do not simply mirror submitted fences as complete:
the executor could still be using the associated commands or vertex data.
[test_fence.py](../tests/test_fence.py) covers registration, invalid pointers,
and concurrent calls to the hook; it mocks the executor setter.

The experimental staged walker follows pushbuffer JUMPs and hardware
CALL/RETURN control flow. It uses PFIFO `CACHE1_DMA_SUBROUTINE` (`0x00324C`)
as the authoritative call state: the guest's software-method handler can
modify that register, including clearing its active bit before another CALL.
Invalid streams stop with diagnostics rather than pretending the GET pointer
reached PUT. [test_pushbuffer.py](../tests/test_pushbuffer.py) covers these
control-flow and error cases.

Nonzero subchannel-0 `NO_OPERATION` parameters can be forwarded as PGRAPH
software-method traps when `RECOMP_PGRAPH_IRQ=1` and the staged walker is
enabled. The host holds the GPU until the matching guest handler has
acknowledged and completed the trap. The observed interrupt is vector 3, with
miniport context `0x0037D508`; its fence event is at miniport `+0x1A4`, also
device `0x0037BD40 + 0x196C`. The title-specific hook validates this
relationship and registers only that event for shadowing. It does not enable
shadowing of arbitrary title events. [test_pgraph.py](../tests/test_pgraph.py)
and [test_fence.py](../tests/test_fence.py) cover trap ordering, interrupt
masking, event registration, and failure cases.

Unless a section says otherwise, the opt-in graphics path below uses the
framework's CPU pushbuffer rasterizer and Direct3D 11 presentation; the
experimental GPU path (`RECOMP_GPU=d3d11`) is described in the performance
section. Isolated runs of the verified build reached the
campaign tutorial and rendered a 3D environment. A scripted A press followed
by left-stick input visibly moved the character; injected Z and W window-key
events exercised the same keyboard-to-pad path. In another run, a later
left-stick input alone did not visibly change the captured frame. In a later
isolated run, presented-frame captures before and after a scripted right-stick
input showed the camera rotating around the character. Subsequent
left-stick input also changed the scene. The script runs relative to the first
USB report, while captures were timed from process launch; this is evidence of
scripted response, not a physical-device or end-to-end control validation.

An earlier title-menu region was torn before the presentation handoff fix;
afterward, one checked region matched a completed flip. Some later tutorial
captures were dark and lacked the HUD, while an earlier capture showed it;
see "Frame structure and the dark captures" below for the likely cause. In
one tutorial diagnostic the rasterizer reported approximately 3.7 flips per
second and 149 distinct unhandled GPU methods. These observations do not
establish overall rendering correctness; investigate unhandled methods rather
than treating draw counts as correctness.

The separately opt-in standalone APU initialized XAudio2 in isolated runs and
produced non-silent 48 kHz stereo PCM during the title and menus. Its GP/EP DSP
is logged as stubbed. These captures were not evaluated by a listener and did
not reach the tutorial, so they do not establish audible, correct music,
effects, or gameplay audio. Menu-script timing changed between the silent and
audio-enabled runs; a timed selection can navigate to a different menu. A
subsequent run isolated both partition and user-data roots and captured 160
seconds of non-silent PCM through campaign cinematics, but its watchdog expired
before the tutorial.

## Window-clip investigation

The title submits `SET_WINDOW_CLIP_TYPE` and horizontal and vertical
window-clip methods that the original software executor counted as unhandled.
[Original-hardware observations](https://github.com/xemu-project/xemu/issues/907)
report that writing one rectangle component updates that component in the
following slots as well. The pinned xemu
[shader implementation](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/glsl/psh.c),
[register masks](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/nv2a_regs.h),
and [AA scaling](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/pgraph.h)
describe eight inclusive or exclusive rectangles, 12-bit coordinate fields,
inclusive endpoints, and anti-aliasing scaling. These observations informed an opt-in
software implementation in
[0009-experimental-window-clipping.patch](../patches/xboxrecomp/0009-experimental-window-clipping.patch).

The [native fixture](../tests/window_clip_test.c) checks propagation, rectangle
unions, coordinate masks and edges, scaled pixels, swizzled targets, parallel
workers, both software draw paths, and clipping before depth writes. It also
checks that default-off behavior is
unchanged and that an unsupported hardware backend or invalid setting stops
explicitly. The initial register state is not established by those tests: the
experimental implementation defers clipping until both axes have been written.
In sequential isolated runs with refreshed presented-frame captures, both the
enabled and disabled settings reached the tutorial, responded to scripted
input, and displayed the HUD help screen. Both also produced dark captures
without the HUD. The captures were not synchronized to the same game state, so
they do not establish that clipping fixes those symptoms or that every visual
detail matches. Reported ten-second flip rates in the tutorial portion were
approximately 3.7–5.3 per second with clipping and 3.7–4.7 without it; these
are not measurements of distinct world frames. Both runs ended at the
145-second watchdog rather than completing the tutorial. The option remains
off by default and is not a substitute for comparison with original hardware.

## Face-culling investigation

The title also submits `SET_CULL_FACE_ENABLE`, `SET_CULL_FACE`, and
`SET_FRONT_FACE`; the software rasterizer previously drew both windings.
An isolated early run recorded a back-face selection (`0x405`) and a
counterclockwise front-face selection (`0x901`), while a later tutorial
report recorded culling enabled. Aggregate last values do not prove the
per-draw state. The pinned xemu
[method definitions](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/nv2a_regs.h)
and [renderer state handling](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/gl/draw.c)
informed an opt-in implementation in
[0010-experimental-face-culling.patch](../patches/xboxrecomp/0010-experimental-face-culling.patch).
It handles front, back, and both faces, adjusts triangle-strip winding, and
defers culling until both face modes have been programmed. The
[native fixture](../tests/window_clip_test.c) exercises both draw paths,
primitive types, swizzled targets, near clipping, depth, and interaction with
window clipping.

Sequential isolated tutorial probes using the same build reached a 3D scene
with culling both on and off and responded to scripted input. The enabled run
reported approximately 4.0 million culled triangles by its last periodic
report. The tutorial portions reported roughly 3.5–6.4 flips per second with
culling and 3.1–5.3 without it; the camera and game state diverged, so these
are neither a controlled performance comparison nor distinct world-frame
measurements. HUD visibility also varied between the sampled frames. Both
runs ended at the 145-second watchdog, not tutorial completion. Culling stays
off by default; its reset state and in-game fidelity require independent
hardware comparison.

A separate 260-second run with clipping and culling disabled showed further
scripted movement and a visible HUD in later captures. The character was near
terrain in the final captures; no tutorial completion or transition was
observed before the watchdog expired. Selected tutorial intervals reported
about 2.7–7.9 flips per second, not distinct moving-world frames. These
observations do not establish that the tutorial is playable or that its
rendering is correct.

A subsequent 360-second isolated run used live scripted input to steer around
terrain. Presented captures showed the character moving and the camera
rotating, but did not show a tutorial completion or transition before the
watchdog. During a roughly 23-second interval without scripted input, repeated
window captures showed the health bar appearing and disappearing. Separately,
captures of a stationary view showed a dark image without the HUD and a
brighter image with it. This narrows the reproduction beyond comparisons of
different camera positions, but does not establish whether the title's timing,
the selected frame, or an unimplemented rendering feature is responsible.
Both experiments remained disabled.

## Programmable near-plane investigation

In one stationary tutorial trace, two post-processing or HUD-related draws
reported zero triangles and pixels; another trace reported 2 and 48 triangles
for corresponding draws. A separate stationary run with the existing
`RECOMP_CLIP_NEAR_Z=0` diagnostic switch retained the health-bar region in
all 533 sampled window captures over roughly 60 seconds. Those captures were
repeated window samples, not 533 distinct world frames; selected tutorial
intervals were roughly 3.2–3.9 flips per second. The trace request followed a
sampled image but the trace starts at a later flip increment, so the image and
traced draws are not proven to represent the same frame. Disabling near-depth
clipping altogether can admit geometry between the camera and the near plane.

The CPU renderer's legacy programmable path uses vertex constant `c[59].z`
as the screen-space near boundary. The pinned xemu
[programmable vertex output conversion](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/glsl/vsh-prog.c)
instead normalizes screen-space Z by a format-dependent positive zeta maximum
before converting to GL clip space. Its
[uniform setup](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/glsl/common.c)
also supplies the guest Z-clip minimum and maximum separately. For positive W,
the programmable vertex conversion implies a screen-space zero near boundary,
independent of `c[59].z`. Xemu's
[pixel shader generation](https://github.com/xemu-project/xemu/blob/dc10b4cea96e403d30c17ed53551fdaf023bc17f/hw/xbox/nv2a/pgraph/glsl/psh.c)
also clips or clamps fragments against the guest Z-clip bounds, depending on
depth-clip state. This experiment does not add that behavior or a far plane.
Neither implementation has been established as equivalent to original
hardware in all viewport and depth modes.

For the two post-processing draws in both earlier traced intervals, the
preceding method writes specified a guest Z-clip minimum of 0, maximum of
16,777,215, and Z-min/max control of `0x00000001`; the first reported vertices
had screen Z 0 and W 240 and 120, respectively. Xemu interprets that control's
Z-clamp field as culling rather than clamping. This supports investigating
the zero boundary for those draws, but is not a hardware comparison or a
general depth rule.

The opt-in
[programmable near-plane experiment](../patches/xboxrecomp/0011-experimental-program-near-plane.patch)
tests that boundary while retaining the eye and near-depth planes and leaving
the default and fixed-function paths unchanged. Its
[native fixture](../tests/window_clip_test.c) covers nonzero viewport offset,
zero, positive, negative, and crossing depths, negative and crossing W, depth
writes, strips, AA, and interactions with the other software experiments.
This tests the proposed behavior, not hardware correctness.

Two sequential isolated runs of generation `20261029-170900-76fb2a5f` used
the same scripted inputs, 12 raster threads, and no window clipping or face
culling. During a nominally stationary 60-second interval, a red-pixel
classifier over the health-bar region found it in 103 of 591 window captures
with the legacy plane and 597 of 597 with screen-zero clipping. The captures
were sampled approximately every 0.1 seconds and can repeat a frame. They are
not a distinct-frame count; the runs are not frame-synchronized or a comparison
with original hardware.

In the legacy run's traced programmable draws, `c[59].z` was `0.0501511`,
while the first three vertices of the 320-by-240 and 160-by-120 passes had
screen Z 0 and positive W 240 and 120, respectively. The legacy near-plane
equation therefore rejected those first triangles; the draws reported zero
triangles. In the experimental run, corresponding draws used selected near Z
0 despite `c[59].z = 0.208894`, and reported 2 and 48 triangles. Each trace
was requested after a sampled image but began at a later flip increment, so it
is not proven to be the exact frame that produced the triggering image.

After the stationary interval, the scripted stick inputs changed the camera
and character position in both runs, and the tutorial HUD-help panel appeared.
Neither run showed tutorial completion or a transition before the
210-second watchdog. Selected experimental tutorial reports were roughly
2.7–4.1 flips per second, not distinct moving-world FPS or a controlled speed
comparison. Physical input, audio, general HUD and lighting correctness, and
the tutorial performance gate remain unverified.

An extended isolated run with screen-zero clipping and scripted live input
showed help panels, movement, and an objective screen, but did not show a
tutorial exit before its 480-second watchdog. Presented captures at about
180 and 190 seconds after launch were entirely black; the scene returned in
the 200-second capture. This experiment therefore does not establish general
HUD or scene correctness.

## Software rasterizer performance

In sequential isolated stationary tutorial probes of generation
`20261029-170900-76fb2a5f`, with screen-zero clipping enabled, the final six
ten-second reports were 3.37–3.59 flips per second with 12 raster threads
and 2.07–2.29 with four threads. These are flip reports during a stationary
scene, not distinct moving-world FPS or proof of playability.

A separate native instruction-pointer sample collected 3,600 observations
across 12 CPU-active threads, including 2,266 waits. It also observed
`sample_tex` 296 times, `nv2a_rc_eval` 180 times, `read_rgb` 119 times,
`nv2a_rc_unpack` 107 times, `xf_rows` 103 times, and `nv2a_vsh_run` 58
times. The guest PFB flush polling function at `0x00372180` appeared 210
times. Sampling overhead and waits make these counts unsuitable as precise
CPU-time attribution. The flush acknowledgement observes flip holds and
PGRAPH state; skipping or prematurely clearing the handshake would not be a
valid optimization.

The color-unpack and texture-sampling optimizations in
[0012](../patches/xboxrecomp/0012-vectorize-color-unpack.patch) and
[0013](../patches/xboxrecomp/0013-optimize-texture-sampling.patch) were
compared in three sequential isolated runs: optimized, baseline, optimized.
Both builds used the generated C from `20261029-175500-dfadbdd9`; the baseline
was independently built against the pre-0012 framework tree
`ee115ae11bda5681fe95ffcf50961c25897f14d1`, with the game executable's
fingerprint verified. Each run used 12 raster threads, a 640-by-480 presented
frame with vsync, experimental screen-zero clipping, and disabled window
clipping and face culling. Captures around 150 seconds showed the same
stationary tutorial location, but are not frame-synchronized comparisons.

The final six ten-second flip reports averaged 4.005, 3.520, and 3.990 per
second, respectively. The optimized averages were about 13–14% higher than
the single baseline run, but this limited comparison does not separate the
two patches or establish a general speedup. All three runs ended at a
165-second watchdog, not tutorial completion. They remain far below the
moving-world playability gate, and flip rates are not distinct world-frame FPS.

## Hardware-renderer evaluation

The pinned [Blood Wake D3D11 renderer](https://github.com/minimalice92/bloodwake-recomp/blob/e631edddfe56d22344e9f696923aeb6d78a9e7ba/src/gpu/bw_d3d11.c)
and shader generators compile against this project's framework headers, but
the renderer has additional title-specific and extended-runtime dependencies.
In an isolated synthetic D3D11 experiment on an NVIDIA GeForce RTX 5070,
generated combiner shaders matched the framework CPU evaluator for modulation
and a two-input final combiner; an alpha-test discard also passed. A generated
vertex shader's position and diffuse output for a synthetic screen-space
epilogue also matched the CPU interpreter after GPU readback. These few cases
do not establish full shader parity, title compatibility, or rendering
performance.

The current backend interface does not pass all four texture stages and
register-combiner state. The executor also writes semaphore releases directly
to guest memory. An asynchronous hardware backend must explicitly complete and
read back relevant GPU work before guest-visible memory, fence releases,
software-method traps, or frame snapshots can rely on it. A compiled shader
or a swap-chain present alone is not that guarantee. Scaling, widescreen HUD
placement, and changed frame timing require additional title-specific work.

The guest-memory statistics patch accounts for the title's guest allocations
rather than the host's available memory. Two observed x87 function entries,
`0x002F04DC` and `0x002F04E3`, are recorded in the seed list. Both were
needed during tutorial-resource bring-up. [test_memory_stats.py](../tests/test_memory_stats.py)
exercises retail, devkit, extended-map, and retained-memory configurations.

## XPP callbacks and interrupt masking

- An indirect call at `0x0037F1CA` reached the XPP callback at `0x0037EE7C`.
  The target begins with a function prologue but was missed by automatic
  discovery in mixed code/data. It is an observed seed in
  [retail-seeds.json](../fingerprints/retail-seeds.json), not a silent stub.
- The observed `sub_00384F99` sequence has `CLI` at `0x00384FA5`, three
  loads, `STI` at `0x00384FB6`, and the pure register move `mov esi,eax` at
  `0x00384FB7`. Its caller was traced through `sub_003855B3`.
- The title-specific handler models only this pair when `RECOMP_USB` is set,
  strict IRQ safe points are enabled without forced ungated delivery, the
  current thread owns the dispatch gate, and IRQL is DISPATCH level 2 or
  device level 16. A diagnostic actually encountered the sequence at IRQL 2.
  The handler updates EFLAGS.IF and checks pair state; unexpected contexts
  abort. It is not general `CLI`/`STI` or `POPFD` emulation.
- [build.py](../tools/build.py) validates the generated loads and the
  post-`STI` register move on generation/build/run. The strict gate defers
  IRQ delivery through this validated region and its relevant STI shadow.
  [test_interrupt.py](../tests/test_interrupt.py) covers permitted levels,
  deferred IRQs, and unsupported contexts.

## USB, saves, and diagnostics

USB is opt-in. Isolated Release diagnostics reported six setup requests and
thousands of periodic reports. Scripted inputs navigated the menus and moved
the character in the tutorial. A separate run injected keyboard messages into
the game window with `RECOMP_KEYBOARD=1`. Physical input devices and broader
playability remain unverified. [test_usb.py](../tests/test_usb.py) exercises
enumeration and worker lifecycle using synthetic fixtures.

The patched `xbox_OhciInit()` returns 1 for active, 0 for disabled, and -1
for failure. Shutdown requires guest threads that may access USB MMIO to stop
first; on shutdown failure, guest mappings and the MMIO handler must remain
available. Because the host cannot safely join arbitrary guest workers, it
terminates the process if guest entry returns with USB active. Clean in-process
restart is not supported. See [main.c](../src/main.c) and the
[OHCI patch](../patches/xboxrecomp/0006-harden-usb-lifecycle.patch).

`BRUTEFORCE_SAVE_ROOT` controls partition-image placement and sets
`RECOMP_SAVE_DIR` only if that variable is unset. Set both to the same fresh,
existing short directory for isolated diagnostics. Unicode paths were
verified on disk, but the upstream kernel logger can truncate non-ASCII path
text. `RECOMP_WATCHDOG_SECS` is a fixed timeout; exit code 3 does not by itself
prove a hang. Successful read-byte traces are opt-in and may contain game
data; failed reads remain logged by default.

## Performance plan: Phase 0 and the first GPU-path results

All runs below used generation `20261029-175500-dfadbdd9`, isolated saves,
12 raster threads, the experimental screen-zero near plane, and the
[benchmark harness](../tools/bench.py). Runs alternated between variants
(A/B/A...). Figures marked "rendered" count flips that each carried a full
draw set; "distinct" counts flips whose picture changed (frame statistics,
patch 0014). Profiles, censuses, captures and logs stay under
`analysis/bench/`.

### Scenes

**Observed:** the menu script reaches the tutorial's 3D world about 64 s
after the first flip on every renderer. The world then shows a fixed-camera
view. On the CPU path every flip in that view is a full re-render (about
1,600 draws and 132,000 program vertices) whose picture is byte-identical to
the previous one, so its distinct-frame rate is 0 by construction. The
harness therefore measures this "stationary hold" as rendered frames per
second over 120 frames.

**Observed:** the hold lasted at least 382 frames in one CPU run and about
360 in another, where the view then changed about 100 s after the world
appeared. **Hypothesis:** its length is a frame count, not a time, so faster
renderers see it end sooner; the harness defines scenes by frames and
content, not by wall-clock windows. In other runs the hold did not end on
its own within 200 s, so the movement scene starts its scripted live input
once the hold has been measured. Help panels pause the game until A is
pressed; the harness presses A when the picture stays unchanged for 8 flips
during movement.

**Observed:** one benchmark boot hung during loading, before the menus,
with the guest calling `KeDelayExecutionThread` millions of times and no USB
interrupts posted. The same build passed that point in every other run. The
harness detects a stall (no flip for 60 s) and retries the run once,
recording the retry.

### Feature census (CPU path, whole run, 3,219 flips)

- Every draw took the vertex-program path, including the HUD; no program
  wrote constants.
- Stencil test enabled in about 12% of draws (ALWAYS with REPLACE and
  increment operations). The CPU rasterizer ignores stencil state.
- Render to texture: 640×480, 320×240 and 160×120 colour surfaces are each
  sampled as textures (517 draws each in this run), and a 512×512 depth-only
  pass precedes part of the scene.
- Stage modes 2D, 3D-projective, cube map and pass-through only; no bump-env
  or dot-product modes.
- No anti-aliasing; one 640×480 A8R8G8B8 + Z24S8 main surface; DXT textures
  dominate; 149 distinct unhandled methods (top: `NO_OPERATION`, other-stage
  input, dot-RGB mapping, cull enable, lighting and specular controls,
  window clip).

### Frame structure and the dark captures

A traced tutorial frame drew 298 scene batches into the back buffer
(`0x002B0000` in that run), a 512×512 depth-only pass, 597 more scene
batches into the back buffer, a 320×240 and then a 160×120 downsample of the
back buffer, and finally 102 batches of glow composite and HUD into the back
buffer before the flip presented it. Front and back buffers alternated with
`PCRTC_START`. **Supported hypothesis:** the earlier dark captures without
the HUD were report-time `RECOMP_FB_DUMP` captures, taken at an arbitrary
point mid-frame, before the composite and HUD. The presenter and the
flip-time frame hashes read the frame at `FLIP_STALL`, after both. Report
dumps from movement runs alternate between finished frames and such dark
frames (UPSTREAM item 21).

### Profiles

Stationary hold, CPU path (60 s window): the game thread spent 82% of its
samples in the guest PFB flush poll `sub_00372180` and 11% in kernel waits.
The NV2A thread was 93% busy: `nv2a_vsh_run` 34%, combiner evaluation,
texture sampling and spans about 40%. Raster workers were about 32% busy
each. The poll does not wait for a slow acknowledgement (the walker clears
the flush bit between commands); D3D waits there for ring space, which only
returns as the NV2A thread executes the commands.

GPU path, movement (one run): the game thread still spent 57% in the flush
poll and 30% in `NtQueryVirtualMemory` (5% on the CPU path; caller not yet
identified). The NV2A thread was 77% busy. Its largest costs were
synchronous visibility-test reads, CPU texture decoding for uploads
(`sample_tex`, 16%), full texture hashing (`get_texture`, 12.5%) and
`getenv` in per-draw code (`strchr`, 4.5%). The title read 200–600
visibility reports per frame, and waiting for them took 1.5–3.9 s per 10 s
of NV2A-thread time.

### Visibility tests with a registered back end

**Observed:** with the first D3D11 back end, the world was not drawn: frames
carried about 8,000 program vertices instead of about 130,000, and only the
HUD and the clear colour appeared. The executor answers visibility tests
(`GET_REPORT`) with pixel counts only the CPU rasterizer accumulates, so the
title saw every test as hidden and culled its geometry. Patch 0016 lets a
back end count them; the D3D11 back end uses occlusion queries, and the
world is then submitted and drawn. The null probe reports every test as
visible, so it draws more than the real path (about 2,040 draws per flip
against 1,610). An earlier asynchronous variant (patch 0018, `RECOMP_GPU_ZPASS=async`)
lost its WARP device at the second poll. The fixture had allowed two threads
to use a single-threaded D3D device concurrently; that API violation was a
plausible contributor, but its effect was not isolated from query-lifetime
changes. The current implementation polls on the device-owning thread,
retires in-flight queries safely, orders each report with an event query, and
surfaces query failures. It passes synthetic WARP and hardware fixtures and
was exercised in the campaign below; asynchronous mode remains opt-in.

### Stationary comparison (13 runs, alternating)

| Variant | Runs | Rendered fps (mean, range) | 1% low | Game thread waiting on GPU | NV2A thread busy |
| --- | --- | --- | --- | --- | --- |
| CPU rasterizer | 3 | 3.20 (2.82–3.58) | 2.52–3.44 | 88.5–93.1% | 92.5–93.0% |
| D3D11, vertex programs on CPU | 3 | 7.46 (7.41–7.53) | 6.81–7.00 | 87.9–88.0% | 92.4–92.7% |
| D3D11, vertex programs on GPU | 3 | 30.41 (30.25–30.64) | 22.81–24.53 | 53.4–54.8% | 80.2–82.9% |
| Null back end | 2 | 4.27 (4.24–4.30) | 3.93–4.18 | 87.8–88.4% | 95.0–95.2% |
| Null, transformed batches dropped | 2 | 32.77 (30.18–35.35) | 15.79–27.58 | 12.7–13.6% | 59.2–60.0% |

All stationary distinct-frame rates were 0, as the hold's picture does not
change. The null rows are not directly comparable: without pictures the hold
is found from the vertex-constant signature, their world was detected at
about 36 s in a different, heavier scene, and their visibility tests read
everything visible. They do show that, with CPU rasterisation removed but
vertex programs still interpreted on the CPU, the rate stays near 4, and
that the walk and the title alone reach about 30–35.

**Hypothesis, not yet established:** the title's native rate is 30 Hz. The
GPU path and the walk-only probe both settle near 30 frames per second in
the hold; the swap interval has not yet been read (flip pacing is off in
these runs).

### Movement (one run each, not yet A/B/A)

The CPU path rendered 12.1 flips per second with 2.96 distinct frames per
second during the scripted route (help panels paused part of it). The GPU
path, with vertex programs on the GPU and synchronous visibility reads,
rendered 63.3 flips per second with 20.8 distinct frames per second (1% low
18.2) and 57.5% game-thread waiting. Neither run is a G2 measurement yet.

### GPU offload milestone (2026-10-29)

The [combined campaign](../analysis/bench/gpu-offload-20261029-fixed/summary.json)
used generation `20261029-175500-dfadbdd9`, framework tree
`ec3fa862999fa1f2bd58d5eaffbaee04d7baf68d`, 12 raster threads, scripted
movement with a stationary hold, flip captures, profiles, and three
alternating runs per variant. Both use `RECOMP_GPU=d3d11` with GPU vertex
programs. The baseline uses `RECOMP_GPU_ZPASS=sync` and
`RECOMP_GPU_TEX_DECODE=0`; the candidate uses `RECOMP_GPU_ZPASS=async` and
`RECOMP_GPU_TEX_DECODE=1`. The D3D11 hardware device was selected by default;
the specific adapter was not established. Both variants use direct BC and
linear texture uploads; the toggle changes compute decoding of the other
eligible formats.

| Metric | Baseline mean (range) | Candidate mean (range) |
| --- | --- | --- |
| Stationary rendered fps | 32.60 (31.72–34.12) | 45.06 (40.91–50.28) |
| Movement pixel-change fps (not world fps) | 25.15 (24.83–25.59) | 35.23 (32.84–39.82) |
| Movement pixel-change 1% low fps (not world fps) | 21.46 (20.54–22.25) | 32.98 (31.04–36.00) |
| Game-thread GPU wait, sampled | 33.54% (33.30–33.77%) | 24.72% (22.30–29.54%) |

The stationary hold has zero distinct frames. A later audit of each movement
window found that 58.855–60.197% of its flips matched the exact hash of a
solid-black 640×480 four-byte image with alpha zero. The percentages count
flips, not elapsed time. The pixel-change rate therefore does not measure
visible world throughput; transitions to and from black and HUD changes can
also count as changes. Texture decoding and asynchronous reports were changed
together; the campaign does not attribute their individual contributions.
One candidate boot stalled after 12 flips and its retry
completed, producing the highest candidate rate. The failed attempt is not
included in the means. This is not a playability or native-rate claim.

Including those black-frame intervals, the game thread spent 51.8% of samples
in `NtQueryVirtualMemory` in the first baseline run and 60.2% in the first
candidate run; texture hashing
accounted for 6.7% and 7.2% of NV2A-thread samples respectively. The
candidate log records GPU compute and direct BC texture uploads and
asynchronous report writes. These are sample and event counts, not a
wall-clock breakdown.

### Rendering equivalence (G4)

On synthetic WARP fixtures the GPU path matches the CPU rasterizer within
2/255 except for pixels whose centre lies exactly on a triangle edge (the
CPU rasterizer includes both sides; D3D's top-left rule does not). This
holds with the vertex program interpreted on the CPU and run on the GPU,
with combiners, a swizzled texture, depth testing, blending, render to
texture without readback, a texture rewritten at the same address and a
visibility count. In the title, by eye, the GPU path draws the world, HUD,
glow and shadows; some frames show a very bright stone statue that has not
been compared with the CPU path.

**Observed:** flip-time captures of the same stationary tutorial panel from
`phase0-stationary/run01-cpu` and `run02-d3d11-cpuvsh` (generation
`20261029-175500-dfadbdd9`, framework tree
`7bce785a6d21f4e22df8a4a5d79353b42afa793e`) differ by more than 8/255 in
66,470 of 307,200 pixels (21.64%). The panel broadly aligns, but large
background areas are darker or nearly black on D3D11, with additional UI
and geometry differences. Stencil, depth, sampling, and edge rules are
possible contributors, not established explanations. G4 is not met.

The new GPU-vertex flip captures match the tutorial panel much more closely:
against that earlier CPU image, the first new baseline differs in 581 pixels
(0.19%) and the first candidate in 1,612 pixels (0.52%) by more than 8/255.
The two new GPU images differ in 285 pixels (0.09%). These are
content-matched stationary captures, not proven frame-locked, and the CPU
image uses the older framework tree. Remaining differences, including a
concentration near the left background, are unexplained. The cause of the
older CPU-vertex image's larger dark regions is also unverified.

The prior GPU-vertex runs did not actually save the requested flip captures:
the harness could open and close its capture flag while processing an
already-buffered batch of frame records. The unsuffixed `capture/hold` file
is a report-time title-screen image, not a tutorial flip. The harness now
waits for two numbered flip images and lets capture settle before timing the
stationary hold. Comparisons must use those numbered images.

The movement campaign also logged repeated failed decodes of texture format
`0x2E` (`LIN_D24S8`) on both the synchronous/CPU-texture and
asynchronous/GPU-texture variants. The reference CPU sampler lacks that
format as well. In a separate null-backend run, the same address
`0x81754080` was recorded as a 512x512 depth surface. This supports
investigating an alias, but separate runs and an address match do not prove
the complete layout, sampling semantics, or effect on the image.

The null surface-guard diagnostic reached the stationary hold and movement,
then aborted with `Invalid GPU semaphore guest address 0x80001000`. Its
recorded colour range `0x80000000..0x800FFFFF` covered that page, and the
semaphore pointer check treats protected pages as inaccessible. The report
cannot establish whether all guest surface readers have been found. The
optional `BRUTEFORCE_SURFACE_GUARD_EXCLUDE_PAGE` now leaves one explicitly
reported page unprotected for a follow-up diagnostic; that page, kernel-mode
accesses, and later accesses to an already-unprotected page remain blind
spots. Do not remove per-flip readback on this evidence.

A follow-up run with only page `0x80001000` excluded completed both the
stationary hold and the 40-second movement window. The last periodic report
recorded 1,871,291 faults and seven non-NV2A-thread reads at an unsymbolized
site, with first address `0x817F0740` inside the guard's estimated
`0x81754080` depth range. Resolving the recorded address using the same
executable's image base from a separate isolated process and its local PDB
(`0x7FF742490000`, giving RVA `0x221242C` for the recorded RIP) points to
`ohci_do_td`'s USB rumble-report read
(`src/usb/ohci.c:674` in the framework). The original process's image base
was not saved, so this identification is provisional. The guard estimates
depth extent from the colour size, and the null backend never writes
rendered depth; the read is not proof of a guest consumer of GPU depth.
Future guard reports include a module-relative offset when symbol lookup
fails. The run's constant-signature rate is a diagnostic only, not a visual
FPS result.

The experimental D3D11 depth path uses D24S8/R24 views and a separate GPU
snapshot when sampling a currently bound depth target. It accepts only exact
address, format, linear layout, dimensions and pitch matches. A newly tracked
eligible surface is initialized from guest Z24S8 memory, rotating its depth
and stencil fields to the D3D layout; a full GPU depth clear also initializes
it. A recreated GPU depth surface is not reseeded from potentially stale
guest memory and is rejected until cleared. PROJECT2D and PROJECT3D shadow
comparisons are supported; guest writes, CPU decoding and partial aliases
remain unsupported. PROJECT3D is limited to extended draws, and a draw with
an unsupported depth alias or stage mode is skipped with a diagnostic. The
resource and view approach is informed by Blood Wake; the comparison behavior
follows xemu's published shader translation. These are emulator prior art,
not proof of original-hardware equivalence.

### Packed depth/stencil aliases and shared presentation

The public [brute-force-2003-reloaded](https://github.com/kduong-dev/brute-force-2003-reloaded)
project is a native Rust/Bevy recreation, not an xboxrecomp GPU backend. Its
glare research describes a 320x240 bright pass, stencil thresholds, a
160x120 blur and an additive composite, with approximations that are not
proof of original-hardware behavior. A local captured bright pass samples
`0x803DC000` as stage-1 format `0x41` after that allocation was used as a
depth surface; a later census recorded 635 active-stage observations of
`0x41`. This makes a packed depth/stencil alias worth testing but does not
prove it causes the reported sun-facing bloom.

The optional D3D11 path reconstructs the Xbox depth bytes and stencil alpha
from GPU-owned D24S8 resources using a compute shader, caches the result
until the depth surface changes, and snapshots a currently bound target.
Exact address, dimensions, pitch, linear layout, format and initialized
depth and stencil planes are required. Unsupported overlapping aliases
are rejected, while non-alias `0x41` textures keep their original path.
Guest writes, partially overlapping aliases and CPU stencil emulation
remain unsupported.

Patch 0024 and the optional renderer path hand one completed 32-bit frame
to the window device through a keyed-mutex shared texture. Snapshot IDs
avoid showing a pending old GPU frame over a newer CPU frame; PVIDEO and
pinned framebuffers fall back to CPU-composited frames. Per-flip guest
readback, screenshots, statistics and guest synchronization are retained.
The one-slot bound applies to presentation snapshots, not all GPU work in
flight, and copying and flushing the shared texture may add cost.

During the alternating comparison, the user reported that the bloom had
returned despite looking improved in most recent runs. A baseline capture
with both new options disabled also shows very bright vegetation and sky,
while another baseline frame and a candidate frame are darker. The camera
angles are not frame-locked, so these captures do not identify the cause or
prove that either new option fixes or causes the effect. The character is
visible in the inspected gameplay captures, but not every frame was checked.

The [six-run comparison](../analysis/bench/20261030-005113/summary.json)
used generation `20261029-175500-dfadbdd9`, tree
`a4489cd69600a1489edea98ac19cf20db242b6d2`, 12 raster threads, isolated
saves, scripted movement, captures and a gated profiler. Each variant ran
three times in alternating order without retries. Both used D3D11, GPU depth
aliases, window clipping and async visibility; the candidate also enabled
packed-stencil aliases and direct presentation.

| Metric | Baseline mean (range) | Both options mean (range) |
| --- | --- | --- |
| Stationary rendered fps | 44.94 (44.19–46.18) | 40.18 (23.56–57.69) |
| Movement flip fps | 68.87 (61.92–77.44) | 66.05 (40.53–87.77) |
| Movement whole-frame pixel-change fps | 67.16 (60.22–76.16) | 64.00 (39.35–84.72) |
| Estimated game-thread GPU-wait samples | 19.43% (17.42–23.34%) | 31.18% (19.72–51.55%) |

These are not distinct world-frame measurements: the hashes include HUD
changes, and captures and profiling perturb timing. The large variation,
especially the slow final candidate run, gives no established performance
gain and may indicate a regression. None of the 16,194 movement flips
matched either known solid-black hash; other dark images and intentional
fades are not classified. Periodic candidate logs recorded 8,929 packed
alias conversions, 13,628 shared snapshots queued and 35,208 busy-slot
events, excluding final partial intervals. Each candidate logged at least
one shared GPU frame actually presented. These counters do not mean every
flipped frame used direct presentation.

The first stationary capture from each baseline/candidate pair differed in
145,364–145,382 of 307,200 RGB pixels (47.32%) beyond 8/255, with markedly
darker candidate backgrounds. Repeated baseline captures differed by only
22 pixels, and repeated candidate captures by 44, at the same tolerance.
The images show the same tutorial panel but are not proven frame-locked.
Against the earlier CPU capture from a different framework tree, the first
baseline differed in 22,666 pixels (7.38%) and the first candidate in
147,760 (48.10%). The CPU renderer ignores stencil, so it is not an oracle
for this glare path; neither that comparison nor the combined A/B isolates
which new option caused the difference or establishes hardware fidelity.
This material rendering difference remains unexplained and G4 is not met.

One combined Release build and targeted pytest batch passed 132 tests,
including WARP and hardware packed-alias, exact shared-pixel transfer,
queue ordering, fallback, existing depth and clear, frame-statistics and
build-tool fixtures. Synthetic success does not establish title-wide
rendering fidelity, bloom correctness or playable world FPS.

### Rectangular clears and software-depth ownership

A traced 640×480 world frame first clears the surface to `FF2674A1` and far
depth, then clears black/zero-depth letterbox strips at y=0–68 and y=410–479.
The D3D11 backend previously treated each strip as a full-surface color and
depth clear, so a strip could zero depth across the scene and reject later
geometry. The backend now clamps and scales the clear bounds, preserves
individual color channels, and clears partial depth and stencil regions with
a scissored draw. Full-surface clears keep their native fast path. Internal
clear draws are excluded from active visibility queries. A partial depth
clear does not make an otherwise uninitialized whole depth resource safe to
sample. If both clear-rectangle axes have not been programmed, the full-clear
fallback remains; original-hardware reset behavior is not established.

The software executor also cleared its 1280×1024 float depth buffer on
every Z clear, even with a GPU backend. Patch 0022 skips that roughly 5 MiB
write only when the registered backend has both draw and clear callbacks.
CPU rendering and incomplete backends retain it. Per-flip readback and guest
flush, semaphore, report and flip ordering are unchanged. The census records
which path owns each depth clear.

One depth/clip diagnostic after the rectangular-clear fix showed the
character in captured bright and turned views. Its movement window had
zero matches to the exact alpha-zero or opaque solid-black 640×480 hashes
among 3,458 flips, compared with 1,932/3,319 in an earlier clipping-only
diagnostic and 2,325/3,713 in an earlier seeded depth/clip diagnostic. The
runs differ in timing and tutorial-panel behavior; they are not a controlled
performance comparison, proof that every frame is correct, or proof that
all dark/black images are absent. Twelve periodic reports recorded 54,354
accepted and zero rejected depth samples, excluding the final partial
interval. The fix is a plausible contributor to the earlier missing geometry,
not proof that it explains every character-visibility report.

After the software-depth offload and flip-only capture fix, one instrumented
depth/clip run captured the character in a gameplay view; an earlier capture
interval still showed a tutorial panel. Its 1,154 movement flips contained no
matches to the two known 640×480 black signatures. The census counted 1,191
gated flips and 3,197 backend-owned depth clears, with no software-owned clears
or dropped census keys. Gate timing and the movement window do not align
exactly. The run reported 28.83 rendered and 27.08 pixel-change fps, with a
pixel-change 1% low of 11.82 fps, but includes the HUD, tutorial timing,
captures and census overhead. It is not a controlled comparison or a
measurement of distinct world fps and does not establish playability.

An initial combined depth/clip campaign was stopped after one completed run
per variant when the character was reported invisible. Before guest-memory
seeding, the depth candidate's 14 periodic reports recorded zero accepted
depth samples and 29,261 rejected samples, because its `0x81754080` depth
surface had not received the assumed GPU clear. These are only reported
interval totals, not a count for the entire run. The baseline had one early
boot stall and succeeded on retry. One clipping-only diagnostic captured the
character in a sun-facing view, but did not isolate every scene or prove the
cause of the reported disappearance. Its route later presented black frames
with roughly 166 batches and 8,000 program vertices for a prolonged interval;
its whole-window fps is not a playability measurement.

After guest-depth seeding, a single depth/clip diagnostic completed. Its 12
periodic reports recorded two depth seeds, 19,029 accepted depth samples, no
snapshots and no rejected depth samples; the final partial interval is not
included. A movement capture shows the character, and the traced frame binds
the 512×512 depth texture in stages 1 and 2. This is evidence that the earlier
rejection was removed in that run, not proof that the character remains
visible throughout gameplay or that the sampled shadows are hardware-correct.
The diagnostic's movement captures and trace perturb timing, and its single
run is not a performance comparison.

The user also observed strongly bloomed, bright vegetation and sky while
looking toward the sun, with a darker view after turning. An earlier traced
world frame includes downsampling and an additive glow composite, but the
effect's fidelity and the cause of the brightness change remain unverified.
In the seeded diagnostic, movement captures also show dark and much brighter
views, but the camera and capture times differ. The new optional movement
captures and adjustable frame-trace timing are diagnostic tools; compare
matched views and a trusted reference before changing lighting or gamma.

The P8 compute path and palette cache use a 256-entry palette, matching the
current CPU decoder. Palette-length bits and shorter palette allocations are
not modeled or validated; disabling compute decoding does not disable the
D3D11 cache's 256-entry palette hash. The existing approximate texture cache
can also miss same-frame raw texture rewrites and changes between sparse hash
samples; P8 palette contents are checked separately on each use.

Known differences, by design: stencil is honoured on the GPU path and
ignored on the CPU path; depth is D24 rather than a float buffer; textures
are sampled at level 0 only on both paths.

## Current validation boundary

Generation `20261029-175500-dfadbdd9` built in Release against verified
framework tree `a4489cd69600a1489edea98ac19cf20db242b6d2`. Patch 0024
applies independently to tree `8d57b4ceb02054f4921a88a7233e6722625c14ff`
and reconstructs the current tree. Patch 0023
applies independently to tree `f2e260a5a6845c2edc0b634e82265210e47fe759`
and reconstructs the preceding tree. Patch 0022 independently reconstructs
that preceding tree from `3472bac8e33c1758c801fb1ca65a346a170ad8e5`.
An earlier combined batch of the renderer, frame-statistics, window-clip and benchmark suites,
plus three targeted build-metadata tests, passed 94 tests with WARP and
hardware devices. These include 16-frame partial-clear fixtures, CPU,
draw-only, clear-only and complete backend ownership cases, and default
versus gated flip-only frame dumps. The benchmark suite also covers known-black
reporting and keeps periodic reports active when capture and census are combined.
Patch 0021 independently reconstructs tree
`3472bac8e33c1758c801fb1ca65a346a170ad8e5` from
`ec3fa862999fa1f2bd58d5eaffbaee04d7baf68d`.

The previous framework tree was
`ec3fa862999fa1f2bd58d5eaffbaee04d7baf68d`. Patch 0020 applies independently
to its preceding tree
`8f679118ffd6173765a505c5ca666a353877bcc1` and reproduces that previous tree.
The project suite passed 194 tests with all native fixtures configured,
including WARP and hardware texture, native BC1/2/3 cube/mip, palette-rewrite,
and asynchronous visibility cases. After the retry-reporting change, the
focused benchmark suite passed 14 tests. An earlier run passed 14 upstream
wrapper and dispatch tests. Generation still reports 12 unresolved call
targets and 34 untranslated instruction sites. One candidate boot stalled and
required a retry. For current commands, see [README.md](../README.md#tests).

Open work includes broader gameplay and input, physical controllers, audio,
graphics compatibility, unresolved calls and instructions, and safe device
reset behavior. Direct3D 12 is deferred.
