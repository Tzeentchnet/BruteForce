# Brute Force user guide

Detailed launcher, runtime, diagnostic, and benchmarking instructions. For
the shortest supported build-and-run path, start with the
[project README](../README.md).

## Requirements and build workflow

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
& ".\Brute Force.cmd"
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
refuses to overwrite a different or modified checkout; preserve and move an
older checkout before rerunning setup for the current patch set. `extract`
verifies the executable before extraction and compares existing files byte-for-byte instead
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
`RECOMP_READ_TRACE=1` to enable their verbose logging and byte dumps. A
graphical `build.py` run selects D3D11 unless `RECOMP_GPU` explicitly selects
the CPU reference or null probe. Failed
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
[FINDINGS](FINDINGS.md#static-gamma-ramp-investigation).

For source-line crash diagnostics, configure an existing build with
`cmake -S . -B build -DBRUTEFORCE_LINK_SYMBOLS=ON` and rebuild. Crash reports
include guest register and stack candidates; `BRUTEFORCE_CRASH_STACK=1` also
dumps raw guest stack words, which may contain local game data.

## Launcher

After building, double-click `Brute Force.bat` (or `Brute Force.cmd`), or run
`python tools\launcher.py`. The launcher starts the same current tutorial
configuration as `run --experimental-tutorial`; it does not make
the game fully playable. It validates the build and game before each launch
and keeps each boot's output in `launcher.log` alongside `xbox_kernel.log`.
The launcher returns when the game closes and reports a nonzero exit.

The Display tab selects windowed or borderless fullscreen, initial window
size, point or linear presentation filtering, and presentation VSync.
Alt+Enter or F11 toggles fullscreen during play. Window sizing and display
filtering do not themselves increase internal resolution or add widescreen.
If the D3D11 presenter falls back to GDI, its filtering and VSync settings do
not apply.

For controller-only play, the independent Windows experiment
`RECOMP_DISPLAY_AWAKE=1` requests that the display stay awake only while the
visible top-level game host is foreground and not minimized. It releases the
request when backgrounded, minimized, closed or shut down. Unset or `0`
leaves this behavior off; other values log an error and leave it off. Windows
API failures are reported and retried on subsequent state updates. This is
not a launcher default and has only synthetic window-state coverage so far.

The Graphics tab selects the default D3D11 renderer or the CPU reference,
internal resolution, GPU or CPU vertex and texture processing, and the
optional persistent pixel shader cache. Internal-resolution choices are
**Native (match output)**, 2160p, 1440p, 1080p, 720p and the default
480p guest resolution. These are 4:3 picture heights: for example, 2160p is
a 2880x2160 rendered picture inside a 3840x2160 output. They do not add
widescreen; true Hor+ is not implemented. The Experimental tab exposes
window clipping, asynchronous visibility reports, programmable near-plane
clipping, depth and stencil aliases, and shared GPU presentation. The launcher
defaults to Direct3D 11 at 480p with experimental options off or using their
existing defaults; existing saved renderer choices are retained. Graphical
`build.py` runs also default to D3D11 unless `RECOMP_GPU` explicitly selects
another path. The individual GPU paths have the limitations below.

The guest-visible EEPROM video preference defaults to NTSC 4:3 480i/60.
Set either option to the exact value `1` before launch to advertise widescreen
or 480p to the title:

```powershell
$env:RECOMP_VIDEO_WIDESCREEN = "1"
$env:RECOMP_VIDEO_480P = "1"
python tools\build.py run --config Release --experimental-tutorial
Remove-Item Env:\RECOMP_VIDEO_WIDESCREEN
Remove-Item Env:\RECOMP_VIDEO_480P
```

Unset variables, `0`, and values other than `1` leave the corresponding mode
off. These flags change the preference returned to guest code; they do not by
themselves change the host window, internal render resolution, presentation
aspect, or title camera. Brute Force has not yet been validated as a complete
widescreen or progressive-scan path, so keep both opt-ins diagnostic.

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

## Tutorial and input path

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

The benchmark's validated Quick Campaign route presses A at 20, 23, 28, 32,
37, 42, 47, 52, 57 and 62 seconds and Down at 36 seconds, measured from the
first USB report. Its complete value is
`RECOMP_PAD_SCRIPT="20000:a:200,23000:a:200,28000:a:200,32000:a:200,36000:down:200,37000:a:200,42000:a:200,47000:a:200,52000:a:200,57000:a:200,62000:a:200"`.
The benchmark does not accept a high draw count as the tutorial world until
80 seconds after the first observed frame, because a campaign menu can also
reach the old draw threshold and the validated route must finish first.
Timings may still need adjustment on another machine or when audio is enabled.
Leave the variable unset for manual input.

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

### Audio capture diagnostics

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

## D3D11 renderer and benchmark diagnostics

```powershell
python tools\build.py run --experimental-tutorial
$env:RECOMP_GPU = "cpu"
python tools\build.py run --experimental-tutorial
Remove-Item Env:\RECOMP_GPU
```

The launcher and graphical `build.py` runs select `d3d11` by default.
`RECOMP_GPU=cpu` selects the CPU rasterizer, the correctness reference;
`RECOMP_GPU=null` is a measurement probe that draws nothing. The D3D11
renderer uses the D3DCompiler runtime
(`d3dcompiler_47.dll`, part of Windows 10 and 11). `RECOMP_GPU_VSH=0` keeps
vertex programs on the CPU. `RECOMP_GPU_VSH_VERIFY=<n>` compares every nth
GPU-program batch with the CPU interpreter; fully culled batches are logged
as skipped because they have no GPU vertex output. Missing or failed readback
and interpreter failures also report skips, capped at 16 messages.
`RECOMP_GPU_SHADER_DIAG=1` logs at most 32 vertex-program queue, compilation,
ready/failure or prolonged-pending events; unset or `0` leaves it off and
other values reject D3D11 initialization. It does not force synchronous
compilation or change submission ordering. Common uncompressed and
paletted textures are decoded on the GPU; `RECOMP_GPU_TEX_DECODE=0` keeps
their CPU decoder for comparison. Compressed textures can be uploaded
directly. Set `RECOMP_GPU_TEXLOG=1` to log texture uploads locally.
The D3D11 texture cache compares saved source and palette bytes on each
ordinary cache use; retained snapshots have a 128 MiB budget, excluding
transient copies and GPU resources. Checked guest copies reject inaccessible
or guarded pages, changed tracked allocation identities, known-short ranges,
and mapping resets; they do not consume the guard-page flag. One instrumented
reviewed campaign
measured exact comparison at 0.832 ms/flip over fully contained periodic
intervals; this is diagnostic overhead in one scene, not a general cost
bound. `RECOMP_GPU_TEX_CACHE_APPROX=1` restores the old approximate checks
for diagnostics, but can leave rewritten textures stale. Nonlinear
multi-level declarations always use exact snapshots, including when a short
chain falls back to level zero, even when that option is enabled.
The exact descriptor lookup uses its collision-safe index by default.
`RECOMP_GPU_TEX_INDEX=0` selects the former complete linear scan and disables
index maintenance for matched performance diagnostics; `1` explicitly
selects the index. Both modes retain the same exact descriptor, allocation,
source and palette checks. Other values reject D3D11 initialization. The
linear path is not a compatibility fallback or a launcher setting.

Guest mipmaps and anisotropy are separate experimental D3D11 switches; both
accept only `0` or `1` and default to `0`. They are environment options, not
launcher settings:

```powershell
$env:RECOMP_GPU_MIPS = "1"      # upload and sample supported guest mip chains
$env:RECOMP_GPU_ANISO = "1"     # optional 8x enhancement for eligible mipped samplers
python tools\build.py run --experimental-tutorial
Remove-Item Env:\RECOMP_GPU_MIPS, Env:\RECOMP_GPU_ANISO
```

`RECOMP_GPU_ANISO=1` alone is inactive and reports why. With mips off,
D3D11 and the unchanged CPU reference continue to sample level zero.
With mips on, supplied swizzled, P8 and BC 2D/cube chains are sampled using
the programmed min/mag/mip filters, LOD clamps and bias. This intentionally
can differ from the CPU reference. No missing levels are generated; linear
textures and render-target aliases remain at level zero. Depth/stencil
aliases reject multi-level declarations.
Unreadable or known-short chains may fall back to level zero if that is safe,
otherwise they are rejected. `[GPU-MIPS]` reports these fallbacks and
approximated or unknown filters; `RECOMP_GPU_TEXLOG=1` reports declared
`levels` and the number `uploaded`. LOD interpretation, title behavior,
matched xemu images, VRAM and performance remain unverified, so these
options are not recommended as defaults.

`BRUTEFORCE_PERF_COUNTERS=1` enables detailed title and D3D11 performance
diagnostics. Approximately every ten seconds, the normal D3D11 statistics
are followed by interval reports for:

- `guest_word` and `readable_guest_range` calls, accepted and rejected
  ranges, total validation time, `VirtualQuery` calls, failures, traversed
  regions and query time;
- texture-key lookups, linear probes, key hits and misses;
- exact source and palette spans submitted to comparison, exact hits and
  comparison time;
- snapshot copies and bytes, rewrites, entry and snapshot evictions,
  failed-upload discards and unsupported-cache hits.

The switch accepts only `0` or `1` and is off by default. It adds QPC reads
and atomic aggregation in the measured paths, so use it to identify work in
a controlled diagnostic run, keep it fixed within any comparison, and retain
an uninstrumented baseline. The standard renderer line separately labels
bytes hashed by the unsafe approximate texture-cache mode; it is not an
exact-comparison or snapshot-copy estimate. For compatibility with existing
report parsers, checked texture and colour-shadow copies contribute to the
`readable_guest_range` call/time counters without issuing `VirtualQuery`;
depth-seed checks can still query pages. Compare counters across versions
with that change in meaning in mind.

The framework CPU decoder and D3D11 P8 path use the programmed 32, 64, 128
or 256-entry palette; an out-of-range index fails deterministically instead
of reading beyond it. The CPU rasterizer retains its failed-sample fallback.
Actual hardware behavior for such indices remains unknown. Compatible exact
colour-surface aliases are reconciled with guest writes; a currently bound
target is sampled from an ordered GPU copy, while conflicting or ambiguous
GPU-dirty overlaps reject the draw. This is not a general guest-write
synchronization contract. A changed colour layout may continue rendering, but
its alias is rejected until a full clear or readback if old GPU-owned contents
or conflicting writes make ownership uncertain. Short-pitch GPU aliases are
also rejected rather than sampled from stale data.

Colour-clear mirrors and readback use checked guest-surface writes; failures
are reported with bounded diagnostics. A failed or partial mirror leaves the
GPU contents unresolved and blocks its alias. If write access is restored
before the flip, a successful full clear or readback can resolve the surface.
Failed or raced tracked readback stops the process before the flip is reported
complete; a retry then requires a new run. Existing guards are preserved by the
preflight read, but concurrent reprotection is not atomic and can still
consume a newly installed guard or leave partial writes.

`RECOMP_GPU_TEXTURE_BOUNDS=1` is an experimental strict diagnostic. It
requires explicit logical suballocation registrations from framework owners
for source and palette spans, including eligible colour aliases; unregistered
spans are rejected. Registered colour-surface identities are also tracked
across logical-range reuse. Unset or `0` retains compatibility with unknown
suballocations; other values reject D3D11 initialization. No title
registrations are assumed, so enabling it alone can reject ordinary game
textures. In either mode, byte snapshots do not make unsynchronized concurrent
writes transactional, and owners must unregister logical ranges before reuse.
Device loss in the renderer or separate
presenter is reported and stops execution rather than reporting GPU work as
complete. Synthetic fault injection covers both; actual loss races and
device recovery have not been validated.

`RECOMP_GPU_SCALE=native` renders the D3D11 path at the current 4:3 picture
inside the output window. A numeric value selects physical pixels per
640x480 guest pixel; the launcher presets are `1`, `1.5`, `2.25`, `3` and
`4.5`. Numeric values from 0.5 through 10 are accepted when their resulting
resources fit D3D11's 16384-pixel dimension limit.
`RECOMP_GPU_OUTPUT=WxH` can provide a fixed diagnostic output only with
`RECOMP_GPU_SCALE=native`; otherwise native mode follows window and
fullscreen changes through a scale epoch. Use decimal dimensions with a
lowercase `x`, such as `3840x2160`; the largest 4:3 picture *inside* those
dimensions must produce a scale from 0.5 through 10. Thus `3840x2160`
selects a 2880x2160 picture at 4.5x, and a portrait `1080x1920` selects
1080x810 at 1.6875x. The launcher removes an inherited
`RECOMP_GPU_OUTPUT` for its non-Native presets so that a fixed preset does
not conflict with a diagnostic Native setting.

If a native resize cannot allocate its resources, the `[GPU-SCALE]` or
`[GPU-D3D11]` diagnostics identify the failure; the last working scale and
resources remain active and a later frame can retry. A busy shared frame
also defers the change until it is safely released. A non-timeout shared
mutex ownership error or a removed/reset device stops execution with a
diagnostic; neither is automatically recovered. This does not silently lower the
requested scale. Resized colour targets retain their contents, but
changed-size depth/stencil targets are recreated with undefined contents
until the guest clears or regenerates them. Native resizing during retail
gameplay has not been validated for that requirement.

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
The complete synthetic D3D11 matrix passes 394 cases with WARP and RTX 5070
hardware enabled, including CPU-written frames with and without a GPU overlay
at scale 1 and scale 2.
Focused synthetic WARP and hardware fixtures also exercise native resize
failures, retry and visibility normalization; a hardware fixture resizes a
hidden presenter window. An earlier bounded retail 4.5x run captured visible
Microsoft, Digital Anvil, and intro-montage frames with zero guest/GPU
conflicts. No matched gameplay, performance, VRAM, native-4K-display or
original-hardware validation has been completed.

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
unexplained. The final-generation title comparison does establish the
intended warm-start effect: cold compile-and-create requests reached
30.623 ms, while warm lookup-and-create requests reached at most 0.535 ms.
Stationary and movement flip throughput did not show a corresponding
steady-state gain. The title captures were visually matched but not
byte-identical or frame-locked, so the cache remains off by default.

`RECOMP_GPU_ZPASS=async` enables experimental asynchronous GPU visibility
reports; synchronous reports remain the default. The asynchronous path
passes synthetic WARP and hardware fixtures but remains experimental in the
title.

`RECOMP_GPU_DEPTH_TEXTURE=1` enables experimental `LIN_D24S8` depth-texture
aliases on D3D11. It only accepts exact matches to GPU-created D24S8 surfaces
with matching linear layout, dimensions and pitch. New eligible surfaces
are seeded from checked guest memory, including row padding and allocation
identity. A changed clean owner is recreated and reseeded; a changed
GPU-dirty owner, guarded/short span or conflicting overlap is rejected.
A stable full-surface clear of both depth and stencil can rebaseline a live
owner; partial or single-plane clears cannot. Sampling a currently bound
depth target uses a GPU snapshot; PROJECT2D and PROJECT3D apply the shadow
comparison function on extended draws; base draws only support PROJECT2D.
Partially overlapping aliases, depth cubes, multiple mip levels and other
depth formats remain unsupported. Draws using rejected depth aliases or stage
modes are skipped with a diagnostic. The option is off by default and has not
established original-hardware equivalence. It cannot be combined with an
active render scale. With this option on but packed-stencil aliasing off,
a `0x41` texture over tracked depth is rejected rather than reading
potentially stale guest bytes; with both options off, legacy behavior remains.

`RECOMP_GPU_STENCIL_ALIAS=1` separately enables experimental packed D24S8
sampling when a `0x41` texture exactly aliases a tracked GPU depth surface.
The GPU reconstructs the depth bytes and stencil alpha into a filterable
texture. Depth and stencil must each be initialized; unsupported overlaps
are rejected rather than read from stale guest memory, including recognized
physical and tiled RAM aliases and unsupported formats over tracked depth.
Ordinary non-overlapping `0x41` textures are unchanged. Read-only draw
states preserve a packed conversion; potentially writing draws invalidate it
even when no fragment ultimately writes. The same conservative guest-span
checks apply; conflicting partial aliases remain unsupported. A synthetic
replay of the traced bright-pass state shows
how stale stencil bytes could exaggerate glow, but the effect on the reported
gameplay bloom and original-hardware fidelity remain unestablished. It cannot
be combined with an active render scale.

Neither alias switch establishes a synchronized guest-write/read contract.
Same-value writes, concurrent writes after a check, unregistered
suballocations and guest reads of GPU-owned depth can evade detection. The CPU
reference cannot decode `0x2E`, reads `0x41` from guest bytes, uses a separate
float depth buffer and does not model stencil. Audit format bindings alone do
not prove sampling or visible effects.

`RECOMP_GPU_DIRECT_PRESENT=1` lets the D3D11 renderer hand a finished
32-bit GPU surface, whose dimensions may differ from the guest framebuffer,
to the D3D11 window presenter through a bounded shared texture. Pinned
framebuffers and PVIDEO overlays use the CPU-composited frame instead. By
default tracked surfaces still read back on flips. CPU-written Bink frames
are first synchronized into the GPU surface and remain eligible for the
scaled shared route. The option is off at the default 480p resolution and
is enabled automatically for scaled modes.

`RECOMP_GPU_SKIP_CLEAN_READBACK=1` is a separate, default-off environment
experiment, not a launcher setting. For an unscaled, native-size 32-bit
surface only, it skips the copy when guest memory is already known to match
the clean GPU image; dirty surfaces, scaled surfaces and 16-bit surfaces
still copy. Initial seeds and guest CPU writes also use normal readback after
the upload; later unchanged frames may elide it. Clear and readback snapshots
are checked for matching bytes, including a recheck after the GPU wait. Setting
`RECOMP_FRAME_STATS`, `RECOMP_FB_DUMP`, `RECOMP_FB_DUMP_FLIPS` or
`RECOMP_PRESENTER_CAPTURE` forces normal readback. CPU presentation and
screenshots continue to use coherent guest pixels. This is not on-demand
guest-read interception or a proven title performance improvement: tracked
surface flips still wait for GPU completion, and arbitrary concurrent guest
write timing remains unverified. Set the option to `0` or unset it for the
reference behavior; other values are rejected.

The renderer also limits submitted GPU work to two event-query batches,
checkpointing at flips or after 128 renderer operations and waiting when
full. This is separate from the presenter's one-slot shared frame. It does
not skip PFB flushes, semaphore releases, visibility reports or flip holds.
Device removal and unrecoverable runtime presenter resource/ownership errors
are fatal and report the operation and result. Missing or exhausted required
draw states also stop rather than rendering with incorrect defaults; restart
rather than relying on automatic recovery. Initialization and non-device
shared-open failures retain their normal CPU/GDI fallbacks.

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

Every successful `--scene movement` collection is marked `inconclusive` in
`result.json` and `summary.json` and exits with status **2**, even if its data
were collected successfully. Failed collection exits **1**. Hash metadata
reports bounded unclassified black/unchanged intervals, not a cause or a
success-shaped world rate. Record these exit codes in automation rather than
treating 2 as a renderer crash.

`--capture-world` records movement flips for later review and names each
`holdNNNNNNNNNN.bmp` with the corresponding `flip` in `frames.csv`. These are
guest-surface snapshots, not proof of what the window displayed: overlays,
pinned surfaces and direct GPU presentation can differ. The timestamp is
recorded after the synchronous dump, and continuous capture affects timing.
Legacy numbered dumps do not carry flip IDs. Review the scene, HUD exclusions,
alignment and presented-image source using the manifest documented in
`tools\bench_world.py`; unlabeled changes are not world-frame FPS.
The fixed movement script moves the character only before measurement, presses
Y to skip the HUD tutorial-choice panel, then starts the measured window after
that choice has settled. The measured 40-second segment alternates camera
input without moving the character, because forward movement can enter a
letterboxed tutorial cutscene at a frame-rate-dependent time. This keeps panel
dismissal and scene content fixed rather than relying on adaptive A presses;
captured runs must still be reviewed to confirm the panel and cutscene are
absent.
Offline analysis uses a separately completed manifest:

```powershell
python tools\bench.py --analyze-world path\to\reviewed-labels.json
```

Labels `intentional_fade` and `intentional_solid` require explicit review;
solid images labelled as such must actually be solid. Output categories
`known_black`, `dark_unknown`, `solid_unknown`, and `unchanged_unknown`
remain unclassified.
The dark heuristic means every RGB channel of the whole image is at most 16,
not that the world or character is missing. The report lists up to 64
unclassified intervals plus the omitted count; its durations are observed
endpoint spans, not inferred time after the last flip.

When every frame is classified, the review metadata is complete and at least
one world change is verified, the offline analyzer reports reviewer-asserted
world changes per second plus
baseline-to-change and change-to-change frame-time percentiles and a 1% low.
Static-world and HUD-only flips remain inside those intervals. Fades, invalid
frames and unreviewed gaps break the cadence, and a trailing incomplete
interval is not counted as a completed world frame. This analyzer exits **0**
for `reviewer_asserted`, **2** for `inconclusive`, and **1** for invalid input.
Review assertions do not prove physical scanout or independently establish
which pixels reached the display.
Phase frame metrics include `frame_ms_max` as well as p50, p90, p99, and the
1% low. Structured runtime diagnostics summarize cache compile and hit counts,
total time, and maximum request time from `[GPU-PSCACHE]` events. Cold compile
timing excludes cache writes; warm hit timing includes lookup and pixel-shader
creation. Multi-second loading transitions can dominate a whole-run maximum,
so do not report that maximum as a shader hitch without event evidence.

Use `--profile-callers` for a complete G3 diagnostic. It enables the
profiler's more expensive stack walk and attributes kernel waits to their
first executable caller. The benchmark counts the two known guest GPU polls
and kernel waits attributed to those polls, the PFB event wait, and the
segment queue-space/lock wait helper as GPU/NV2A wait time; other game-thread
kernel waits are reported separately. Without this option,
`gpu_wait_pct` is a direct-poll lower bound rather than a complete G3 result.
Structured D3D11 interval diagnostics also retain synchronous visibility
reads and wait time, asynchronous reports written, and polls that found the
oldest report pending. Older logs without the two asynchronous fields remain
parseable and mark those values unavailable. A zero synchronous wait in
async mode means completion moved to report polling; it does not establish
zero report-processing cost or correct report order.

For an A1 request audit, pass `RECOMP_GPU_AUDIT=1` on the benchmark variant.
The harness owns the report path and writes `gpu_audit.csv` inside each
attempt directory, including retries. It records both the enable value and
resolved path in that attempt's metadata and fails the attempt when the
post-window report is missing or incomplete. Do not pass
`RECOMP_GPU_AUDIT_FILE` in a variant: one shared path could let attempts
overwrite each other and is rejected. Audit CSVs can contain guest addresses;
keep them local. `batch_geometry` and `post_clip_triangles` distinguish CPU
post-clip output from GPU pre-clip submissions; non-dropped visibility is
unknown. Dropped `batch_geometry` rows, `unsupported_state` and
`recent_method_for_drop` provide bounded temporal context, not evidence that
a method caused a visible defect.
`cpu_depth_binding` counts exact current-zeta addresses at request time, not
successful texture samples.

The default-off E1 camera diagnostic uses:

```powershell
$env:BRUTEFORCE_CAMERA_TRACE = "1"
$env:BRUTEFORCE_CAMERA_TRACE_LIMIT = "256" # 1..4096
$env:BRUTEFORCE_CAMERA_TRACE_EVERY = "1"   # sample every Nth call
.\build\Release\bruteforce.exe
```

It writes bounded `[CAMERA-TRACE]` JSON lines to standard error. Records
include caller and entry state, input/output projection matrices, and
explicitly labelled candidate owner, camera and viewport fields. `null`
means the value was unreadable or inapplicable; `hyp_*` and `*_candidate`
fields are not verified ownership. The trace does not change the matrix.
Keep the log local, and unset all three variables for normal runs. A build
containing a changed `src/recomp_manual.c` fingerprint must be regenerated
before a retail trace.

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
[FINDINGS](FINDINGS.md#presenter-source-capture-alignment).

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

## Experimental window clipping

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
needs investigation. Expanded synthetic CPU, WARP and hardware fixtures
cover rectangle unions/exclusions, AA, scale, vertex paths, clears and GPU
depth/stencil discard side effects; the CPU reference does not model stencil.
Other registered draw backends must advertise support
for window clips or the executor stops with an error. This option is not a
4K mode.

For local surface-read diagnostics, `BRUTEFORCE_SURFACE_GUARD_EXCLUDE_PAGE`
may specify one page-aligned guest address to leave unprotected when
`BRUTEFORCE_SURFACE_GUARD` is enabled with the null backend. The report
records the exclusion; reads and writes on that page are not observed.
Neither a clean report with an excluded page nor this diagnostic's other
coverage limitations justify skipping guest readback.
The current framework also needs guest-visible colour pixels for its CPU
flip snapshot, GDI/SDL or D3D11 CPU upload, pinned/PVIDEO fallback, frame
dumps, frame statistics, window dumps and presenter capture. Shared D3D11
presentation can consume the GPU texture directly, but the current flip still
publishes the CPU snapshot. The current-generation guard run resolved
non-NV2A access inside an accumulated colour range to generated title code,
but that range also overlaps staged pushbuffer data, ranges are not retired,
and one page remains excluded. Preserve the exact executable and adjacent PDB
when collecting a report: unresolved sites fall back to
`module+relative-address` for offline lookup. This diagnostic should not be
rerun unchanged; `RECOMP_GPU_SKIP_CLEAN_READBACK=1` remains a narrow
coherent-surface diagnostic rather than general on-demand readback.

## Texture-index performance comparison

The current Milestone 2 gate compares the exact linear reference with the
default descriptor index in one executable. It is six native recompiled-title
runs, not an xemu campaign. Every attempt requires an operator to skip the
cinematics and mission briefing, stop at the same Basic Controls popup, and
acknowledge the per-run token. The fixed script then dismisses the tutorial
panels before the measured camera-only interval. Retries are disabled:

```powershell
python tools\bench.py --performance --scene movement --readiness manual `
  --ready-state "Basic Controls popup fully visible at the initial tutorial spawn; no cinematic or briefing" `
  --no-auto-panels --capture-world --profile-callers --runs 3 --retries 0 `
  --variant "linear:BRUTEFORCE_PERF_COUNTERS=1,RECOMP_GPU_TEX_INDEX=0" `
  --variant "indexed:BRUTEFORCE_PERF_COUNTERS=1,RECOMP_GPU_TEX_INDEX=1"
```

The harness alternates linear/indexed attempts, requires one executable and
framework identity, and records the exact runtime options. Collection remains
inconclusive until every captured interval, input sequence and presentation
state is reviewed. Do not infer distinct-world cadence from flips alone.

## Index-word batching comparison

`RECOMP_PB_INDEX_RUNS=1` enables the separate, default-off pushbuffer
index-word candidate. Unset, empty or `0` retains scalar dispatch; any other
value stops with an explicit diagnostic. Eligible non-incrementing
subchannel-zero `ARRAY_ELEMENT16/32` runs are grouped only within the current
packet, PUT, segment and physical-wrap boundary. The first executor word, GPU
audit, other methods/subchannels and increasing packets stay scalar. Startup
prints `[PB-INDEX-RUN] non-incrementing index runs enabled`; audit prints the
scalar-fallback notice; periodic snapshots report calls, 16-bit words, 32-bit
words and buffered indices.

Synthetic scalar/run state equivalence passes, but no matched rendered-output
or performance result exists. When an operator is available, collect this as
its own six-attempt native campaign at the same Basic Controls state. Keep the
texture-descriptor candidate in exact linear mode in both variants so only
index-word batching changes:

```powershell
python tools\bench.py --performance --scene movement --readiness manual `
  --ready-state "Basic Controls popup fully visible at the initial tutorial spawn; no cinematic or briefing" `
  --no-auto-panels --capture-world --profile-callers --runs 3 --retries 0 `
  --variant "scalar:BRUTEFORCE_PERF_COUNTERS=1,RECOMP_GPU_TEX_INDEX=0,RECOMP_PB_INDEX_RUNS=0" `
  --variant "runs:BRUTEFORCE_PERF_COUNTERS=1,RECOMP_GPU_TEX_INDEX=0,RECOMP_PB_INDEX_RUNS=1"
```

The operator must skip cinematics and the mission briefing, verify every
ready-state token, and review captured intervals and matched CPU/D3D11 frames.
Do not run this simultaneously with the texture-index comparison or report
the retained census as a speedup.

## Optional compiler-profile comparisons

The supported default remains the ordinary Release build. Two experimental
profiles build separate executables without overwriting it:

```powershell
python tools\build.py build --config Release --parallel 4 --compiler-profile ltcg
python tools\build.py build --config Release --parallel 4 --compiler-profile avx2
```

`ltcg` enables `/GL` plus link-time code generation for title-executable
sources. `avx2` enables `/arch:AVX2` for those sources and therefore requires a
compatible host CPU. Profiles are Release-only, mutually exclusive, and kept
under `build\compiler-ltcg` and `build\compiler-avx2`; the normal executable
remains under `build\Release`. A successful build establishes neither
correctness nor speed.

When the higher-priority Phase 5 gates are complete and an operator can
confirm every attempt, run each compiler candidate as its own campaign:

```powershell
python tools\bench.py --performance --compiler-comparison ltcg `
  --scene movement --readiness manual `
  --ready-state "Basic Controls popup fully visible at the initial tutorial spawn; no cinematic or briefing" `
  --no-auto-panels --capture-world --profile-callers --runs 3 --retries 0

python tools\bench.py --performance --compiler-comparison avx2 `
  --scene movement --readiness manual `
  --ready-state "Basic Controls popup fully visible at the initial tutorial spawn; no cinematic or briefing" `
  --no-auto-panels --capture-world --profile-callers --runs 3 --retries 0
```

This mode owns both variants, alternates the default and selected profile,
enables performance counters in both, and keeps every runtime performance
switch off. Collection rejects changed generation/toolkit or per-profile
binary identity. Offline performance review permits the two executable hashes
to differ only because the recorded compiler profiles differ, and applies G4
and G5 to the candidate binary. Skip cinematics and the mission briefing;
never combine LTCG and AVX2 or infer performance from executable size.

## Experimental NV2A wait and segment options

The current default-off Phase 4 implementation can replace the title's busy
poll with a wait on the real framework acknowledgement:

```powershell
$env:RECOMP_PFB_FLUSH_WAIT = "1"
python tools\build.py run --config Release --experimental-tutorial
Remove-Item Env:\RECOMP_PFB_FLUSH_WAIT
```

Unset or `0` retains the generated polling routine; any other value stops with
a diagnostic. This option does not clear the PFB bit itself and does not bypass
flip holds. Its event is signalled only after the framework's real
acknowledgement clears the bit, while a short timer provides IRQ safe points
and mandatory rechecks. It is default-off. The bounded revision reached
reviewed movement once in a no-retry all-three bring-up, but it has not passed
the reviewed alternating performance comparison, G2, G3, or CPU-reference
equivalence and may change guest interrupt timing. The 24-run Phase 4
comparison is deferred. Do not use this as a general performance preset or
start that campaign without a new project decision.
The wait now stops with an error after 30 seconds if the real busy bit remains
set; it does not clear the bit on timeout.

To investigate a long event-driven wait, set `RECOMP_NV2A_WAIT_DIAG=1` as
well. It logs the still-busy register, elapsed time, last observed flip hold
and counters after two seconds, at most eight times. It never clears the bit,
acknowledges a flush or bypasses a hold; the diagnostic is off unless set to
the exact value `1` and does not apply to the original polling path.

Two other switches are independent and default off:

- `RECOMP_NV2A_IDLE_WAIT=1` lets the worker spin briefly after work, then
  wait on a retained wake event with a 200 µs timer backstop. All normal
  service work still runs each pass; a stalled timer or failed wait stops
  with an error.
- `RECOMP_NV2A_SEGMENTS=1` requires Windows, `RECOMP_PB_EXEC` present,
  `RECOMP_PB_WALK=staged`, `RECOMP_PGRAPH_IRQ=1`, and a fresh generation
  with the current store hooks. It walks each captured PUT separately,
  advances GET only after the walk, and defers the next segment during a
  held flip. The USER block is read-only to guest code and writable through
  a separate host alias. Any untracked guest write, GET reset, invalid PUT,
  unavailable mapping, queue-lock timeout or full-queue timeout stops the
  process instead of silently losing commands. Such a failure requires
  investigation; do not disable the guard to obtain a result.

The queue has 128 entries and a five-second producer/lock limit. It does not
bypass guest fence or GET waits. A flush is normally not acknowledged while
an older queued segment or flip hold remains. If the executor is waiting for
a guest PGRAPH software-trap acknowledgement, PFB may acknowledge so the
guest can leave its poll and service the interrupt; GET, the queued segment,
the flip hold and PGRAPH interrupt state remain unchanged until normal
completion. `BRUTEFORCE_PERF_COUNTERS=1` records interval PFB and PUT calls
and elapsed wall time by verified guest call site and return address; unknown
callers are zero, and table overflow is reported.
The same flag records interval pixel-shader compile/create time and failures
even with the persistent shader cache disabled; cache-hit timings remain in
the existing `GPU-PSCACHE` diagnostics when that cache is enabled.
The framework emits cumulative idle and queue snapshots. These counters and
`--profile-callers` add overhead and must be equal on both sides. Synthetic
fixtures cover the mechanisms. One no-retry segment-only and one no-retry
all-three smoke reached reviewed tutorial movement, but they do not establish
matched presentation or performance.

The following four-pair, 24-run workflow is retained for future use but is
explicitly deferred and should not be scheduled as current work. If it is
resumed after a new project decision, collect a separate baseline/variant pair
for each experiment (example: PFB only). The Release build must include debug
symbols for the complete caller-attribution gate; see the
[symbolized-build instructions](#requirements-and-build-workflow).

```powershell
python tools\bench.py --performance --scene movement --readiness manual `
  --ready-state "precise visible tutorial state" --no-auto-panels `
  --capture-world --profile-callers --runs 3 `
  --variant "baseline:BRUTEFORCE_PERF_COUNTERS=1" `
  --variant "pfb:BRUTEFORCE_PERF_COUNTERS=1,RECOMP_PFB_FLUSH_WAIT=1"
```

Repeat with idle only, segments only, and all three switches. `--performance`
requires two variants, at least three runs each, manual readiness, fixed
input, continuous reviewed-capture material and caller profiling; it rejects
variant overrides of harness-owned inputs, thread counts, output paths and
profiling controls. It records the effective options, executable hash,
generation and framework tree. Collection itself remains inconclusive.
Continuous guest-surface capture does not prove presented pixels and changes
timing. Every run needs independent scene, alignment and presentation review.
Enabled `BRUTEFORCE_PERF_COUNTERS=1` and non-overflowed PFB/PUT interval
records are required in each run, so diagnostic collection is comparable.
PFB/PUT and shader records carry QPC-based report-window timestamps on the
same clock as the flip log; calls are counted when they complete, so a call
can begin before its recorded window. The first report has unknown start
(`0`). A reviewed movement window must contain a complete later report
interval with both PFB and PUT caller records, or comparison is inconclusive.
Unknown/zero callers in those intervals and any caller-table overflow also
make it inconclusive.

`python tools\bench.py --compare-performance MATCHES.json` is an offline
review gate. `MATCHES.json` contains `reviewer`, `state`,
`input_tolerance_ms` (0–500), and at least three consecutive A/B `pairs`.
Each pair names `baseline_result`, `candidate_result`, `baseline_labels`
and `candidate_labels`, sets `matched` to true, and supplies nonempty
`start_match`, `end_match` and `interval_match` descriptions. Paths are
relative to the manifest. Label manifests follow `tools\bench_world.py`;
each interval needs at least 100 reviewed world changes over 10 seconds.
The output includes each cadence's p50/p90/p99 and 1% low, not flip-based
world FPS. It refuses differing generations, hashes, toolkit trees, thread
counts, input, or nonexperimental options.

Optional gate evidence in the same manifest is deliberately explicit:

- `native`: `labels`, `matched: true`, and `evidence` for a reviewed native
  movement interval. The review must use the same image dimensions, region,
  HUD exclusions and tolerance as the candidate. G2 requires each candidate
  run to reach its distinct world rate, with 1% low at least 80% of that
  rate.
- `g4`: `reviewer`, `matched: true`, `matching_evidence`, and `pairs` with
  `cpu_result`, `gpu_result`, `cpu_image` and `gpu_image`. Images must be
  recorded stationary captures from the same generation, thread count and
  options except the renderer, with the candidate switches enabled. List
  differences in `difference_explanation` and explicitly set
  `all_differences_explained` if pixels differ.
- `g5`: `reviewer`, `matched: true`, `matching_evidence`,
  `native_hold_labels`, and `scaled_results` naming at least three distinct
  2× stationary capture results. `scaled_review_evidence` maps each exact
  result path to its separate nonempty stationary/presentation review.
  Scaled runs need completed holds of at least 100 flips; native hold labels
  must review at least 100 consecutive presented flips over 10 seconds. G5
  compares those stationary **flip** rates, not world FPS.

G3 requires a complete symbolized game-thread profile for every candidate
run and less than 10% GPU wait. Attributed queue-space/lock waits count;
ordinary PUT-store work does not. Missing, unmatched, unreviewed or insufficient
evidence makes the relevant gate and overall result inconclusive. Failed gates
are reported separately; the offline command exits 0 only when all gates pass,
otherwise 2. Human match and presentation statements remain assertions.

## Experimental face culling

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

## Experimental programmable near plane

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

## Isolated controller diagnostic

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
