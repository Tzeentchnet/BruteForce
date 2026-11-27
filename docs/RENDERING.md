# Brute Force rendering architecture

This is a reference for the rendering path in this recompilation, not a
specification of the original Brute Force engine or Xbox hardware. It combines
observations from the supported North American retail executable with the
current host implementation. Guest addresses and layouts are revision
specific; the executable fingerprint is in
[retail.json](../fingerprints/retail.json).

For the disc and asset investigation, see [ARCHITECTURE.md](./ARCHITECTURE.md)
and [ASSETFORMATS.md](./ASSETFORMATS.md).
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
    |-- D3D11 backend --> GPU colour --flip sync/readback--> guest memory --> window
    |                    GPU depth/stencil                           |    presenter
    |                       |-- optional shared colour snapshot -----+
    |-- Vulkan backend -> GPU colour --mandatory readback------------+
    |                    |-- optional completed-image queue --> Vulkan presenter
    |                    GPU depth/stencil
    `-- null probe: no rasterization; visibility reported as visible

  Guest CPU <---- semaphore, interrupt, visibility report and flip coordination
```

The normal framebuffer handoff produces a CPU-side snapshot, optionally
composes video, and sends it to the window presenter. The default-off Vulkan
provider can consume an exact-frame completed image, but CPU-required routes
still use that snapshot. The shared GPU path is an optional scale-1
presentation optimization and the required high-resolution handoff for
scaled D3D11 modes; it is not a replacement for guest readback.
The CPU renderer is a useful comparison path, not proof of original-hardware
correctness. The null backend is a measurement probe and does not render a
correct image.

The accepted 2026-11-20 direction is to make Vulkan the fresh graphical
default, with Direct3D 12 as an automatic startup fallback. An explicit
Vulkan 1.3 backend now implements the base contract, CPU-transformed extended
batches, four texture stages, register combiners, cube maps and precise
visibility through mandatory guest readback and the existing CPU presenter.
Eligible untransformed vertex-program batches also run in a static SPIR-V
interpreter from gathered float inputs, with a compute readback verifier.
The default-off mip switch also exposes supplied 2D/cube chains. Its captured
Basic Controls frame is near the matched D3D11 output, and reviewed movement
guest-surface captures now retain the same tutorial scene and camera route.
CPU/PVIDEO writes made before GPU ownership are preserved. The default-off
native presenter now reaches operator-confirmed visible Basic Controls
through a validation-enabled FIFO swap chain. The opening Bink movies were
not observed in that audio-off run or its audio-off retry. A later bounded
audio-on trace opened both movie files and produced changing CPU-owned
publications through the native route, so source ownership is not the cause;
physical movie review remains open. Vulkan is still not the default, and
broader parity/stability gates remain open. D3D12 and production `auto`
selection remain unimplemented, and D3D11 remains the launcher and graphical
`build.py` default until the migration gates below pass.

## What has been observed in the title

A CPU-path tutorial census covering 3,219 flips observed vertex-program draws,
stencil state, render-to-texture and mainly DXT textures. Colour surfaces of
640x480, 320x240 and 160x120 and a 512x512 depth pass were observed in that
run; these dimensions are not an inventory of every scene or asset.

