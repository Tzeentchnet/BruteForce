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
    |-- D3D11 backend --> GPU colour --flip sync/readback--> guest memory --> window
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
| Bounded PFB acknowledgement, idle waiting and guarded PUT segments | [patch 0045](../patches/xboxrecomp/0045-bounded-nv2a-segments-and-waits.patch), [recomp_manual.c](../src/recomp_manual.c) |
| Extended backend interface and vertex-program batches | [patch 0016](../patches/xboxrecomp/0016-extended-render-backend.patch), [patch 0017](../patches/xboxrecomp/0017-gpu-vertex-programs.patch) |
| D3D11 device, surfaces, state, draws, queries and readback | [d3d11_backend.c](../src/gpu/d3d11_backend.c) |
| Render-scale authority and GPU seed/readback/final resolves | [render_scale.c](../src/gpu/render_scale.c), [render_scale.h](../src/gpu/render_scale.h) |
| Texture layout, decode and sampling | [texture_decode.c](../src/gpu/texture_decode.c), [texture_sampler.c](../src/gpu/texture_sampler.c), [d3d11_backend.c](../src/gpu/d3d11_backend.c) |
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
Retained snapshots have a 128 MiB budget; transient copies, decoded buffers
and GPU resources are outside that budget. Evicted snapshots require another
upload. The approximate cache is opt-in and can miss same-frame or sparse
rewrites, leaving stale textures. Exposed mip chains always use exact
snapshots even when approximate caching is requested; declared and uploaded
level counts participate in their cache identity.

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

`RECOMP_GPU_MIPS=1` enables the separately validated mip path; `0` or unset
retains the previous level-zero sampler. It constructs checked 2D or six-face
layouts for swizzled, P8 and BC data, with each mip at its guest offset.
D3D11 can expose no more levels than the natural chain through 1x1; declared
extra 1x1 storage still determines the 128-byte-aligned cube-face stride.
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
YUV bounds include a complete pair even for odd widths. GPU-compute, direct
BC and per-level CPU-decoded uploads create all exposed D3D subresources and
the matching 2D/cube SRV. Failure or approximation paths report bounded
`[GPU-MIPS]` or `[GPU-D3D11]` diagnostics.

The sampler maps minification, magnification and mip filtering separately.
Non-mip minification remains at level zero; unknown filters use point and
convolution filters approximate linear, with diagnostics. For mip filters,
the implementation interprets CONTROL0's 12-bit min/max fields as unsigned
4.8 values, bounds them to exposed levels and collapses inverted ranges to
the maximum. The filter's signed 13-bit bias is divided by 256 and clipped
to D3D11's representable positive limit. This 4.8 interpretation follows
NV30 driver evidence, but differs from xemu v0.8.139's raw-integer clamp
handling and is not yet validated against NV2A hardware. See
[FINDINGS](./FINDINGS.md#experimental-mipmap-sampling-synthetic-evidence-2026-11-11).

`RECOMP_GPU_ANISO=1` is a separate, default-off 8x enhancement, active only
when mips are enabled and both spatial filters are linear with a usable mip
range. D3D11 anisotropy can also change the underlying mip filtering. Full
effective sampler descriptors, including LOD clamps and bias, key the sampler
cache. The CPU reference still samples level zero, so enabled mips and
anisotropy are intentional image differences, not proof of G4 equivalence.

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
scene. Scaled modes reject both alias options explicitly because their
guest-layout seed, snapshot and packed sampling semantics have not been
defined for physical-resolution depth.

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
texture cache. `RECOMP_GPU_PSCACHE=1` stores validated bytecode under
`%LOCALAPPDATA%\BruteForce\shader-cache` by default, keyed by generated
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
statistics. A tracked surface that cannot be made guest-visible now stops
with a fatal diagnostic rather than completing a success-shaped flip.
Failure or cache exhaustion for a required depth-stencil, blend or sampler
state also stops before the draw instead of substituting D3D defaults.

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
| Title CPU readers | The completed 2026-10-29 guard run observed one non-NV2A colour read that remains unsymbolized. Seven depth-range reads are provisionally attributed to USB rumble handling, not a title depth consumer. One page was excluded. | Unresolved; general readback removal remains unsafe. |

This closes the static framework-consumer inventory, not the dynamic
title-reader investigation. A future on-demand contract needs a
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
| Renderer | D3D11 hardware | The launcher and graphical `build.py` runs select D3D11 unless `RECOMP_GPU` explicitly selects `cpu` or `null`. CPU remains the correctness reference; `null` is a probe, not a renderer. `RECOMP_D3D11_WARP=1` selects the D3D11 software device. |
| Internal resolution | 480p guest (`RECOMP_GPU_SCALE=1`) | Native, 2160p, 1440p, 1080p and 720p are available for D3D11. The CPU renderer remains at guest resolution. |
| Vertex programs / texture decoding | GPU / GPU where supported | CPU fallbacks remain; `RECOMP_GPU_VSH=0` and `RECOMP_GPU_TEX_DECODE=0` are comparison switches. |
| Guest mipmaps / anisotropy | Off / off | `RECOMP_GPU_MIPS=1` exposes supported supplied chains; `RECOMP_GPU_ANISO=1` separately enhances eligible mipped samplers. |
| Visibility | Synchronous | `RECOMP_GPU_ZPASS=async` is opt-in. |
| Clean readback elision | Off | `RECOMP_GPU_SKIP_CLEAN_READBACK=1` is an environment-only experiment; dirty and diagnostic frames retain readback. |
| Persistent pixel-shader cache | Off | `RECOMP_GPU_PSCACHE=1` enables the combiner cache. |
| Window clip / near plane | Off / HUD-safe | HUD-safe retains non-additive UI and clips zero-depth additive draws. Legacy/inherit and full screen-zero remain comparison modes. Face culling is off unless separately enabled. |
| Depth alias / stencil alias / shared presentation | Off / off / off at 480p | Scaled modes reject the aliases and enable shared presentation automatically. |
| Window / display scaling / VSync | Windowed / 2x linear / on | Display size is separate from a fixed internal-resolution preset; native mode follows its 4:3 picture. |

The launcher starts the tutorial configuration. To compare the renderers
from the command line after building, set `RECOMP_GPU` to `cpu` or `d3d11`
and run `python tools\build.py run --experimental-tutorial`; keep all other
settings and the scene controlled. Full setup and options are in the
[user guide](./USER_GUIDE.md#d3d11-renderer-and-benchmark-diagnostics).

## Diagnostics and validation

- The periodic `[GPU-D3D11]` report includes draws, texture paths and
  unsupported-cache hits, readback time and coherent-copy elisions, visibility
  waits, submitted-work batches/backpressure, alias activity and
  shared-presentation counts. `RECOMP_GPU_TEXLOG=1` logs individual
  texture uploads.
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
  [benchmark instructions](./USER_GUIDE.md#d3d11-renderer-and-benchmark-diagnostics).
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

- The reported roughly 6 FPS during movement with default launcher settings
  has not been explained or shown fixed by a controlled, matched benchmark.
  The final cache-fix binary was not rebenchmarked. Earlier whole-frame
  change rates are not validated world FPS.
- One native xemu Basic Controls scene is matched: the packed-stencil option
  reduces the large whole-frame brightness difference, while its unsupported
  ownership cases and title-wide behavior keep G4 open. Sun-facing, fade and
  movement equivalence remain open. Split-screen equivalence is explicitly
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

Keep disc content, generated game code, screenshots, runtime captures and
logs local. Use synthetic fixtures for shareable reproduction. Current
priorities are in [NEXTSTEPS.md](./NEXTSTEPS.md).
