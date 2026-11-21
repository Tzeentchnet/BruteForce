# Brute Force static recompilation

An early Windows port of the original Xbox game built with
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp).

This repository contains no game executable, assets, or generated game code.
You must supply an XDVDFS/XISO image made from your own North American retail
copy.

> [!NOTE]
> **Blood Wake acknowledgement:** This project owes a great deal to the
> [Blood Wake recompilation project](https://github.com/minimalice92/bloodwake-recomp)
> for both inspiration and implementation details, particularly its rendering
> work. Its prior art materially shaped Brute Force's renderer and overall
> bring-up approach.

## Status

This is an experimental Windows release for the North American retail game:

- Direct3D 11 is the current default renderer; the CPU reference remains
  available. An explicit, non-default Vulkan 1.3 bring-up backend now covers
  the base and CPU-transformed extended draw contracts, four texture stages,
  register combiners, cube maps, default-off supplied mip chains, exact
  texture caching, compatible color render-target aliases, precise visibility,
  depth/stencil, bounded submission, RGB565 conversion, and mandatory guest
  readback. An additional default-off host-owned Vulkan presentation path now
  has a bounded completed-image queue, Win32 surface and swap chain, resize,
  aspect-fit blit, VSync selection, and required CPU fallback. Eligible
  vertex-program batches now run through a static Vulkan interpreter from
  gathered float inputs; raw guest-stream fetch remains on the framework CPU
  path. At the captured Basic Controls state, 134 of 307,200 pixels exceed
  8/255 versus the matched D3D11 frame.
  Review places those sparse pixels in animated world content visible around
  the tutorial panel; three opaque panel regions have no pixels over 8/255.
  NVIDIA and AMD fixtures cover the interpreter, whole-triangle non-finite
  rejection, verification readback, CPU/PVIDEO surface preservation and
  dirty-conflict handling. The formerly black manual menu route now reaches
  visible Basic Controls. Complete guest-surface review of one matched
  movement pair shows the same tutorial scene and scripted camera route with
  zero unknown frames; continuous capture prevents treating its rates as a
  performance comparison. A later no-capture same-build diagnostic required
  only one manual run per renderer: D3D11 reported 26.409 stationary and
  48.193 movement flips/s, while Vulkan reported 23.164 and 17.469. Inputs
  matched within 78 ms, but the frames remain unreviewed and one pair is not
  A/B/A or distinct-world-frame evidence. The native path is validation-clean
  in its NVIDIA and AMD hardware fixture, including borderless-fullscreen
  enter/restore and fail-stop surface loss. One operator-confirmed retail
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
- Internal-resolution scaling retains a 4:3 picture; true widescreen is not
  implemented.
- Experimental rendering and performance options remain default-off unless
  explicitly documented otherwise.
- The pinned framework is xboxrecomp v0.18.0. Its clean patch replay, focused
  synthetic fixtures, retail regeneration, and bounded CPU and D3D11 Basic
  Controls smokes pass. Brute Force also enables a default-off framework
  option that gives zero-delay guest polls a 1 ms scheduler backoff; its
  producer/worker fixture and three no-retry D3D11 startup checks pass. This
  is bring-up evidence, not full-game correctness or a performance result.

Detailed evidence and open gates live in
[current findings](docs/FINDINGS.md), [next steps](docs/NEXTSTEPS.md),
[rendering architecture](docs/RENDERING.md), and the
[performance plan](docs/PERFPLAN.md).

## Quick start

You need 64-bit Windows 10 or 11, a Direct3D 11-capable GPU, Python 3.10 or
later with Tkinter, Git, CMake 3.20 or later, Visual Studio 2022 Build Tools
17.5 or later with the C++ workload and Windows SDK, and an XDVDFS/XISO image
made from the supported retail disc. Explicit Vulkan testing additionally
requires a Vulkan 1.3-capable driver. The build downloads pinned Vulkan
headers and DXC; no Vulkan SDK installation is required. See the
[user guide](docs/USER_GUIDE.md#requirements-and-build-workflow) for the full
requirements and incremental workflow.

Open PowerShell in the repository root:

```powershell
python -m pip install -r tools\requirements.txt
python tools\build.py all --disc "F:\path\to\your\Brute Force.xiso.iso"
& ".\Brute Force.cmd"
```

The `all` command downloads the pinned public framework and Vulkan build
dependencies, verifies and extracts the supplied disc, generates the
recompiled sources, and builds a Release executable. Disc contents, generated
sources, logs, saves, and build products remain local and are excluded from
Git. If an older framework checkout is already installed, preserve and move
it before rerunning setup; generations from the previous pin must be
regenerated.

The supported executable fingerprint is recorded in
[`fingerprints/retail.json`](fingerprints/retail.json); other revisions are
rejected. Launcher settings, keyboard controls, rebuilding, diagnostics,
benchmarking, and save isolation are documented in the
[user guide](docs/USER_GUIDE.md).

## Documentation

- [User guide](docs/USER_GUIDE.md): detailed setup, launcher settings,
  controls, runtime options, captures, and diagnostics.
- [Development and tests](docs/DEVELOPMENT.md): synthetic fixtures and
  validation commands.
- [Current findings](docs/FINDINGS.md): observed runtime behavior,
  measurements, and validation limits.
- [Next steps](docs/NEXTSTEPS.md): active work and completion gates.
- [Architecture](docs/ARCHITECTURE.md): executable, game data, runtime, and
  asset organization.
- [Rendering architecture](docs/RENDERING.md): graphics ownership,
  synchronization, presentation, and known differences.
- [Performance plan](docs/PERFPLAN.md): performance targets and phase status.
- [Upstream work](docs/UPSTREAM.md): framework patches and contribution
  status.

## Local and proprietary data

Do not commit or redistribute disc images, extracted assets, generated game
code, saves, screenshots, captures, profiles, or runtime logs. The repository
documents fingerprints and structural observations without including game
content.

## License

Project-owned source is MIT-licensed. The linked executable includes xemu's
GPL-2.0-or-later DSP interpreter and is distributed, if at all, under
GPL-2.0-or-later as a combined work. Some components use
LGPL-2.0-or-later or LGPL-2.1-or-later.

See [LICENSE](LICENSE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md),
[`LICENSES/`](LICENSES/), and
[`src/xemu_dsp/NOTICE.md`](src/xemu_dsp/NOTICE.md).
