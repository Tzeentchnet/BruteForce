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
probe visibly rotated the camera. Other gameplay, physical controllers and
keyboards, audible audio, and overall rendering correctness remain unverified.
Some tutorial captures are dark and missing the HUD. The experimental path can
still stall or fail on untested behavior and is disabled by default. A
stationary tutorial comparison reported about 3.5 flips per second before
recent software optimizations and 4.0 afterward. These are not measurements of
distinct moving-world frames and are far below the playability target.

By default the graphics path uses xboxrecomp's CPU pushbuffer rasterizer and
Direct3D 11 framebuffer presentation. An experimental Direct3D 11 renderer
(`RECOMP_GPU=d3d11`) renders and runs vertex programs on the GPU. In the
tutorial's stationary view an earlier comparison measured about 30 rendered
frames per second against about 3 on the CPU path. A later three-run GPU
comparison reported movement pixel-change rates of 25.2 and 35.2 fps, but
58.9–60.2% of its movement flips matched a solid-black frame. Those rates do
not measure playable world fps. A later clear fix preserved the character in
two captured views in one diagnostic; sun-facing bloom remains unexplained.
An experimental packed-stencil and shared-presentation comparison produced
much darker stationary backgrounds but did not establish a performance gain
or original-hardware correctness.
One earlier boot stalled and needed a retry. The newest GPU-vertex tutorial
capture is close to the CPU reference but still has unexplained differences;
an older CPU-vertex GPU capture differs substantially. Rendering correctness
and the title's native frame rate remain unverified, so the GPU renderer is
not the default. Native 4K rendering, true widescreen, and unlocked frame
rates are not implemented.

The pipeline recognizes one North American retail `default.xbe` (title ID
`0x4D53001E`, SHA-256 recorded in `fingerprints/retail.json`). That fingerprint
identifies the executable used for this bring-up; it is not an independent
authentication of a disc image or its assets. Other revisions must be analyzed
and tested before they can be supported.

## Project documentation

- [Architecture and asset formats](docs/ARCHITECTURE.md), including the
  evidence and open questions for a future Blender importer.
- [Brute Force findings](docs/FINDINGS.md), including executable-specific
  addresses, diagnostics, and bring-up limits.
- [Upstream candidates](docs/UPSTREAM.md), including existing selected PRs and
  possible contributions that have not yet been filed.
- [Performance plan](docs/PERFPLAN.md), the phased move from the CPU
  rasterizer to a GPU-authoritative D3D11 renderer.

## Requirements

- 64-bit Windows 10 or 11, a Direct3D 11-capable GPU, and sufficient disk space
  for the extracted disc, generated C, and build.
- Python 3.10 or later, Git, CMake 3.20 or later, and Visual Studio 2022
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

For source-line crash diagnostics, configure an existing build with
`cmake -S . -B build -DBRUTEFORCE_LINK_SYMBOLS=ON` and rebuild. Crash reports
include guest register and stack candidates; `BRUTEFORCE_CRASH_STACK=1` also
dumps raw guest stack words, which may contain local game data.

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
`RECOMP_PAD_SCRIPT="20000:a:200,23000:a:200,26000:down:100,28000:a:200"`;
times are milliseconds from the first USB report and may need adjustment on
another machine or when audio is enabled. Leave the variable unset for manual
input.

Audio is separately opt-in:

```powershell
python tools\build.py run --experimental-tutorial --experimental-audio
```

This enables the toolkit's standalone APU and host audio output. Its DSP is
stubbed, and a local title/menu diagnostic produced non-silent PCM, not a
listening or tutorial-audio validation. Sound may be incorrect or absent, and
enabling it can change startup timing. To capture a bounded local WAV for
diagnosis, set `RECOMP_AUDIO_WAV` to a writable file path and
`RECOMP_AUDIO_WAV_SECS` to the desired maximum number of seconds before
launching. Captures contain game audio and should remain private.

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
vertex programs on the CPU. Common uncompressed and paletted textures are
decoded on the GPU; `RECOMP_GPU_TEX_DECODE=0` keeps their CPU decoder for
comparison. Compressed textures can be uploaded directly. Set
`RECOMP_GPU_TEXLOG=1` to log texture uploads locally.
P8 palette-length bits are not modeled: the D3D11 cache reads 256 entries,
so shorter palettes and their allocations remain unverified. Same-frame raw
texture rewrites and changes missed by sparse hashes can leave cached images
stale.

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
equivalence. Per-flip guest readback and synchronization are unchanged.

`RECOMP_GPU_STENCIL_ALIAS=1` separately enables experimental packed D24S8
sampling when a `0x41` texture exactly aliases a tracked GPU depth surface.
The GPU reconstructs the depth bytes and stencil alpha into a filterable
texture. Depth and stencil must each be initialized; unsupported overlaps
are rejected rather than read from stale guest memory. Ordinary `0x41`
textures are unchanged. Guest writes and partially overlapping aliases
remain unsupported, and the effect on sun-facing bloom is not established.

`RECOMP_GPU_DIRECT_PRESENT=1` lets the D3D11 renderer hand a finished
32-bit GPU surface to the D3D11 window presenter through a bounded shared
texture. Pinned framebuffers and PVIDEO overlays use the CPU-composited
frame instead. Guest readback still happens on every flip for guest-visible
memory, frame statistics and captures, so this does not remove that stall
or bound all GPU work in flight. Both options are off by default.

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
`--scene movement --capture-movement` also captures presented frames at two
scripted movement points; this diagnostic can perturb the measured frame rate.
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

The software rasterizer also has an independent, opt-in face-culling
experiment:

```powershell
$env:RECOMP_CULL_FACE = "1"
python tools\build.py run --experimental-tutorial
Remove-Item Env:\RECOMP_CULL_FACE
```

It uses the title's front-face and cull-face settings and corrects triangle
strip winding while culling is active. Culling remains inactive until both
face settings have been programmed; their hardware reset state is not yet
verified. This mode is off by default, is not validated against original
hardware, and can hide geometry incorrectly. It is not a GPU renderer or a
guarantee of improved performance. Registered hardware draw backends are
rejected while the option is enabled.

### Experimental programmable near plane

The software rasterizer can also test a screen-space zero near plane for
programmable vertex output:

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
depth-range clipping. It cannot be combined with
`RECOMP_CLIP_NEAR_Z=0` or a registered hardware draw backend. This is not a
hardware-rendering, 4K, widescreen, or frame-rate enhancement.

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
See `THIRD_PARTY_NOTICES.md` and the toolkit's own `LICENSE` and `NOTICE` for
licensing and attribution.
