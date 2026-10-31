# Brute Force bring-up findings

These observations apply to the one locally supplied North American retail
executable identified by [retail.json](../fingerprints/retail.json). Guest
addresses and layouts must not be assumed valid for other revisions. Asset
organization and importer research are tracked in
[ARCHITECTURE.md](./ARCHITECTURE.md), the current rendering architecture is
in [RENDERING.md](./RENDERING.md), and general framework contributions are
in [UPSTREAM.md](./UPSTREAM.md).

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

## Private xemu reference attempt (2026-10-30)

The supplied XISO was read without extraction using the toolkit's read-only
XISO reader. Its root `default.xbe` matched all four fields in `retail.json`:
size, SHA-256, title ID and entry point. The same check passed after the run;
the ISO's size and modification time also remained unchanged. This verifies
the executable, not every disc byte.

One isolated xemu 0.8.139 session (source commit
`aa54c34cae1c09410d74c69a44210a9867fe2d16`) started paused with
networking, update checks and NVIDIA profile setup explicitly disabled, dummy
audio, a private EEPROM and `-snapshot`. Before its single `cont`, QMP
confirmed the requested optical image and its backing were read-only and the
HDD snapshot backed a separate private copy. QMP's named block nodes placed
both snapshot overlay files in the private run directory; the HDD backing's
hash was unchanged afterward. The configured display was OpenGL, surface
scale 1, nearest filtering and 4:3. xemu reported an RTX 5070 with NVIDIA
driver 617.42; its DXGI presentation initialization failed and it reported an
SDL OpenGL swap fallback, with no established effect on the captures.

One prelaunch and 23 running captures targeted only the PID-verified xemu
client window. They are 426×320 scaled/composited `PrintWindow` images,
**not** pixel-exact 640×480 guest captures. At 41.94 guest seconds, two local
OCR preprocessing variants recognized the developer name Digital Anvil; an
independent synthetic-text OCR control passed. This is evidence of displayed
guest startup content, not a unique identification of the game executable.
No tutorial stationary, sun-facing, turned, or end-of-opening-fade state was
independently matched. QMP running status and changing window images alone do
not prove those scenes or a successful Brute Force game boot.

The target never acquired verified foreground focus; `SetForegroundWindow`
returned false. No keys or F12 were sent in this run, which ended normally via
QMP quit after about 99 guest seconds. QMP `screendump` was unavailable. The
earlier no-disc probe's targeted F12 `PostMessage` also produced no file.
xemu's source shows that its F12 action renders the framebuffer texture
through its DAC gamma shader before PNG output and uses second-resolution
filenames that can overwrite existing files; raw guest-framebuffer bit
identity remains unverified. A future operator-assisted session must focus
and verify the exact xemu window, verify a visible control response, and use
a fresh empty screenshot inbox with collision guards before independently
matching all four scenes.

The earlier no-disc retry displayed a guest disc-insertion prompt, not a
confirmed dashboard, and reported `SHUTDOWN` reason `host-ui` at about
41.8 seconds. The xemu source has host-UI paths for window-close/quit events
and the shutdown action, but the initiating event was not established.
This disc-inserted run continued past that time and ended with
`host-qmp-quit`; it does not explain the earlier shutdown.

The local [session result](../analysis/xemu-reference-game-20261030-133140-7d1b5af7/reference-results.json),
[QMP media verification](../analysis/xemu-reference-game-20261030-133140-7d1b5af7/run/qmp-media-verification.json)
and [offline classification](../analysis/xemu-reference-game-20261030-133140-7d1b5af7/run/offline-classification.json)
are ignored private artifacts; the disc, captures, EEPROM and HDD remain
local. No frame-rate or rendering-equivalence measurement was made.

## Launcher and pixel-shader cache

On 2026-10-30, the Windows launcher was tested with generation
`20261029-175500-dfadbdd9`, the North American retail executable above,
isolated saves and a 25-second watchdog. A D3D11 launch opened the framebuffer
window and reported point filtering and VSync off. A second bounded startup
opened borderless fullscreen and F11 restored the window. A third reported
linear filtering and VSync on but did not reach a cacheable combiner shader
before the watchdog. These are startup and window-control checks, not
evidence of gameplay or a cache failure.

Following the user's preference, new launcher settings now default to the
experimental D3D11 renderer; existing saved renderer choices are retained.
The command-line default remains the CPU reference. The packed alias and
other experimental options remain off by default.

The opt-in cache was also measured with the same Release-build synthetic
D3D11 scene on a hardware device, four raster threads, and three alternating
cold/warm runs after priming the warm cache. Every output frame was
byte-identical. Cold runs compiled three shaders each; their median sum of
compile-and-create times was 26.356 ms. Warm runs loaded three each; their
median sum of lookup-and-create times was 0.532 ms. Median whole-process
times were 250.4 ms cold and 222.9 ms warm. Cold compile times exclude cache
writes; process times include startup and other work. The adapter model was
not recorded. These synthetic numbers do not establish gameplay stutter or
world-frame performance.

A subsequent [scripted stationary-tutorial comparison](../analysis/bench/shader-cache-20261030-013241/summary.json)
used the same generation, four raster threads, D3D11 GPU vertex programs,
VSync off, `RECOMP_PROGRAM_NEAR_ZERO=1`, isolated saves, and three alternating
runs each with an empty cache or a cache seeded from the first cold run.
There were no retries. Each cold run compiled 31 combiner shaders, with
compile-and-create sums of 387.674–457.867 ms (median 454.624 ms), excluding
cache writes. Each warm run loaded 31, with lookup-and-create sums of
4.504–108.891 ms (median 4.794 ms). These sums do not directly measure
frame-time stutter.

The static hold produced 20.523–37.589 rendered fps cold and
27.815–38.836 warm; these are not distinct world-frame rates, and the
variation does not establish a steady-state speedup. The first captured
640x480 flip in each content-matched cold/warm pair differed by more than
8/255 in 176, 412, and 350 pixels respectively (about 0.06–0.13%).
Captures were not frame-locked and the differences remain unexplained. They
do not establish G4 or prove the cache caused a visual difference.

[test_d3d11.py](../tests/test_d3d11.py) also checks cold/warm frame identity
with CPU-transformed and GPU vertex-program batches on WARP and hardware,
cache-key inputs, damaged-entry recovery, concurrent writes, Unicode paths,
and the logged unavailable-directory fallback. Forced read-only and
deny-delete destinations reproduced Windows error 5 on replacement. The
cache now briefly retries transient publication errors and accepts an existing
entry only after verifying its key, checksum and byte-for-byte equality with
the newly compiled output; differing or damaged entries still report the
failure. Fifty concurrent two-process synthetic cases passed, with one
identical-entry recovery observed. The complete D3D11 fixture suite passed 106
tests with WARP and the default hardware device. These cases explain a
reproducible form of the failure but do not prove the cause of the earlier
intermittent occurrence. The renderer's existing CPU reference parity
fixtures still run with the cache disabled.

