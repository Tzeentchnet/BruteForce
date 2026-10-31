# Brute Force static recompilation

An experimental, disc-based Windows port of the original Xbox game using
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp). This repository does not
contain the game, its assets, or generated game code. Supply a disc image made
from your own copy.

## Status

This is **experimental bring-up**, not a complete playable release. An opt-in
graphics and USB diagnostic reached the campaign tutorial, rendered its 3D
environment, and showed the character moving in response to scripted
left-stick input and injected keyboard events. A separate scripted right-stick
probe visibly rotated the camera. Audio is now enabled by default and runs the
framework's stereo all-mixbin fold-down. The accurate xemu DSP56300 path
remains an experimental opt-in: a user reported crackly, mono-sounding
dialogue and gunfire while music sounded acceptable. Bounded captures found
no host underruns or recurring sample-boundary discontinuities, but the
accurate output was substantially more left/right correlated than the simple
path. Broader listening and missions are still required. Other gameplay,
physical controllers and keyboards, and overall rendering correctness remain
unverified.
Some tutorial captures are dark and missing the HUD. The experimental path can
still stall or fail on untested behavior and is disabled by default. A
stationary tutorial comparison reported about 3.5 flips per second before
recent software optimizations and 4.0 afterward. These are not measurements of
distinct moving-world frames and are far below the playability target.

