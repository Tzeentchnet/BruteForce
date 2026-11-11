# Development and tests

Developer validation commands and synthetic fixtures. These tests do not
require proprietary game data.

## Tests

```powershell
python -m pytest -q tests
```

The tests include a synthetic disc image and do not require game data. The
synthetic extraction test uses the installed toolkit's XISO reader.

After changing the xboxrecomp base, selected commits, or local framework
patches, verify the reproducible checkout and run its recompiler suite:

```powershell
python tools\toolkit.py --verify
Push-Location .deps\xboxrecomp
python -m pytest -q tools
Pop-Location
```

For an already prepared isolated checkout, verify it instead:

```powershell
python tools\toolkit.py --verify --checkout build\pipeline-toolkit
```

The `--checkout` option is verify-only; it cannot install or modify a checkout.

The direct MSVC differential cases require the Visual Studio 2022 x86 C/C++
tools. The focused native fixtures that cover the current selected backend
contract, EEPROM options, GPU audit, PFB wait, and raster diagnostic can be
rebuilt without game data:

```powershell
$fixtures = @(
  "display_awake",
  "kernel_eeprom_settings",
  "memory_regressions",
  "nv2a_ffp_backend",
  "nv2a_gpu_audit",
  "nv2a_pfb_flush",
  "nv2a_raster_batch"
)
foreach ($fixture in $fixtures) {
  $build = "build\xboxrecomp-$fixture"
  cmake -S ".deps\xboxrecomp\tests\$fixture" -B $build `
    -G "Visual Studio 17 2022" -A x64
  cmake --build $build --config Release --parallel 4
  ctest --test-dir $build -C Release --output-on-failure
}
```

To exercise the native host and all synthetic fixtures, including crash
diagnostics, guest
memory accounting, pushbuffer control flow, and graphics interrupts:

```powershell
cmake -S . -B build\smoke -G "Visual Studio 17 2022" -A x64 `
  "-DBRUTEFORCE_GEN_DIR=$PWD\tests\fixtures\generated"
cmake --build build\smoke --config Release --parallel 4 --target `
  bruteforce bruteforce_fence_test bruteforce_camera_trace_test `
  bruteforce_read_trace_test `
  bruteforce_present_test bruteforce_usb_test bruteforce_interrupt_test `
  bruteforce_crash_test bruteforce_memory_stats_test `
  bruteforce_pushbuffer_test bruteforce_pgraph_test bruteforce_window_clip_test `
  bruteforce_color_unpack_test bruteforce_texture_math_test `
  bruteforce_frame_stats_test bruteforce_gpu_audit_test `
  bruteforce_d3d11_test bruteforce_gpu_shared_present_test
$env:BRUTEFORCE_TEST_HOST = "$PWD\build\smoke\Release\bruteforce.exe"
$env:BRUTEFORCE_TEST_FENCE = "$PWD\build\smoke\Release\bruteforce_fence_test.exe"
$env:BRUTEFORCE_TEST_CAMERA_TRACE = "$PWD\build\smoke\Release\bruteforce_camera_trace_test.exe"
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
$env:BRUTEFORCE_TEST_GPU_AUDIT = "$PWD\build\smoke\Release\bruteforce_gpu_audit_test.exe"
$env:BRUTEFORCE_TEST_D3D11 = "$PWD\build\smoke\Release\bruteforce_d3d11_test.exe"
$env:BRUTEFORCE_TEST_GPU_SHARED_PRESENT = "$PWD\build\smoke\Release\bruteforce_gpu_shared_present_test.exe"
python -m pytest -q tests
```

For hardware-device coverage as well as WARP, set
`BRUTEFORCE_TEST_D3D11_HARDWARE=1` before the pytest command.
The host fixture does not contain game code and cannot run the game.

For the camera-trace and presenter-size safety fixtures:

```powershell
cmake -S . -B build\smoke -G "Visual Studio 17 2022" -A x64 `
  "-DBRUTEFORCE_GEN_DIR=$PWD\tests\fixtures\generated"