A separate forced temporary-name collision exposed another failure: after a
later `CreateFileW` succeeded, the earlier Windows error 80 was retained and
the valid entry was not published. The store now clears that stale error
when creation succeeds. Synthetic fixtures verify both successful retry
without changing the colliding file and explicit failure after exhausting
all 32 names; they do not establish that this caused the earlier error 5.
The nine focused collision, blocked-publication and key tests passed after
the fix. The later integrated D3D11, frame-statistics, audit, clipping and
presentation run passed 174 tests, including 108 D3D11 cases on WARP and
hardware.

## GPU audit instrumentation

On 2026-10-30, framework patch 0025 was built against tree
`cb43fad03befa678166d84b56acbc75b3663e9ce`. This was a **synthetic**
validation using the framework's test XBE, not a run of the retail revision
identified above or of generation `20261029-175500-dfadbdd9`. No game was
played and no performance campaign or new distinct-world-frame measurement
was made. The patch changes runtime code only; the framework's code-generation
subtrees are unchanged.

Set `RECOMP_GPU_AUDIT=1` to enable the default-off observer;
`RECOMP_GPU_AUDIT_FILE` names its CSV, defaulting to `gpu_audit.csv`.
Unset, `0`, and other values leave it disabled. It does not register a
rendering backend, so the existing CPU rasterizer remains the reference.
Reports are rewritten at executor reports, on a flip after 20 seconds have
elapsed (or the wall clock moves backwards), and at normal exit. An active
BEGIN appears separately as `open_request` and `open_input`, not as a
completed request. Forced termination can lose the latest interval, and a
reader can encounter a partially rewritten report. Method values can include
guest addresses; keep reports and other runtime captures local.

The CSV columns are `category,key,count,vertices`. Completed requests are
classified by primitive, input source and actual route: GPU vertex program,
CPU vertex program, fixed function, screen space, or dropped. The input rows
count requested, buffered and discarded array indices, immediate vertices,
or **inline words**; inline request vertex totals require a known stride.
Backend rows count base and extended draw callbacks and accepted or rejected
GPU-vertex callbacks, not visible pixels. Existing executor triangle
counters supply output, software-cull, clip and offscreen rows; even a
triangle emitted by the executor can produce no pixels. `visibility,UNKNOWN`
therefore includes every non-dropped route. `visibility,NOT_SUBMITTED`
identifies dropped routes, including a rejected GPU callback that falls back
and then drops, rather than asserting that no callback was attempted.

`method_seen` counts dispatched methods by subchannel, including the
subchannel-0 semaphore release and subchannel-5 DMA KickOff early returns.
`method_unhandled` means the executor called its existing unhandled-method
path; `method_mirror_only` means a texture-stage method was mirrored but not
decoded there. A backend may consume that mirror, so neither category proves
that hardware ignores the method. Clear flags, sizes and rectangle state and
flip-event dimensions/current format are recorded separately; current format
is not necessarily the presented surface's format. There are eight
subchannels and 2,048 method slots each, with at most six retained values per
non-bulk method; bulk payload values are omitted while their occurrences are
counted. The aggregate table has 4,096 slots and admits up to 3,072 distinct
keys. All counters saturate at 64 bits; the overflow rows report lost table
events, extra method values, saturation and invalid method keys. Once overflow
occurs, affected aggregates are lower bounds, not a complete inventory.

The Windows x64 Release fixtures used MSVC 19.44. The focused run passed 16
audit cases, 40 frame-statistics/window-clip cases and nine existing
CPU/GPU-reference cases; four standalone framework CTest cases also passed.
The audit fixture used one raster thread; the D3D11 reference fixture used
four. Audit-off/on synthetic guest bytes, stdout, stderr and fake-backend
callback order matched; WARP and hardware-mode D3D11 output also matched
between audit-off/on runs. The separate CPU/GPU parity fixtures allow their
documented edge-pixel differences. The D3D11 fixture did **not** record its
adapter identity. A separate `D3D11CreateDevice(NULL, HARDWARE)` probe
selected NVIDIA GeForce RTX 5070 (vendor `0x10de`, device `0x2f04`); that
observation does not prove which adapter the fixture used. Linux/pthread,
additional backends and real-title behavior remain unverified.

The focused commands below ran from the project root; they use the private
build and scratch directories. Choose a **new, nonexistent** pytest
`--basetemp` when repeating them, since pytest deletes a reused basetemp.

```powershell
$root = (Get-Location).Path
$work = Join-Path $root 'build\gpu-audit-20261030-7beb2474'
$env:TEMP = Join-Path $work 'scratch'; $env:TMP = $env:TEMP
$env:PYTHONDONTWRITEBYTECODE = '1'
cmake -S $root -B $work -G 'Visual Studio 17 2022' -A x64 "-DBRUTEFORCE_GEN_DIR=$root\tests\fixtures\generated"
cmake --build $work --config Release --parallel 4 --target bruteforce_gpu_audit_test bruteforce_frame_stats_test bruteforce_window_clip_test bruteforce_d3d11_test -- /verbosity:quiet
$env:BRUTEFORCE_TEST_GPU_AUDIT = Join-Path $work 'Release\bruteforce_gpu_audit_test.exe'
$env:BRUTEFORCE_TEST_D3D11 = Join-Path $work 'Release\bruteforce_d3d11_test.exe'
$env:BRUTEFORCE_TEST_D3D11_HARDWARE = '1'
$env:BRUTEFORCE_TEST_FRAME_STATS = Join-Path $work 'Release\bruteforce_frame_stats_test.exe'
$env:BRUTEFORCE_TEST_WINDOW_CLIP = Join-Path $work 'Release\bruteforce_window_clip_test.exe'
$private = Join-Path $work 'pytest-audit-final'
if (Test-Path $private) { throw "Refusing to reuse pytest basetemp: $private" }
python -m pytest -q -p no:cacheprovider --basetemp $private tests\test_gpu_audit.py tests\test_frame_stats.py tests\test_window_clip.py tests\test_d3d11.py::test_gpu_matches_cpu_reference
$private = Join-Path $work 'pytest-toolkit-metadata'
if (Test-Path $private) { throw "Refusing to reuse pytest basetemp: $private" }
python -m pytest -q -p no:cacheprovider --basetemp $private tests\test_build.py::test_generation_survives_runtime_only_toolkit_changes tests\test_build.py::test_generation_rejects_changed_recompiler_or_template tests\test_build.py::test_run_rejects_an_executable_built_against_another_toolkit
cmake -S (Join-Path $root '.deps\xboxrecomp\tests\nv2a_gpu_audit') -B (Join-Path $work 'upstream-audit') -G 'Visual Studio 17 2022' -A x64
cmake -S (Join-Path $root '.deps\xboxrecomp\tests\nv2a_ffp_backend') -B (Join-Path $work 'upstream-ffp') -G 'Visual Studio 17 2022' -A x64
cmake -S (Join-Path $root '.deps\xboxrecomp\tests\nv2a_raster_batch') -B (Join-Path $work 'upstream-raster') -G 'Visual Studio 17 2022' -A x64
foreach ($fixture in @('upstream-audit','upstream-ffp','upstream-raster')) {
    $build = Join-Path $work $fixture
    cmake --build $build --config Release --parallel 2 -- /verbosity:quiet
    ctest --test-dir $build -C Release --output-on-failure
}
python tools\toolkit.py --verify
```

