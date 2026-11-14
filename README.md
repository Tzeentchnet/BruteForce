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

- Direct3D 11 is the default renderer; the CPU reference remains available.
- Tutorial gameplay is running, but full-game correctness is not established.
- Networking is not supported.
- Internal-resolution scaling retains a 4:3 picture; true widescreen is not
  implemented.
- Experimental rendering and performance options remain default-off unless
  explicitly documented otherwise.

Detailed evidence and open gates live in
[current findings](docs/FINDINGS.md), [next steps](docs/NEXTSTEPS.md),
[rendering architecture](docs/RENDERING.md), and the
[performance plan](docs/PERFPLAN.md).

## Quick start

You need 64-bit Windows 10 or 11, a Direct3D 11-capable GPU, Python 3.10 or
later with Tkinter, Git, CMake 3.20 or later, Visual Studio 2022 Build Tools
17.5 or later with the C++ workload and Windows SDK, and an XDVDFS/XISO image
made from the supported retail disc. See the
[user guide](docs/USER_GUIDE.md#requirements-and-build-workflow) for the full
requirements and incremental workflow.

Open PowerShell in the repository root:

```powershell
python -m pip install -r tools\requirements.txt
python tools\build.py all --disc "F:\path\to\your\Brute Force.xiso.iso"
& ".\Brute Force.cmd"
```

The `all` command downloads the pinned public framework, verifies and extracts
the supplied disc, generates the recompiled sources, and builds a Release
executable. Disc contents, generated sources, logs, saves, and build products
remain local and are excluded from Git.

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
