# Brute Force static recompilation

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

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

Experimental: tutorial gameplay runs on the North American retail game, but
full-game correctness is not established and networking is not supported.
See the [current status](docs/STATUS.md) for renderer, asset-format and
framework details.

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

### With RecompLauncher

Releases include a [`recomplauncher.json`](recomplauncher.json) for
[RecompLauncher](https://recomplauncher.com). Install the same build tools
first; the launcher then asks for your disc image, builds the game on your PC
in the background, and offers the launcher settings on the game's page. See
the [user guide](docs/USER_GUIDE.md#recomplauncher).

The supported executable fingerprint is recorded in
[`fingerprints/retail.json`](fingerprints/retail.json); other revisions are
rejected. Launcher settings, keyboard controls, rebuilding, diagnostics,
benchmarking, and save isolation are documented in the
[user guide](docs/USER_GUIDE.md).

## Documentation

- [Status](docs/STATUS.md): what works, what is experimental, and what
  remains open.
- [User guide](docs/USER_GUIDE.md): detailed setup, launcher settings,
  controls, runtime options, captures, and diagnostics.
- [Development and tests](docs/DEVELOPMENT.md): synthetic fixtures and
  validation commands.
- [Current findings](docs/FINDINGS.md): observed runtime behavior,
  measurements, and validation limits.
- [Next steps](docs/NEXTSTEPS.md): active work and completion gates.
- [Completed work](docs/COMPLETED.md): finished campaigns, fixed defects,
  and completed milestone steps moved out of next steps.
- [Architecture](docs/ARCHITECTURE.md): executable, game data, runtime, and
  asset organization.
- [Asset formats](docs/ASSETFORMATS.md): decoded resource formats and the
  Blender-importer plan for characters, props, and levels.
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
