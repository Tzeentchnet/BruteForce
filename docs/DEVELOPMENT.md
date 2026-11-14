# Development and tests

Developer validation commands and synthetic fixtures. These tests do not
require proprietary game data.

## Tests

```powershell
python -m pytest -q tests
```

The title's one-core guest-thread default, explicit override, and benchmark
option recording have a focused selection:

```powershell
python -m pytest -q tests\test_build.py tests\test_bench_performance.py
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
  "nv2a_idle_wait",
  "nv2a_segments",
  "nv2a_raster_batch",
  "nv2a_pb_constants",
  "nv2a_pb_walk"
)
foreach ($fixture in $fixtures) {
  $build = "build\xboxrecomp-$fixture"
  cmake -S ".deps\xboxrecomp\tests\$fixture" -B $build `
    -G "Visual Studio 17 2022" -A x64
  cmake --build $build --config Release --parallel 4
  ctest --test-dir $build -C Release --output-on-failure
}
```

For the Phase 4 changes using a private, already reconstructed toolkit and a
`build\performance-main` configured against that toolkit and an existing
generation (the synthetic D3D11 test does not run the game):

```powershell
$toolkit = "$PWD\build\performance-toolkit"
python tools\toolkit.py --verify --checkout $toolkit
foreach ($fixture in @("nv2a_pfb_flush", "nv2a_idle_wait", "nv2a_segments")) {
  cmake -S "$toolkit\tests\$fixture" -B "build\performance-tests\$fixture"
  cmake --build "build\performance-tests\$fixture" --config Release
  ctest --test-dir "build\performance-tests\$fixture" -C Release --output-on-failure
}
python -m pytest -q "$toolkit\tools\recomp\test_store32_hooks.py" `
  --basetemp build\pyt-store
python -m pytest -q tests\test_bench.py tests\test_bench_control.py `
  tests\test_bench_world.py tests\test_bench_match.py tests\test_bench_audit.py `
  tests\test_bench_performance.py --basetemp build\pyt-bench
cmake --build build\performance-main --config Release --target bruteforce_d3d11_test
$env:BRUTEFORCE_TEST_D3D11 = "$PWD\build\performance-main\Release\bruteforce_d3d11_test.exe"
$env:BRUTEFORCE_TEST_XBE = "$toolkit\tools\conformance\test.xbe"
python -m pytest -q tests\test_d3d11.py::test_detailed_performance_counters `
  --basetemp build\pyt-d3d
```

The segment fixture checks the read-only alias, fail-closed untracked stores
and resets, initial GET/JUMP, partial packets, JUMP/CALL/RETURN, producer
backpressure and held-flip dispatch. Its ordering case also verifies that a
pending PGRAPH trap can release PFB without advancing GET or consuming the
queued segment. Its executor uses synthetic ordered markers, not the real
PGRAPH/report backend; the existing fence, pushbuffer, PGRAPH and interrupt
fixtures cover those components separately. These tests do not establish
integrated retail timing, presented pixels or performance.
The title fence fixture also checks the opt-in aggregate shader compile
counter's success, failure and reset behavior; the WARP fixture checks an
actual uncached pixel-shader compile, and the benchmark parser checks its
interval records. Its PFB-attribution case verifies all five known generated
call-site stack layouts; an offset regression must fail instead of producing
an accepted zero-caller interval.

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
  bruteforce_crash_test bruteforce_memory_stats_test bruteforce_pfb_flush_test `
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
$env:BRUTEFORCE_TEST_PFB_FLUSH = "$PWD\build\smoke\Release\bruteforce_pfb_flush_test.exe"
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

For the render-artifact audit, depth-ownership, clipping and benchmark
regressions using an already prepared **isolated** framework checkout:

```powershell
$toolkit = "$PWD\build\artifact-toolkit"
python tools\toolkit.py --verify --checkout $toolkit
cmake -S "$toolkit\tests\nv2a_gpu_audit" -B build\artifact-audit `
  -G "Visual Studio 17 2022" -A x64
cmake --build build\artifact-audit --config Release
ctest --test-dir build\artifact-audit -C Release --output-on-failure
cmake -S . -B build\artifact-fixtures -G "Visual Studio 17 2022" -A x64 `
  "-DXBOXRECOMP_DIR=$toolkit" "-DBRUTEFORCE_GEN_DIR=$PWD\tests\fixtures\generated"
cmake --build build\artifact-fixtures --config Release --target `
  bruteforce_gpu_audit_test bruteforce_window_clip_test bruteforce_d3d11_test
