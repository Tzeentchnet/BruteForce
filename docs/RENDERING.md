# Brute Force rendering architecture

This is a reference for the rendering path in this recompilation, not a
specification of the original Brute Force engine or Xbox hardware. It combines
observations from the supported North American retail executable with the
current host implementation. Guest addresses and layouts are revision
specific; the executable fingerprint is in
[retail.json](../fingerprints/retail.json).

For the disc and asset investigation, see [ARCHITECTURE.md](./ARCHITECTURE.md).
[FINDINGS.md](./FINDINGS.md) records observations, experiments and their
evidence; [PERFPLAN.md](./PERFPLAN.md) defines performance and correctness
gates. The framework version and ordered patches are pinned by
[toolkit.py](../tools/toolkit.py).

## Overview

The recompiled title still submits Xbox NV2A graphics commands. The
xboxrecomp runtime walks and executes those commands, using either its
software rasterizer or a registered rendering backend. The experimental
Direct3D 11 backend translates the relevant state and draws into host GPU
resources; it does not replace the game engine with a native PC renderer.

```text
  Statically recompiled guest game
    | Xbox graphics state, pushbuffer commands, textures and surfaces
    v
  xboxrecomp PFIFO/PGRAPH and NV2A executor
    |-- CPU reference rasterizer -------> guest colour memory --------+
    |-- D3D11 backend --> GPU colour --flip readback--> guest memory -+--> window
    |                    GPU depth/stencil                           |    presenter
    |                       |-- optional shared colour snapshot -----+
    `-- null probe: no rasterization; visibility reported as visible

  Guest CPU <---- semaphore, interrupt, visibility report and flip coordination