The first pytest command reported **65 passed in 3.47 seconds**; the
toolkit-metadata command reported **3 passed in 0.24 seconds**. The CTest
runs reported **2/2**, **1/1** and **1/1** passed respectively.
`toolkit.py --verify` passed; an independent private-index reconstruction
applied all seven selected upstream commits and 25 local patches to the base
and produced the same tree.

### Unmatched retail GPU audit and vertex probe

On 2026-10-30, one isolated, audio-off retail diagnostic used generation
`20261030-135733-86f4d023`, the same framework tree, four raster threads,
the D3D11 hardware path, `RECOMP_PROGRAM_NEAR_ZERO=0`, GPU audit and
`RECOMP_GPU_VSH_VERIFY=100`. The framebuffer window was verified minimized;
the legacy timed menu script and its stationary-state detection are
heuristics. The run lasted 96.6 seconds and ended by console control; guest
threads were not cleanly joined. There were no scene screenshots or operator
labels, so the reached content and the physical display are unverified. The
diagnostic instrumentation and minimization make its frame rate unsuitable
for a performance comparison.

The last periodic audit was written before shutdown with one BEGIN active;
its 10,464 flips and other counts are a partial snapshot. It recorded
2,303,415 completed requests, including 174 line-list requests with 696
vertices, and 174 unsupported-primitive drops with those same vertex totals.
No point request was recorded in this snapshot; that is not evidence that
points are unused in other scenes. The executor excludes primitive types
below triangles, so the line requests are a concrete compatibility lead,
not proof that any affected pixels were visible. The snapshot had no table
or counter saturation, but 1,412,042 method values fell into the bounded
`METHOD_VALUE_OTHER` bucket.

For prioritization, the unhandled-path count for subchannel-0 cull enable
(`0x0308`) was 52,118, including 19,044 writes of `1`; front and back cull
face values were also written. The culling switch was off, and the mirrored
state and submitted geometry do not prove a visible effect. The higher
`0x0100` count includes NO_OPERATION/software-method traffic; nonzero values
can also trigger a software trap, so its count must not be interpreted as
an entirely ignored or visually defective feature. `0x1E74` was always
zero and `0x1E78` was zero except for one retained value in this snapshot;
their visual importance remains unknown. Match scenes before changing these
paths or enabling culling.

The last vertex-verification report showed 23,400 sampled GPU-program
batches and 383,560 vertices checked, with zero reported differences or odd
inputs; no per-vertex difference message appeared in the log. This compares
only every hundredth eligible batch before the last report and does not
establish clipping, culling, full-scene correctness or visible pixels.
Matched CPU, D3D11 and xemu captures and classified real movement frames
remain necessary; neither this audit nor flip counts close G4.

## Presenter-source capture alignment

Framework patch 0027 adds a default-off Windows presenter diagnostic. It
copies each GDI or D3D11 CPU-upload input after the existing pixel conversion
and PVIDEO composition, before window scaling. A ledger records the
publication and candidate statistics flip IDs, submission route, API status,
format, overlay and timing. A separate verifier checks the completed ledger,
BMPs and the identity and rows of the same run's statistics file. Repeated or
skipped presentations remain separate events rather than being inferred from
filename order.

On MSVC/Windows, 17 synthetic presenter tests passed, including repeated and
skipped flips, RGB565 and AA conversion, overlays, corrupt or incomplete
output, collision protection, concurrent publication, WARP and hardware
shared-texture rejection, and capture-off project presentation fixtures. A
hidden-window D3D11 CPU-upload fixture also compared its capture with the
uploaded texture. The integrated D3D11, statistics, audit, clipping and
presentation suites passed 174 cases, and four framework CTests passed.
Hardware-mode tests do not themselves identify the adapter.

Two short, isolated, audio-off retail probes then used Release generation
`20261030-142754-81f8c6e6`, the D3D11 hardware renderer and guest-sized
readback, with no controller inputs and separate save roots. The framebuffer
window was verified minimized before the bounded close, which used `WM_CLOSE`
and did not cleanly join guest threads. Each run produced a completion marker
and passed the ledger/image/statistics verifier. The GDI route recorded 80
submissions: one successful link, 79 no-client submissions and one image.
The D3D11 CPU-upload route recorded eight submissions and images: one
successful link and seven occluded results. The successful submission in
each run was the first and may have occurred before minimization. Both links
identified their own run's second recorded flip; neither capture recorded a
PVIDEO overlay. The renderer logged a hardware D3D11 device but did not identify
the adapter. The scenes were not classified or matched, and SDL capture was
not tested. Private captures and logs remain local.

The verifier links a successful submission to a recorded flip; it does not
prove that a guest dump, statistics hash and composed input have the same
pixels, or that the image reached unobscured physical scanout. Pinned inputs,
missing or unknown statistics, failed/occluded API results and incomplete
captures are not accepted as aligned evidence. Direct shared-GPU capture is
explicitly rejected, and an independent guest writer can still race the
snapshot. The diagnostic synchronously writes every submission, can consume
several GiB per minute, and must not be used to measure performance. Real
scene review, window visibility and integration with the benchmark labels
remain open.

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

### Audio integration and listening result

Audio is default-on. `RECOMP_AUDIO=0` and `--no-audio` are the explicit off
paths. The default `RECOMP_APU_DSP=fold` path uses the framework's stereo
all-mixbin fallback. `xemu` explicitly selects the experimental title GP/EP
path on the vendored xemu v0.8.139 DSP56300 C interpreter. Output remains
48 kHz stereo.

The framework-side integration is reproducible as patches 0030-0034. Patch
0030 records whether an APU physical translation named RAM or the contiguous
window and adds optional external DSP hooks. Patch 0031 finalizes an external
DSP after the APU frame thread has joined. Patch 0032 lets independent cleanup
hooks run on window, firmware-return, and shutdown exits. Patch 0033 raises the
APU frame worker to high priority on Windows. Patch 0034 raises the queue
refill threshold from one half to three quarters while retaining the same
maximum. Upstream PR #264, at commit
`eb7b1f5833205a24fcc0eda6eb4ed2df4f2bc2e3`, supplies the selected 2,048-sample
SSL restart hold. PRs #266/#270 and #261/#286 were not adopted without
evidence that their additional behavior is needed.

