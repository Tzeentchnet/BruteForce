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
troubleshooting. Native-resolution resize rollback has synthetic coverage;
retail gameplay and depth/stencil reinitialization after resize remain
unvalidated. Guest mipmap sampling and 8x anisotropic filtering are separate
default-off D3D11 experiments with synthetic coverage; title and xemu
comparisons remain open. See the [user guide](docs/USER_GUIDE.md#d3d11-renderer-and-benchmark-diagnostics).
Submitted GPU work is bounded and device-loss paths fail explicitly.
Default-off clean-surface readback elision has synthetic coverage only; dirty
frames still read back and tracked-surface flips still wait. The static
consumer inventory confirms that CPU presentation, PVIDEO composition,
frame dumps, frame statistics and presenter capture currently depend on
guest-visible colour pixels. A completed null-backend guard run still has one
unresolved non-NV2A colour read and an excluded page, so it does not justify
general on-demand readback. Retail correctness and performance of this
experiment are unverified.

Three independent default-off Phase 4 switches now have synthetic coverage:
event-driven PFB acknowledgement, NV2A idle waiting and guarded per-PUT
segment walking. The latter requires current generated store hooks and fails
closed on untracked USER-register writes or resets. A reproduced
segment/PGRAPH/PFB deadlock is fixed without advancing GET or completing the
queued segment. One no-retry segment-only smoke and one no-retry all-three
smoke reached reviewed tutorial gameplay; neither is an A/B/A performance
campaign, distinct-world FPS result or CPU-reference match. The planned
24-run flush/idle/segment/all-three campaign is deferred; all three switches
remain default-off. See the
[user guide](docs/USER_GUIDE.md#experimental-nv2a-wait-and-segment-options).

Packed depth/stencil aliases and window clipping remain default-off experiments;
their expanded ownership and clipping coverage is synthetic, not a gameplay
or original-hardware guarantee. Movement benchmark collection exits 2 as
inconclusive; a separate reviewed-world analysis must establish content and
presentation. Black hashes alone cannot identify intentional fades.

Texture and colour-shadow inputs now use checked snapshots and tracked
allocation identities; colour-surface writes, compatible render-target aliases
and bounded P8 palettes have synthetic coverage. Unknown suballocations and
concurrent guest writes still need an ownership contract, and retail image
parity and cost remain unvalidated. `RECOMP_DISPLAY_AWAKE=1` is an
independent, default-off foreground-only Windows display-awake experiment; it
is not a launcher default.

True Hor+ widescreen is not implemented; internal-resolution choices retain
the 4:3 picture. The camera trace is diagnostic only. Initial widescreen work
is scoped to proven single-view scenes; split-screen reference, layout,
widescreen and performance work are deferred, and no split-screen support is
claimed.

The pinned framework has been updated to xboxrecomp v0.17.0 plus selected
post-release fixes. Integrated Release generation
`20261111-084222-7bdb6e56` uses the final runtime tree. Before the
runtime-only Phase 4 fix, its codegen-identical executable completed one
audio-off CPU and one D3D11 tutorial/movement bring-up without retries. The
post-deadlock executable completed the two reviewed Phase 4 smokes described
above. The current texture-index rebuild also contains an explicit diagnostic
linear-reference mode so both sides of its planned comparison use one
executable. The index remains the default. This rebuild has synthetic
validation only and has not been run on retail content. Reviewed samples from
the preceding executables show gameplay without the controls panel or a
cinematic, but none of those single attempts is an A/B/A performance result.
Matched-reference and reviewed performance gates remain open, and the
published measurements still belong to the previous reviewed generation.

The pre-fix executable also completed one additional D3D11 attempt at the 4K
preset with mipmaps and anisotropy enabled. It confirms the 2880x2160 4:3
render path can run on the available hardware, but the observed tutorial
texture declared no lower levels and the 3440x1440 display cannot establish
native 4K output.

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
python tools\build.py setup
python tools\build.py generate
python tools\build.py build --config Release --parallel 4
```

If the existing `.deps\xboxrecomp` checkout has an older or modified patch
set, setup refuses to overwrite it. Preserve and move that checkout before
running setup again.

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