cmake --build build\smoke --config Release --target `
  bruteforce_camera_trace_test bruteforce_gpu_shared_present_test bruteforce_present_test
$env:BRUTEFORCE_TEST_CAMERA_TRACE = "$PWD\build\smoke\Release\bruteforce_camera_trace_test.exe"
$env:BRUTEFORCE_TEST_GPU_SHARED_PRESENT = "$PWD\build\smoke\Release\bruteforce_gpu_shared_present_test.exe"
$env:BRUTEFORCE_TEST_PRESENT = "$PWD\build\smoke\Release\bruteforce_present_test.exe"
$env:BRUTEFORCE_TEST_D3D11_HARDWARE = "1" # omit to use WARP only
python -m pytest -q --basetemp build\smoke\pytest-camera-present `
  tests\test_camera_trace.py tests\test_present.py
```

The generated camera function in this fixture is a stub: it produces
synthetic 4:3, 16:9 and 64:27 perspective-shaped matrices or leaves
orthographic/degenerate inputs unchanged. It checks trace output and
guest/host state equivalence, including a split-shaped *candidate* viewport.
The presenter fixture demonstrates that different clients can yield the
same fitted 4:3 size. These tests do not establish camera ownership, reject
split screen, or implement a wide mode. The shared-frame protocol case uses
WARP; hardware coverage remains opt-in.

For the focused render-scale transaction and launcher checks, use the
configured synthetic build above:

```powershell
cmake --build build\smoke --config Release --target bruteforce_d3d11_test
$env:BRUTEFORCE_TEST_D3D11 = "$PWD\build\smoke\Release\bruteforce_d3d11_test.exe"
# Set to 1 to include the default hardware adapter and live presenter window.
$env:BRUTEFORCE_TEST_D3D11_HARDWARE = "1"
python -m pytest -q --basetemp build\smoke\pytest-scale tests\test_d3d11.py `
  -k "scale_configuration_is_transactional or native_resize or native_output_only or visibility_query_spanning"
python -m pytest -q --basetemp build\smoke\pytest-launcher tests\test_launcher.py `
  -k "fixed_resolution_clears_inherited_native_output"
```

These fixtures inject initialization and replacement failures and exercise
rollback, retry, busy shared frames, output-only changes, and query-start
normalization. The hidden live-window test requires a hardware device.
Fault-injection hooks exist only in the native test target. They do not prove
retail gameplay, depth/stencil reinitialization, device recovery, a VRAM
budget or performance.

For the focused mipmap and texture-regression matrix, use the same synthetic
build and set `BRUTEFORCE_TEST_D3D11` as above:

```powershell
cmake --build build\smoke --config Release --target bruteforce_d3d11_test
# Optional: include the default hardware adapter as well as WARP.
$env:BRUTEFORCE_TEST_D3D11_HARDWARE = "1"
python -m pytest -q --basetemp build\smoke\pytest-mips tests\test_d3d11.py `
  -k "mip or gpu_texture_decode_matches_cpu or native_bc_upload or texture_cache_observes or approximate_cache or unsupported_texture_cache or failed_texture_uploads or short_p8 or invalid_p8 or cached_p8 or gpu_matches_cpu_reference or gpu_scene_has_expected_content"
```

The fixture checks mip layout through the final cube face and level,
guarded/short ranges and palettes, direct BC and CPU/GPU-decoded uploads,
sampler cache keys, filters, fractional clamps, signed bias, and same-frame
rewrites, including guarded base-level fallback under approximate-cache mode.
Rendered 2D and cube scenes exercise supplied levels, stage-specific clamps,
the disabled path and 2x physical-scale LOD selection. It does not prove title
image equivalence, hardware NV2A behavior, resource budgets or performance.

For the checked texture, colour-alias and preparation-diagnostic changes:

```powershell
cmake --build build\smoke --config Release --target bruteforce_d3d11_test
$env:BRUTEFORCE_TEST_D3D11 = "$PWD\build\smoke\Release\bruteforce_d3d11_test.exe"
$env:BRUTEFORCE_TEST_D3D11_HARDWARE = "1" # omit to use WARP only
python -m pytest -q --basetemp build\p tests\test_d3d11.py `
  -k "texture or mip or cache or render_target or readback or depth or gpu_matches_cpu_reference or detailed_performance_counters or shader or color_alias"