The DSP wrapper mirrors the GP/EP windows against shadow copies, resolves SGE
pages using recorded physical-translation intent, and writes EP FIFO 0 to the
stereo monitor block. A joinable worker pipelines the DSP stage beside the VP
frame thread. Teardown signals and joins that worker, frees both interpreters
and all shadows, and resets static queues and counters; a native fixture
completed six init/shutdown cycles without leaking process handles.

`HalReturnToFirmware` previously called `ExitProcess`, bypassing `atexit` and
leaving a stale RIFF header. The generic exit hook now performs full APU
shutdown first. A retail firmware-return capture closed at 1,618,944 frames
(33.73 seconds), and its RIFF size exactly matched the file. The simple
fold-down comparison also finalized exactly.

With the original four 512-sample XAudio2 buffers, an accurate-DSP menu probe
reported three underruns while the fold-down comparison reported none. Six
buffers removed those events in a short matched route, but one longer,
logging-heavy gameplay capture still saw one dry spell during a 44.9 ms host
pause. Eight buffers initially provided a 42.7 ms low-water mark and an
85.3 ms maximum queue. A fresh rebuilt-Accurate campaign then reproduced two
dry detections around a heavy ten-second diagnostic burst; a traced run
observed the queue fall from its 2,048-sample low-water mark to 1,024 samples
during a 33.6 ms submit gap. Periodic DSP cost/wait reports are now opt-in via
`RECOMP_APU_DSP_TRACE=1`, and patch 0034 raises the refill threshold to 3,072
samples (64.0 ms) without changing the 4,096-sample maximum.

The final rebuilt Accurate default reported `3072..4096` pacing. A 74.51-second
menu-transition probe, with host tracing and WAV capture active, reported
zero underruns, starvation events, or refused submissions; its 3,576,320
stereo frames had an exact RIFF length. Whole-capture analysis found nonzero
content in both channels, zero clipped samples, and no 32-frame, 256-frame, or
near-2,048 paired-hole candidates. Its last periodic host snapshot precedes
the watchdog close by about 0.23 seconds, so the analyzer conservatively
labels periodic coverage inconclusive even though no event line was logged.
The earlier 115-second transition probe and a separate 260-second
tutorial-world stress run with the prioritized worker likewise reported zero
for all three counters.

A subsequent capture of that then-default Accurate path extended the evidence
through 164.47 seconds and 7,894,528 stereo frames. It reached an interval
with exactly 421 draws per flip, matching the observed tutorial-world
signature, then accepted the fixed A and stick-input sequence through 158
seconds. A 38.5 ms submit gap reduced the queue to 1,536 samples without
draining it. All 163 periodic host snapshots and the complete event scan
reported zero starvation events, refused submissions, and underruns; the RIFF
length again matched the file exactly. Startup, cinematics/menu, tutorial, and
scripted-gameplay intervals were each non-silent in both channels, with zero
clipped samples and no 32-frame, 256-frame, or near-2,048 paired-hole
candidates. The analyzer labels host coverage conservatively because its last
periodic snapshot precedes watchdog shutdown, not because it found a host
event.

A private 155-second accurate-DSP WAV reached startup, campaign cinematics,
tutorial assets, and scripted tutorial input. Marked-interval analysis found
nonzero stereo content, no clipped samples, and no bounded 32- or 256-frame
zero-hole candidates in any interval. The capture remains private and one
earlier six-buffer run contained the dry event described above; it is evidence
for signal shape and category coverage, not the final queue setting.

The user subsequently reported that dialogue and gunfire in Accurate mode
sound crackly and mono while music sounds acceptable. This is negative human
listening evidence, not a diagnosis of the individual DSP instruction or
voice responsible. The saved launcher settings used D3D11, 4.5x rendering and
`RECOMP_APU_DSP=xemu`, so the report applies to the tested accurate path.

Objective follow-up did not find a host transport failure. The 164.47-second
accurate capture is valid 48 kHz, 16-bit stereo; left/right RMS is
approximately 1,522/1,554, whole-capture channel correlation is approximately
0.72, about 3.6% of frames have identical channels, and no large discontinuity
was found. Matched opening intervals nevertheless show much narrower Accurate
output than Simple: correlation is approximately 0.878 versus 0.156 at
5-10 seconds and 0.892 versus 0.210 at 10-15 seconds. Neither path shows
elevated discontinuities at 32- or 256-sample boundaries. The emulated EEPROM
advertises stereo (`XC_AUDIO=0`).

For Accurate, the observation is therefore healthy host delivery with an
audible mix defect; the specific GP/EP fault remains unidentified. The user
then reported that the recommended Simple build still sounded mono/centred
for dialogue and effects. That is a second failed human quality observation,
not evidence that the Accurate and Simple paths share one root cause.

The first follow-up traces did not reach gameplay. At 95 and 145 seconds they
showed only 2D-list voices in slots 69-99 with equal bin 0/1 gains, and all
six remaining routes set to `0xFFF`. A later framebuffer capture identified
the displayed scene as the main menu and Campaign Type screen. Those voices
were menu audio, so the initial conclusion that Brute Force did not exercise
3D/HRTF was invalid. Advertising Dolby Surround through a temporary EEPROM
diagnostic did not change that menu routing; the product still advertises
stereo (`XC_AUDIO=0`).

A corrected script selected Campaign, Quick Campaign, and skipped the campaign
movies with A presses at 20, 23, 28, 32, 37, 42, 47, 52, 57, and 62 seconds
and `down` at 36 seconds, measured from the first USB report. Generation
`20261030-142754-81f8c6e6` then rendered the world by approximately 87 seconds.
Actual world audio populated the 3D voice list in slots 25-31 and used HRTF
routes in bins 6-10.

That route exposed a generic VP defect. A mono source is duplicated for HRTF,
which produces distinct left/right samples, but the bin mixer selected the
filtered result with the original one-channel source width. Every route
therefore read HRTF channel zero. A synthetic filter emitting `+1` left and
`-1` right reproduced `+1/+1` before the fix and `+1/-1` afterward. Patch
0035 tracks the post-filter output width and selects two channels after HRTF.

Three matched 108-second Simple runs used the corrected Quick Campaign route
and fired while rotating. In the 92-97 second firing interval, fixed,
baseline, and fixed captures had left/right correlation
`0.415/0.542/0.422`; their side-to-mid RMS ratios were
`0.641/0.544/0.636`. The two clean fixed legs and the baseline reported zero
XAudio2 underruns, APU starvation events, refused submissions, clipped
samples, and bounded 32/256-frame hole candidates. One additional fixed
repeat reproduced the channel metrics but incurred nine host underruns during
a transient machine-load event; it was discarded and replaced rather than
mixed into the clean transport evidence. The analyzer still labels periodic
host coverage conservatively because the last one-second snapshot precedes
watchdog close.