```

The normal framebuffer handoff produces a CPU-side snapshot, optionally
composes video, and sends it to the window presenter. The shared GPU path is an optional scale-1 presentation optimization and the
required high-resolution handoff for scaled D3D11 modes; it is not a
replacement for guest readback.
The CPU renderer is a useful comparison path, not proof of original-hardware
correctness. The null backend is a measurement probe and does not render a
correct image.

## What has been observed in the title

A CPU-path tutorial census covering 3,219 flips observed vertex-program draws,
stencil state, render-to-texture and mainly DXT textures. Colour surfaces of
640x480, 320x240 and 160x120 and a 512x512 depth pass were observed in that
run; these dimensions are not an inventory of every scene or asset.

One traced tutorial frame submitted scene batches to a back buffer, a
512x512 depth-only pass, more scene batches, 320x240 and 160x120 downsample
passes, then glow composite and HUD batches before the flip. A separate trace
and synthetic replay investigated a bright pass that samples a packed
depth/stencil alias; its effect on the reported gameplay bloom is not
established. These are observations of particular runs, not an asserted
render-graph specification for the original game. See the
[feature census and frame trace](./FINDINGS.md#performance-plan-phase-0-and-the-first-gpu-path-results)
and [packed-alias investigation](./FINDINGS.md#packed-depthstencil-aliases-and-shared-presentation).

## Code and ownership map

| Area | Source of truth |
| --- | --- |
| Renderer selection and tutorial startup | [main.c](../src/main.c), [build.py](../tools/build.py), [launcher.py](../tools/launcher.py) |
| Staged pushbuffer walking and PGRAPH handling | [patch 0008](../patches/xboxrecomp/0008-experimental-pgraph-and-staged-walker.patch), [recomp_manual.c](../src/recomp_manual.c) |
| Extended backend interface and vertex-program batches | [patch 0016](../patches/xboxrecomp/0016-extended-render-backend.patch), [patch 0017](../patches/xboxrecomp/0017-gpu-vertex-programs.patch) |
| D3D11 device, surfaces, state, draws, queries and readback | [d3d11_backend.c](../src/gpu/d3d11_backend.c) |
| Render-scale authority and GPU seed/readback/final resolves | [render_scale.c](../src/gpu/render_scale.c), [render_scale.h](../src/gpu/render_scale.h) |
| Texture layout and decode | [texture_decode.c](../src/gpu/texture_decode.c), [d3d11_backend.c](../src/gpu/d3d11_backend.c) |
| Generated vertex and combiner shaders | [vsh_hlsl.c](../src/gpu/vsh_hlsl.c), [rc_hlsl.c](../src/gpu/rc_hlsl.c), [shader_cache.c](../src/gpu/shader_cache.c) |
| Null renderer and surface-read probe | [null_backend.c](../src/gpu/null_backend.c) |
| Frame handoff and optional shared presentation | [patch 0005](../patches/xboxrecomp/0005-synchronize-presented-frames.patch), [patch 0024](../patches/xboxrecomp/0024-gpu-shared-presentation.patch), [patch 0028](../patches/xboxrecomp/0028-scaled-gpu-presentation.patch) |
| Measurements and capture | [bench.py](../tools/bench.py), [patch 0014](../patches/xboxrecomp/0014-frame-statistics.patch), [patch 0026](../patches/xboxrecomp/0026-flip-id-frame-captures.patch), [patch 0027](../patches/xboxrecomp/0027-presenter-source-captures.patch) |

The setup-created `.deps\xboxrecomp` checkout contains the patched
`nv2a_pb_exec.c`, `nv2a_backend.h`, `nv2a_backend_ext.h` and framebuffer
presenter. Those files are useful when tracing execution locally, but the
tracked patches and the toolkit pin are the reproducible references.

## Command execution and backend contract

The experimental tutorial configuration enables the staged pushbuffer
walker, PGRAPH interrupts, vblank, USB, the pushbuffer executor and a
framebuffer window. The walker handles the submitted method stream and its
JUMP and CALL/RETURN control flow; the executor maintains NV2A state and
dispatches clear, draw, report and flip operations.

The base backend interface supplies clears, screen-space draws and flips.
The extended interface supplies transformed batches, four texture stages
and combiner state, optional untransformed GPU vertex-program batches,
readback, and visibility reporting. The D3D11 backend registers both. Calls
to the rendering backend and its single-threaded D3D11 immediate context
are serialized on the NV2A thread. A separate worker compiles vertex-shader
bytecode; it does not own the rendering context.

The guest graphics hooks in [recomp_manual.c](../src/recomp_manual.c)
validate and register the title's semaphore target and, when enabled, its
graphics interrupt event. PGRAPH software-method traps and the staged walker
have their own ordering rules. A changed semaphore target stops execution
because pending releases cannot safely be assigned to a new target.

**Important boundary:** the executor writes semaphore releases to guest
memory after processing the corresponding command, but that is not by
itself proof that all prior asynchronous D3D11 work has completed.
Per-flip readback and synchronous visibility queries can wait for GPU work;
they do not establish a general completion guarantee at every guest fence,
trap or surface read. Correct GPU completion ordering and loss races around
guest-visible releases remain open. Do not equate command submission,
`Present`, a fence release and finished GPU work.

## D3D11 rendering systems

### Surfaces and clears

The backend tracks colour and depth surfaces by guest address. A new colour
surface can be initialized from guest memory when its layout permits it;
the GPU copy then holds rendering results. A tracked colour surface can be
sampled directly as a texture when its address matches and it is not the
current render target. Partial overlaps are counted but do not imply a
supported render-to-texture alias. The backend unbinds texture views when
binding a surface for drawing.

Depth and stencil use D3D11 depth resources. With the optional depth-view
paths enabled, an eligible new linear Z24S8 surface can be seeded from guest
memory, converting the packed byte order. A recreated GPU depth resource is
not reseeded from potentially stale guest bytes. A full GPU clear can
initialize the relevant planes; a partial clear cannot establish that an
entire uninitialized plane is safe to sample.

Full-surface clears use D3D11 clear operations where possible. Rectangular or
channel-masked clears use a scissored draw to preserve other pixels and
channels; those internal draws are excluded from visibility counts. When a
backend owns both draw and clear, the executor skips its redundant software
depth clear. See the
[clear investigation](./FINDINGS.md#rectangular-clears-and-software-depth-ownership).

### Internal resolution and physical resources

[render_scale.c](../src/gpu/render_scale.c) is the single render-scale
authority adapted from Blood Wake's generic resolution system. `S` is
physical pixels per 640x480 guest-screen pixel:

- `RECOMP_GPU_SCALE=native` uses the height of the current 4:3 output picture
  divided by 480 and changes its epoch when the window or fullscreen picture
  changes.
- Numeric values select a fixed scale. Launcher presets are 480p = `1`,
  720p = `1.5`, 1080p = `2.25`, 1440p = `3`, and 2160p = `4.5`.
- `RECOMP_GPU_OUTPUT=WxH` supplies a diagnostic output bound only in native
  mode. Its 4:3 picture must produce a scale from 0.5 through 10. Scale 1
  remains the default.

Each tracked colour surface keeps logical `w/h` and physical `pw/ph`.
Physical colour and depth resources follow the selected scale. Guest
anti-aliasing factors are folded out of scaled resource dimensions, following
Blood Wake's model. A scale epoch or AA-layout change recreates resources;
because colour is GPU-authoritative, a resized colour resource is migrated
from the old GPU view instead of being reseeded from stale guest memory.
Render-target aliases sample the physical resource while texture coordinates
remain in guest-logical units.

Guest vertices and shader constants remain logical. The D3D11 viewport uses
physical dimensions, so rasterization gains resolution without changing
guest geometry. Surface scissors, partial clears and inclusive window-clip
rectangles are rounded into physical pixels explicitly. Occlusion-query
samples are divided by each query's physical-to-logical area ratio before
they become guest visibility reports. Point size, line width and title-wide
visual equivalence at scaled resolutions still need gameplay validation.

GPU full-screen passes seed a new physical target with bilinear scaling,
box-average a completed target back to guest size for readback, and choose an
exact copy, box downsample or Catmull-Rom upscale for the shared output
picture. Invalid values, dimensions beyond D3D11's 16384-pixel limit and
resource-creation failures are reported; the renderer does not silently lower
the selected resolution.

### Textures and caching

[texture_decode.c](../src/gpu/texture_decode.c) and the backend handle
linear, swizzled, paletted and compressed layouts within their supported
formats. Eligible DXT blocks are uploaded as BC textures, eligible linear
32-bit colour data can be uploaded directly, and common formats use compute
decoding. A CPU decode path remains available as a fallback and for
comparison. The renderer samples level 0; cube-face spacing accounts for
the guest's mip layout, but this is not general mip-level sampling.

Render-target and enabled depth/stencil aliases are checked before the
ordinary texture cache. For ordinary successful textures, the default cache
compares retained source bytes and, for P8, palette bytes on each use.
Retained snapshots have a 128 MiB budget; transient copies, decoded buffers
and GPU resources are outside that budget. Evicted snapshots require another
upload. The approximate cache is opt-in and can miss same-frame or sparse
rewrites, leaving stale textures.

P8 binds check page readability and remaining size of known allocations for
the source and programmed 32-, 64-, 128- or 256-entry palette; sampled
indices are validated on upload. Invalid ranges or indices stop with a
diagnostic rather than guessing hardware behavior. Exact render-target
aliases bypass the ordinary path. Page queries and snapshots cannot prevent
concurrent guest writes or protection changes from racing subsequent reads;
untracked suballocation boundaries are also not established.

Deterministically unsupported non-P8 CPU decodes, when no GPU decode was
attempted, receive bounded negative cache entries. Retryable upload failures
release their slots. In particular, avoiding repeated unsupported `0x2E`
decodes does **not** implement depth-texture sampling. The user-reported
default-D3D11 movement regression remains unresolved; see the
[cache regression evidence](./FINDINGS.md#default-d3d11-movement-regression-2026-10-30).

### Optional depth and stencil aliases

- `RECOMP_GPU_DEPTH_TEXTURE=1` enables experimental `0x2E` linear D24S8
  depth sampling. It requires a tracked, initialized surface with an exact
  address, dimensions, pitch and supported linear layout. Sampling a
  currently bound depth target uses a GPU snapshot. PROJECT2D comparisons
  are supported on base and extended draws; PROJECT3D is limited to extended
  draws. Unsupported depth aliases or stage modes cause the draw to be
  skipped with a diagnostic.
- `RECOMP_GPU_STENCIL_ALIAS=1` enables an independent packed D24S8 path when
  a `0x41` texture overlaps a tracked depth surface. Supported exact aliases
  require initialized depth and stencil planes. A compute shader rebuilds
  filterable depth bytes and stencil alpha, reusing the result until the
  GPU surface changes. Unsupported overlaps are rejected; ordinary
  non-alias `0x41` textures keep their normal path.

Both options are off by default. Guest writes to GPU-owned depth, partial
aliases, depth cubes and multi-level depth aliases are not supported. These
implementations have synthetic coverage but have not established
original-hardware or complete gameplay equivalence. Scaled modes reject both
alias options explicitly because their guest-layout seed, snapshot and packed
sampling semantics have not been defined for physical-resolution depth.

### Vertex processing and pixel combiners

The executor can transform vertices on the CPU or send eligible
vertex-program batches to D3D11. [vsh_hlsl.c](../src/gpu/vsh_hlsl.c)
translates NV2A programs to HLSL using the framework interpreter as its
semantic reference. Compatible attribute streams can be submitted as guest
bytes; otherwise the executor gathers float inputs. Programs that write
constants remain on the CPU. New GPU programs compile on a worker; batches
fall back to CPU transformation until their shader is ready. Optional
`RECOMP_GPU_VSH_VERIFY=<n>` reads back every nth GPU-program batch for
comparison with the interpreter and is a slow diagnostic.

[rc_hlsl.c](../src/gpu/rc_hlsl.c) generates a pixel shader from the texture
stages and register-combiner configuration, including up to eight general
combiner stages and the final combiner. It follows the current CPU
implementation, including known gaps: bump-environment and dot-product
stage modes read zero, and only texture level 0 is sampled. Colours and fog
are interpolated affinely while texture coordinates are perspective-correct.
The backend caches shader and D3D11 state objects.

The optional persistent **combiner pixel-shader** cache is separate from the
texture cache. `RECOMP_GPU_PSCACHE=1` stores validated bytecode under
`%LOCALAPPDATA%\BruteForce\shader-cache` by default, keyed by generated
source, compiler binary and compilation inputs. It is off by default; other
shader types are not covered. Cache errors are logged and compiled shaders
can still run. See the [launcher and cache findings](./FINDINGS.md#launcher-and-pixel-shader-cache).

### Raster state, clipping and known differences

The backend maps blend, depth/stencil, sampler and rasterizer state to D3D11.
It honors stencil state, unlike the CPU rasterizer, and uses D24 depth rather
than the CPU path's float depth buffer. Triangle-edge rules also differ.
Neither renderer establishes original Xbox output on its own.

Window clipping (`RECOMP_WINDOW_CLIP=1`), face culling
(`RECOMP_CULL_FACE=1`) and the programmable screen-zero near plane
(`RECOMP_PROGRAM_NEAR_ZERO=1`) are independent experiments. The first two
wait for the required guest state before applying their rules. The launcher
defaults to `RECOMP_PROGRAM_NEAR_ZERO=hud`: ordinary and alpha-blended
programmable draws use screen zero, while zero-depth additive draws use a
small positive near plane. This keeps the synthetic HUD visible without its
synthetic additive glow and leaves positive-depth additive geometry intact.
Legacy `c[59].z` and full screen-zero behavior remain selectable. HUD-safe
and screen-zero modes cannot be combined with `RECOMP_CLIP_NEAR_Z=0`. The
extended D3D11 path supports these modes; other registered draw backends must
provide the relevant extended interface or capability. Gameplay and
original-hardware validation remain incomplete.

### Visibility reports

The title uses visibility counts to decide what to draw. D3D11 maps these to
occlusion queries; the default synchronous report waits for completed
queries at `GET_REPORT`. `RECOMP_GPU_ZPASS=async` instead queues reports,
uses GPU events and polls on the NV2A thread, writing results to guest memory
in command order when ready. Its bounded queue can still wait when full.
Async reporting changes timing and is experimental despite synthetic WARP
and hardware coverage. The null probe reports geometry visible so it does not
accidentally measure a scene culled by missing results. Scaled D3D11 queries
are normalized back to guest-pixel counts in both synchronous and
asynchronous paths.

## Flip, readback and presentation

At `FLIP_STALL`, the executor identifies the completed colour surface from
the last drawn buffer, which may differ from the next buffer in current
state. At scale 1, the D3D11 readback callback copies that surface to a
staging resource. At another scale, it first resolves the physical surface
into a guest-sized GPU target. It then maps the staging resource and copies
guest-sized rows to memory before the normal framebuffer handoff and flip
statistics. If readback is unavailable or fails, guest memory may not contain
the rendered GPU frame; it must not be treated as a successful readback.

The default window path snapshots and converts guest pixels and can compose
PVIDEO before presentation. The Windows presenter normally uses a separate
D3D11 swap chain and can fall back to GDI. With
`RECOMP_GPU_DIRECT_PRESENT=1`, an eligible tightly pitched 32-bit surface
can also be resolved into a keyed-mutex shared texture for the D3D11
presenter. Patch 0028 lets that texture have dimensions independent of the
guest framebuffer and exposes the current 4:3 output-picture size to the
renderer. Pinned framebuffers and PVIDEO use the CPU-composited path, and
snapshot IDs prevent an older pending GPU image from replacing a newer frame.
The shared path still performs per-flip guest readback; its one-slot snapshot
bound is not a bound on all GPU work in flight.

Window size and fullscreen determine native mode's output picture. The
point/linear display filter applies to the CPU route and scale-1 fallback;
the scaled GPU final pass owns its reconstruction. None of these controls
adds widescreen or unlocks the title's frame rate. Presentation VSync and
the optional guest flip-pacing mechanism are distinct. See
[README.md](../README.md#graphics-launcher) for current controls and
[PERFPLAN.md](./PERFPLAN.md) for unimplemented on-demand readback and pacing
work.

## Configuration and defaults

The following describes a fresh launcher configuration with no inherited
overrides to unmanaged settings. Saved launcher choices can differ.
[launcher.py](../tools/launcher.py) is authoritative for the current
setting-to-environment mapping.

| Setting | Fresh launcher default | Notes |
| --- | --- | --- |
| Renderer | D3D11 hardware | Experimental; direct command-line runs default to CPU when `RECOMP_GPU` is unset. `RECOMP_GPU=null` is a probe, not a renderer. `RECOMP_D3D11_WARP=1` selects the D3D11 software device. |
| Internal resolution | 480p guest (`RECOMP_GPU_SCALE=1`) | Native, 2160p, 1440p, 1080p and 720p are available for D3D11. The CPU renderer remains at guest resolution. |
| Vertex programs / texture decoding | GPU / GPU where supported | CPU fallbacks remain; `RECOMP_GPU_VSH=0` and `RECOMP_GPU_TEX_DECODE=0` are comparison switches. |
| Visibility | Synchronous | `RECOMP_GPU_ZPASS=async` is opt-in. |
| Persistent pixel-shader cache | Off | `RECOMP_GPU_PSCACHE=1` enables the combiner cache. |
| Window clip / near plane | Off / HUD-safe | HUD-safe retains non-additive UI and clips zero-depth additive draws. Legacy/inherit and full screen-zero remain comparison modes. Face culling is off unless separately enabled. |
| Depth alias / stencil alias / shared presentation | Off / off / off at 480p | Scaled modes reject the aliases and enable shared presentation automatically. |
| Window / display scaling / VSync | Windowed / 2x linear / on | Display size is separate from a fixed internal-resolution preset; native mode follows its 4:3 picture. |

The launcher starts the tutorial configuration. To compare the renderers
from the command line after building, set `RECOMP_GPU` to `cpu` or `d3d11`
and run `python tools\build.py run --experimental-tutorial`; keep all other
settings and the scene controlled. Full setup and options are in the
[README](../README.md#experimental-gpu-renderer-and-benchmark).

## Diagnostics and validation

- The periodic `[GPU-D3D11]` report includes draws, texture paths and
  unsupported-cache hits, readback time, visibility waits, alias activity
  and shared-presentation counts. `RECOMP_GPU_TEXLOG=1` logs individual
  texture uploads.
- `RECOMP_FRAME_STATS` records flip and whole-frame change statistics;
  `RECOMP_GPU_CENSUS` records requested features; `BRUTEFORCE_PROFILE`
  collects samples. [bench.py](../tools/bench.py) can coordinate these with
  scripted tutorial input. Captures and instrumentation can change timing.
- Use numbered flip-time captures for image comparisons. Report-time dumps
  can be mid-frame, before HUD and composite. Flips can repeat, and
  whole-frame pixel changes can come from the HUD or fades; neither measure
  automatically establishes distinct visible-world FPS.
- `RECOMP_PRESENTER_CAPTURE` can record and validate Windows GDI or
  CPU-upload D3D11 presenter inputs. It rejects the shared-GPU route and
  does not prove physical scanout or an unobscured window. A guest flip
  capture and a presented frame are different evidence.
- [test_d3d11.py](../tests/test_d3d11.py) covers synthetic renderer parity,
  texture rewrites and failures, P8, clear rectangles, aliases, shaders,
  visibility, scale-two readback, render-to-texture and physical clear/clip
  mapping. [test_present.py](../tests/test_present.py) and
  [test_frame_stats.py](../tests/test_frame_stats.py) cover handoff,
  presentation and frame accounting. Fence and pushbuffer fixtures cover
  parts of the guest synchronization contract.

For example, after building and setting the native fixture paths as described
in [README tests](../README.md#tests):

```powershell
python -m pytest -q tests\test_d3d11.py tests\test_present.py tests\test_frame_stats.py
```

Without the corresponding `BRUTEFORCE_TEST_*` fixture variables, native
tests are skipped. Set `BRUTEFORCE_TEST_D3D11_HARDWARE=1` for hardware-device
coverage in addition to WARP. Synthetic tests need no game data and do not
replace matched gameplay or original-hardware validation.

## Current limits

- The reported roughly 6 FPS during movement with default launcher settings
  has not been explained or shown fixed by a controlled, matched benchmark.
  The final cache-fix binary was not rebenchmarked. Earlier whole-frame
  change rates are not validated world FPS.
- Matched-scene image equivalence remains open, including brightness, glow,
  HUD and depth/stencil behavior. The CPU reference has its own known gaps.
- Guest reads and writes of GPU-owned surfaces, concurrent texture changes,
  generalized fence completion, and safe removal of per-flip readback need
  further investigation. A clean surface-guard diagnostic alone is
  insufficient to remove readback.
- Device removal is reported and the renderer stops rather than claiming
  outstanding GPU work completed. Device/resource recovery, the separate
  presenter device and loss races at guest-visible synchronization are not
  established.
- Fixed and native resolution controls have synthetic WARP coverage, not a
  matched retail gameplay campaign. Performance, VRAM headroom, mip/LOD
  behavior, 4K display output and hardware-D3D11 scaled rendering remain
  unvalidated. The launcher therefore keeps 480p as its default.

Keep disc content, generated game code, screenshots, runtime captures and
logs local. Use synthetic fixtures for shareable reproduction. Current
priorities are in [NEXTSTEPS.md](../NEXTSTEPS.md).