$env:BRUTEFORCE_TEST_XBE = "$toolkit\tools\conformance\test.xbe"
$env:BRUTEFORCE_TEST_GPU_AUDIT = "$PWD\build\artifact-fixtures\Release\bruteforce_gpu_audit_test.exe"
$env:BRUTEFORCE_TEST_WINDOW_CLIP = "$PWD\build\artifact-fixtures\Release\bruteforce_window_clip_test.exe"
$env:BRUTEFORCE_TEST_D3D11 = "$PWD\build\artifact-fixtures\Release\bruteforce_d3d11_test.exe"
$env:BRUTEFORCE_TEST_D3D11_HARDWARE = "1" # omit to use WARP only
python -m pytest -q --basetemp build\artifact-fixtures\pytest-render `
  tests\test_gpu_audit.py tests\test_window_clip.py tests\test_d3d11.py `
  -k "gpu_audit or window_clip or packed_depth or depth_alias or stencil_alias or traced_glare or pipeline_packed_revision or pipeline_scaled_depth_guest_range or rectangular_clears or gpu_matches_cpu_reference or gpu_scene_has_expected_content or gpu_vertex_culling"
python -m pytest -q --basetemp build\artifact-fixtures\pytest-benchmark `
  tests\test_bench.py tests\test_bench_control.py tests\test_bench_world.py `
  tests\test_bench_match.py tests\test_bench_audit.py
```

`BRUTEFORCE_TEST_XBE` points the render-test modules and benchmark-audit
fixture at the isolated checkout's synthetic conformance XBE; unset it to
use the usual `.deps` path. The fixtures cover clean/dirty guest-byte changes,
padding, allocation reuse, guarded ranges, overlap and clear recovery, injected
seed/baseline races, scale rejection, clip side effects and audit semantics.
Hardware tests use the default D3D11 adapter when requested. CPU stencil is
unmodeled and checked separately on GPU. None of these fixtures validates
retail content, scanout, arbitrary concurrent writes or performance.

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
checks that a guarded write respects executable read-only protection. The
texture-descriptor-index case covers exact key fields, deliberate hash
collisions, rewrite/rekey, compacting discard, moved-entry relocation, and
full-cache LRU eviction without allocating GPU resources. The linear-reference
case runs the same exact rewrite fixture with `RECOMP_GPU_TEX_INDEX=0` and
`1`, requires byte-identical output, and rejects invalid values.
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
checks state-aware packed-depth invalidation, rejected overlapping and scaled
depth/stencil aliases, scaled guest-range overlap accounting, and synthetic
guest writes at coherence boundaries, and injects producer and required
draw-state failures before completion.
Hardware presenter tests require a usable Windows D3D11 adapter. Run the
framework `nv2a_pfb_flush` and `nv2a_ffp_backend` CTests above as well.

The pushbuffer fixture covers default-off index-word runs in staged and
guarded-segment walkers, including split packets, CALL/RETURN, physical wrap,
other-subchannel and increasing-packet fallback. The framework
`nv2a_pb_constants` fixture compares complete scalar/run executor state, final
method values, exact capacity and audit counts; `nv2a_pb_walk` covers DMA
boundaries and fail-closed streams. For the focused index-word patch plus
benchmark metadata:

```powershell
cmake --build build\smoke --config Release --target bruteforce_pushbuffer_test
$env:BRUTEFORCE_TEST_PUSHBUFFER = "$PWD\build\smoke\Release\bruteforce_pushbuffer_test.exe"
python -m pytest -q tests\test_pushbuffer.py tests\test_bench.py `
  tests\test_bench_performance.py tests\test_toolkit.py
python tools\toolkit.py --verify
```

For compiler-profile infrastructure without starting the title:

```powershell
python -m pytest -q tests\test_build.py tests\test_bench.py `
  tests\test_bench_control.py tests\test_bench_performance.py
python tools\build.py build --config Release --parallel 4 --compiler-profile ltcg
python tools\build.py build --config Release --parallel 4 --compiler-profile avx2
```

The profiles use isolated build directories and exact metadata. Inspect the
generated Release project only as build evidence:
`WholeProgramOptimization=true` identifies the LTCG profile and
`EnableEnhancedInstructionSet=AdvancedVectorExtensions2` identifies AVX2.
CMake rejects enabling both options, and the build helper rejects either
profile outside Release. Do not start the game merely to validate these
builds; runtime correctness and performance require the separately reviewed
campaign in the user guide.

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