The corrected A/B/A objectively confirms that patch 0035 restores differing
HRTF channels in the title's real gameplay route. It does not prove that the
result sounds correct, that all dialogue should be positional, or that the
user's full report is resolved. The user subsequently reported that the fixed
Simple path sounds improved but is not fully correct; no more specific
residual symptom was recorded before audio work was deferred. Simple remains
the default and recommended mode, but its listening gate is only partially
met. Accurate remains available as an explicitly labelled experimental
option. Launcher settings versions 1-3 migrate the old `xemu` default to
`fold`; a version-4 user choice of Accurate is retained. A five-second
bounded retail boot with
`RECOMP_APU_DSP` absent selected stereo fold-down. Six repeated Accurate DSP
init/shutdown cycles passed, the focused audio-analysis and launcher selection
passed 105 tests, and the integrated hardware-enabled selection passed 639
tests before patch 0035. Final patch validation rebuilt the Release
executable, passed the standalone `apu_vp` regression, reproduced framework
tree `25a2a713738b7aabb1c21ef6720765ff93ccffea`, passed 144 focused
audio/build/launcher tests, and passed 274 integrated tests with 365
hardware/platform cases skipped.

Default-on audio changed the old timed route: `down` selected `DEMOS` on the
title screen. The current diagnostic uses A presses at 20, 23, and 28 seconds
from the first USB report and reached campaign/tutorial loading. The
benchmark's previous 700-draw world threshold also exceeded this route's
observed 421 draws per frame; its evidence-based threshold is now 400.

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

### User-reported HUD and glow correlation

On 2026-10-30, the user supplied live-window screenshots of nearly the
same view: one is darker with no HUD, while the other is brighter with the
HUD visible. They report that moving the mouse while the window is active
can make the HUD and glow disappear together. The screenshots differ in
window size and focus and are not frame-locked. The earlier explanation for
arbitrary report-time dumps does not by itself explain these live-window
images; neither the mouse input nor HUD is established as the cause.

The existing near-plane investigation below offers one possible mechanism.
A new synthetic hardware-D3D11 fixture draws a scene, an additive glow
quad, and a HUD-like quad, followed by positive- and negative-depth
controls. With the legacy programmable plane, a positive `c[59].z` clips the
zero-depth glow and HUD quads, while a negative value lets both through.
With `RECOMP_PROGRAM_NEAR_ZERO=1`, they remain visible at
both offsets; positive-depth geometry remains visible and the negative-depth
control is clipped. The fixture passes with GPU and CPU-transformed vertex
programs on hardware D3D11. It does not replay the title's HUD, blur or
input, and does not prove that the reported gameplay frames use those
particular clip values or that either image matches original hardware.

After the user confirmed that the UI still flickered and the visible-UI
state bloomed at 1440p, framework patch 0029 added a separate HUD-safe mode.
It selects screen zero for ordinary and alpha-blended programmable draws,
but selects at least a small positive near plane for additive blends whose
destination factor is `ONE`. The title has emitted both `ONE/ONE` and
`SRC_ALPHA/ONE` blends. This clips zero-depth additive geometry without
discarding positive-depth additive geometry. Legacy and full screen-zero
modes remain available.

The launcher now defaults to HUD-safe mode and migrates version-1 settings
that retained the old inherited default. CPU and WARP D3D11 fixtures cover
positive and negative `c[59].z` offsets, GPU and CPU vertex transformation,
unblended and alpha-blended HUD-like geometry, zero-depth additive glow, and
positive-depth additive geometry. In those fixtures the HUD remains present
and the additive glow remains absent. This is a targeted title workaround,
not proof of original-hardware behavior or a reviewed retail result. The
separate packed depth/stencil alias remains opt-in and unavailable with
scaled rendering.

For GPU-executed vertex programs, the current frame trace can print
`s_xf` vertices left by an earlier CPU-transformed batch. Its GPU triangle
count is submitted triangles, not post-clip output, and its pixel count
does not measure GPU fragments. Do not use those fields as proof of the
vertices or clipping in a GPU-program batch.

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