```

Use a short workspace-local `--basetemp`: the Windows shader-cache fixture
appends long key and temporary filenames. The synthetic matrix includes
checked guard and reprotection behavior, tracked allocation reuse through
RAM mirrors and tiled mappings, registered and unregistered bounds, P8 lengths
and invalid indices, exact, freed, short-pitch and conflicting colour aliases,
seed/upload/clear/readback races, layout migration and replacement, guarded
and read-only colour writes, failed partial writes and retry, padded rows, a
bound-target copy, CPU/GPU parity and shader diagnostics. The fixture also
checks that a guarded write respects executable read-only protection.
`memory_regressions` separately
checks logical-range registration/reuse and mapping reset; `nv2a_ffp_backend`
checks checked CPU decoding, input/output overlap and bounded verification
skips; `nv2a_pfb_flush` checks diagnostic
bounds and the real handshake; `display_awake` substitutes window and power
APIs. These do not prove retail behavior, unsynchronized write coherence,
real display policy or actual device loss.

For the GPU pipeline, synchronization and presenter failure boundaries:

```powershell
cmake --build build\smoke --config Release --target bruteforce_d3d11_test `
  bruteforce_fence_test bruteforce_frame_stats_test bruteforce_present_test `
  bruteforce_gpu_shared_present_test bruteforce_pushbuffer_test
$env:BRUTEFORCE_TEST_D3D11 = "$PWD\build\smoke\Release\bruteforce_d3d11_test.exe"
$env:BRUTEFORCE_TEST_FENCE = "$PWD\build\smoke\Release\bruteforce_fence_test.exe"
$env:BRUTEFORCE_TEST_FRAME_STATS = "$PWD\build\smoke\Release\bruteforce_frame_stats_test.exe"
$env:BRUTEFORCE_TEST_PRESENT = "$PWD\build\smoke\Release\bruteforce_present_test.exe"
$env:BRUTEFORCE_TEST_GPU_SHARED_PRESENT = "$PWD\build\smoke\Release\bruteforce_gpu_shared_present_test.exe"
$env:BRUTEFORCE_TEST_PUSHBUFFER = "$PWD\build\smoke\Release\bruteforce_pushbuffer_test.exe"
$env:BRUTEFORCE_TEST_D3D11_HARDWARE = "1" # omit to use WARP only
python -m pytest -q --basetemp build\smoke\pytest-pipeline tests\test_d3d11.py -k pipeline_
python -m pytest -q --basetemp build\smoke\pytest-sync tests\test_fence.py `
  tests\test_frame_stats.py tests\test_present.py tests\test_pushbuffer.py tests\test_toolkit.py
```

The pipeline fixture compares every guest frame with elision off/on, forces
diagnostic and 16-bit readback, exercises queue saturation and failure,
checks state-aware packed-depth invalidation and rejected overlapping or
scaled aliases and synthetic guest writes at coherence boundaries, and injects
producer and required draw-state failures before completion.
Hardware presenter tests require a usable Windows D3D11 adapter. Run the
framework `nv2a_pfb_flush` and `nv2a_ffp_backend` CTests above as well.

From a VS 2022 x64 developer prompt, the isolated framework's capture and
presenter-device fixture can be built without retail data:

```powershell
& build\pipeline-toolkit\tests\presenter_capture\run_windows.ps1 `
  -ProjectTests "$PWD\tests"
```

Its hidden-window and WARP/hardware tests inject presenter loss and resource
errors and verify fail-stop markers; add `-SkipHardware` when no hardware
adapter is available. Tests do not simulate actual driver loss or prove
retail guest-reader coverage, image parity or throughput.

Project-owned source is MIT-licensed. The linked executable includes xemu's
GPL-2.0-or-later DSP interpreter and is distributed, if at all, under
GPL-2.0-or-later as a combined work. Some DSP DMA and framework APU files use
LGPL-2.0-or-later or LGPL-2.1-or-later. See [LICENSE](../LICENSE),
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md),
[`LICENSES/`](../LICENSES/), and
[`src/xemu_dsp/NOTICE.md`](../src/xemu_dsp/NOTICE.md), plus the toolkit's own
`LICENSE` and `NOTICE`, for terms and attribution.
