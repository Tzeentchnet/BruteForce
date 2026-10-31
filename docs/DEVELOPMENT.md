# Development and tests

Developer validation commands and synthetic fixtures. These tests do not
require proprietary game data.

## Tests

```powershell
python -m pytest -q tests
```

The tests include a synthetic disc image and do not require game data. The
synthetic extraction test uses the installed toolkit's XISO reader. To exercise
the native host and all synthetic fixtures, including crash diagnostics, guest
memory accounting, pushbuffer control flow, and graphics interrupts:

```powershell
cmake -S . -B build\smoke -G "Visual Studio 17 2022" -A x64 `
  "-DBRUTEFORCE_GEN_DIR=$PWD\tests\fixtures\generated"
cmake --build build\smoke --config Release --parallel 4 --target `
  bruteforce bruteforce_fence_test bruteforce_read_trace_test `
  bruteforce_present_test bruteforce_usb_test bruteforce_interrupt_test `
  bruteforce_crash_test bruteforce_memory_stats_test `
  bruteforce_pushbuffer_test bruteforce_pgraph_test bruteforce_window_clip_test `
  bruteforce_color_unpack_test bruteforce_texture_math_test `
  bruteforce_frame_stats_test bruteforce_d3d11_test bruteforce_gpu_shared_present_test
$env:BRUTEFORCE_TEST_HOST = "$PWD\build\smoke\Release\bruteforce.exe"
$env:BRUTEFORCE_TEST_FENCE = "$PWD\build\smoke\Release\bruteforce_fence_test.exe"
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
$env:BRUTEFORCE_TEST_D3D11 = "$PWD\build\smoke\Release\bruteforce_d3d11_test.exe"
$env:BRUTEFORCE_TEST_GPU_SHARED_PRESENT = "$PWD\build\smoke\Release\bruteforce_gpu_shared_present_test.exe"
python -m pytest -q tests
```

For hardware-device coverage as well as WARP, set
`BRUTEFORCE_TEST_D3D11_HARDWARE=1` before the pytest command.
The host fixture does not contain game code and cannot run the game.
Project-owned source is MIT-licensed. The linked executable includes xemu's
GPL-2.0-or-later DSP interpreter and is distributed, if at all, under
GPL-2.0-or-later as a combined work. Some DSP DMA and framework APU files use
LGPL-2.0-or-later or LGPL-2.1-or-later. See [LICENSE](../LICENSE),
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md),
[`LICENSES/`](../LICENSES/), and
[`src/xemu_dsp/NOTICE.md`](../src/xemu_dsp/NOTICE.md), plus the toolkit's own
`LICENSE` and `NOTICE`, for terms and attribution.
