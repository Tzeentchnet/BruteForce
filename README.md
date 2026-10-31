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

This is an experimental release.

> [!NOTE]
> This build currently does not support networking.

Direct3D 11 is the default renderer for new launcher settings. The CPU
reference renderer remains available from the launcher for comparison and
troubleshooting.

See [current findings](docs/FINDINGS.md) and
[next steps](docs/NEXTSTEPS.md) for the detailed validation boundary.

## Requirements

- 64-bit Windows 10 or 11 with a Direct3D 11-capable GPU.
- Python 3.10 or later with Tkinter.
- Git and CMake 3.20 or later.
- Visual Studio 2022 Build Tools 17.5 or later with the C++ workload and
  Windows SDK.
- Enough free disk space for the extracted disc, generated C, and native
  build.
- An XDVDFS/XISO image containing the supported North American retail
  `default.xbe`.

The supported executable fingerprint is recorded in
[`fingerprints/retail.json`](fingerprints/retail.json). Other revisions are
rejected until they are analyzed and explicitly supported.

## Build and run

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

After the first build, start the launcher with `Brute Force.cmd`,
`Brute Force.bat`, or:

```powershell
python tools\launcher.py
```

Choose **Play** to launch. Settings are saved in
`%LOCALAPPDATA%\BruteForce\settings.json`.

### Keyboard controls

| Input | Keys |
| --- | --- |
| Left stick | W, A, S, D |
| Right stick | I, J, K, L |
| D-pad | Arrow keys |
| Start | Enter |
| A / B / X / Y | Z / X / C / V |

Input is read from the focused game window.

## Updating or rebuilding

To rebuild after source changes:

```powershell
python tools\build.py generate
python tools\build.py build --config Release --parallel 4
```

The build pipeline verifies the game fingerprint, generated-code provenance,
selected framework revision, local patch set, and build metadata before it
runs the executable.

For individual setup, extraction, generation, runtime options, diagnostics,
benchmarking, and save isolation, see the
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
