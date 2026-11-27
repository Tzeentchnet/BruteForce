# Status

What works, what is experimental, and what remains open. The
[README](../README.md) has the quick start.

This is an experimental Windows release for the North American retail game:

- Direct3D 11 is the current default renderer; the CPU reference remains
  available. An explicit, non-default Vulkan 1.3 bring-up backend now covers
  the base and CPU-transformed extended draw contracts, four texture stages,
  register combiners, cube maps, default-off supplied mip chains, exact
  texture caching, compatible color render-target aliases, precise visibility,
  depth/stencil, opt-in window clipping, fixed numeric and native render
  scaling (first retail runs complete, no matched comparison yet),
  bounded submission, RGB565 conversion, and mandatory guest readback. An additional default-off host-owned Vulkan presentation path now
  has a bounded completed-image queue, Win32 surface and swap chain, resize,
  aspect-fit blit, VSync selection, and required CPU fallback. Eligible
  vertex-program batches now run through a static Vulkan interpreter from
  gathered float inputs; raw guest-stream fetch remains on the framework CPU
  path. At the captured Basic Controls state, 134 of 307,200 pixels exceed
  8/255 versus the matched D3D11 frame.
  Review places those sparse pixels in animated world content visible around
  the tutorial panel; three opaque panel regions have no pixels over 8/255.
  NVIDIA and AMD fixtures cover the interpreter, whole-triangle non-finite
  rejection, verification readback, inclusive/exclusive and AA-scaled window
  clipping, CPU/PVIDEO surface preservation and dirty-conflict handling. The formerly black manual menu route now reaches
  visible Basic Controls. Complete guest-surface review of one matched
  movement pair shows the same tutorial scene and scripted camera route with
  zero unknown frames; continuous capture prevents treating its rates as a
  performance comparison. A later no-capture same-build diagnostic required
  only one manual run per renderer: D3D11 reported 26.409 stationary and
  48.193 movement flips/s, while Vulkan reported 23.164 and 17.469. Inputs
  matched within 78 ms, but the frames remain unreviewed and one pair is not
  A/B/A or distinct-world-frame evidence. The native path is validation-clean
  in its NVIDIA and AMD hardware fixture, including borderless-fullscreen
  enter/restore, fail-stop surface loss, and exact composed-image checks for
  native and CPU routes. One operator-confirmed retail
  run reached visible Basic Controls through a 640x480 FIFO swap chain with
  validation enabled. Both movie observations had disabled audio; a bounded
  no-script audio-on trace subsequently opened both Bink logo files and kept
  changing CPU-owned movie frames on the native route. Physical movie review,
  movement-gap attribution, direct raw-stream fetch, broader scene parity,
  and accepted performance evidence remain open.
- A Vulkan-first default with D3D12 startup fallback remains the gated design
  direction. D3D12 and production `auto` selection are not implemented, and
  D3D11 remains selectable through both parity gates.
- Tutorial gameplay is running, but full-game correctness is not established.
- Networking is not supported.
- Asset-format research for a future Blender importer has decoded the
  resource bundles, name hash, and complete binary-XML descriptor and schema
  formats. Validation covers all 7 schemas, 759 nonempty descriptors, and
  806 compressed blocks; `tools\assets.py` decodes every descriptor on the
  disc. The decoded texture descriptors contain 12,647 sized entries whose
  aligned ranges and sizes match Xbox mip-chain expectations except for two
  entries. Mesh, texture, and animation payloads are not decoded yet, and no
  importer exists.
- Internal-resolution scaling retains a 4:3 picture; true widescreen is not
  implemented.
- Experimental rendering and performance options remain default-off unless
  explicitly documented otherwise.
- [RecompLauncher](https://recomplauncher.com) can set the game up from the
  player's disc image and start it, using the build tools listed in the
  README; releases are source zips because the game is built on the player's
  PC. Settings, saves, caches and logs share one
  [data folder](USER_GUIDE.md#data-folder). RecompLauncher itself has not yet
  been confirmed to install a release, and the project is not in its catalog.
- The pinned framework is xboxrecomp v0.18.0. Its clean patch replay, focused
  synthetic fixtures, retail regeneration, and bounded CPU and D3D11 Basic
  Controls smokes pass. Brute Force also enables a default-off framework
  option that gives zero-delay guest polls a 1 ms scheduler backoff; its
  producer/worker fixture and three no-retry D3D11 startup checks pass. This
  is bring-up evidence, not full-game correctness or a performance result.

Detailed evidence and open gates live in
[current findings](FINDINGS.md), [next steps](NEXTSTEPS.md),
[rendering architecture](RENDERING.md), and the
[performance plan](PERFPLAN.md).