At the time of this evaluation, the base backend interface did not pass all
four texture stages and register-combiner state. The later extended interface
does; see [RENDERING.md](./RENDERING.md#command-execution-and-backend-contract).
The executor still writes semaphore releases directly to guest memory. An
asynchronous hardware backend must explicitly complete and read back relevant
GPU work before guest-visible memory, fence releases, software-method traps,
or frame snapshots can rely on it. General GPU completion ordering remains
open: a compiled shader or a swap-chain present alone is not that guarantee.
Scaling, widescreen HUD placement, and changed frame timing require additional
title-specific work.

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

The existing depth-seed movement trace used generation
`20261029-175500-dfadbdd9` and framework tree `3472bac8e33c1758c801fb1ca65a346a170ad8e5`.
Its 320x240 bright pass reads the 640x480 scene and the `0x41` alias; a
160x120 additive pass and a full-size additive composite follow. The trace
is from gameplay, not the opening-to-gameplay transition. Its five-stage
bright-pass combiner uses the alias's alpha, corresponding to the stencil
byte, to gate scene brightness.

A synthetic 64x64 fixture replays that combiner state with uniform scene
colors and GPU-cleared stencil, followed by a simple additive composite.
It does not replay the title's blur or geometry. With gray `0xA0`, GPU
stencil `0xC0`, and deliberately stale zero guest stencil, the alias-off
D3D11 path produces `0xA0` in the bright pass and saturates to `0xFF` after
the synthetic composite; the alias-on path produces zero bright-pass output
and retains `0xA0`. Tests exercise values on both sides of the threshold,
including one-byte differences, mixed RGB and partial scene alpha, on WARP
and hardware with fixed and programmable vertices, both with and without the
separate depth-texture option. The same fixture on the CPU path reads the
unchanged zero guest stencil after its software clear.
This establishes a mechanism by which stale stencil can amplify glow, not
that the actual gameplay stencil is stale zero or that enabling the option
matches Xbox hardware.

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
for this glare path; neither that comparison nor the combined A/B isolated
which new option caused the difference or established hardware fidelity.
G4 is not met.

One later hardware D3D11 diagnostic isolated the packed alias from shared
presentation. It used the same generation and toolkit tree, 12 raster
threads, GPU depth aliases, window clipping and async visibility; packed
aliases were on and shared presentation was explicitly off. There was one
isolated-save run, no automatic retry and no profiler. Its stationary tutorial
capture differed from the three earlier alias-off captures in
145,363–145,369 pixels (47.32%) beyond 8/255, but from the earlier
alias-plus-presentation captures in only 20–473 pixels (0.01%–0.15%).
The images show the same tutorial panel but are not proven frame-locked.
Seven periodic reports recorded 1,373 packed alias samples and conversions,
zero depth-alias rejections and zero shared presentations, excluding the
final partial interval. This supports the packed alias as the main source
of the earlier stationary brightness change rather than shared presentation.
It does not establish which brightness matches original hardware, explain
every gameplay view, or directly capture the opening fade. One diagnostic
is not a performance comparison.

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
The user later reported that the excessive bloom starts as the scripted
opening ends and gameplay begins, while the vignette/letterbox bars fade.
This is a user-reported timing observation, not evidence that the fade
causes the bloom.
In the seeded diagnostic, movement captures also show dark and much brighter
views, but the camera and capture times differ. The new optional movement
captures and adjustable frame-trace timing are diagnostic tools; compare
matched views and a trusted reference before changing lighting or gamma.

### Static gamma-ramp investigation

The retail generation `20261029-175500-dfadbdd9` contains a game-code routine
that constructs three 256-byte tables, compares them with saved state, and
conditionally calls a D3D-section routine that writes an index followed by
256 RGB triplets to offsets `0x6813C8` and `0x6813C9` from a runtime base.
Another D3D-section caller also reaches that writer. The offsets match the
NV2A user DAC palette registers, but static code alone does not establish
the runtime base, whether either call executes during the opening, or the
ramp values or visual effect.

The selected framework maps the title's NV2A register aperture as ordinary
RAM, and its `PRMDIO` handler is a stub. Repeated writes to the candidate
offsets through that aperture do not retain a DAC palette.
The framework's separate D3D8 wrapper implements `SetGammaRamp`, but that
does not establish that the statically linked title path reaches the wrapper
or the framebuffer presenter. A presentation effect remains unproven.

An opt-in `BRUTEFORCE_DAC_TRACE=1` title-specific entry hook now records the
candidate writer's miniport address, runtime base, ramp address and 768
planar RGB bytes with a host-monotonic timestamp. It is off when unset or
set to `0`; invalid settings or inaccessible and guarded guest spans abort
explicitly. Synthetic fixtures cover the exact byte order, disabled behavior,
guest register and memory preservation, invalid and partial spans, and
concurrent complete log records. The entry and its snapshots precede the
generated writer; they do not prove completed DAC writes. They are not atomic
with guest writes or protection changes, and logging changes timing.

In the [unmatched retail diagnostic](#unmatched-retail-gpu-audit-and-vertex-probe),
the probe logged exactly one entry, about 4 ms before the first recorded
flip, and no later entry before shutdown. Its runtime base was `0xFD000000`,
the framework's NV2A aperture; all three captured planes were exactly the
identity sequence `0..255`. This establishes the observed entry and snapshot,
not completed writes or displayed output. The unreviewed legacy-scripted
interval does not identify the opening fade and the hook does not cover
other possible writers. A nonidentity ramp, a visual effect, and any need for
presentation-side gamma remain unverified; raw logs remain local.

### Default D3D11 movement regression (2026-10-30)

The user reported roughly 6 fps while moving with the default launcher
settings. Earlier faster movement figures used different options and included
substantial unclassified or known-black intervals, so they do not establish
the size or cause of a directly comparable regression.

The previous renderer reserved a texture-cache slot before uploading, but did
not return it if the upload failed. Repeated unsupported `0x2E` depth-texture
requests could therefore fill the 2,048-entry table with unkeyed entries and
repeatedly copy source bytes. The renderer now retains bounded negative
entries for deterministic, unsupported non-P8 CPU decodes when no GPU decode
was attempted. It still reports the unsupported format; this does not add
depth-texture support. Other upload failures remain retryable and release
their slots. Depth and render-target aliases are checked before the negative
cache, and successful textures still use the existing exact snapshot checks.

One instrumented movement profile on Release generation
`20261030-142754-81f8c6e6` attributed 31.2% of NV2A-thread samples to
`NtQueryVirtualMemory` called by `readable_guest_range`, and 19.7% of
main-thread samples to the same system call from `guest_word`. These are
single-run samples, not a causal or frame-time breakdown. P8 source and
palette validation was experimentally changed to reuse a `VirtualQuery`
result within one bind. The later comparison's periodic reports counted
378,708, 409,446 and 411,612 actual P8 range queries in its three completed
candidate runs and **zero** reuses. The experiment was removed; the final
renderer independently queries both ranges as before. Page queries cannot
prevent guest writes or protection changes racing the subsequent reads.

A preliminary cache-only A/B used that generation, 12 raster threads,
D3D11, GPU vertex programs and texture decoding, synchronous visibility,
VSync on, and the launcher-default experimental options off. The launcher
inherited an unset near-plane option, represented here as `0`. One old/new
run measured stationary rates of 15.294/16.100 flips per second and
movement rates of 30.063/30.025 flips and 28.387/28.575 whole-frame pixel
changes per second. These do not show a measured speedup; the baseline also
received an extra brief diagnostic capture during movement. This preliminary
run also suppressed the launcher's normal ten-second pushbuffer reports.
Both runs had zero known-black matches, but all 1,203/1,202 movement frames
were otherwise unclassified.

The subsequent [alternating comparison](../analysis/bench/20261030-152042/summary.json)
used three old and three candidate runs, restored the ten-second report
interval, and captured both the stationary hold and two movement points.
It used isolated saves, the legacy readiness heuristic, scripted input and
automatic panel presses. Rates below are individual runs, not world FPS:

| Variant | Stationary flips/s | Movement flips/s | Whole-frame changes/s | Whole-frame change 1% low/s |
| --- | --- | --- | --- | --- |
| Before | 8.378, 8.465, 15.066 | 16.840, 7.849, 33.250 | 14.413, 7.010, 31.700 | 6.437, 1.217, 13.192 |
| Candidate | 14.512, 15.396, 15.033 | 33.086, 35.080, 34.435 | 31.561, 33.755, 33.434 | 12.162, 12.902, 14.272 |

The candidate combined the failed-upload fix with the subsequently removed
P8 query-reuse experiment and its counters. Its periodic reports counted one
unsupported descriptor and 21,165, 23,777 and 22,679 cached hits across the
respective runs. The first attempt for the second candidate run hit the
420-second watchdog in the world phase and succeeded on retry. Automatic
panel presses differed between runs: 4/2/3 before and 3/3/3 for the candidate.
The first stationary and movement captures show similar tutorial views, but
the complete movement intervals were not independently matched. Paired
stationary captures differed by more than 8/255 in 4,076 (1.33%), 1,202
(0.39%) and 316 (0.10%) pixels; those differences remain unexplained, and
there was no matched CPU-reference comparison. No movement hashes matched
the two known-black signatures, but all 2,324/4,106 before/candidate movement
frames remain unclassified. Captures and frame-statistics instrumentation
can affect timing, and guest-frame captures do not establish scanout.

The table suggests a possible improvement but cannot attribute it to either
change or establish distinct visible-world FPS. The final executable was
rebuilt **after** removing the ineffective P8 experiment and was not
rebenchmarked; the user's reported 6 fps and the actual cause remain open.

A separate one-run exact/approximate-cache diagnostic measured
19.723/8.042 stationary flips and 30.230/16.890 movement whole-frame
changes per second, respectively. It is not a controlled estimate of
snapshot cost; the approximate cache can miss same-frame and sparse rewrites
and remains an experimental opt-in. The 6 fps report and its root cause are
not resolved by these measurements.

Synthetic WARP and hardware fixtures check unsupported-cache hits with
protected source memory, alias precedence, retryable failed uploads and
unchanged cache occupancy. Additional fixtures protect P8 source or palette
memory after an earlier successful bind and require a diagnostic on the next
bind, for both stages, decoders and cache modes. The full D3D11 suite and
related fence, pushbuffer, presentation, audit and frame-statistics tests
passed 246 cases on the experimental candidate. After removing the P8
experiment, the rebuilt final renderer passed 53 focused unsupported-cache,
failed-upload, P8-reprotection and CPU-reference parity cases. These tests do
not establish matched gameplay or hardware NV2A equivalence.

### Synthetic D3D11 compatibility hardening

On 2026-10-30, a synthetic texture rewrite remained red when rewritten green
within one frame; another rewrite after 30 stable frames missed the old sparse
samples and remained green instead of blue. The renderer now compares saved
source and palette bytes on every ordinary texture-cache use. Retained
snapshots have a 128 MiB budget and evicted snapshots force an upload; the
budget excludes transient copies, decoded buffers and GPU resources. The old
approximation is available only through `RECOMP_GPU_TEX_CACHE_APPROX=1`, emits
a warning, and still misses those two synthetic rewrites. The new default's
CPU and memory costs have not been isolated by a controlled comparison; no
performance gain is claimed.

The framework's `NV097_SET_TEXTURE_PALETTE` definitions and the
[nxdk register reference](https://github.com/XboxDev/nxdk/blob/f8c52e4be65bab7d6e10455db8d86d69d232bbe3/lib/pbkit/nv_regs.h)
specify bits 2–3 as 256, 128, 64 or 32 entries, but the executor strips
those bits when populating `Nv2aTexture.palette`. These register definitions
do not establish behavior for out-of-range indices. The D3D11 renderer now
reads the raw stage register,
checks page readability and remaining size of known allocations, snapshots
only the programmed palette length, and validates all sampled level-0 P8
indices before upload. Its compute and software-texture decoders consume that
snapshot; the compute raw view has the programmed size. In synthetic scenes,
palettes ending at a no-access page work at all four lengths, in stages 0 and
1; invalid indices and a tracked short allocation stop with diagnostics.
Synthetic cube faces with mip/padding bytes outside the sampled level also
match the CPU decoder, and index, length and palette rewrites are checked.
The framework's CPU reference renderer is unchanged.

These are synthetic checks, not a title or NV2A hardware-equivalence result.
Out-of-range P8 hardware behavior is unknown, so the renderer reports it
rather than guessing. Page-rounded contiguous allocations and untracked
suballocations can hide object boundaries; `VirtualQuery` does not prove
ownership or prevent concurrent changes. Exact render-target aliases bypass
the ordinary cache. Guest writes racing snapshot/compare operations, the
framework CPU decoder's non-P8 guest reads, P8 aliases, and ABA races still
need explicit synchronization or tracking. The approximate mode can also
reuse an old, previously validated P8 texture after a guest rewrite.

Separate synthetic GPU vertex fixtures cover SHORT3/4, UBYTE3/4 and packed
11:11:10 normals in both raw and gathered streams, with all sampled GPU
vertices compared to the CPU interpreter. Relative-constant fixtures include
fractional address-register values and indices outside the interpreter's
constant range. CPU, CPU-transformed D3D11 and GPU-program D3D11 fixtures
compare culling disabled and enabled, front/back/both faces, the enable low
bit and triangle-strip winding. The focused selection passed 24 cases on
WARP and the default hardware D3D11 device; its adapter identity was not
recorded. The complete D3D11 suite passed 132 cases with both device modes
enabled. These are interpreter/renderer comparisons, not evidence of NV2A
hardware behavior or matched gameplay.

The culling fixture reproduced a diagnostic bug: after a batch culled on
both faces, vertex verification could compare the previous same-sized GPU
batch and report false differences. The D3D11 backend now invalidates the
saved verification batch on each draw attempt and logs a skipped readback for
a fully culled batch, which has no GPU vertex output. The fixture checks that
the skips occur and that subsequent visible draws still match. Culling remains
off by default.

[Microsoft documents](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-getdeviceremovedreason)
removed, reset, hung, driver-internal and invalid-call device reasons.
Renderer work, creation failures, readback, flip, keyed-mutex operations and
visibility polling now check device health and fail-stop with the operation,
result and removal reason. Mocked COM cases cover removed/reset/hung/driver
results, device-reported loss and a pending query; their synthetic completion
marker is not advanced after injected loss. This is not a real adapter
removal or TDR. Device/resource recreation, the separate framework presenter
device and loss races with guest-visible semaphore completion remain open.
[Microsoft's recovery guidance](https://learn.microsoft.com/en-us/windows/uwp/gaming/handling-device-lost-scenarios)
requires recreating the device-dependent resources; this renderer does not.

The tests below used the coordinated framework tree
`fe8216622a8195fd7a2531a7fc4f1f723f402aa3`, the synthetic XBE and generated
test textures, not retail game code. WARP and the default hardware D3D11
device were enabled; the fixture did not record the selected adapter identity.
The focused compatibility selection passed 44 tests. After adding three
guarded-range regressions, the full D3D11 run passed 100, and the separate
fence, pushbuffer, presentation and GPU-audit suites passed 50. An earlier
full run had 96 passes and one concurrent shader-cache publication failure
(Windows error 5); a subsequent publication fix and its bounded tests are
described in the [shader-cache section](#launcher-and-pixel-shader-cache).
No gameplay or performance campaign was run.

Reproduction from the project root (scratch paths remain under `build`):

```powershell
$root=(Get-Location).Path
$env:TEMP="$root\build\compat-hardening-scratch"; $env:TMP=$env:TEMP; $env:TMPDIR=$env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
cmake --build .\build\compat-hardening --config Release --target bruteforce_d3d11_test --parallel 4
cmake --build .\build\compat-hardening --config Release --target bruteforce_fence_test bruteforce_pushbuffer_test bruteforce_present_test bruteforce_gpu_shared_present_test bruteforce_gpu_audit_test --parallel 4 -- /verbosity:quiet
$env:BRUTEFORCE_TEST_D3D11="$root\build\compat-hardening\Release\bruteforce_d3d11_test.exe"
$env:BRUTEFORCE_TEST_D3D11_HARDWARE='1'
$env:BRUTEFORCE_TEST_FENCE="$root\build\compat-hardening\Release\bruteforce_fence_test.exe"
$env:BRUTEFORCE_TEST_PUSHBUFFER="$root\build\compat-hardening\Release\bruteforce_pushbuffer_test.exe"
$env:BRUTEFORCE_TEST_PRESENT="$root\build\compat-hardening\Release\bruteforce_present_test.exe"
$env:BRUTEFORCE_TEST_GPU_SHARED_PRESENT="$root\build\compat-hardening\Release\bruteforce_gpu_shared_present_test.exe"
$env:BRUTEFORCE_TEST_GPU_AUDIT="$root\build\compat-hardening\Release\bruteforce_gpu_audit_test.exe"
python -m pytest tests\test_d3d11.py -q --tb=short --basetemp=build\compat-hardening-pytest10
python -m pytest tests\test_fence.py tests\test_pushbuffer.py tests\test_present.py tests\test_gpu_audit.py -q --tb=short --basetemp=build\compat-hardening-pytest11
```

Framework follow-up: preserve palette length in the generic texture state and
bound CPU P8 reads before considering an upstream fix. Existing P8 work
[#145](https://github.com/sp00nznet/xboxrecomp/pull/145) is merged, and
tracking index [#184](https://github.com/sp00nznet/xboxrecomp/pull/184) is
open, as checked on 2026-10-30; coordinate rather than creating a competing
contribution. The [UPSTREAM worklist](./UPSTREAM.md#local-patches-to-evaluate)
records the follow-up; no pull request was filed.

Known differences, by design: stencil is honoured on the GPU path and
ignored on the CPU path; depth is D24 rather than a float buffer; textures
are sampled at level 0 only on both paths.

### Blood Wake-derived internal-resolution milestone

The renderer now adopts the generic physical-resolution architecture from
the pinned Blood Wake commit rather than continuing as isolated scale fixes.
`src/gpu/render_scale.c` is the single scale authority and contains the
adapted bilinear seed, box downsample and Catmull-Rom upscale passes. Tracked
colour surfaces retain logical and physical dimensions, depth follows the
physical target, and native output changes advance a scale epoch. Guest
geometry and texture coordinates remain logical; viewports are physical,
while scissors, partial clears and inclusive window clips are mapped
explicitly.

The existing Brute Force presentation protocol was extended instead of
replacing it with Blood Wake's direct swap-chain ownership. Framework patch
0028 allows a keyed-mutex source whose dimensions differ from the guest
framebuffer and reports the window's current 4:3 picture. This retains
Brute Force's flip IDs, one-slot bound, pinned-framebuffer and PVIDEO
fallbacks, presenter-source capture refusal and mandatory guest readback.
The final shared pass copies matching dimensions, box-averages a larger
render or Catmull-Rom reconstructs a smaller render. Guest readback always
comes from a guest-sized GPU resolve.

Physical resizing initially exposed a backend-authority regression in the
synthetic AA clear sequence: recreating the colour resource from guest memory
discarded a full clear that existed only on the GPU. Scale-driven recreation
now migrates the previous GPU surface before releasing it. Scale-two fixtures
also established that raw D3D11 occlusion counts grow with physical area;
sync and async reports now normalize each query by its physical/logical area
ratio and retain the guest counts.

The reported blank 4K opening movies exposed the opposite authority boundary.
Retail traces opened `MGS_Logo_Final.bik`, `DA_Logo_Final1.bik` and
`Intro_Montage.bik` while the D3D11 statistics reported hundreds of clears but
zero draws. Temporary PVIDEO-bank instrumentation recorded no submitted
overlay frame. These are observations that the linked Bink path writes the
guest colour surface directly; the earlier PVIDEO colour-key explanation was
not supported.

Before the fix, the scaled backend published its GPU-only cleared surface and
then read it back over the CPU-decoded pixels. The D3D11 backend now retains a
logical guest shadow per colour surface, mirrors full GPU colour clears into
guest memory, and detects subsequent CPU changes. It uploads a changed logical
image before the first GPU overlay draw or at flip, scaling it into the
physical render target before shared presentation. If a later guest write is
detected while unresolved GPU colour work exists, the backend reports a
conflict and preserves the GPU frame rather than silently overwriting it.

A 4.5x hardware run captured a visible Microsoft Game Studios frame after
386 guest uploads in 392 flips, with zero conflicts and 156 accepted shared
submissions. Longer isolated runs captured a visible Digital Anvil logo and a
frame from `Intro_Montage.bik`; every ten-second report continued to show zero
conflicts while the Bink sequences contained zero GPU draws. A separate
scale-1 run retained visible movie output. These are guest-window captures,
not an independent unobscured swap-chain capture, but the changed logical
frame is uploaded before the same physical texture is submitted to the shared
presenter.

The launcher offers Native, 2160p, 1440p, 1080p, 720p and 480p, with 480p
still the default. Scaled modes enable shared presentation automatically.
The existing experimental depth and packed-stencil aliases are rejected when
scaling is active because their guest-layout seed, snapshot and sampling
semantics are not defined at physical resolution. Scale 1 keeps the prior
resource and readback paths.

The Release fixtures and main executable built successfully. The complete
D3D11 matrix passed 190 tests with hardware coverage enabled; the WARP-only
selection passes 123 and skips 67 hardware cases. It includes
scale-two render-to-texture, guest-sized readback, partial clears, window
clips, resource migration, synchronous/asynchronous visibility, native-mode
fallback and explicit output, incompatible-option rejection, the HUD-safe
near-plane split, and CPU-written frames with and without a later GPU overlay
at scale 1 and scale 2. The
presentation fixture and shared-presenter WARP case passed; its hardware case
also passed, and all 17 launcher tests passed. After relinking the final
executable, the integrated hardware-enabled selection passed all 639 tests.
The verified framework tree is
`9bec55267a19bf1fa54320274cb073d785a8d411`.

These synthetic results and bounded opening captures do not establish retail
gameplay image equivalence, performance, VRAM headroom, mip/LOD behavior or
output on a native 4K display. No matched gameplay campaign was run. In
particular, this milestone does not explain or fix the user's reported
roughly 6 fps with the default 480p launcher settings.

## Current validation boundary

The documented retail generation `20261029-175500-dfadbdd9` built in Release
against verified framework tree `a4489cd69600a1489edea98ac19cf20db242b6d2`.
Patch 0024 applies independently to tree
`8d57b4ceb02054f4921a88a7233e6722625c14ff` and reconstructs that earlier
tree. The coordinated current tree adds patches 0025 and 0026; neither was
retested on that retail generation in this work.
Patch 0023
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