One traced tutorial frame submitted scene batches to a back buffer, a
512x512 depth-only pass, more scene batches, 320x240 and 160x120 downsample
passes, then glow composite and HUD batches before the flip. A separate trace
and synthetic replay investigated a bright pass that samples a packed
depth/stencil alias. A matched Basic Controls comparison establishes stale
packed stencil as the large brightness difference in that state; sun-facing
gameplay and the opening fade remain unmatched. These are observations of
particular runs, not an asserted
render-graph specification for the original game. See the
[feature census and frame trace](./FINDINGS.md#performance-plan-phase-0-and-the-first-gpu-path-results)
and [packed-alias investigation](./FINDINGS.md#packed-depthstencil-aliases-and-shared-presentation).

## Code and ownership map

| Area | Source of truth |
| --- | --- |
| Renderer selection and tutorial startup | [main.c](../src/main.c), [build.py](../tools/build.py), [launcher.py](../tools/launcher.py) |
| Staged pushbuffer walking and PGRAPH handling | [patch 0008](../patches/xboxrecomp/0008-experimental-pgraph-and-staged-walker.patch), [recomp_manual.c](../src/recomp_manual.c) |
| Bounded PFB acknowledgement, idle waiting and guarded PUT segments | [patch 0045](../patches/xboxrecomp/0045-bounded-nv2a-segments-and-waits.patch), [recomp_manual.c](../src/recomp_manual.c) |
| Default-off exact index-word run dispatch | [patch 0046](../patches/xboxrecomp/0046-index-word-runs.patch), [bench.py](../tools/bench.py) |
| Default-off zero-delay guest-poll backoff | [patch 0047](../patches/xboxrecomp/0047-zero-delay-sleep.patch), [main.c](../src/main.c), [build.py](../tools/build.py) |
| Host-owned framebuffer presentation provider | [patch 0048](../patches/xboxrecomp/0048-present-provider.patch) |
| Extended backend interface and vertex-program batches | [patch 0016](../patches/xboxrecomp/0016-extended-render-backend.patch), [patch 0017](../patches/xboxrecomp/0017-gpu-vertex-programs.patch) |
| D3D11 device, surfaces, state, draws, queries and readback | [d3d11_backend.c](../src/gpu/d3d11_backend.c) |
| Vulkan loader, device, surfaces, base/extended/VSH draws, combiners, queries and readback | [vulkan_backend.c](../src/gpu/vulkan_backend.c), [vulkan_base.hlsl](../src/gpu/vulkan_base.hlsl) |
| Transactional renderer startup and checked surface layout | [renderer_startup.c](../src/gpu/renderer_startup.c), [surface_layout.c](../src/gpu/surface_layout.c) |
| Renderer-neutral render-scale authority | [render_scale.c](../src/gpu/render_scale.c), [render_scale.h](../src/gpu/render_scale.h) |
| D3D11 seed/readback/final resolves | [render_scale_d3d11.c](../src/gpu/render_scale_d3d11.c), [render_scale_d3d11.h](../src/gpu/render_scale_d3d11.h) |
| Vulkan fixed-scale surfaces and seed/readback resolves | [vulkan_backend.c](../src/gpu/vulkan_backend.c), [vulkan_base.hlsl](../src/gpu/vulkan_base.hlsl) |
| Texture layout, decode and sampling | [texture_layout.c](../src/gpu/texture_layout.c), [texture_filter.c](../src/gpu/texture_filter.c), [texture_decode.c](../src/gpu/texture_decode.c), [texture_sampler.c](../src/gpu/texture_sampler.c), [d3d11_backend.c](../src/gpu/d3d11_backend.c), [vulkan_backend.c](../src/gpu/vulkan_backend.c) |
| Generated vertex and combiner shaders | [vsh_hlsl.c](../src/gpu/vsh_hlsl.c), [rc_hlsl.c](../src/gpu/rc_hlsl.c), [shader_cache.c](../src/gpu/shader_cache.c) |
| Null renderer and surface-read probe | [null_backend.c](../src/gpu/null_backend.c) |
| Frame handoff and optional shared presentation | [upstream snapshot synchronization](https://github.com/sp00nznet/xboxrecomp/pull/300), [patch 0024](../patches/xboxrecomp/0024-gpu-shared-presentation.patch), [patch 0028](../patches/xboxrecomp/0028-scaled-gpu-presentation.patch) |
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

`RECOMP_PB_INDEX_RUNS=1` is a default-off host dispatch optimization, not a
new synchronization mode. Both selected walkers may group only contiguous
pieces of non-incrementing, subchannel-zero `ARRAY_ELEMENT16/32` packets.
They stop at packet, PUT, segment and physical-memory-wrap boundaries; CALL,
RETURN and later methods remain ordered by the walker. The executor leaves
its initialization word scalar, expands 16-bit words low half first, and
preserves the final method register and index-buffer capacity behavior of
repeated scalar calls. GPU audit deliberately forces scalar method dispatch
so per-word values and counts remain exact. The option never advances GET
past a pending boundary, consumes no extra segment, and does not acknowledge
PGRAPH/PFB work, release a semaphore or alter flip holds.

The base backend interface supplies clears, screen-space draws and flips.
The extended interface supplies transformed batches, four texture stages
and combiner state, optional untransformed GPU vertex-program batches,
readback, and visibility reporting. The D3D11 backend registers both. Calls
to the rendering backend and its single-threaded D3D11 immediate context
are serialized on the NV2A thread. A separate worker compiles vertex-shader
bytecode; it does not own the rendering context.

The Vulkan bring-up backend registers the official base interface and the
existing local transformed-draw, untransformed vertex-program, readback and
visibility callbacks. Eligible vertex programs execute in Vulkan from
framework-gathered float inputs. `vsh_raw_ok` remains unset, so raw guest
streams still pass through `fetch_attr`; programs that write constants keep
the exact CPU interpreter fallback.
Its Vulkan queue, command slots, surfaces, textures, pipelines, queries and
fences are backend-owned; the executor and presenter receive no Vulkan
objects.

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

## Vulkan-first backend architecture and bring-up status

This section records the design accepted on 2026-11-20 and the implemented
base, visibility, CPU-transformed full-pixel-pipeline and gathered GPU-VSH
milestones. Pinned
dependencies, transactional startup, exact texture caching, compatible
color-target aliases, register combiners, cube maps, precise visibility and
an explicit Vulkan draw/flip/readback path now exist, but user-facing
defaults are unchanged. The existing D3D11 path remains the validated retail
implementation while Vulkan is developed and compared against the CPU
reference.

### Selection and failure policy

The current selector adds explicit `RECOMP_GPU=vulkan` while retaining
`d3d11`, `cpu`, and `null`. The future selector also adds `auto` and `d3d12`.

| Selection | Required behavior |
| --- | --- |
| `auto` | Initialize Vulkan completely before guest execution. If Vulkan is unavailable or its startup initialization fails, report the exact reason and initialize D3D12. If D3D12 also fails, stop; do not silently select CPU, D3D11, or `null`. |
| `vulkan` | Implemented for base, CPU-transformed extended and eligible gathered-input GPU-VSH draws. Initialize Vulkan completely or fail closed with a structured stage and reason. An explicit backend request never falls through to another renderer. |
| `d3d12` | Initialize D3D12 or fail closed. |
| `d3d11` | Keep the current renderer available throughout migration and parity validation. It is not part of the future automatic chain. |
| `cpu` / `null` | Preserve the correctness reference and measurement probe as explicit choices. |

Once all default gates pass, fresh launcher settings and graphical
`build.py` runs will select `auto`. Existing saved `d3d11`, `cpu`, or other
explicit choices must survive the settings-version migration. Bare
low-level launches with graphics disabled may continue to leave
`RECOMP_GPU` unset and use the CPU path.

Fallback is limited to startup, before a backend is registered and before
the guest entry point runs. Loader absence, insufficient API or feature
support, no suitable physical device, device/queue creation failure, shader
compiler initialization failure, or host presentation-module initialization
failure are backend startup failures. Invalid common configuration is not an
availability failure and must be reported without trying to disguise it by
selecting another API.

No backend switch occurs after guest GPU work begins. Device loss, command
submission failure, allocation failure after startup, failed readback, or
failed presentation remains fatal and reported. Runtime failover would need
to reconstruct GPU-authoritative colour and depth resources, texture and
pipeline caches, visibility queries, pending reports, presentation epochs,
and exact semaphore/flip ordering. That is a separate recovery design, not
part of the D3D12 backup policy.

Each new backend therefore needs a two-stage lifecycle:

1. Parse and validate common options without committing renderer state.
2. Create the loader/compiler, adapter, device, queues, synchronization
   primitives and a probe pipeline, then establish compatibility with the
   host presentation service. Only a fully initialized candidate may
   register the official base contract and any accepted optional extensions.

Initialization must return a structured stage and reason for diagnostics.
The current boolean D3D11 installer can remain during migration, but the new
selector must not infer availability from a partially registered backend.

The Vulkan implementation follows that lifecycle now: it dynamically loads
`vulkan-1.dll`, validates options, creates the instance, selected physical
device, logical device, graphics queue, eight command slots and fences,
descriptor/pipeline layouts, shader modules, white texture and probe pipeline,
and only then registers callbacks. Failure injection verifies that loader and
probe-pipeline failures leave no partial registration. The generic selector
fixture also covers explicit no-fallback, Vulkan-to-D3D12 startup fallback,
invalid-configuration stop, and both-candidates-fail behavior with fake
candidates. Production `auto` is intentionally not exposed before D3D12 and
the presentation gates exist.

### Ownership and code boundaries

The upstream authority is `Nv2aBackend`, evolved incrementally. In
[sp00nznet/xboxrecomp#306](https://github.com/sp00nznet/xboxrecomp/issues/306)
the maintainer chose a mixed boundary: keep that contract, add capabilities,
failure reporting and renderer-neutral state one synthetic-test PR at a
time, keep full renderers downstream for now, and keep presentation as a
host service. The executor must not take a GPU round trip per draw, and every
callback's thread and borrowed-data lifetime must be documented.
[sp00nznet/xboxrecomp#319](https://github.com/sp00nznet/xboxrecomp/pull/319),
included in v0.18.0, is the first accepted step: a reusable fake backend that
checks callback order, exactly-once delivery, deep copies of borrowed data,
thread ownership, registration, replacement and removal.

Brute Force's broader `Nv2aBackendExt` remains useful implementation evidence,
but it is not the target upstream ABI and must not be proposed wholesale.
The Vulkan exemplar consumes official `Nv2aBackend` plus the existing local
CPU-transformed draw, readback and visibility callbacks. Each capability—
versioning, durable failure, texture stages, combiner state, vertex programs,
visibility or another semantic—still needs an independent game-data-free
fixture and upstream-shaped change before proposal. Local patch 0016 can
continue serving the current D3D11 and Vulkan paths while those pieces
converge; the portable Vulkan core contains no Brute Force addresses and does
not depend on upstream accepting that bundled seam.

The intended layering is:

```text
  NV2A executor and official Nv2aBackend contract
      | optional, versioned renderer-neutral extensions
                  |
                  v
  Brute Force renderer-neutral policy and immutable batch helpers
      | texture/surface layout, snapshots, cache keys, scale math
      | shader source/IR, frame identity, ownership transitions
      +-------------------+-------------------+
      |                   |                   |
      v                   v                   v
  Vulkan backend      D3D12 backend      D3D11 transition backend
      |                   |                   |
      +------ completed image, sync, frame ID and readback ------+
                                                                  |
                                                                  v
              host presentation service and CPU fallback
                 | Vulkan/D3D12/D3D11 presentation modules
                 `-- owns window, surface/swapchain and Present
```

Extraction is incremental, not a big-bang rewrite of
`d3d11_backend.c`. First move only pure, API-independent behavior with
existing D3D11 fixtures proving unchanged results. Suitable shared pieces
include validated texture and surface layouts, immutable guest snapshots,
cache keys, logical/physical scale calculations, frame identities, and
shader-generation inputs. Devices, resources, views/descriptors, pipeline
objects, command recording, queries and render fences stay in each backend.
Platform windows, presentation surfaces, swap chains and final presentation
stay in the host service.

The renderer-neutral layer is project code until a second implementation and
synthetic fixtures prove that a piece is genuinely generic. Guest addresses,
Brute Force structures, and title-observed policy never move into the
framework. Generic contract extensions, helpers, fixtures and backend-authoring
documentation are the first upstream candidates recorded in
[UPSTREAM.md](./UPSTREAM.md). The Vulkan renderer remains a downstream
exemplar until those seams are accepted and maintainers explicitly approve a
full optional backend PR; it is nevertheless organized and tested to make
that PR the end goal rather than a title-specific fork.

The proven extractions now comprise:

- the checked surface-layout helper, whose fixture covers pitched 32-bit and
  16-bit layouts plus rejected formats and dimensions;
- immutable texture storage/span calculations for linear, swizzled, block
  compressed, paletted, mipmapped and cube declarations, with rejected
  overflow and unsupported cases; and
- checked guest snapshots that verify the memory-layout generation, tracked
  heap or contiguous allocation identity, optional known bounds, mapping
  identity and readable pages before and after `ReadProcessMemory`; and
- texture minification, magnification, mip-filter, LOD-clamp and signed-bias
  decoding shared by the D3D11 and Vulkan sampler adapters; and
- the default stage-zero-times-diffuse combiner-state constructor shared by
  D3D11 and Vulkan.

D3D11 retains its performance-counter wrapper around the shared snapshot
helper, and its regression fixtures remain unchanged. No device, resource or
synchronization ownership moved into the neutral layer.

### Vulkan texture identity and render-target aliases

Vulkan's image cache is backend-specific but uses the shared immutable
layout and snapshot contracts. It has 2,048 entries, a 4,096-bucket exact
descriptor index and a 128 MiB retained
source-snapshot budget. A hit requires an exact descriptor, allocation/mapping
identity, 2D/cube identity and source-byte match; P8 textures also require an
exact palette snapshot match. Decoding consumes the immutable snapshots
rather than live guest memory. Six cube faces retain the NV2A 128-byte face
stride and become one cube-compatible image. Hits add no GPU wait.
Replacement and eviction retire images against the command-slot submission
fence that last used them rather than waiting for the whole device. They do
not weaken submission or flip ordering to gain throughput.

Color render targets can feed compatible stage samples without first
publishing the whole surface to guest memory:

- a full alias of a non-bound surface samples its tracked Vulkan image
  directly;
- a full alias of the currently bound color target takes an ordered copy
  before drawing, avoiding a read/write feedback loop; and
- a row-aligned compatible subregion takes an ordered transient copy sized to
  the programmed texture, preserving texture-local wrap and clamp behavior.

The implemented alias formats are the observed 32-bit `0x12`, `0x06` and
`0x2E` cases and 16-bit `0x11`. A surface sampled and then rebound for
rendering is transitioned back to color-attachment ownership. Incompatible
formats, pitches, bounds or overlaps still fail explicitly rather than
sampling stale guest memory. A non-aliased `LIN_D24S8` stage samples white,
matching the CPU reference's unsupported-format rule; that is not a
GPU-owned depth-alias implementation. These paths have synthetic full-alias,
self-alias, subregion, cube-face and packed-depth-fallback coverage. Mipmaps
remain level-zero-only for aliases; ordinary swizzled, P8 and BC textures can
expose supplied levels under `RECOMP_GPU_MIPS=1`.

### Vulkan guest-surface ownership

Vulkan tracks a guest-memory shadow, allocation identity and GPU-dirty state
for each color surface. Initial creation snapshots guest pixels and uploads
them. A full color clear mirrors its logical value into guest memory and
leaves both copies coherent. A partial clear or draw marks the GPU copy dirty.
If guest bytes change while the GPU copy is clean, the guest copy is
authoritative: readback preserves it, and the next GPU use uploads it before
recording the partial clear or draw. This retains CPU/PVIDEO and linked Bink
output instead of overwriting it with an earlier Vulkan clear.

If guest bytes change while the GPU copy is dirty, neither side can be merged
safely. The backend reports the surface, flip, changed-byte span and layout,
then preserves and reads back the completed GPU frame. It does not upload the
conflicting guest bytes or pretend that work completed on both owners.
Successful readback refreshes the shadow and clears the dirty state. The
hardware fixture covers clean guest preservation, upload before a partial
clear, GPU-wins conflict behavior, and the required submission wait when a
mirrored full clear makes the guest pixels already coherent.

This policy is conservative, not a complete arbitrary-access protocol. In
retail gameplay a retained 512x512 surface at `0x80000000` conflicts while
the active 640x480 output is at `0x802B0000`; changed bytes advance through
the entire 1 MiB range. Prior guard evidence places staged pushbuffer accesses
in that same range, and the executor still ignores the base named by
`NV097_SET_CONTEXT_DMA_COLOR`. Staged ring reuse is the supported explanation,
but the exact writer was not intercepted. Do not convert that pattern into a
guest-wins rule or skip readback. Generic DMA-object resolution plus live
surface lifetime tracking is the framework-level prerequisite.

### Vulkan window clipping

The Vulkan backend advertises `NV2A_BACKEND_CAP_WINDOW_CLIP` and consumes the
same opt-in `Nv2aWindowClip` state as D3D11. Each base, CPU-transformed
extended and gathered GPU-VSH draw uploads eight `uint4` rectangles plus a
count/exclusive flag to a fragment-only uniform binding. Logical inclusive
endpoints expand to inclusive physical bounds when either AA axis is doubled.
Incomplete horizontal/vertical state uploads a zero count and leaves the draw
unclipped, matching the CPU helper.

Both Vulkan pixel paths discard a fragment before color or depth output when
it is outside an inclusive union or inside an exclusive union. Clear shaders
do not reference the clip uniform, so color and depth clears remain
unaffected. The validation-enabled NVIDIA and AMD fixture covers multiple
rectangles, inclusive and exclusive masks, incomplete axes, AA scaling,
clear isolation, suppressed depth writes, and base, extended and GPU-VSH
draws. `RECOMP_WINDOW_CLIP` remains default-off because those synthetic
semantics do not establish original-hardware reset state or retail parity.

### Vulkan render scaling

Vulkan consumes the same renderer-neutral
[render_scale.c](../src/gpu/render_scale.c) authority as D3D11, so a fixed
numeric `RECOMP_GPU_SCALE` selects the same physical sizes, including the
folded-out guest AA factor. `RECOMP_GPU_SCALE=native` starts at scale 2
(or the `RECOMP_GPU_OUTPUT` picture) and, with `RECOMP_VULKAN_PRESENT=1`,
follows the presentation provider's output picture through scale epochs, as
described below. Scale 1 keeps every Vulkan resource at its previous logical
size and skips all resolve passes.

At a fixed scale, each tracked color surface keeps its logical layout and a
physical `pw/ph`. The color image and any attached depth/stencil image are
physical. Guest vertices and shader constants stay logical; only the
viewport, render area, surface scissor, clear rectangles and window-clip
rectangles become physical. Those edges use the D3D11 backend's
`scale_edge` rounding, so inclusive window-clip bounds round the exclusive
logical edge before subtracting one. A guest layout or AA change after
creation still stops with the existing layout-change diagnostic; only a
scale epoch migrates a surface.

A per-surface logical image serves both directions of guest exchange. A
clean CPU/PVIDEO guest write is decoded, uploaded at logical size and
bilinear-seeded into the physical target. Mandatory readback box-downsamples
the physical target into the logical image and copies that image to guest
memory, so the guest always receives a 4:3 logical picture. The resolve
shaders are SPIR-V ports of the Blood Wake-derived D3D11 bilinear and box
passes; both directions choose box filtering only when the source is larger.
The physical image remains GPU-authoritative after readback. Full-surface
render-target aliases sample the physical image with unchanged normalized
coordinates. Self-alias snapshots are physical, and subregion aliases copy
the rounded physical rectangle, keeping at least one physical texel.

In native mode the backend asks the registered provider for its output size
at each flip readback, before the readback and source publication. The
provider returns the client area fitted to the aspect of the last presented
guest frame; for Brute Force's 640x480 frame that is the same 4:3 picture
D3D11 uses. `gpu_scale_prepare_output` validates the size and scale. When
the scale changes, every replacement color and depth/stencil image is
allocated first; any allocation failure discards the candidates and keeps
the prior scale, epoch and images, and a later flip retries. After
`gpu_scale_commit_output`, one submission resamples each color surface from
its old physical image (box when shrinking, bilinear when growing), because
color is GPU-authoritative and guest memory can be stale. Resized
depth/stencil images are cleared to depth 1 and stencil 0 and require guest
reinitialization; D3D11 has the same limitation. The submission is waited on
before old images and self-alias snapshots are destroyed. Without a
registered provider, and with a fixed `RECOMP_GPU_OUTPUT`, the scale stays
at its initial value, matching D3D11 without its shared presenter.

Visibility queries record a per-query divisor of the guest AA factor times
the physical-to-logical area ratio, so counts remain guest-screen pixels.
Native presentation receives the physical image unchanged through the
existing completed-image queue; the provider's fitted blit performs the final
scaling. The validation-enabled NVIDIA and AMD fixture runs a scale-2 child
process covering clears, exact half-surface coverage, sub-guest-pixel
coverage, CPU-written seeding, window clipping, normalized visibility,
physical depth testing, full and subregion aliases, clear rectangles and the
native presentation routes. It establishes synthetic backend semantics, not
scaled retail parity or performance. Validation-clean retail runs at fixed
scale 2 and in native mode with live window resizes are recorded in
[FINDINGS](FINDINGS.md#vulkan-scaled-and-native-mode-retail-runs-2026-11-23).
They also show that, because the executable is not DPI-aware, the
provider's output size is in logical pixels on scaled displays.

### Extended combiners, GPU vertex programs, and visibility

The existing `Nv2aBackendExt.draw` seam supplies `Nv2aXVertex` data after the
framework CPU path has run the title's vertex program, clipping and ordinary
CPU-side culling. Its static extended vertex shader converts the screen-space
position to Vulkan clip space and carries diffuse/specular colors, fog and
four coordinate sets.

Eligible `draw_vsh` batches instead upload the NV2A instructions, 192
constants, gathered float inputs and a remapped 32-bit triangle index list.
A statically compiled SPIR-V interpreter implements the same source mux,
swizzle, negation, temporary/output register, MAC/ILU, relative-constant,
mask, `+0` W, AA, near-clip, depth, color, fog and texture-coordinate rules as
the D3D11 generator. A geometry stage drops the whole triangle when any
transformed position or clip distance is non-finite. The path intentionally
does not claim raw stream support; leaving `vsh_raw_ok` unset preserves the
framework's gathered-input and CPU-fallback rules.

When `RECOMP_GPU_VSH_VERIFY=<n>` is active, the backend retains only the last
selected batch, re-runs the same interpreter in a compute shader, waits for
that diagnostic dispatch and maps `Nv2aXVsOut` back to executor coordinates.
Normal draws allocate no verification output and add no verification wait.

One static extended pixel shader interprets up to eight general register
combiner stages and the final combiner from an aligned uniform buffer. It
implements projected/normalized 2D sampling, native cube sampling,
passthrough, clip-plane modes, per-stage constants, mux/dot/output mappings,
final-combiner inputs and alpha test. Four image/sampler pairs, four cube
fallback views and the uniform buffer use a fixed descriptor ABI. Null
combiner state uses the shared texture-zero-times-diffuse constructor.
Synthetic tests cover a two-texture multiply, that default constructor,
positive-X cube selection and alpha-preserving packed-depth fallback.

Vulkan associates one precise occlusion query with each of its eight bounded
command slots. Query reset/begin/end remains in the draw's command order;
results are consumed only after the slot fence completes, accumulated with
saturation, and divided by `aa_sx * aa_sy` to report logical guest pixels.
`CLEAR_REPORT_VALUE` drains older queries before resetting the total. This is
the synchronous correctness policy, not a throughput optimization. The retail
visibility run restored 1,607.6 draws and 132,454.7 vertices per frame, closely
matching D3D11's 1,608.4 and 132,641.7, but its one completed stationary
interval was only 1.261 flips/s and is not A/B/A performance evidence.

### Shader and dependency policy

Vulkan and D3D12 should share the existing HLSL generators rather than grow
two independently maintained NV2A shader languages. A pinned DirectX Shader
Compiler version will compile the same generated source to SPIR-V for Vulkan
and DXIL for D3D12. D3D11 keeps its existing `D3DCompile` path until a
separate, measured migration is justified.

Shader-cache identity must include at least the backend, DXC version, target
environment/profile, compile flags, descriptor/root-signature ABI, generated
source, and relevant renderer options. D3D11 bytecode is never reusable by
Vulkan or D3D12. Cache incompatibility must miss or reject explicitly rather
than loading bytecode under the wrong API contract.

The build pins Vulkan-Headers `vulkan-sdk-1.4.363.0` at commit
`6802bb4733b63ed5efd3adb308a6c885ef180ea1` and DXC `v1.9.2609`. The DXC
archive SHA-256 is
`ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1`;
the installer also verifies the header tree, required compiler files and
archive extraction paths. Build-time DXC compiles nine base, partial-clear, extended and VSH
HLSL entry points to Vulkan 1.3 SPIR-V. Both `build.py` and direct CMake
configuration compare the required extracted DXC files with the verified
archive before executing the compiler. End users need no Vulkan SDK.

The backend requires a Vulkan 1.3 loader and device, dynamic rendering,
`shaderDemoteToHelperInvocation`, geometry shaders, shader clip distances,
precise occlusion queries, a graphics queue, BGRA8 color
attachment/sampling/transfer support, and compatible depth formats. Runtime adapter
preference is discrete, integrated, virtual, then CPU;
`RECOMP_VULKAN_DEVICE=<decimal-index>` selects one explicitly. The production
future `auto` path will treat a missing loader or insufficient device as the
reported reason to try D3D12. D3D12 continues to come from the supported
Windows SDK.

### Presentation and synchronization

The initial CPU-upload route is implemented for explicit Vulkan bring-up:
mandatory flip readback publishes guest pixels to the existing window. It is
not sufficient for default promotion because it discards any
higher-resolution GPU result and cannot replace current scaled native
presentation.

The hard boundary from sp00nznet/xboxrecomp#306 is that presentation remains
a host service. `Nv2aBackend` produces and reads back surfaces; it does not
create or pump the top-level window, own the presentation surface or swap
chain, or call the platform's final present operation. The framework host
continues to own input, the CPU snapshot, PVIDEO composition, fullscreen
state, VSync policy, frame identity and the presentation thread. Pinned
framebuffers, PVIDEO, captures and diagnostics must still be able to request
the CPU route without creating a competing swap chain.

Local framework patch 0048 now provides the first separate host-service seam
without putting window callbacks or API types into `Nv2aBackend`. A
downstream host registers one copied, versioned `RecompPresentProvider`
before framebuffer-window initialization. Its context remains borrowed until
shutdown. `initialize`, `present` and `shutdown` run on the framebuffer
window thread; `output_size` may run concurrently from a renderer thread.
Registration, replacement and removal are rejected after initialization
begins.

Each `present` callback receives stable CPU pixels and dimensions,
`allow_source`, and the publication frame ID. `allow_source == 0` requires
the CPU input for pinned frames, PVIDEO and presenter-source captures;
otherwise a downstream provider may use its own same-API source. The callback
returns paced, unpaced or fatal. A fatal initialization or presentation
result stops explicitly instead of falling back to GDI or D3D11. Shutdown
first rejects new calls, waits for in-flight callbacks, then invokes the
provider exactly once. With no provider registered, the existing GDI and
D3D11 paths are unchanged.

The default-off downstream Vulkan provider now implements that split behind
`RECOMP_VULKAN_PRESENT=1`. The host service creates the window while the
provider owns its Win32 Vulkan surface, swap chain, acquisition, resize,
aspect-fitted final blit, VSync mode and presentation. The backend and
provider share the selected instance, device, graphics queue and queue
family; one lock supplies Vulkan's required external queue synchronization.
The backend copies the finished color image during flip readback and still
completes mandatory guest readback before framebuffer publication.

Three device-local source images form the bounded handoff. The renderer may
claim FREE or oldest READY storage, but never WRITING or PRESENTING storage;
reusing READY storage records an explicit coalesced source. A source becomes
READY only after the existing submission fence completes. The provider takes
only an exact publication-frame ID, and otherwise uploads the stable CPU
pixels. A clean guest-written surface with a pending GPU upload deliberately
publishes no native source, preventing an older GPU image from replacing the
CPU-owned frame. Pinned frames, PVIDEO and presenter-source capture also
force the CPU route and discard eligible queued sources. Resize or
`VK_ERROR_OUT_OF_DATE_KHR` recreates the swap chain; device/surface failures
remain fatal. D3D12 follows the same ownership split with a future host
D3D12 module.

Every 300 successful WSI presentations, a cumulative diagnostic separates
native frames, forced CPU frames, CPU frames with no exact native source, and
coalesced, forced or stale source drops. The existing 300-flip backend
diagnostic also counts native-source publication suppressed for pending clean
guest uploads. These counters attribute the early movie route without
changing synchronization or requiring continuous capture.

The provider lifecycle/CPU-fallback seam and its downstream Vulkan
image/synchronization implementation are complete for the synthetic
NVIDIA/AMD fixture, but that API-specific descriptor is not an accepted
upstream interface. Open
[sp00nznet/xboxrecomp#312](https://github.com/sp00nznet/xboxrecomp/issues/312)
asks about adjacent downstream host hooks and has no maintainer answer for
presentation. Keep the API-specific handoff downstream with fake
producer/consumer fixtures and outside the executor contract; upstream only
the smallest renderer-neutral seam that maintainers approve.

Making Vulkan depend on importing into the current D3D11 keyed-mutex
presenter would turn cross-API interop into a default hardware requirement
and is not the target architecture. The current D3D11 shared path remains intact, and omitting the default-off
Vulkan switch leaves instance and device presentation requirements disabled.

The API primitives may differ, but their externally visible ordering may
not:

- command buffers/lists may batch work only within the current submitted-work
  bound and must apply backpressure at the same logical checkpoints;
- flip readback must publish guest pixels before snapshots, dumps, hashes,
  and CPU consumers;
- visibility reports must retire in command order and normalize scaled
  samples to guest-pixel counts;
- render-to-texture transitions must not force whole-frame guest readback;
- semaphore releases, PFB acknowledgement, software-method traps, report
  completion, flip holds, and queued PUT segments keep their existing
  ordering;
- resize and native-output changes use one committed presentation/scale epoch
  so projection, physical resources, and the displayed picture cannot
  disagree;
- device removal or loss reports the failed API operation and stops rather
  than claiming queued work completed or switching backend.

### Rollout and completion gates

The migration proceeds in independently reviewable stages:

The project has adopted the v0.18/#319 contract baseline, added selector
lifecycle coverage, extracted checked surface/texture layouts and guest
snapshots, pinned the dependencies, and completed stage 4's base,
CPU-transformed pixel-pipeline and gathered-input GPU-VSH subset. Stage 5 is in progress: the
CPU-presented retail path now reaches a near-matched Basic Controls state with
exact 2D/cube/mip texture reuse, register combiners, compatible color aliases
and precise visibility. Its default-inactive host presentation-provider seam
and the downstream Vulkan WSI module are implemented and synthetic-tested.
The WSI fixture uses real NVIDIA and AMD surfaces and swap chains with
validation enabled. One validation-enabled retail run reached an operator-
confirmed visible Basic Controls screen through its 640x480 FIFO swap chain;
opening movies were not observed because both attempts disabled audio. A
bounded audio-on no-script trace subsequently opened the movie files and
produced changing CPU-owned frames through the native provider.
Stages 2-3 continue as full parity reveals proven generic seams; stages 6-9
remain open.

1. **Adopt the accepted contract baseline.** Reuse the merged
   sp00nznet/xboxrecomp#319 fixture; do not duplicate it. Add project tests
   only for lifecycle, selector and presentation-source behavior outside its
   existing callback-order scope.
2. **Grow the contract one concern at a time.** Prepare version/capability,
   durable failure and required renderer-state extensions as separate
   synthetic changes shaped for official `Nv2aBackend`. Rebase local adapters
   when a piece is accepted rather than reviving the #184 seam wholesale.
3. **Extract neutral helpers.** Move pure renderer behavior with unchanged
   D3D11 tests. Only helpers proven by the Vulkan exemplar become upstream
   candidates.
4. **Explicit downstream Vulkan bring-up (transformed pixel pipeline and gathered GPU-VSH complete).**
   The loader/device/queue, validation diagnostics, base, CPU-transformed
   draws, four 2D/cube texture stages, default-off supplied mip chains,
   gathered-input NV2A vertex programs with verification readback, register
   combiners, precise visibility, depth/stencil, bounded submission, readback
   and CPU presentation route are available behind
   `RECOMP_GPU=vulkan`. The backend remains non-default and contains no title
   addresses or generated game data.
5. **Host-owned Vulkan presentation and parity (in progress).** Exact
   snapshot-checked 2D/cube/mip caching, compatible full/self/subregion color
   aliases, and a near-matched NVIDIA Basic Controls capture are implemented.
   Review places all 134 over-8/255 pixels in exposed animated world content;
   three opaque panel regions have none, while their remaining differences
   stay within six channel levels. The wider-scene profile proved CPU
   vertex-program interpretation was a renderer bottleneck; the gathered
   Vulkan interpreter removes it from the sampled NV2A hot list and materially
   improves unreviewed flip cadence. Patch 0048 now supplies the separate
   provider lifecycle, threading, CPU-fallback, capture and pacing seam with
   no registered-provider behavior change. The default-off WSI provider now
   covers a three-slot completed-image ownership handoff, exact frame IDs,
   forced CPU fallback, aspect-fit scaling, resize and VSync on validation-
   enabled NVIDIA and AMD fixtures. The same fixtures enter and restore the
   host's borderless-fullscreen monitor bounds, recreate the swap chain, and
   require an injected surface loss to return fatal. Fixture-only swap-chain
   readback verifies exact source color at the fitted center and opaque-black
   outer bars for native, forced-CPU, source-allowed CPU and fullscreen
   routes. The integrated retail route reaches
   visible Basic Controls. Bounded no-script evidence now shows that
   audio-off skips the title's movies while audio-on opens both Bink files and
   sends changing CPU-owned frames through the provider. Fold physical movie
   review into a later required validation rather than scheduling a separate
   operator boot; physical fullscreen review remains open. The Vulkan base,
   CPU-transformed extended and gathered GPU-VSH paths now share the opt-in
   window-clip state with cross-vendor synthetic coverage. Fixed numeric
   render scaling now renders physical color/depth images, resolves guest
   readback and hands physical sources to native presentation, with
   cross-vendor scale-2 synthetic coverage; native-mode scale epochs pass
   cross-vendor synthetic migration fixtures.
   The first no-capture D3D11/Vulkan sequence was invalidated by a recurring
   pre-movie guest-worker poll on run 3. A default-off framework zero-delay
   backoff now passes its focused fixture and three no-retry D3D11 startup
   checks. A fresh one-run-per-renderer diagnostic then completed on the same
   executable with input dispatch matched within 78 ms. Vulkan was 12.29%
   below D3D11 in stationary flips and 63.75% below during movement, but the
   frames remain unreviewed and this is not A/B/A evidence. The final
   Vulkan-only counter run is now aligned to ending flips. Three complete
   movement intervals attribute 17.59% of wall time to exact texture snapshots
   inside draw callbacks, 13.54% to mandatory readback and 11.97% to
   asynchronous report polling, versus 0.56% to backend gathered-vertex
   copies. Draw, texture, wait, report-completion and fence-poll counters have
   nested boundaries and are not additive.    Report polls whose oldest item is both unsubmitted and younger than the
   existing 0.25 ms flush deadline are now deferred; assigned reports,
   backpressure, synchronous drains and guest-visible ordering are unchanged,
   and a forced pre-deadline/aged-report fixture passes on both available GPU
   vendors. Exact snapshot elision is deferred because no exact write/
   invalidation mechanism exists for the aliased file-mapped guest memory;
   approximate hashes, unchecked direct access or stale per-frame reuse would
   weaken guard-page and allocation-lifetime behavior.
   Direct raw-stream fetch remains open because framework gathering is
   unmeasured, but backend copy time does not make it the first target. Defer
   a multi-run acceptance campaign until native presentation makes it
   actionable.
   Compare matched frames with CPU and D3D11 and exercise NVIDIA, AMD and
   Intel where available. The current capture closes one stationary state,
   not broad parity.
6. **Upstream extraction and Vulkan PR readiness.** Submit only approved
   generic contract, helper, fixture and documentation pieces, each with its
   own synthetic evidence. Once the contract and host presentation boundary
   are accepted and the downstream renderer passes parity, request explicit
   approval to open an optional, default-off Vulkan backend PR. No upstream
   interaction is implied by this plan.
7. **D3D12 parity.** Implement the same accepted contract with D3D12, using
   WARP for deterministic no-hardware fixtures where applicable. It remains
   explicit until it meets the same output and ordering gates.
8. **Automatic selection.** Test injected Vulkan startup failures,
   successful D3D12 fallback, both-backends-fail behavior, explicit
   no-fallback selections, settings migration, and unambiguous active-backend
   diagnostics. Only then make `auto` the fresh graphical default.
9. **D3D11 reassessment.** Keep D3D11 selectable through Vulkan and D3D12
   parity and field validation. Retirement is a later explicit decision, not
   an automatic consequence of switching the fresh default.

Every backend must pass the shared synthetic state, texture, clear, shader,
visibility, readback, scale, presentation, failure-injection, and ordering
fixtures. Default promotion additionally requires matched-frame coverage for
the opening, tutorial stationary and movement scenes, menus, movies, HUD,
fades, render-to-texture and PVIDEO/CPU fallback. Performance comparisons
come only after correctness and use the same-generation A/B/A protocol,
reporting distinct world frames separately from flips. A different graphics
API is not assumed to improve the PFB flush wait or any other guest-side
bottleneck.

## D3D11 rendering systems

### Surfaces and clears

The backend tracks colour and depth surfaces by guest address. A new colour
surface can be initialized from guest memory when its layout permits it;
the GPU copy then holds rendering results. A compatible exact colour alias
is sampled after reconciling its checked guest shadow; a currently bound
target uses an ordered GPU copy, with the target restored before drawing.
Partial or ambiguous GPU-dirty overlaps and conflicting guest writes are
rejected rather than sampled from stale backing bytes. Aliased views do not
establish a general guest-reader or guest-writer synchronization contract.
The backend unbinds texture views when binding a surface for drawing.

Depth and stencil use D3D11 depth resources. With the optional depth-view
paths enabled, an eligible new linear Z24S8 surface can be seeded from guest
memory, converting the packed byte order. Checked snapshots include row
padding and allocation/layout identity. Changed guest bytes on a clean owner
trigger transactional recreation and reseeding; changed bytes after a
potential GPU write, unreadable/short spans, or conflicting dirty owners
reject affected work rather than reusing stale data. A stable full-surface
clear of both depth and stencil can rebaseline a live conflicted allocation;
partial or single-plane clears cannot. A changed allocation lifetime can
retire its stale owner, while a freed tracked allocation cannot be reseeded
or cleared. A dirty owner cannot be silently recreated in another layout.
This is conservative snapshot detection, not a guest-write interception or
general synchronization contract.

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
  mode. Its fitted 4:3 picture must produce a scale from 0.5 through 10.
  For example, 3840x2160 yields 2880x2160 at 4.5x; 1080x1920 yields
  1080x810 at 1.6875x. Scale 1 remains the default.

Each tracked colour surface keeps logical `w/h` and physical `pw/ph`.
Physical colour and depth resources follow the selected scale. Guest
anti-aliasing factors are folded out of scaled resource dimensions, following
Blood Wake's model. A scale epoch or AA-layout change recreates resources;
because colour is GPU-authoritative, a resized colour resource is migrated
from the old GPU view instead of being reseeded from stale guest memory.
Render-target aliases sample the physical resource while texture coordinates
remain in guest-logical units.

On the NV2A owner thread, initial scaled-device resources are prepared
before the configuration commits. For a live native resize, the backend
prepares replacement tracked colour and depth targets, shared-output texture,
and existing readback resources as needed, and commits the dimensions and
epoch only after preparation succeeds. The flip-only fallback preflights the
last readback surface, or the current target before the first readback, so
it does not bypass known readback allocations. Output-only changes can
replace the shared texture without changing the epoch. An allocation, view
or migration failure is diagnosed and retains the prior scale and resources
for retry.
A pending shared frame defers the transition until the normal keyed-mutex
handoff permits it. A non-timeout mutex ownership error or device loss stops
execution; neither is automatically recovered.

Changed-size depth/stencil targets are recreated, **not migrated**: their
contents are undefined until the guest clears or regenerates them. Whether
the title does so at each native resize remains a retail validation gate.

Guest vertices and shader constants remain logical. The D3D11 viewport uses
physical dimensions, so rasterization gains resolution without changing
guest geometry. Surface scissors, partial clears and inclusive window-clip
rectangles are rounded into physical pixels explicitly. The backend ends and
restarts an occlusion query at target changes and internal blits, retaining
each segment's starting physical-to-logical pixel-area ratio. Synchronous and
asynchronous reports normalize each segment using that saved ratio before
summing it. Point size, line width and title-wide visual equivalence at
scaled resolutions still need gameplay validation.

GPU full-screen passes seed a new physical target with bilinear scaling,
box-average a completed target back to guest size for readback, and choose an
exact copy, box downsample or Catmull-Rom upscale for the shared output
picture. Invalid values, dimensions beyond D3D11's 16384-pixel limit and
resource-creation failures are reported; the renderer does not silently lower
the selected resolution.

### Proposed true-widescreen boundary

True widescreen is not implemented. A default-off title camera trace now
wraps the central perspective builder and records bounded caller,
input/output-matrix and explicitly labelled candidate camera/viewport state
without changing the generated result. Its synthetic state-equivalence and
dispatch coverage passes. One fresh-generation scripted route observed only
one of the two static direct callers and six candidate camera states; the
scenes were not operator-classified, and culling ownership remains unknown.
The static investigation in
[FINDINGS](./FINDINGS.md#blood-wake-feature-review-and-true-widescreen-investigation)
establishes a viable architecture, not validated runtime behavior.
Framebuffer widening alone is explicitly insufficient because it would
stretch 2D content and leave title culling and guest readback undefined.

The proposed model keeps render scale and aspect independent:

```text
S = physical pixels per guest pixel in both axes
K = requested picture aspect / (4/3), K >= 1

eligible screen target:  pw ~= logical_width * S * K
                         ph ~= logical_height * S
ordinary off-screen target: existing S-only rule
```

Only a runtime-proven set of device-sized colour/depth targets may use `K`.
Downsample, glow, shadow, reflection and other render-to-texture surfaces
retain their existing aspect unless title evidence establishes otherwise.
The current CPU renderer stays at 4:3 and scale 1 as the comparison path.

Title code must preserve vertical field of view while expanding horizontal
projection and every corresponding visibility structure. A final-matrix-only
hook is not enough: objects rejected by 4:3 title culling never reach either
renderer. World-to-screen markers also need correction if their consumers
remain in the pillarboxed guest coordinate system.

The renderer needs ordered classification of title-known 2D and movie
batches. The proposed generic framework surface is an application-metadata
callback for tagged, otherwise inert pushbuffer methods; the tag and the
functions that emit it remain title-specific. For a view centred at NDC x
`c`, wide 3D keeps the normal x mapping while protected 2D uses:

```text
x' = x / K + c * (1 - 1 / K)
```

This preserves HUD, text and movie proportions inside full-screen and
split-screen views. Full-view fades, flashes and copies of an already-wide
scene need explicit classification rather than a blanket 2D rule. Marker
state is reset at every flip so a missing end marker is bounded to one
frame. The split-screen form is retained as future design context only;
split-screen implementation and validation are deferred. Current widescreen
work targets proven single full-screen views.

At flip, the shared GPU source keeps the wide dimensions and the presenter
fits that source aspect inside the client area. Guest memory, CPU captures
and guest-facing frame statistics continue to receive a guest-sized centre
crop, downsampled first when `S` is not one. The presenter's current
`gpu_present_output_size` query computes only a 4:3 rectangle, so native
wide sizing requires an aspect-aware framework API even though the shared
presenter already respects submitted source dimensions. It is not possible
to reconstruct a wide output from the returned 4:3 rectangle: the synthetic
presenter fixture shows that 1280x960 and 1600x960 clients both report
1280x960, while their fitted 16:9 pictures would be 1280x720 and 1600x900.
Their 64:27 pictures would likewise be 1280x540 and 1600x675.

The requested aspect alone is not an authority for the displayed frame.
The presenter can reject a shared frame because it is unavailable, stale,
blocked on its keyed mutex, disallowed by CPU/PVIDEO composition or a pinned
framebuffer, or incompatible with capture. It may then use the CPU picture,
or fail the presentation path. A wide projection therefore also requires
an ordered frame/resize-epoch and fallback contract that prevents a stale
or rejected wide picture from being treated as an accepted wide frame.
No such Hor+ contract is implemented.

### Textures and caching

[texture_decode.c](../src/gpu/texture_decode.c) and the backend handle
linear, swizzled, paletted and compressed layouts within their supported
formats. Eligible DXT blocks are uploaded as BC textures, eligible linear
32-bit colour data can be uploaded directly, and common formats use compute
decoding. A CPU decode path remains available as a fallback and for
comparison. By default the renderer exposes level 0 only, while retaining
the guest's declared mip padding for cube-face spacing.

Render-target and enabled depth/stencil aliases are checked before the
ordinary texture cache. For ordinary successful textures, checked copies
snapshot the source and, for P8, the programmed palette on each use, and the
default cache compares those bytes with its retained snapshots. Tracked
heap/contiguous allocation identities and the mapping generation are checked
around each copy and are part of cache validity. Known-short ranges and failed
copies, including guarded or reprotected pages, are rejected; checked reads
do not clear a guard flag. The CPU texture-decoding fallback uses the same
immutable input snapshot rather than rereading guest memory.
The v0.18 framework rebase preserves the upstream heap split/address ordering
and contiguous-page protection rules while tracking those identities. This
has synthetic coverage. Bounded audio-on CPU and D3D11 runs reached the same
Basic Controls state on the regenerated retail target; their stable 640x480
captures differed by more than 8/255 in 677 pixels (0.2204%). That closes the
base-upgrade smoke gate, not movement equivalence, full-game ownership, G4, or
performance.
Retained snapshots have a 128 MiB budget; transient copies, decoded buffers
and GPU resources are outside that budget. Evicted snapshots require another
upload. The approximate cache is opt-in and can miss same-frame or sparse
rewrites, leaving stale textures. Exposed mip chains always use exact
snapshots even when approximate caching is requested; declared and uploaded
level counts participate in their cache identity.

Windows `MEM_WRITE_WATCH` cannot replace these exact checks in the current
framework memory model. Main RAM, its mirrors, contiguous memory and the tiled
aperture are file-mapped alias views; `GetWriteWatch` only accepts regions
reserved by `VirtualAlloc` with write watching enabled. Page protection is
also per virtual view and cannot prove that every alias stayed clean. No
dirty-page shortcut or `RECOMP_*` switch is implemented.

The 2,048-entry cache uses a 4,096-bucket exact descriptor index rather than a
linear scan. Buckets chain collisions and still compare the complete
descriptor key before reuse. Rewrites, failed-upload discards, compacting
entry moves and LRU replacement update the index explicitly; an index
invariant failure stops instead of falling back to an unverified entry.
Indexing only selects a candidate and never replaces the checked source,
palette, allocation-lifetime or exact-byte validation above.
`RECOMP_GPU_TEX_INDEX=0` is a diagnostic measurement reference that restores
the complete linear descriptor scan and disables index maintenance; unset or
`1` uses the index. It does not relax any descriptor, lifetime, snapshot or
byte comparison, and it is not a compatibility fallback.

P8 binds validate sampled indices against the programmed 32-, 64-, 128- or
256-entry palette. Framework texture metadata carries this length into both
backend interfaces and the CPU reference; zero on manually constructed
metadata retains the legacy 256-entry default. The whole-texture CPU decoder
rejects invalid indices before changing its output; per-pixel CPU sampling
uses its existing failed-sample fallback. Snapshot decoders reject
input/output overlap before writing. The policy is deterministic, not a claim
about NV2A hardware.

An untracked suballocation's logical extent cannot be inferred from its host
pages or parent heap block. Framework owners may register non-overlapping
logical ranges and must unregister them before reuse; parent allocation reuse
or mapping reset invalidates their identity, including registrations through
mirrored RAM and tiled contiguous views. Default-off
`RECOMP_GPU_TEXTURE_BOUNDS=1` requires such registrations for texture and
palette inputs, including eligible exact colour aliases, and tracks any
registered colour-surface lifetime so logical-range reuse cannot expose its
previous GPU contents. With it off, unregistered ranges retain legacy
treatment. Checked copies, protection and
allocation rechecks detect the tested changes but cannot make concurrent
guest writes transactional or replace an ordered write-notification contract.

The legacy exact `0x06` swizzled-colour and `0x2E` compatibility aliases are
retained for their supported cases; the latter applies only with the
experimental depth-texture path off. They are backend compatibility behavior,
not proof that those reinterpretations match hardware. Other GPU-dirty
overlaps are rejected conservatively, including recognized physical aliases
through RAM and tiled apertures. A freed tracked surface cannot fall back to
its stale guest bytes, and a GPU-owned surface with a short row pitch is not
treated as an exact alias. A logical colour-layout replacement remains usable
for rendering, but an alias is rejected until a full clear or readback when
its old GPU-owned contents or conflicting writes make ownership uncertain.
Scaled migrations retain GPU ownership and mark observed migration races
uncertain. A clean old surface with changed guest bytes is seeded from the
current guest snapshot.

Full colour-clear mirrors and GPU readback also preflight the guest surface
and recheck its identity. Windows exception-guarded writes respect read-only
protection and leave row padding untouched. An already-guarded surface is
rejected by the checked read without consuming the guard. A failed or partial
mirror leaves GPU ownership unresolved; a failed readback or a detected
post-copy write rejects aliases and stops the process before reporting the
tracked flip as guest-visible. Before a failed flip, a successful full clear
or readback can resolve the ambiguity. These checks are not
atomic: a concurrent reprotection after the preflight can still consume a
newly set guard or leave a partial write.

#### Experimental mipmaps and anisotropy

`RECOMP_GPU_MIPS=1` enables the separately validated D3D11/Vulkan mip path;
`0` or unset retains the previous level-zero sampler. It constructs checked
2D or six-face layouts for swizzled, P8 and BC data, with each mip at its
guest offset. Both backends expose no more levels than the natural chain
through 1x1; declared extra 1x1 storage still determines the
128-byte-aligned cube-face stride.
This spacing is a compatibility assumption, not proven for malformed
declarations on NV2A hardware. The checked span includes the complete last
exposed face and level, including face gaps. Dimensions, arithmetic and a
64 MiB source-span limit are checked before copying. The opt-in path checks
readability and the remaining size of tracked allocations on each bind; it
falls back to level zero only if that layout is readable and the cube stride
remains compatible, otherwise it rejects the texture. P8 validates every
exposed index against the programmed, separately checked palette range.
Nonlinear multi-level declarations use exact cache snapshots even if an
incomplete chain falls back to level zero and approximate caching is enabled.
Malformed non-power-of-two swizzles are rejected on the mip path. With mips
off, the legacy CPU decode remains available only when its Morton indices
stay within the old level-zero source span; unsafe declarations are rejected.

Linear and packed-YUV textures and render-target aliases expose only level
zero. Depth/stencil aliases support only single-level declarations and
reject multi-level ones; none has generated mipmaps. On opt-in, packed
YUV bounds include a complete pair even for odd widths. D3D11 GPU-compute,
direct BC and per-level CPU-decoded uploads create all exposed D3D
subresources and the matching 2D/cube SRV. Vulkan decodes each exposed
face/level from the same immutable snapshot, uploads one tightly packed copy
region per subresource and creates one multi-level 2D or cube image. Failure
or approximation paths report bounded `[GPU-MIPS]`, `[GPU-D3D11]` or
`[GPU-Vulkan]` diagnostics.

The sampler maps minification, magnification and mip filtering separately.
Non-mip minification remains at level zero; unknown filters use point and
convolution filters approximate linear, with diagnostics. For mip filters,
the implementation interprets CONTROL0's 12-bit min/max fields as unsigned
4.8 values, bounds them to exposed levels and collapses inverted ranges to
the maximum. The filter's signed 13-bit bias is divided by 256 and clipped
to the selected API/device limit. This 4.8 interpretation follows NV30 driver
evidence, but differs from xemu v0.8.139's raw-integer clamp handling and is
not yet validated against NV2A hardware. See
[FINDINGS](./FINDINGS.md#experimental-mipmap-sampling-synthetic-evidence-2026-11-11).

`RECOMP_GPU_ANISO=1` is a separate, default-off D3D11-only 8x enhancement,
active only when mips are enabled and both spatial filters are linear with a
usable mip range. D3D11 anisotropy can also change the underlying mip
filtering. Full effective D3D11 sampler descriptors, including LOD clamps and
bias, key its sampler cache; Vulkan currently creates bounded transient
samplers per draw. The CPU reference still samples level zero, so enabled
mips and anisotropy are intentional image differences, not proof of G4
equivalence.

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
  filterable depth bytes and stencil alpha, reusing the result until a
  depth/stencil-writing clear or potentially writing draw invalidates it.
  Draw states with no reachable depth/stencil writes do not advance the
  revision. A potentially writing draw can still produce no fragments, so
  this is conservative state-based invalidation, not pixel-level proof.
  Partial and ambiguous overlaps, including recognized RAM/tiled aliases,
  spans crossing aperture boundaries, and unsupported texture formats over
  tracked depth, skip the draw with a diagnostic rather than decode stale
  guest bytes. Ordinary non-alias `0x41` textures keep their normal path.

Both options are off by default. Checked snapshots detect many writes, but
same-value writes, concurrent writes after a check, unregistered
suballocations and guest reads of GPU-owned contents remain unresolved.
Conflicting color/depth owners and partial aliases are rejected; depth cubes
and multi-level depth aliases are unsupported. The CPU reference cannot
decode `0x2E`: it samples `0x41` from guest bytes, uses a separate float depth
buffer and does not model stencil. Audit binding rows do not prove that a
texture was sampled or affected visible pixels. With depth aliasing enabled
but packed-stencil aliasing disabled, a `0x41` texture over tracked depth is
rejected instead of reading possibly stale guest bytes; with both switches
off the legacy CPU/GPU reference behavior is unchanged. These implementations
have synthetic coverage but have not established complete gameplay equivalence.
One native 640x480 xemu Basic Controls match now gives
the packed-stencil option direct title evidence: with every other diagnostic
control fixed, enabling it reduced pixels differing from xemu by more than
8/255 from 43.4320% to 3.7119% and mean channel error from 10.5953 to 1.2485.
That establishes the matched bright-pass mechanism, not every depth alias or
scene. Scaled modes reject both alias options. An experimental exact scaled
packed `0x41` path passed a scale-two glare replay and logical/physical-size
fixtures, but a subsequent 4.5x retail check showed the player absent in two
alias-on boots and visible in the otherwise matched alias-off spawn control.
The renderer and launcher therefore fail closed instead of exposing that
path. The internal pass that removed the model is unresolved; the retail
result overrides the narrower synthetic success.

### Vertex processing and pixel combiners

The executor can transform vertices on the CPU or send eligible
vertex-program batches to D3D11 or Vulkan. [vsh_hlsl.c](../src/gpu/vsh_hlsl.c)
translates NV2A programs to HLSL using the framework interpreter as its
semantic reference. Compatible attribute streams can be submitted as guest
bytes; otherwise the executor gathers float inputs. Programs that write
constants remain on the CPU. New GPU programs compile on a worker; batches
fall back to CPU transformation until their shader is ready. Optional
`RECOMP_GPU_VSH_VERIFY=<n>` reads back every nth GPU-program batch for
comparison with the interpreter and is a slow diagnostic.

Vulkan uses a different compilation strategy under the same `draw_vsh`
contract: one pinned, static interpreter shader consumes uploaded instructions
and gathered float inputs. It does not advertise raw-stream support, compile
per-program shaders, or add a worker dependency. Its compute verifier returns
the same `Nv2aXVsOut` layout to the framework comparison. NVIDIA and AMD
fixtures cover a known MOV program, exact position/color output, non-finite
primitive rejection, and readback.

Fixed-function batches are still transformed by the executor on the CPU
before D3D11 receives them. The executor also rejects primitive types below
triangles. One partial retail audit recorded 174 line-list requests and no
point requests; the latter does not prove points are absent elsewhere.
Blood Wake v0.3.1 is current prior art for optional raw fixed-function batches
and GPU-expanded point sprites, with CPU fallbacks and readback verification.
It does not handle the observed line-list gap. Any adaptation must extend the
existing backend contract rather than replace Brute Force's ownership model,
keep the CPU path working, and first establish fixed-function cost or visible
point use.

The default-off request audit reports CPU-emitted triangles after clipping
separately from GPU pre-clip submitted triangles; neither count proves pixel
visibility. Dropped requests retain primitive/input counts and at most eight
recent unhandled or mirror-only method events, expired after 256 method events
and cleared at flips. Those associations are temporal clues, not proven causes.
GPU-program frame traces do not display old CPU-transformed vertices; any
CPU-transformed vertices shown are pre-clip. No line/point behavior was added
without independently verified NV2A semantics.

[rc_hlsl.c](../src/gpu/rc_hlsl.c) generates a pixel shader from the texture
stages and register-combiner configuration, including up to eight general
combiner stages and the final combiner. It follows the current CPU
implementation, including known gaps: bump-environment and dot-product
stage modes read zero. Texture sampling is level zero by default; the
experimental mip path above lets the D3D11 sampler select exposed levels.
Colours and fog
are interpolated affinely while texture coordinates are perspective-correct.
The backend caches shader and D3D11 state objects.

The optional persistent **combiner pixel-shader** cache is separate from the
texture cache. `RECOMP_GPU_PSCACHE=1` stores validated bytecode under the data folder's
`cache\shader-cache` when launched through `build.py` or a launcher, and
under `%LOCALAPPDATA%\BruteForce\shader-cache` when the executable is started
directly, keyed by generated
source, compiler binary and compilation inputs. It is off by default; other
shader types are not covered. Cache errors are logged and compiled shaders
can still run. See the [launcher and cache findings](./FINDINGS.md#launcher-and-pixel-shader-cache).
Blood Wake v0.3.1 additionally precompiles vertex variants observed through
its title-specific shader-creation wrapper during loading. That is a
discovery and cache-warm-up pattern, not directly reusable addresses or
shader lists for this title.

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
original-hardware validation remain incomplete. Synthetic CPU, WARP and
hardware fixtures cover rectangle unions/exclusions, state changes, AA shifts,
scale-two mapping, vertex paths, clears and depth/stencil discard side effects.
The stencil case is checked on GPU separately because CPU stencil is
unmodeled. The window-clip switch remains off by default.

### Visibility reports

The title uses visibility counts to decide what to draw. D3D11 and Vulkan map
these to occlusion queries; the default synchronous report waits for completed
queries at `GET_REPORT`. `RECOMP_GPU_ZPASS=async` instead queues exact,
ordered reports and polls on the NV2A thread, writing results to guest memory
only when the owning submission is complete. Vulkan uses a 1,024-report ring,
may share a submission across report boundaries, and flushes an unsubmitted
report batch after 0.25 ms. Its completion status is published only after a
memory barrier. Bounded queues can still wait when full; no result is skipped
or fabricated. Async reporting changes timing and is experimental despite
synthetic cross-vendor coverage. The null probe reports geometry visible so
it does not accidentally measure a scene culled by missing results. Scaled
D3D11 queries are normalized back to guest-pixel counts in both synchronous
and asynchronous paths.

## Flip, readback and presentation

At `FLIP_STALL`, the executor identifies the completed colour surface from
the last drawn buffer, which may differ from the next buffer in current
state. At scale 1, the D3D11 readback callback copies that surface to a
staging resource. At another scale, it first resolves the physical surface
into a guest-sized GPU target. It then maps the staging resource and copies
guest-sized rows to memory before the normal framebuffer handoff and flip
statistics. A tracked surface that cannot be made guest-visible now stops
with a fatal diagnostic rather than completing a success-shaped flip.
Failure or cache exhaustion for a required depth-stencil, blend or sampler
state also stops before the draw instead of substituting D3D defaults.

Vulkan performs the same mandatory guest-visible handoff but first reconciles
its per-surface shadow. Clean guest changes are retained and uploaded before
later GPU work; dirty guest/GPU conflicts are reported and the completed GPU
frame wins. Full clears mirror their logical pixels into guest memory so an
early clear cannot later erase CPU/PVIDEO movie output merely because a
tracked image exists. This fixes the observed black manual menu route, but
does not bypass the title's audio dependency: `RECOMP_AUDIO=0` skips the
opening Bink movies. The executor's ignored color-DMA object base remains
unresolved.

The default-off `RECOMP_GPU_SKIP_CLEAN_READBACK=1` elides that copy only for
an unscaled, native-sized 32-bit surface whose guest bytes are already known
to match its clean GPU image. Initial seeds, guest uploads, GPU draws, partial
clears and migration require the ordinary readback before establishing that
proof. Colour shadows retain the guest bytes used for a seed or upload,
expected clear bytes, or GPU staging bytes after readback. An observed
guest-write race is treated as a conflict rather than silently publishing a
stale surface alias. Full-clear mirrors and readbacks validate their snapshots
against those expected bytes, and the guest snapshot is checked again after
the GPU wait.
Statistics, frame-dump and presenter-capture settings force the ordinary
readback. Screenshots and the CPU-composited route still use guest memory. A
tracked surface flip, including an elided copy, waits for prior GPU work; this
does not intercept arbitrary guest readers or prove all concurrent guest-write
timings.

The current consumer inventory is:

| Consumer | Pixel source and evidence | Readback boundary |
|---|---|---|
| Flip publication (`fb_publish`) | The executor calls the back-end readback, then copies the completed guest colour surface into the window's two-slot CPU snapshot on the NV2A thread. This copy still occurs when a shared GPU frame is eligible. | Requires guest-visible colour pixels under the current framework contract. |
| GDI, SDL and D3D11 CPU-upload presentation | Convert or upload the published CPU snapshot. | Requires a CPU pixel snapshot; today that snapshot comes from guest memory. |
| Shared D3D11 presentation | The back end publishes the finished GPU texture before staging readback, and a frame ID prevents stale replacement. | The shared route itself does not need guest pixels, but the executor still performs readback and CPU flip publication. |
| PVIDEO and pinned framebuffers | PVIDEO snapshots a separate guest YUY2 bank and composites it over the CPU base image; pinned frames also follow the CPU path. Both reject shared presentation. | Requires the base colour image to be CPU-visible; PVIDEO's separate source is guest memory too. |
| Flip dumps and frame statistics | `dump_surface_bmp` and `nv2a_frame_hash` read the completed guest colour surface after the flip readback. | Diagnostic settings force ordinary readback. |
| Window BMP dumps and presenter-source capture | Window dumps serialize the converted CPU image. Presenter capture rejects direct shared presentation and binds captured pixels to the frame-statistics identity. | Requires the CPU/readback route and, for presenter capture, valid guest pixels and statistics. |
| Render-to-texture and visibility reports | Colour aliases sample the GPU render target directly; D3D11 visibility reports retire query values into their report records. | Do not require whole-surface guest readback. |
| Title CPU readers | The current-generation 2026-11-11 rerun resolved two reads at `0x80000784` to generated title function `sub_003855B3+0x35C`. The accumulated `0x80000000..0x800FFFFF` colour range also contains staged pushbuffer reads and many writes, so the guard cannot prove that the range still had live render-target ownership. No depth read recurred. Page `0x80001000` remained excluded. | General readback removal remains unsafe without live lifetime tracking and arbitrary guest-access interception. |

The current-generation rerun closes the planned diagnostic collection but
does not prove an absence or presence of live title surface reads. A future
on-demand contract needs a
metadata-only shared flip publication, an explicit CPU snapshot request for
fallback and diagnostics, and a way to synchronize arbitrary guest reads and
writes. It cannot infer safety merely from shared-presentation eligibility.

The default window path snapshots and converts guest pixels and can compose
PVIDEO before presentation. The Windows presenter normally uses a separate
D3D11 swap chain and can fall back to GDI. With
`RECOMP_GPU_DIRECT_PRESENT=1`, an eligible tightly pitched 32-bit surface
can also be resolved into a keyed-mutex shared texture for the D3D11
presenter. Patch 0028 lets that texture have dimensions independent of the
guest framebuffer and exposes the current 4:3 output-picture size to the
renderer. Pinned framebuffers and PVIDEO use the CPU-composited path, and
snapshot IDs prevent an older pending GPU image from replacing a newer frame.
The one-slot shared snapshot is separate from the renderer's two-event
submitted-work bound. The latter checkpoints after at most 128 renderer
operations or at a flip and applies backpressure before submitting more;
it also polls ordered visibility reports while waiting. Neither bound
shortens PFB flush, semaphore, report or flip-hold synchronization.

The independent experimental per-PUT queue is upstream of this renderer:
the producer publishes a captured PUT, and the NV2A worker walks that
segment before publishing GET. A pending queue or held flip normally prevents
PFB acknowledgement; a held flip also prevents starting the next segment.
There is one narrower dependency exception: while the executor waits for a
guest PGRAPH software-trap acknowledgement, PFB may acknowledge so the guest
can leave its poll and run the ISR. This does not advance GET, consume the
queued segment, clear PGRAPH interrupt state or release a held flip. Without
that exception, the queue and synchronous trap wait deadlocked during retail
tutorial loading. The read-only guest USER mapping rejects untracked writes,
while framework GET/PUT updates use its writable alias. The 128-entry queue
and five-second wait limit fail explicitly instead of dropping work.
Synthetic marker tests cover this ordering boundary. One fixed-tree
segment-only and one all-three retail smoke reached reviewed movement, but
matched CPU output and presentation parity remain unproven. The 24-run
flush/idle/segment/all-three A/B/A performance campaign is explicitly
deferred; the smokes are not performance evidence and all switches remain
default-off.

Window size and fullscreen determine native mode's output picture. The
point/linear display filter applies to the CPU route and scale-1 fallback;
the scaled GPU final pass owns its reconstruction. None of these controls
adds widescreen or unlocks the title's frame rate. Presentation VSync and
the optional guest flip-pacing mechanism are distinct. See
[USER_GUIDE.md](./USER_GUIDE.md#launcher) for current controls and
[PERFPLAN.md](./PERFPLAN.md) for unimplemented on-demand readback and pacing
work.

## Configuration and defaults

The following describes a fresh launcher configuration with no inherited
overrides to unmanaged settings. Saved launcher choices can differ.
[launcher.py](../tools/launcher.py) is authoritative for the current
setting-to-environment mapping.

| Setting | Fresh launcher default | Notes |
| --- | --- | --- |
| Renderer | D3D11 hardware | The launcher and graphical `build.py` runs select D3D11 unless `RECOMP_GPU` explicitly selects `vulkan`, `cpu` or `null`. Vulkan is a non-default bring-up path with CPU-transformed extended draws; CPU remains the correctness reference, and `null` is a probe. `RECOMP_D3D11_WARP=1` selects the D3D11 software device. |
| Internal resolution | 480p guest (`RECOMP_GPU_SCALE=1`) | Native, 2160p, 1440p, 1080p and 720p are available for D3D11. The CPU renderer remains at guest resolution. |
| Vertex programs / texture decoding | GPU / GPU where supported | CPU fallbacks remain; `RECOMP_GPU_VSH=0` and `RECOMP_GPU_TEX_DECODE=0` are comparison switches. |
| Guest mipmaps / anisotropy | Off / off | `RECOMP_GPU_MIPS=1` exposes supported supplied chains on D3D11 and Vulkan; `RECOMP_GPU_ANISO=1` separately enhances eligible D3D11 samplers. |
| Visibility | Synchronous | `RECOMP_GPU_ZPASS=async` is opt-in. |
| Clean readback elision | Off | `RECOMP_GPU_SKIP_CLEAN_READBACK=1` is an environment-only experiment; dirty and diagnostic frames retain readback. |
| Persistent pixel-shader cache | Off | `RECOMP_GPU_PSCACHE=1` enables the combiner cache. |
| Window clip / near plane | Off / HUD-safe | HUD-safe retains non-additive UI and clips zero-depth additive draws. Legacy/inherit and full screen-zero remain comparison modes. Face culling is off unless separately enabled. |
| Depth alias / stencil alias / shared presentation | Off / off / off at 480p | Scaled modes reject both alias options and enable shared presentation automatically. |
| Window / display scaling / VSync | Windowed / 2x linear / on | Display size is separate from a fixed internal-resolution preset; native mode follows its 4:3 picture. |

The launcher starts the tutorial configuration. To compare the renderers
from the command line after building, set `RECOMP_GPU` to `cpu`, `d3d11` or
the bring-up-only `vulkan` and run
`python tools\build.py run --experimental-tutorial`; keep all other settings
and the scene controlled. Full setup and options are in the
[user guide](./USER_GUIDE.md#d3d11-vulkan-and-benchmark-diagnostics).

## Diagnostics and validation

- The periodic `[GPU-D3D11]` report includes draws, texture paths and
  unsupported-cache hits, readback time and coherent-copy elisions, visibility
  waits, submitted-work batches/backpressure, alias activity and
  shared-presentation counts. `RECOMP_GPU_TEXLOG=1` logs individual
  texture uploads.
- Vulkan startup reports the selected device, vendor/device IDs, Vulkan and
  driver versions, queue family, DXC pin and validation state. Its periodic
  report counts flips, draws, clears, readbacks, surfaces and pipelines.
  `RECOMP_VULKAN_VALIDATION=1` requires the Khronos validation layer and
  debug-utils extension; any validation error fails the native fixture.
- `RECOMP_GPU_SHADER_DIAG=1` bounds vertex-program preparation reports to 32
  events. `RECOMP_GPU_VSH_VERIFY` caps skip reports at 16; the separate
  `RECOMP_NV2A_WAIT_DIAG=1` reports prolonged event-driven PFB waits at most
  eight times without advancing the handshake. All are off by default.
- `RECOMP_FRAME_STATS` records flip and whole-frame change statistics;
  `RECOMP_GPU_CENSUS` records requested features; `BRUTEFORCE_PROFILE`
  collects samples. [bench.py](../tools/bench.py) can coordinate these with
  scripted tutorial input. Captures and instrumentation can change timing.
- `RECOMP_GPU_AUDIT=1` records bounded route, primitive, post-clip and
  recent-method diagnostics; format-binding counts are not visibility.
  Movement collection and unexplained black/dark/solid/unchanged intervals
  are inconclusive until separately reviewed; see the
  [benchmark instructions](./USER_GUIDE.md#d3d11-vulkan-and-benchmark-diagnostics).
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
  mapping. It also checks mip layout, sampled filters and clamps, 2D/cube
  rewrites and guarded lower levels on WARP and hardware.
  [test_present.py](../tests/test_present.py) and
  [test_frame_stats.py](../tests/test_frame_stats.py) cover handoff,
  presentation and frame accounting. Fence and pushbuffer fixtures cover
  parts of the guest synchronization contract.

For example, after building and setting the native fixture paths as described
in [development tests](./DEVELOPMENT.md#tests):

```powershell
python -m pytest -q tests\test_d3d11.py tests\test_present.py tests\test_frame_stats.py
```

Without the corresponding `BRUTEFORCE_TEST_*` fixture variables, native
tests are skipped. Set `BRUTEFORCE_TEST_D3D11_HARDWARE=1` for hardware-device
coverage in addition to WARP. Synthetic tests need no game data and do not
replace matched gameplay or original-hardware validation.

## Current limits

- Vulkan now has validation-clean base and CPU-transformed extended draws,
  gathered-input GPU execution of eligible NV2A vertex programs with
  verification readback and non-finite triangle rejection,
  exact 2D/cube texture cache/rewrite tracking, four texture stages, register
  combiners, compatible color render-target aliases, precise visibility and
  default-off supplied mip chains, and an integrated retail draw/flip/readback
  capture through the existing CPU presenter. Its default-off native path now
  has a real Win32 Vulkan surface and swap chain, bounded source ownership,
  fitted scaling, resize, borderless-fullscreen enter/restore, VSync, CPU
  fallback, fatal surface loss and final composed-image pixels in synthetic
  cross-vendor tests. One retail native run reached visible Basic Controls.
  Two audio-off
  observations skipped the opening movies; a bounded audio-on trace opened
  both Bink files and produced changing CPU-owned native-route frames. It
  still lacks physical movie review and direct raw guest-stream vertex
  fetch. Fixed numeric and native-mode render scaling pass synthetic
  fixtures on both vendors but have no retail run. Surface
  layout changes after creation fail rather
  than migrate. In one matched Basic Controls capture, 134 of
  307,200 pixels
  (0.0436%) exceed 8/255 versus D3D11; mean channel error is 0.374562 and the
  maximum is 29. That closes the first stationary full-pixel-pipeline
  comparison. Component review places all 134 over-8 pixels in exposed
  animated world content around the opaque tutorial panel; three opaque panel
  regions have none and differ by at most six channel levels. The exact cause
  of those subthreshold cross-API numeric differences remains unisolated.
  This is not movement/movie/fade parity, native presentation, stability, or
  a performance gate.
- The previous uninstrumented Vulkan diagnostics isolated gameplay intervals
  around 8.960-11.240 flips/s. A later single legacy-route GPU-VSH diagnostic
  reported 26.391 stationary and 38.862 movement flips/s, and a caller-profiled
  repeat reported 26.683 and 38.503. These observations show that CPU vertex
  interpretation was the immediate Vulkan throughput blocker, but the route
  and frames remain unreviewed and the runs are not alternating A/B/A or
  distinct-world-frame evidence.
- A later Vulkan-only counter run reported 32.558 movement flips/s, rebounding
  from 17.469 in the reduced renderer pair and confirming that neither
  single-run rate is stable performance evidence. Line-order alignment selects
  the 300-flip intervals ending at 8,400, 8,700 and 9,000 as wholly inside
  movement. Their inclusive CPU timings place exact texture snapshots at
  17.59% of wall time, mandatory readback at 13.54%, asynchronous report
  polling at 11.97%, and backend gathered-vertex copies at 0.56%. Report
  completion and fence polling are nested within report paths; submission
  waits can be nested in multiple callers. Detailed timing itself adds
  overhead. The resulting actionability guard now defers only an unsubmitted
  report before its existing flush deadline. Cross-vendor fixtures verify
  that the early poll does not submit or publish and that the aged report
  later completes with its exact ordered value. This is not skipped
  completion or a retail performance claim. Exact texture snapshot elision
  was reviewed and deferred: current checked copies preserve guard-page,
  mapping and allocation-lifetime behavior, while Windows write-watch cannot
  track the aliased file mappings. The default-inactive framework provider
  seam passes lifecycle/concurrency and presenter-capture fixtures. Its
  downstream WSI consumer passes exact-frame native routing, READY-source
  coalescing, forced CPU fallback, resize and explicit shutdown with Vulkan
  validation enabled on NVIDIA and AMD. Integrated retail presentation reaches
  Basic Controls; synthetic fullscreen enter/restore, final composed-image
  readback and fail-stop surface loss also pass on both vendors. Physical
  fullscreen/movie review and real device-loss behavior remain open.
- One native xemu Basic Controls scene is matched: the packed-stencil option
  reduces the large whole-frame brightness difference, while its unsupported
  ownership cases and title-wide behavior keep G4 open. Sun-facing, fade and
  movement equivalence remain open. The attempted scaled extension is
  disabled because it removed the player model in the matched retail spawn
  check despite passing the narrower synthetic fixture. Split-screen
  equivalence is explicitly
  deferred and is not a current G4 gate. The CPU reference has its own known
  stencil gap.
- True widescreen remains an unimplemented, default-off workstream. It needs
  coordinated title projection/culling, ordered 2D classification, selective
  target widening, centred guest readback and aspect-aware presentation;
  changing the framebuffer dimensions alone is not an acceptable mode. The
  current scope is single-view; split-screen widening remains unsupported and
  deferred.
- Guest reads and writes of GPU-owned surfaces, concurrent texture changes,
  generalized fence completion, and general on-demand readback need further
  investigation. A clean surface-guard diagnostic alone is insufficient to
  elide readback for a GPU-dirty surface.
- Device removal is reported and the renderer stops rather than claiming
  outstanding GPU work completed. The separate Windows presenter likewise
  stops on device loss or unrecoverable runtime resize, present or shared
  ownership errors. Initial setup and ordinary shared-open failures retain
  their safe fallback paths. Full device/resource recovery and real-driver
  loss races are not established. Blood Wake's newer recovery worker is prior art, but it does
  not by itself define how Brute Force should settle pending PFB flush,
  semaphore, report, flip, shared-present, and guest-readback work.
- Fixed and native resolution controls and mip sampling have synthetic
  coverage plus one integrated 4.5x movement bring-up. Representative gameplay
  frames were valid, but the observed tutorial texture declared no lower mip
  levels and the run was neither matched nor continuously reviewed.
  Performance, VRAM headroom, broader title mip/LOD behavior and native 4K
  display output remain unvalidated. The launcher keeps 480p as its default.
- A later Vulkan mip probe took the legacy script's wrong Join Deathmatch
  route and was stopped. It uploaded one supplied six-level format-`0x0E` BC
  chain without a Vulkan validation error, proving integrated title data
  reached the path but not scene correctness, movie playback, image parity or
  performance. The script deliberately sent A presses through the intro
  movies; their absence on screen in that run is not a renderer conclusion.

Keep disc content, generated game code, screenshots, runtime captures and
logs local. Use synthetic fixtures for shareable reproduction. Current
priorities are in [NEXTSTEPS.md](./NEXTSTEPS.md).