The command-line graphics path defaults to xboxrecomp's CPU pushbuffer
rasterizer and Direct3D 11 framebuffer presentation. The launcher defaults
to the experimental Direct3D 11 renderer (`RECOMP_GPU=d3d11`), which renders
and runs vertex programs on the GPU. In the
tutorial's stationary view an earlier comparison measured about 30 rendered
frames per second against about 3 on the CPU path. A later three-run GPU
comparison reported movement pixel-change rates of 25.2 and 35.2 fps, but
58.9–60.2% of its movement flips matched a solid-black frame. Those rates do
not measure playable world fps. A later clear fix preserved the character in
two captured views in one diagnostic; sun-facing bloom remains unexplained.
An experimental packed-stencil and shared-presentation comparison produced
much darker stationary backgrounds but did not establish a performance gain
or original-hardware correctness. One later alias-only hardware D3D11
diagnostic reproduced the darker stationary image without shared presentation.
One earlier boot stalled and needed a retry. The newest GPU-vertex tutorial
capture is close to the CPU reference but still has unexplained differences;
an older CPU-vertex GPU capture differs substantially. Rendering correctness
and the title's native frame rate remain unverified. Blood Wake-derived
native and fixed internal-resolution switches now pass synthetic WARP tests,
but have not been validated in retail gameplay or on a native 4K display.
True widescreen and unlocked frame rates are not implemented. A source and
executable investigation of a genuine Hor+ path is recorded in
[FINDINGS](docs/FINDINGS.md#blood-wake-feature-review-and-true-widescreen-investigation)
and staged in [NEXTSTEPS](docs/NEXTSTEPS.md#e-true-hor-widescreen-investigation-complete-implementation-pending).
The user's reported roughly 6 fps while moving at the default 480p D3D11
settings remains unresolved; selecting a higher resolution is not a
performance fix.

The pipeline recognizes one North American retail `default.xbe` (title ID
`0x4D53001E`, SHA-256 recorded in `fingerprints/retail.json`). That fingerprint
identifies the executable used for this bring-up; it is not an independent
authentication of a disc image or its assets. Other revisions must be analyzed
and tested before they can be supported.

## Project documentation

- [Architecture and asset formats](docs/ARCHITECTURE.md), including the
  evidence and open questions for a future Blender importer.
- [Rendering architecture](docs/RENDERING.md), including the guest-to-host
  pipeline, D3D11 systems, synchronization, presentation and validation limits.
- [Brute Force findings](docs/FINDINGS.md), including executable-specific
  addresses, diagnostics, and bring-up limits.
- [Upstream candidates](docs/UPSTREAM.md), including existing selected PRs and
  filed contributions, overlap decisions, and remaining candidates.
- [Performance plan](docs/PERFPLAN.md), the phased move from the CPU
  rasterizer to a GPU-authoritative D3D11 renderer.

## Requirements

- 64-bit Windows 10 or 11, a Direct3D 11-capable GPU, and sufficient disk space
  for the extracted disc, generated C, and build.
- Python 3.10 or later with Tkinter for the launcher, Git, CMake 3.20 or later, and Visual Studio 2022
  17.5 or later Build Tools with the C++ workload and Windows SDK. The MSVC
  build enables its experimental C11 atomics support for the guest memory
  fence.
- A local XDVDFS/XISO image of the supported game. The build downloads only
  public tooling; it does not upload the disc, extracted files, or generated
  code.

From the project directory:

```powershell
python -m pip install -r tools\requirements.txt
python tools\build.py all --disc "F:\path\to\your\Brute Force.xiso.iso"
python tools\build.py run --experimental-tutorial
```

The equivalent individual steps are:

```powershell
python tools\build.py setup
python tools\build.py extract --disc "F:\path\to\your\Brute Force.xiso.iso"
python tools\build.py generate
python tools\build.py build --config Release --parallel 4
python tools\build.py run --config Release --experimental-tutorial
```

To build the game and selected native fixtures in one build, repeat
`--extra-target`, for example `--extra-target bruteforce_d3d11_test
--extra-target bruteforce_gpu_shared_present_test`.

`setup` installs the pinned framework and fixes in `.deps\xboxrecomp`. It
refuses to overwrite a different or modified checkout. `extract` verifies the
executable before extraction and compares existing files byte-for-byte instead
of overwriting them. `generate` analyzes all executable sections, recovers
ABIs, includes the observed function entries in `fingerprints\retail-seeds.json`,
and writes each run to a fresh directory. It records logs and a manifest under
`analysis\runs`; generated code is kept under `generated`. `build` uses the
latest successful generation and MSVC's x64 toolchain. Generation verifies
that each seed is present in dispatch, and build and run reject generations
whose game, fingerprint, seeds, manual source, or pinned toolkit no longer
match.

The executable checks the XBE fingerprint again at startup. Its default game
directory is `game\disc` relative to the current working directory; an
absolute game directory can also be passed as its only argument. The `run`
command starts each boot in a fresh directory under that generation's
`analysis\runs\...\boots` so the toolkit's kernel log is not overwritten. It
enables the toolkit's vblank and unresolved-stub diagnostics unless the
corresponding `RECOMP_VBLANK` or `RECOMP_STUB_LOG` environment variable is
already set. `run --graphics` explicitly enables the upstream pushbuffer
executor, framebuffer window, and Direct3D 11 presentation; the default run
is headless. For a bounded diagnostic, set `RECOMP_WATCHDOG_SECS` to a number
of seconds before running. The watchdog exits after that fixed time even if
the game is making progress. Successful file reads are quiet by default; set
`RECOMP_READ_TRACE=1` to enable their verbose logging and byte dumps. Failed
reads remain logged. When running the executable directly, the toolkit writes
`xbox_kernel.log` in the current directory and replaces any existing file with
that name. Untranslated instructions and unresolved indirect calls stop the
host with a diagnostic. Generated unresolved direct-call stubs still use the
toolkit's behavior and are logged when reached; a stub hit is a correctness
issue, not evidence that the game is working.

`BRUTEFORCE_DAC_TRACE=1` enables a title-specific diagnostic at the candidate
DAC-ramp writer. It logs the entry's guest addresses, runtime base and a
snapshot of its three 256-byte RGB planes to stderr; unset or `0` leaves it
off. Invalid settings and unreadable or guarded guest spans stop with an
error. Snapshots can race with guest writes or memory-protection changes,
and logging perturbs timing. An entry does not prove that the DAC writes
completed or that the ramp reached the displayed image. Keep the resulting
logs local. One unreviewed isolated diagnostic observed a single identity
ramp at the NV2A base; it does not establish the ramp at a matched opening
fade or rule out other writers. See
[FINDINGS](docs/FINDINGS.md#static-gamma-ramp-investigation).

For source-line crash diagnostics, configure an existing build with
`cmake -S . -B build -DBRUTEFORCE_LINK_SYMBOLS=ON` and rebuild. Crash reports
include guest register and stack candidates; `BRUTEFORCE_CRASH_STACK=1` also
dumps raw guest stack words, which may contain local game data.

### Graphics launcher

After building, double-click `Brute Force.bat` (or `Brute Force.cmd`), or run
`python tools\launcher.py`. The launcher starts the same experimental
tutorial configuration as `run --experimental-tutorial`; it does not make
the game fully playable. It validates the build and game before each launch
and keeps each boot's output in `launcher.log` alongside `xbox_kernel.log`.
The launcher returns when the game closes and reports a nonzero exit.

The Display tab selects windowed or borderless fullscreen, initial window
size, point or linear presentation filtering, and presentation VSync.
Alt+Enter or F11 toggles fullscreen during play. Window sizing and display
filtering do not themselves increase internal resolution or add widescreen.
If the D3D11 presenter falls back to GDI, its filtering and VSync settings do
not apply.

The Graphics tab selects the CPU reference or experimental D3D11 renderer,
internal resolution, GPU or CPU vertex and texture processing, and the
optional persistent pixel shader cache. Internal-resolution choices are
**Native (match output)**, 2160p, 1440p, 1080p, 720p and the default
480p guest resolution. These are 4:3 picture heights: for example, 2160p is
a 2880x2160 rendered picture inside a 3840x2160 output. They do not add
widescreen. The Experimental tab exposes window clipping, asynchronous
visibility reports, programmable near-plane clipping, depth and stencil
aliases, and shared GPU presentation. The launcher defaults to Direct3D 11
at 480p with experimental options off or using their existing defaults;
existing saved renderer choices are retained. The command-line path still
defaults to the CPU reference. The individual GPU paths have the limitations
below.

The Audio tab defaults to **Simple (recommended)**. Launcher settings from
versions 1-3 that selected the old Accurate default migrate once to Simple;
after that migration, explicitly selecting Accurate is retained. Accurate is
labelled experimental.

The launcher defaults to **HUD without additive bloom**. This mode uses the
screen-zero near plane for ordinary and alpha-blended programmable UI draws,
but keeps zero-depth additive draws behind a small positive plane. Positive-
depth additive geometry is unaffected. Version-1 launcher settings that used
the old inherited default migrate to this mode. **Legacy** and **Screen-zero
(HUD and glow)** remain available for comparison. The latter can increase
bloom. HUD-safe and screen-zero modes cannot be combined with
`RECOMP_CLIP_NEAR_Z=0`; their original-hardware fidelity still needs retail
and reference validation.

For the reported excessive bloom, the D3D11-only **Packed depth/stencil
aliases** option is an experimental diagnostic, not a confirmed fix. It
markedly darkened one stationary gameplay view even with shared presentation
off; whether that brightness is correct remains unknown. The user also
observed the HUD and excessive glow disappearing together in the live
window. To isolate the alias's effect, leave shared GPU presentation and
other options unchanged; set the alias option back to Off to restore the
previous behavior. It has no effect on the CPU renderer.

Settings are saved to `%LOCALAPPDATA%\BruteForce\settings.json` on Save or
Play. The chosen values override the corresponding environment variables for
the launched process only; unrelated environment variables are inherited.
An invalid settings file is reported and left unchanged. Choose the build
configuration that matches the most recent build.

### Experimental tutorial path

After building, start a fresh PowerShell session and opt into the tutorial
configuration:

```powershell
python tools\build.py run --config Release --experimental-tutorial
```

This enables graphics, vblank, USB, the staged pushbuffer walker, and PGRAPH
interrupts, and defaults `RECOMP_KEYBOARD` to `1` for the launched process.
Set `RECOMP_KEYBOARD=0` before running to disable keyboard input. The strict
interrupt gate described below is required; an explicitly disabled gate or
disabled IRQ safe points are not supported.

The keyboard mapping uses arrows for the D-pad, Enter for Start, Z/X/C/V for
Xbox A/B/X/Y, WASD for the left stick, and IJKL for the right stick. Input
comes from the focused game window. Keyboard tests injected window messages
rather than using a physical keyboard; physical controllers and other campaign
scenes have not been verified. In one probe, the tutorial reported approximately
3.7 flips per second, not a measurement of distinct world-frame FPS.

A menu script used in one diagnostic was
`RECOMP_PAD_SCRIPT="20000:a:200,23000:a:200,28000:a:200"`;
times are milliseconds from the first USB report and may need adjustment on
another machine or when audio is enabled. Leave the variable unset for manual
input.

Audio is enabled by default on the command line and in the launcher:

```powershell
python tools\build.py run --experimental-tutorial
```

The default `Simple` mode uses the stereo all-mixbin fold-down and outputs
48 kHz stereo PCM. It is the recommended path after the accurate mode failed
the listening gate for dialogue and gunfire. It also includes a framework fix
that preserves the distinct left/right channels produced when HRTF filters a
mono 3D voice; the previous path copied the left HRTF result into every
route. The host uses eight 512-sample XAudio2 buffers by default for an
85.3 ms maximum queue. It refills at the 3,072-sample (64.0 ms) low-water
mark without increasing that maximum; `RECOMP_AUDIO_BUF_COUNT` remains an
expert override. The launcher can select `Accurate (experimental Xbox DSP)`,
which runs the title's GP and EP programs on xemu v0.8.139's DSP56300
interpreter.

The last long Accurate validation, recorded while Accurate was the default,
ran 164.47 seconds from startup through the observed 421-draw tutorial world
and scripted movement input. It recorded zero host underruns, starvation
events, or refused submissions; every marked startup, cinematics/menu,
tutorial, and gameplay interval was non-silent, with no clipped samples or
recurring 32/256/near-2,048-frame hole candidates. After the default changed,
a bounded boot of the final linked executable with `RECOMP_APU_DSP` unset
selected the stereo fold-down.

A corrected Quick Campaign diagnostic reached the rendered world and
confirmed that Brute Force uses 3D/HRTF voices for gameplay effects. In a
matched fixed/baseline/fixed firing interval, left/right correlation measured
0.415/0.542/0.422 and side-to-mid RMS measured 0.641/0.544/0.636. The three
selected captures reported no starvation, underruns, refused submissions,
clipping, or recurring 32/256-frame holes. These measurements verify channel
routing only. A subsequent gameplay listening pass found the fixed Simple
path improved but not fully correct; further audio work is deferred, and the
remaining symptom has not yet been isolated.

Controls:

- `--no-audio` or `RECOMP_AUDIO=0` disables the APU and host output.
- `RECOMP_AUDIO=1` explicitly enables audio. Unset also means enabled.
- `RECOMP_APU_DSP=fold` selects the default simple stereo path; `xemu`
  selects experimental accurate processing. Unset selects `fold`.
- `RECOMP_APU_DSP_FALLBACK=0` disables automatic fallback when the accurate
  DSP repeatedly cannot keep real time.
- `RECOMP_APU_DSP_THREAD=0` runs the DSP stage on the APU frame thread for
  diagnosis.
- `RECOMP_APU_DSP_TRACE=1` enables the five-second DSP cost and worker-wait
  reports. They are off by default so diagnostic I/O cannot block an audio
  worker.

Invalid `RECOMP_AUDIO` values fail explicitly. The old
`--experimental-audio` argument remains a hidden compatibility alias but is
no longer needed. Audio can change timing relative to older silent runs; the
current diagnostic menu script accounts for the default-on path.

To capture a bounded local WAV for diagnosis, set `RECOMP_AUDIO_WAV` to a
writable file path and `RECOMP_AUDIO_WAV_SECS` to the desired maximum number
of seconds before launching. Captures contain game audio and should remain
private. Automated waveform and host-continuity checks do not replace a
listening pass.

#### Audio capture diagnostics

The standalone checker uses only Python's standard library. Capture a WAV
and a separate host log from the **same run**; use a fresh shell and unique
paths so existing captures are not overwritten. For example:

```powershell
New-Item -ItemType Directory -Force analysis | Out-Null
$run = [guid]::NewGuid().ToString("N")
$wav = "$PWD\analysis\audio-$run.wav"
$log = "$PWD\analysis\audio-$run-host.log"
$env:RECOMP_AUDIO_WAV = $wav
$env:RECOMP_AUDIO_WAV_SECS = "30"
$env:RECOMP_AUDIO_HOST = "1"
python tools\build.py run --config Release --experimental-tutorial 2> $log
```

After the run has closed the capture, replace the example ranges below with
the actual marked portions and analyze both files locally:

```powershell
python tools\audio_analysis.py $wav --host-log $log `
  --active "music:48000:480000" --silence "intentional:480000:528000" `
  > "analysis\audio-$run.jsonl"
```

Ranges are optional, repeatable, non-overlapping **stereo sample-frame**
intervals with zero-based, inclusive start and exclusive end indices. Their
labels are arbitrary and must be unique; `START:END` also works and creates a
label. Unmarked gaps remain unlabelled. `--silence` means intentionally quiet,
not necessarily digital zero, and is excluded from active-interval tests.
The checker rejects empty, out-of-range or overlapping intervals and invalid,
truncated, stale-header or trailing-byte RIFF files. It accepts PCM and
extensible PCM only at 48 kHz, stereo and signed 16-bit. A valid header alone
cannot prove the capture finalized gracefully.

The streamed JSON Lines include metadata, event records, per-segment summaries
and a final `audio_summary`. Per-channel RMS is in s16 units and dBFS relative
to 32768; clipping counts samples exactly at -32768 or 32767. `zero_run`
events give the maximal physical run's start and end for each channel and for
both channels zero together, even across label boundaries. Runs of all lengths
are counted; events default to at least 32 frames, or use `--min-zero-run 1`
to emit every run. Per-segment run statistics are split at interval boundaries.

`silent_2048_period` events and `active_2048_periods` count fully zero stereo
periods and their fraction **only within marked active intervals**. Each
interval is partitioned into complete, non-overlapping 2,048-frame periods
starting at its first frame; the final partial period is excluded and counted
separately. No complete periods gives a null fraction, not zero. The sequencer
phase is unknown, so a different alignment can change the fraction; inspect
the full zero runs too. The 32- and 256-frame `hole_candidate` events require
both channels to be exactly zero and nonzero stereo neighbors *within* the
same active interval. They report the sample indices and spacing to the
previous candidate and to the previous candidate of the same length.
Spacings 2,016–2,080 frames inclusive are counted as near 2,048. These are
indicators, not proof that the sound was unintended.

Zero-crossing rates count sign changes between nonzero samples, bridging at
most 31 consecutive exact zeros and resetting at interval boundaries. The
broadband indicator is mean spectral flatness over Hann-windowed 256-frame
samples taken every 4,096 frames within each segment; incomplete windows and
windows with RMS below one s16 unit are excluded, DC is omitted and the power
floor is `1e-12` times the window's mean power. Neither indicator is a
calibrated noise verdict.

`RECOMP_AUDIO_HOST=1` (or `RECOMP_APU_TRACE=1`) enables the actual
`[AUDIO-HOST]` periodic counters; the parser also recognizes `[XA2] underrun
xN` and the supplementary `[APU-IRQ]` count. Snapshot `starves`,
`starved_ms`, `refused` and `underruns` are cumulative, while `wav` and
`played` are submitted and device-consumed frame counts. Event-line counts
are not totals: underrun messages are rate-limited and refusal messages are
capped. Starvation events identify a WAV insertion point; refusal events give
the end index of a refused buffer. Indices beyond a capped WAV can occur.
The WAV tap precedes device acceptance, so a clean WAV cannot rule out
starvation, discarded submissions, or silence inserted by the device.

Missing, incomplete, inconsistent or unsupported diagnostics, a snapshot
ending before the WAV, and non-XAudio2 or mixed output yield an
`inconclusive` host status. `no_events_reported_in_snapshots` is scoped to
the logged snapshots, **not a pass**: there is no complete end-of-run host
summary, the starvation detector has a jitter threshold, and the checker
cannot prove the log and WAV belong to the same run. A successful exit means
analysis completed, not that playback is correct; a listening pass remains
necessary. Invalid arguments/files return a nonzero exit, and a failed stream
may contain partial JSON without the final summary. Processing is bounded by
8,192-frame input chunks, 256-frame spectrum windows, 1,024 marked intervals
and 16,384-character log lines. UTF-8 and BOM-marked UTF-16 logs are accepted.

The focused fixtures use synthetic PCM only. To run them without touching
other pytest temporary runs, choose a fresh private directory inside `analysis`:

```powershell
New-Item -ItemType Directory -Force analysis | Out-Null
$testdir = "analysis\audio-tests-$([guid]::NewGuid().ToString('N'))"
python -m pytest -q -p no:cacheprovider -o tmp_path_retention_count=1000000 `
  -o tmp_path_retention_policy=all --basetemp $testdir tests\test_audio_analysis.py
```

### Experimental GPU renderer and benchmark

```powershell
$env:RECOMP_GPU = "d3d11"
python tools\build.py run --experimental-tutorial
Remove-Item Env:\RECOMP_GPU
```

`RECOMP_GPU` selects the renderer: unset or `cpu` is the CPU rasterizer, the
correctness reference; `d3d11` renders on the GPU; `null` is a measurement
probe that draws nothing. The D3D11 renderer uses the D3DCompiler runtime
(`d3dcompiler_47.dll`, part of Windows 10 and 11). `RECOMP_GPU_VSH=0` keeps
vertex programs on the CPU. `RECOMP_GPU_VSH_VERIFY=<n>` compares every nth
GPU-program batch with the CPU interpreter; fully culled batches are logged
as skipped because they have no GPU vertex output. Common uncompressed and
paletted textures are decoded on the GPU; `RECOMP_GPU_TEX_DECODE=0` keeps
their CPU decoder for comparison. Compressed textures can be uploaded
directly. Set `RECOMP_GPU_TEXLOG=1` to log texture uploads locally.
The D3D11 texture cache compares saved source and palette bytes on each
ordinary cache use; retained snapshots have a 128 MiB budget, excluding
transient copies and GPU resources. The performance cost has not been
measured. `RECOMP_GPU_TEX_CACHE_APPROX=1` restores the old approximate
checks for diagnostics, but can leave rewritten textures stale.

The D3D11 P8 path uses the programmed 32, 64, 128 or 256-entry palette and
rejects unreadable or known-short allocations and out-of-range indices.
Actual hardware behavior for out-of-range indices, untracked suballocations,
render-target aliases and concurrent guest writes remain unverified. The CPU
reference path is unchanged. Device loss in the renderer is reported and
stops execution rather than reporting GPU work as complete; device recovery
and the separate presenter device have not been validated.

`RECOMP_GPU_SCALE=native` renders the D3D11 path at the current 4:3 picture
inside the output window. A numeric value selects physical pixels per
640x480 guest pixel; the launcher presets are `1`, `1.5`, `2.25`, `3` and
`4.5`. Numeric values from 0.5 through 10 are accepted when their resulting
resources fit D3D11's 16384-pixel dimension limit.
`RECOMP_GPU_OUTPUT=WxH` can provide a fixed diagnostic output only with
`RECOMP_GPU_SCALE=native`; otherwise native mode follows window and
fullscreen changes through a scale epoch. Its 4:3 picture must produce a
scale from 0.5 through 10.

Scaled colour and depth targets retain guest-logical dimensions separately
from their physical resources. Clear rectangles, scissors and window clips
are mapped to physical pixels. Render targets remain directly sampleable,
and the completed frame is box-filtered back to guest size before the
mandatory CPU readback. The shared presenter receives an independently sized
GPU frame; its final pass uses an exact copy, box downsample or Catmull-Rom
upscale to fit the output picture. Visibility-query samples are normalized
back to guest-pixel units. Scale 1 keeps the prior allocation and readback
paths and remains the default.

Bink opening movies write decoded pixels directly into the guest colour
surface rather than using PVIDEO or D3D11 draws. The D3D11 backend keeps a
logical guest shadow, mirrors full colour clears, and uploads detected CPU
writes before the first GPU overlay draw or at flip. The upload is scaled into
the physical render target before shared presentation. A detected CPU write
that races unresolved GPU colour work is reported and does not overwrite that
GPU frame.

Selecting a scaled or native resolution enables shared GPU presentation.
Explicitly setting `RECOMP_GPU_DIRECT_PRESENT=0` with an active scale is an
error because a guest-sized CPU frame cannot carry the higher-resolution
result. Experimental depth and packed-stencil aliases are also rejected in
scaled modes; their guest-layout seed and sampling semantics have not been
defined at physical resolution. The CPU renderer remains at guest resolution.
The complete synthetic D3D11 matrix passes 190 cases with hardware enabled
(123 pass and 67 hardware cases skip in WARP-only mode), including CPU-written
frames with and without a GPU overlay at scale 1 and scale 2. A bounded retail
4.5x run captured visible Microsoft, Digital Anvil, and intro-montage frames
with zero guest/GPU conflicts. No matched gameplay, performance, VRAM,
native-4K-display or original-hardware validation has been completed.

`RECOMP_GPU_PSCACHE=1` enables the optional persistent combiner pixel-shader
cache (also available in the launcher). It is off by default. After a shader
has compiled successfully, later launches can reuse its bytecode from
`%LOCALAPPDATA%\BruteForce\shader-cache`; vertex and other shaders still
compile as before. The key covers the generated HLSL, compiler binary,
source name, entry point, target, and compile flags. Files have bounded sizes,
checksums, and atomic replacement. A blocked replacement is retried briefly;
an existing entry is accepted only if its key, checksum and bytes match the
newly compiled shader. Other publication errors, damaged entries and
unavailable directories are logged; the shader still runs from the compiled
bytecode. For diagnostics,
`RECOMP_GPU_PSCACHE_DIR` can name an existing directory; use an absolute path
because each boot has its own working directory. Delete the cache directory
to discard the derived bytecode. Synthetic cold/warm tests preserved the
same output; small differences in content-matched tutorial captures remain
unexplained, and a gameplay stutter improvement is not yet established.

`RECOMP_GPU_ZPASS=async` enables experimental asynchronous GPU visibility
reports; synchronous reports remain the default. The asynchronous path
passes synthetic WARP and hardware fixtures but remains experimental in the
title.

`RECOMP_GPU_DEPTH_TEXTURE=1` enables experimental `LIN_D24S8` depth-texture
aliases on D3D11. It only accepts exact matches to GPU-created D24S8 surfaces
with matching linear layout, dimensions and pitch. New eligible surfaces
are seeded from guest memory; a full-surface GPU depth clear also initializes
them.
Recreated surfaces are not reseeded from potentially stale guest memory and
must receive a full-surface depth clear before sampling. Sampling a currently
bound depth target uses a GPU snapshot; PROJECT2D and PROJECT3D apply the shadow comparison
function on extended draws; base draws only support PROJECT2D. Guest writes,
partially overlapping aliases, depth cube textures, multiple mip levels,
other depth formats, and CPU reference decoding are not supported. Draws
using rejected depth aliases or stage modes are skipped with a diagnostic.
The option is off by default and has not established original-hardware
equivalence. It cannot be combined with an active render scale. Per-flip
guest readback and synchronization are unchanged.

`RECOMP_GPU_STENCIL_ALIAS=1` separately enables experimental packed D24S8
sampling when a `0x41` texture exactly aliases a tracked GPU depth surface.
The GPU reconstructs the depth bytes and stencil alpha into a filterable
texture. Depth and stencil must each be initialized; unsupported overlaps
are rejected rather than read from stale guest memory. Ordinary `0x41`
textures are unchanged. Guest writes and partially overlapping aliases
remain unsupported. A synthetic replay of the traced bright-pass state shows
how stale stencil bytes could exaggerate glow, but the effect on the reported
gameplay bloom and original-hardware fidelity remain unestablished. It cannot
be combined with an active render scale.

`RECOMP_GPU_DIRECT_PRESENT=1` lets the D3D11 renderer hand a finished
32-bit GPU surface, whose dimensions may differ from the guest framebuffer,
to the D3D11 window presenter through a bounded shared texture. Pinned
framebuffers and PVIDEO overlays use the CPU-composited frame instead. Guest
readback still happens on every flip for guest-visible memory, frame
statistics and captures, so this does not remove that stall or bound all GPU
work in flight. CPU-written Bink frames are first synchronized into the GPU
surface and remain eligible for the scaled shared route. The option is off at
the default 480p resolution and is enabled automatically for scaled modes.

The D3D11 renderer respects partial color, depth and stencil clear rectangles
instead of clearing the whole target for each strip. When a registered backend
owns both drawing and clearing, the executor no longer redundantly clears its
software depth buffer; CPU and incomplete-backend paths retain that clear.

`python tools\bench.py` boots the tutorial with isolated saves and a fixed
input script and measures the stationary and movement scenes; see
`python tools\bench.py --help` and `docs/PERFPLAN.md`. Results stay under
`analysis\bench`. `RECOMP_FRAME_STATS`, `RECOMP_GPU_CENSUS` and
`BRUTEFORCE_PROFILE` are the per-flip statistics, feature census and sampling
profiler it uses. `--scene movement --capture` collects both stationary and
movement measurements and stationary flip-time captures in the same run. Summaries
report failed attempts separately from failed runs, including retries.
`--scene movement --capture-movement` also captures guest-surface flips at two
scripted movement points. They may differ from presented frames, and this
diagnostic can perturb the measured frame rate.
`--trace-frame --trace-after 5` traces a frame five seconds into movement.
When captures and `--census` are combined, periodic reports remain enabled
to write the census. The harness sets `RECOMP_FB_DUMP_FLIPS_ONLY=1` to
suppress report-time and after-draw dumps in the numbered capture sequence;
the unnumbered `hold` dump may still be mid-frame. Use numbered flip captures
for image comparisons.
Results also count pixel hashes matching a solid-black 640×480 four-byte image
with alpha 0 or 255. This is a diagnostic, not a detector for every black or
dark frame; intentional fades can match too. Pixel-distinct fps, including HUD
changes, is not necessarily distinct world fps.

`--capture-world` records movement flips for later review and names each
`holdNNNNNNNNNN.bmp` with the corresponding `flip` in `frames.csv`. These are
guest-surface snapshots, not proof of what the window displayed: overlays,
pinned surfaces and direct GPU presentation can differ. The timestamp is
recorded after the synchronous dump, and continuous capture affects timing.
Legacy numbered dumps do not carry flip IDs. Review the scene, HUD exclusions,
alignment and presented-image source using the manifest documented in
`tools\bench_world.py`; unlabeled changes are not world-frame FPS.

For a separate Windows diagnostic, `RECOMP_PRESENTER_CAPTURE` records the
GDI or CPU-upload D3D11 presenter's converted input and its API result.
Use a new capture directory and a new statistics filename under an existing
parent, then close the framebuffer window and verify after the process exits:

```powershell
$env:RECOMP_FB_WINDOW = "1"
$env:RECOMP_PRESENTER_CAPTURE = "C:\local-captures\run001"
$env:RECOMP_FRAME_STATS = "C:\local-captures\run001-stats.csv"
# Launch the title and close its framebuffer window when finished.
python .deps\xboxrecomp\tools\validate_present_capture.py `
  "C:\local-captures\run001" --stats "C:\local-captures\run001-stats.csv"
```

This opt-in writes and flushes an image for every presenter submission; it
can consume several GiB per minute and perturb timing. It is not a benchmark
or a screenshot and is not yet integrated with `--capture-world`. Direct
shared-GPU presentation and SDL are rejected rather than substituted;
pinned frames cannot be aligned. A verified flip link identifies the source
event, not identical guest-snapshot pixels, physical scanout or an unobscured
window. The verifier rejects incomplete captures, including runs that end
without a completion marker. Minimized windows can produce no-client or
occluded submissions, which the verifier does not accept as aligned evidence.
Keep images and logs local. See
[FINDINGS](docs/FINDINGS.md#presenter-source-capture-alignment).

For matched audio-on/off runs, `--readiness manual` requires an operator to
reach and confirm the same visible state for every attempt. `--capture-audio`
uses unique WAV files and checks their finalization; the console-control
shutdown and WAV-validation helpers also passed the final firmware-exit
capture. The complete matched manual A/B/A workflow has not yet been
validated with the game.
The benchmark removes inherited audio switches before applying each variant;
audio is on when neither switch is supplied. Use `RECOMP_AUDIO=1` and
`RECOMP_AUDIO=0` for explicit matched variants. Legacy
`RECOMP_AC97_READY` is presence-based, so even `RECOMP_AC97_READY=0` enables
it and is rejected for a manual comparison. Reviewed A/B/A comparisons
require the matching manifest described in `tools\bench_match.py` and remain
inconclusive until real captures and inputs have been checked.

For matched captures, the frame comparator can label either source without
treating it as ground truth:

```powershell
$comparison = [guid]::NewGuid().ToString("N")
python tools\compare_frames.py "analysis\reference.png" "analysis\gpu.png" `
  --reference-label xemu --candidate-label GPU `
  --diff "analysis\frame-diff-$comparison.png" > "analysis\frame-compare-$comparison.json"
```

Compare the same scene and aspect at the same pixel dimensions; the tool
rejects unequal sizes and does not align, crop, resize, or color-correct its
inputs. Its RGB statistics include whole-image absolute differences and all
non-identical grid cells, with a default 16-pixel cell size. It also counts
pixels with any channel differing by strictly more than 8/255, independently
of `--tolerance`; the optional mask uses the selected tolerance. Alpha is
ignored. A CPU reference and xemu provide useful independent comparisons, but
neither alone proves original-hardware correctness.

### Experimental window clipping

The software rasterizer and D3D11 renderer can optionally apply the title's
NV2A window-clip rectangles:

```powershell
$env:RECOMP_WINDOW_CLIP = "1"
python tools\build.py run --experimental-tutorial
Remove-Item Env:\RECOMP_WINDOW_CLIP
```

This is disabled by default and has not been validated against original
hardware during gameplay. Before both clip axes have been programmed, this
implementation leaves clipping inactive; the hardware's initial state still
needs investigation. Other registered draw backends must advertise support
for window clips or the executor stops with an error. This option is not a
4K mode.

For local surface-read diagnostics, `BRUTEFORCE_SURFACE_GUARD_EXCLUDE_PAGE`
may specify one page-aligned guest address to leave unprotected when
`BRUTEFORCE_SURFACE_GUARD` is enabled with the null backend. The report
records the exclusion; reads and writes on that page are not observed.
Neither a clean report with an excluded page nor this diagnostic's other
coverage limitations justify skipping guest readback.

### Experimental face culling

The software rasterizer and the extended D3D11 backend support an independent,
opt-in face-culling experiment:

```powershell
$env:RECOMP_CULL_FACE = "1"
python tools\build.py run --experimental-tutorial
Remove-Item Env:\RECOMP_CULL_FACE
```

It uses the title's front-face and cull-face settings and corrects triangle
strip winding while culling is active. Culling remains inactive until both
face settings have been programmed; their hardware reset state is not yet
verified. This mode is off by default, is not validated against original
hardware, and can hide geometry incorrectly. Synthetic fixtures compare CPU
and D3D11 CPU-transformed and GPU-program paths, but do not establish title
fidelity or a performance improvement. Registered backends without the
extended draw interface are rejected while the option is enabled.

### Experimental programmable near plane

The software rasterizer and extended D3D11 backend can also test a screen-space
zero near plane for programmable vertex output:

```powershell
$env:RECOMP_PROGRAM_NEAR_ZERO = "1"
python tools\build.py run --experimental-tutorial
Remove-Item Env:\RECOMP_PROGRAM_NEAR_ZERO
```

This is off by default. It keeps near-depth and eye-plane clipping active,
but replaces the legacy `c[59].z` near boundary for programmable vertices;
fixed-function behavior is unchanged. The setting is experimental and may
introduce visual artifacts: it has not been validated against original
hardware or all depth-range modes, and does not add far-plane or guest
depth-range clipping. It cannot be combined with `RECOMP_CLIP_NEAR_Z=0`;
registered draw backends without the extended interface are rejected. This
is not a 4K, widescreen, or frame-rate enhancement.

In one pair of isolated stationary tutorial runs, the health-bar region was
visible in 597 of 597 sampled window images with this option, versus 103 of
591 with the legacy plane. Samples can repeat the same frame; this is not a
hardware reference or proof that the rest of the HUD is correct.

### Isolated controller diagnostic

Use a fresh, existing, short directory for each isolated run. The toolkit creates
partition images there, which may require several gigabytes of free space.
For example, in a fresh PowerShell session:

```powershell
$save = Join-Path $env:TEMP ("bruteforce-probe-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $save | Out-Null
$env:RECOMP_USB = "1"
$env:RECOMP_USB_STATS = "1"
$env:RECOMP_PAD_SCRIPT = "0:start:500,1500:a:500"
$env:RECOMP_WATCHDOG_SECS = "25"
python tools\build.py run --graphics --isolated-saves $save
```

`--isolated-saves` requires an existing empty directory and routes both toolkit
roots to it, overriding inherited root settings for that process only. It
does not delete the directory; the partition images and diagnostic data remain
there after the run. Use a new directory for each isolated run. To retain and
reload progress across runs, deliberately configure a persistent save root
instead of reusing this fresh-directory option.

`BRUTEFORCE_SAVE_ROOT` controls the toolkit's partition-image root and must
name an existing directory with room for additional save paths. It also sets
`RECOMP_SAVE_DIR` if that variable is unset; an existing `RECOMP_SAVE_DIR`
overrides the user-data location, so set both to the same fresh directory when
isolation matters. Check `xbox_kernel.log` for the selected save and user-data
paths; the upstream log may truncate non-ASCII paths, so also verify the
directories on disk. These files are local diagnostic data, not game assets to
distribute.

With `RECOMP_USB` present, the host enables the dispatch gate unless it was
explicitly configured. USB initialization rejects a disabled gate, disabled
IRQ safe points, or forced ungated IRQ delivery. A title-specific guard models
only the observed XPP `CLI`/`STI` sequence at DISPATCH or device IRQL; it is
not general interrupt or `POPF` emulation. The generated sequence is checked
before build and run. If the guest entry returns with USB active, the host
terminates the process rather than unmapping memory that guest worker threads
could still access; clean in-process restart is not supported.

All disc files, locally derived analysis, generated sources, downloaded
tooling, and build products are excluded by `.gitignore`. Do not distribute
them without the necessary rights.

## Upstream work

The framework is pinned to commit
`f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b` (v0.16.0). Setup applies these
independently reviewed upstream pull requests at fixed commits and verifies
the resulting Git tree:

- [#262](https://github.com/sp00nznet/xboxrecomp/pull/262): preserve alias
  bodies during disassembly gap detection.
- [#280](https://github.com/sp00nznet/xboxrecomp/pull/280): preserve general
  EBP across guest calls.
- [#277](https://github.com/sp00nznet/xboxrecomp/pull/277): correct shift
  counts, narrow multiplication, and x87 save/restore.
- [#278](https://github.com/sp00nznet/xboxrecomp/pull/278): correct narrow
  arithmetic immediates and flags.
- [#264](https://github.com/sp00nznet/xboxrecomp/pull/264): hold the APU frame
  while SSL restarts the sequencer.

Two generic fixes that remain in the local patch stack have now been submitted
upstream:

- [#297](https://github.com/sp00nznet/xboxrecomp/pull/297): validate
  extra-thunk lookbehind ordinals before rewriting them.
- [#298](https://github.com/sp00nznet/xboxrecomp/pull/298): preserve both HRTF
  output channels after filtering a mono voice.

Both PRs are open. The local patches remain authoritative for this project
until an accepted upstream revision is selected and the pinned tree is
revalidated. The complete selected, filed, overlapping, and planned worklist
is in [UPSTREAM](docs/UPSTREAM.md).

Local patches reject out-of-range kernel ordinals when scanning before the
import table, lift `WBINVD` to a host memory fence, synchronize publication of
the graphics semaphore target and presentation frames, and make successful-read
tracing opt-in. They also account for guest memory and add the opt-in staged
pushbuffer walker, PGRAPH interrupt path, and experimental software window
clipping and face culling. The USB patch guards initialization,
MMIO protection, worker shutdown, and interrupt retirement. The thunk fix
avoids overwriting unrelated high-bit-set values immediately before this
game's table. Further local patches add per-flip frame statistics, a GPU
feature census and ceiling probe, an extended render back-end interface
(full pixel pipeline, readback before presentation, visibility counts), GPU
vertex-program hand-off, asynchronous visibility reports (experimental), and
cached environment lookups in per-draw code, texture metadata, GPU window-clip
state, GPU-owned depth-clear offload, and opt-in flip-only captures. The memory
fence models ordering without executing the privileged instruction on the
host; guest CPU caches themselves are not modeled. The
graphics-only manual hook discovers and validates the game's semaphore before
registering it with the executor. A changed target aborts rather than risking
completion of a pending release at the wrong address. Synthetic regression
tests cover these changes.

The larger stacked [#197](https://github.com/sp00nznet/xboxrecomp/pull/197)
and speculative graphics or performance changes are not applied without
title-specific evidence. The disc-to-build workflow, isolated run logs, and
local fingerprints are informed by the
[Blood Wake recompilation](https://github.com/minimalice92/bloodwake-recomp);
its game-specific patches and assets are not copied.

## Tests

```powershell
python -m pytest -q tests
```

The tests include a synthetic disc image and do not require game data. The
synthetic extraction test uses the installed toolkit's XISO reader. To exercise
the native host and all synthetic fixtures, including crash diagnostics, guest
memory accounting, pushbuffer control flow, and graphics interrupts:

```powershell
cmake -S . -B build\smoke -G "Visual Studio 17 2022" -A x64 `
  "-DBRUTEFORCE_GEN_DIR=$PWD\tests\fixtures\generated"
cmake --build build\smoke --config Release --parallel 4 --target `
  bruteforce bruteforce_fence_test bruteforce_read_trace_test `
  bruteforce_present_test bruteforce_usb_test bruteforce_interrupt_test `
  bruteforce_crash_test bruteforce_memory_stats_test `
  bruteforce_pushbuffer_test bruteforce_pgraph_test bruteforce_window_clip_test `
  bruteforce_color_unpack_test bruteforce_texture_math_test `
  bruteforce_frame_stats_test bruteforce_d3d11_test bruteforce_gpu_shared_present_test
$env:BRUTEFORCE_TEST_HOST = "$PWD\build\smoke\Release\bruteforce.exe"
$env:BRUTEFORCE_TEST_FENCE = "$PWD\build\smoke\Release\bruteforce_fence_test.exe"
$env:BRUTEFORCE_TEST_READ_TRACE = "$PWD\build\smoke\Release\bruteforce_read_trace_test.exe"
$env:BRUTEFORCE_TEST_PRESENT = "$PWD\build\smoke\Release\bruteforce_present_test.exe"
$env:BRUTEFORCE_TEST_USB = "$PWD\build\smoke\Release\bruteforce_usb_test.exe"
$env:BRUTEFORCE_TEST_INTERRUPT = "$PWD\build\smoke\Release\bruteforce_interrupt_test.exe"
$env:BRUTEFORCE_TEST_CRASH = "$PWD\build\smoke\Release\bruteforce_crash_test.exe"
$env:BRUTEFORCE_TEST_MEMORY_STATS = "$PWD\build\smoke\Release\bruteforce_memory_stats_test.exe"
$env:BRUTEFORCE_TEST_PUSHBUFFER = "$PWD\build\smoke\Release\bruteforce_pushbuffer_test.exe"
$env:BRUTEFORCE_TEST_PGRAPH = "$PWD\build\smoke\Release\bruteforce_pgraph_test.exe"
$env:BRUTEFORCE_TEST_WINDOW_CLIP = "$PWD\build\smoke\Release\bruteforce_window_clip_test.exe"
$env:BRUTEFORCE_TEST_COLOR_UNPACK = "$PWD\build\smoke\Release\bruteforce_color_unpack_test.exe"
$env:BRUTEFORCE_TEST_TEXTURE_MATH = "$PWD\build\smoke\Release\bruteforce_texture_math_test.exe"
$env:BRUTEFORCE_TEST_FRAME_STATS = "$PWD\build\smoke\Release\bruteforce_frame_stats_test.exe"
$env:BRUTEFORCE_TEST_D3D11 = "$PWD\build\smoke\Release\bruteforce_d3d11_test.exe"
$env:BRUTEFORCE_TEST_GPU_SHARED_PRESENT = "$PWD\build\smoke\Release\bruteforce_gpu_shared_present_test.exe"
python -m pytest -q tests
```

For hardware-device coverage as well as WARP, set
`BRUTEFORCE_TEST_D3D11_HARDWARE=1` before the pytest command.
The host fixture does not contain game code and cannot run the game.
Project-owned source is MIT-licensed. The linked executable includes xemu's
GPL-2.0-or-later DSP interpreter and is distributed, if at all, under
GPL-2.0-or-later as a combined work. Some DSP DMA and framework APU files use
LGPL-2.0-or-later or LGPL-2.1-or-later. See `LICENSE`,
`THIRD_PARTY_NOTICES.md`, `LICENSES/`, `src/xemu_dsp/NOTICE.md`, and the
toolkit's own `LICENSE` and `NOTICE` for terms and attribution.
